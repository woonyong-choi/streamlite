#include "candidate_checkpoint_internal.h"

#include <string.h>

#include "storage/candidate_digest.h"

uint64_t candidate_checkpoint_read(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (i * 8));
}

uint32_t candidate_checkpoint_chunks(const candidate_checkpoint_summary_t *s)
{
    return (s->dirty_count + 127) / 128 + (s->transaction_count + 127) / 128;
}

static candidate_wal_status_t copy_payload(const uint8_t *bytes, size_t size,
    uint8_t *out, size_t capacity, size_t *written)
{
    if (capacity < size) return CANDIDATE_WAL_LIMIT;
    memcpy(out, bytes, size); *written = size;
    return CANDIDATE_WAL_OK;
}

static void write_header(uint8_t *out, unsigned kind)
{
    memcpy(out, "SLCP", 4); write_le(out + 4, 1, 2); write_le(out + 6, kind, 2);
}

candidate_wal_status_t candidate_checkpoint_encode_begin(const candidate_checkpoint_summary_t *s,
    uint8_t *out, size_t capacity, size_t *written)
{
    if (s == NULL || out == NULL || written == NULL) return CANDIDATE_WAL_INVALID;
    if (s->anchor.backend != CANDIDATE_CONTROL_WAL) return CANDIDATE_WAL_UNSUPPORTED;
    if (s->dirty_count > CANDIDATE_CHECKPOINT_DIRTY || s->transaction_count > CANDIDATE_CHECKPOINT_TRANSACTIONS)
        return CANDIDATE_WAL_LIMIT;
    uint8_t control[8192];
    candidate_wal_status_t status = candidate_control_encode(&s->anchor, control, sizeof(control));
    if (status != CANDIDATE_WAL_OK) return status;
    const candidate_control_state_t *a = &s->anchor;
    uint8_t bytes[160] = {0}; write_header(bytes, 0); memcpy(bytes + 8, a->uuid, 16);
    uint64_t values[] = {a->checkpoint_id,a->redo_begin,a->owner_epoch,a->transaction_highwater,
        a->page_highwater,a->request_epoch,a->retention_floor,a->visible_csn,a->gc_horizon,
        a->outcome_root.id,a->outcome_root.generation,a->allocation_root.id,a->allocation_root.generation,
        a->catalog_root.id,a->catalog_root.generation};
    for (unsigned i = 0; i < 15; i++) write_le(bytes + 24 + i * 8, values[i], 8);
    write_le(bytes + 144, s->dirty_count, 4); write_le(bytes + 148, s->transaction_count, 4);
    write_le(bytes + 152, candidate_checkpoint_chunks(s), 4);
    return copy_payload(bytes, sizeof(bytes), out, capacity, written);
}

static bool is_lsn(uint64_t lsn) { return lsn >= 64 && lsn % 8 == 0; }

static bool is_transaction(const candidate_checkpoint_transaction_t *t)
{
    if (t->id == 0 || t->owner_epoch == 0) return false;
    if (!is_lsn(t->first_lsn) || !is_lsn(t->last_lsn) || t->first_lsn > t->last_lsn) return false;
    if (t->first_lsn < t->last_lsn && t->last_lsn - t->first_lsn < 112) return false;
    if (t->state == CANDIDATE_CHECKPOINT_ACTIVE) return t->terminal_end_lsn == 0 && t->csn == 0;
    if (t->first_lsn == t->last_lsn) return false;
    if (!is_lsn(t->terminal_end_lsn) || t->terminal_end_lsn <= t->last_lsn) return false;
    uint64_t size = t->terminal_end_lsn - t->last_lsn;
    if (t->state == CANDIDATE_CHECKPOINT_COMMIT) {
        if (size < 248 || size > CANDIDATE_WAL_RECORD_BYTES) return false;
        return t->csn > 0 && (size - 248) % 16 == 0;
    }
    return t->state == CANDIDATE_CHECKPOINT_ABORT && t->csn == 0 && size == 128;
}

candidate_wal_status_t candidate_checkpoint_encode_chunk(const candidate_checkpoint_chunk_t *c,
    uint8_t *out, size_t capacity, size_t *written)
{
    if (c == NULL || out == NULL || written == NULL) return CANDIDATE_WAL_INVALID;
    if (c->checkpoint_id == 0 || c->count == 0) return CANDIDATE_WAL_INVALID;
    if (c->count > 128 || c->ordinal >= 96) return CANDIDATE_WAL_LIMIT;
    if (c->kind != 1 && c->kind != 2) return CANDIDATE_WAL_INVALID;
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD] = {0}; write_header(bytes, c->kind);
    write_le(bytes + 8, c->checkpoint_id, 8); write_le(bytes + 16, c->ordinal, 4); write_le(bytes + 20, c->count, 4);
    for (unsigned i = 0; i < c->count; i++) {
        if (c->kind == 1) {
            const candidate_checkpoint_dirty_t *d = &c->entries.dirty[i];
            if (d->ref.id == 0 || d->ref.generation == 0 || !is_lsn(d->rec_lsn)) return CANDIDATE_WAL_INVALID;
            if (i > 0 && d->ref.id <= c->entries.dirty[i-1].ref.id) return CANDIDATE_WAL_INVALID;
            uint8_t *p = bytes + 32 + i * 24;
            write_le(p, d->ref.id, 8); write_le(p + 8, d->ref.generation, 8); write_le(p + 16, d->rec_lsn, 8);
        } else {
            const candidate_checkpoint_transaction_t *t = &c->entries.transactions[i];
            if (!is_transaction(t)) return CANDIDATE_WAL_INVALID;
            if (i > 0 && t->id <= c->entries.transactions[i-1].id) return CANDIDATE_WAL_INVALID;
            uint8_t *p = bytes + 32 + i * 56;
            uint64_t values[] = {t->id,t->first_lsn,t->last_lsn,t->terminal_end_lsn,t->csn,t->owner_epoch};
            for (unsigned j = 0; j < 6; j++) write_le(p + j * 8, values[j], 8);
            write_le(p + 48, t->state, 4);
        }
    }
    size_t size = 32 + c->count * (c->kind == 1 ? 24 : 56);
    return copy_payload(bytes, size, out, capacity, written);
}

candidate_wal_status_t candidate_checkpoint_decode_chunk(const uint8_t *p, size_t size,
    candidate_checkpoint_chunk_t *out)
{
    if (size < 32) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(p, "SLCP\1\0", 6) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    unsigned kind = (unsigned)candidate_checkpoint_read(p + 6, 2);
    if (kind != 1 && kind != 2) return CANDIDATE_WAL_UNSUPPORTED;
    if (candidate_checkpoint_read(p + 24, 8) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_checkpoint_chunk_t c = {.kind=kind, .checkpoint_id=candidate_checkpoint_read(p+8,8),
        .ordinal=(uint32_t)candidate_checkpoint_read(p+16,4), .count=(uint32_t)candidate_checkpoint_read(p+20,4)};
    if (c.count > 128 || c.ordinal >= 96) return CANDIDATE_WAL_LIMIT;
    if (size != 32 + c.count * (kind == 1 ? 24 : 56)) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < c.count; i++) {
        const uint8_t *entry = p + 32 + i * (kind == 1 ? 24 : 56);
        if (kind == 1) {
            c.entries.dirty[i] = (candidate_checkpoint_dirty_t){{candidate_checkpoint_read(entry,8),
                candidate_checkpoint_read(entry+8,8)}, candidate_checkpoint_read(entry+16,8)};
        } else {
            if (candidate_checkpoint_read(entry+52,4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
            unsigned state = (unsigned)candidate_checkpoint_read(entry+48,4);
            if (state < 1 || state > 3) return CANDIDATE_WAL_UNSUPPORTED;
            c.entries.transactions[i] = (candidate_checkpoint_transaction_t){candidate_checkpoint_read(entry,8),
                candidate_checkpoint_read(entry+8,8),candidate_checkpoint_read(entry+16,8),candidate_checkpoint_read(entry+24,8),
                candidate_checkpoint_read(entry+32,8),candidate_checkpoint_read(entry+40,8),(candidate_checkpoint_state_t)state};
        }
    }
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD]; size_t written;
    candidate_wal_status_t status = candidate_checkpoint_encode_chunk(&c, bytes, sizeof(bytes), &written);
    if (status != CANDIDATE_WAL_OK) return status == CANDIDATE_WAL_INVALID ? CANDIDATE_WAL_CORRUPT : status;
    *out = c; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_checkpoint_encode_end(const candidate_checkpoint_end_t *e,
    uint8_t *out, size_t capacity, size_t *written)
{
    if (e == NULL || out == NULL || written == NULL) return CANDIDATE_WAL_INVALID;
    if (e->checkpoint_id == 0 || !is_lsn(e->begin_lsn)) return CANDIDATE_WAL_INVALID;
    if (e->chunks > 96) return CANDIDATE_WAL_LIMIT;
    uint8_t bytes[64] = {0}; write_header(bytes, 3); write_le(bytes + 8, e->checkpoint_id, 8);
    write_le(bytes + 16, e->begin_lsn, 8); write_le(bytes + 24, e->chunks, 4); memcpy(bytes + 32, e->digest, 32);
    return copy_payload(bytes, sizeof(bytes), out, capacity, written);
}

candidate_wal_status_t candidate_checkpoint_digest(const uint8_t previous[32], const uint8_t *payload,
    size_t size, uint8_t out[32])
{
    if (previous == NULL || payload == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    if (size < 8 || size > CANDIDATE_CHECKPOINT_PAYLOAD) return CANDIDATE_WAL_INVALID;
    uint8_t bytes[32+CANDIDATE_CHECKPOINT_PAYLOAD]; memcpy(bytes, previous, 32); memcpy(bytes + 32, payload, size);
    return candidate_digest_sha256(bytes, size + 32, out);
}
