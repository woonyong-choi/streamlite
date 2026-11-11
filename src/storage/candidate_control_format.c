#include "storage/candidate_control.h"

#include <string.h>

#include "storage/checksum.h"

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (i * 8); return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static bool is_ref(candidate_page_ref_t ref, bool optional, uint64_t highwater)
{
    if (ref.id == 0) return optional && ref.generation == 0;
    return ref.generation > 0 && ref.id <= highwater;
}

static bool is_state(const candidate_control_state_t *s)
{
    if (is_zero(s->uuid, 16) || s->generation == 0 || s->owner_epoch == 0 || s->checkpoint_id == 0) return false;
    if (s->slot != (s->generation - 1) % 2) return false;
    if (s->backend == CANDIDATE_CONTROL_WAL) {
        if (s->checkpoint_begin < 64 || s->checkpoint_begin % 8 != 0 || s->checkpoint_end % 8 != 0) return false;
        if (s->checkpoint_end <= s->checkpoint_begin || s->checkpoint_end - s->checkpoint_begin < 192) return false;
        if (s->redo_begin < 64 || s->redo_begin > s->checkpoint_begin || s->redo_begin % 8 != 0) return false;
    } else if (s->backend == CANDIDATE_CONTROL_COW) {
        if (s->checkpoint_begin != 0 || s->checkpoint_end != 0 || s->redo_begin != 0) return false;
    } else return false;
    if (s->request_epoch == 0 || s->retention_floor == 0 || s->request_epoch < s->retention_floor) return false;
    if (s->request_epoch - s->retention_floor >= 4) return false;
    if (s->visible_csn > s->transaction_highwater || s->gc_horizon > s->visible_csn) return false;
    if (!is_ref(s->outcome_root, false, s->page_highwater) || !is_ref(s->allocation_root, false, s->page_highwater)) return false;
    if (!is_ref(s->catalog_root, true, s->page_highwater)) return false;
    if (s->outcome_root.id == s->allocation_root.id) return false;
    if (s->catalog_root.id != 0 && (s->catalog_root.id == s->outcome_root.id || s->catalog_root.id == s->allocation_root.id)) return false;
    return true;
}

static uint32_t checksum(const uint8_t *p)
{
    const uint8_t zero[4] = {0}; uint32_t crc = crc32c(0, p, 176);
    crc = crc32c(crc, zero, 4); return crc32c(crc, p + 180, CANDIDATE_CONTROL_BYTES - 180);
}

candidate_wal_status_t candidate_control_encode(const candidate_control_state_t *s, uint8_t *out, size_t capacity)
{
    if (s == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    if (!is_state(s)) return CANDIDATE_WAL_INVALID;
    if (capacity < CANDIDATE_CONTROL_BYTES) return CANDIDATE_WAL_LIMIT;
    uint8_t p[CANDIDATE_CONTROL_BYTES] = {0};
    memcpy(p, "SLCT", 4); write_le(p + 4, 1, 2); write_le(p + 6, 192, 2); memcpy(p + 8, s->uuid, 16);
    write_le(p + 24, s->backend, 4); write_le(p + 28, s->slot, 4); write_le(p + 32, s->generation, 8);
    write_le(p + 40, s->owner_epoch, 8); write_le(p + 48, s->checkpoint_id, 8);
    write_le(p + 56, s->checkpoint_begin, 8); write_le(p + 64, s->checkpoint_end, 8); write_le(p + 72, s->redo_begin, 8);
    write_le(p + 80, s->transaction_highwater, 8); write_le(p + 88, s->page_highwater, 8);
    write_le(p + 96, s->request_epoch, 8); write_le(p + 104, s->retention_floor, 8); write_le(p + 112, s->visible_csn, 8);
    write_le(p + 120, s->outcome_root.id, 8); write_le(p + 128, s->outcome_root.generation, 8);
    write_le(p + 136, s->allocation_root.id, 8); write_le(p + 144, s->allocation_root.generation, 8);
    write_le(p + 152, s->catalog_root.id, 8); write_le(p + 160, s->catalog_root.generation, 8);
    write_le(p + 168, s->gc_horizon, 8); write_le(p + 176, checksum(p), 4);
    memcpy(out, p, sizeof(p)); return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_control_decode(const uint8_t uuid[16], const uint8_t *p, size_t size,
    candidate_control_state_t *out)
{
    if (uuid == NULL || p == NULL || out == NULL || is_zero(uuid, 16)) return CANDIDATE_WAL_INVALID;
    if (size != CANDIDATE_CONTROL_BYTES) return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 176, 4) != checksum(p)) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(p, "SLCT", 4) != 0 || read_le(p + 4, 2) != 1 || read_le(p + 6, 2) != 192) return CANDIDATE_WAL_UNSUPPORTED;
    if (memcmp(p + 8, uuid, 16) != 0 || !is_zero(p + 180, CANDIDATE_CONTROL_BYTES - 180)) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t backend = read_le(p + 24, 4);
    if (backend != CANDIDATE_CONTROL_WAL && backend != CANDIDATE_CONTROL_COW) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_control_state_t s = {.backend = (uint32_t)backend, .slot = (uint32_t)read_le(p + 28, 4),
        .generation = read_le(p + 32, 8), .owner_epoch = read_le(p + 40, 8), .checkpoint_id = read_le(p + 48, 8),
        .checkpoint_begin = read_le(p + 56, 8), .checkpoint_end = read_le(p + 64, 8), .redo_begin = read_le(p + 72, 8),
        .transaction_highwater = read_le(p + 80, 8), .page_highwater = read_le(p + 88, 8), .request_epoch = read_le(p + 96, 8),
        .retention_floor = read_le(p + 104, 8), .visible_csn = read_le(p + 112, 8),
        .outcome_root = {read_le(p + 120, 8),read_le(p + 128, 8)}, .allocation_root = {read_le(p + 136, 8),read_le(p + 144, 8)},
        .catalog_root = {read_le(p + 152, 8),read_le(p + 160, 8)}, .gc_horizon = read_le(p + 168, 8)};
    memcpy(s.uuid, uuid, 16); if (!is_state(&s)) return CANDIDATE_WAL_CORRUPT;
    *out = s; return CANDIDATE_WAL_OK;
}
