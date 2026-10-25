#include "storage/candidate_page.h"

#include <string.h>

#include "storage/checksum.h"

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static bool is_page_ref(candidate_page_ref_t ref)
{
    return (ref.id == 0 && ref.generation == 0) || (ref.id > 0 && ref.generation > 0);
}

static bool is_version_ref(candidate_version_ref_t ref)
{
    if (!is_page_ref(ref.page)) return false;
    if (ref.page.id == 0) return ref.slot == 0 && ref.slot_generation == 0;
    return ref.slot_generation > 0;
}

static bool is_same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static uint32_t checksum(const uint8_t *p)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0, p, 52);
    crc = crc32c(crc, zero, 4);
    return crc32c(crc, p + 56, CANDIDATE_PAGE_BYTES - 56);
}

static bool is_location(const candidate_page_location_t *location)
{
    if (location == NULL || is_zero(location->uuid, 16)) return false;
    if (!is_page_ref(location->ref) || location->ref.id == 0) return false;
    return location->lsn % 8 == 0;
}

static void encode_header(const candidate_page_location_t *location, uint16_t type,
    size_t body, uint8_t *out)
{
    memcpy(out, location->uuid, 16); memcpy(out + 16, "SLPG", 4);
    write_le(out + 20, 1, 2); write_le(out + 22, type, 2);
    write_le(out + 24, location->ref.id, 8); write_le(out + 32, location->ref.generation, 8);
    write_le(out + 40, location->lsn, 8); write_le(out + 48, body, 4);
    write_le(out + 52, checksum(out), 4);
}

static bool is_self_previous(const candidate_page_version_t *v,
    candidate_page_ref_t page, unsigned slot)
{
    if (!is_same_page(v->previous.page, page)) return false;
    return v->previous.slot == slot && v->previous.slot_generation == v->slot_generation;
}

static candidate_wal_status_t version_size(const candidate_page_version_t *v, size_t *size)
{
    if (v->transaction_id == 0 || v->slot_generation == 0) return CANDIDATE_WAL_INVALID;
    if (v->table_id == 0 || v->row_id == 0) return CANDIDATE_WAL_INVALID;
    if (!is_version_ref(v->previous) || !is_page_ref(v->overflow)) return CANDIDATE_WAL_INVALID;
    if (v->is_tombstone) {
        if (v->payload_bytes != 0 || v->inline_bytes != NULL || v->overflow.id != 0)
            return CANDIDATE_WAL_INVALID;
        *size = 72; return CANDIDATE_WAL_OK;
    }
    *size = 104;
    if (v->overflow.id != 0) {
        if (v->inline_bytes != NULL || v->payload_bytes == 0) return CANDIDATE_WAL_INVALID;
    } else {
        if (v->payload_bytes > CANDIDATE_PAGE_BYTES - 104) return CANDIDATE_WAL_LIMIT;
        if (v->payload_bytes > 0 && v->inline_bytes == NULL) return CANDIDATE_WAL_INVALID;
        *size += (size_t)v->payload_bytes;
    }
    return CANDIDATE_WAL_OK;
}

static void encode_version(const candidate_page_version_t *v, uint8_t *p)
{
    write_le(p, v->transaction_id, 8); write_le(p + 8, v->frozen_csn, 8);
    write_le(p + 16, v->previous.page.id, 8); write_le(p + 24, v->previous.page.generation, 8);
    write_le(p + 32, v->previous.slot, 4); write_le(p + 36, v->previous.slot_generation, 4);
    write_le(p + 40, v->table_id, 8); write_le(p + 48, v->row_id, 8);
    write_le(p + 56, v->payload_bytes, 8); write_le(p + 64, v->is_tombstone ? 1 : 0, 4);
    if (v->is_tombstone) return;
    write_le(p + 72, v->overflow.id == 0 ? 1 : 2, 4);
    write_le(p + 80, v->payload_bytes, 8);
    write_le(p + 88, v->overflow.id, 8); write_le(p + 96, v->overflow.generation, 8);
    if (v->overflow.id == 0 && v->payload_bytes > 0)
        memcpy(p + 104, v->inline_bytes, (size_t)v->payload_bytes);
}

candidate_wal_status_t candidate_page_encode_versions(const candidate_page_location_t *location,
    const candidate_page_version_t *versions, size_t count, uint8_t *out, size_t capacity)
{
    if (!is_location(location) || out == NULL) return CANDIDATE_WAL_INVALID;
    if (capacity < CANDIDATE_PAGE_BYTES || count > CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_LIMIT;
    if (count > 0 && versions == NULL) return CANDIDATE_WAL_INVALID;
    uint8_t page[CANDIDATE_PAGE_BYTES] = {0};
    size_t at = 72 + count * 8;
    write_le(page + 64, count, 4);
    for (unsigned i = 0; i < count; i++) {
        size_t size;
        candidate_wal_status_t status = version_size(&versions[i], &size);
        if (status != CANDIDATE_WAL_OK) return status;
        if (is_self_previous(&versions[i], location->ref, i)) return CANDIDATE_WAL_INVALID;
        if (size > CANDIDATE_PAGE_BYTES - at) return CANDIDATE_WAL_LIMIT;
        uint8_t *slot = page + 72 + i * 8;
        write_le(slot, at, 2); write_le(slot + 2, size, 2);
        write_le(slot + 4, versions[i].slot_generation, 4);
        encode_version(&versions[i], page + at); at += size;
    }
    encode_header(location, 1, at - 64, page);
    memcpy(out, page, sizeof(page)); return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_page_encode_overflow(const candidate_page_location_t *location,
    candidate_page_ref_t next, const uint8_t *bytes, size_t size, uint8_t *out, size_t capacity)
{
    if (!is_location(location) || out == NULL) return CANDIDATE_WAL_INVALID;
    if (!is_page_ref(next) || is_same_page(next, location->ref)) return CANDIDATE_WAL_INVALID;
    if (capacity < CANDIDATE_PAGE_BYTES || size > CANDIDATE_PAGE_OVERFLOW_BYTES) return CANDIDATE_WAL_LIMIT;
    if (size == 0 || bytes == NULL) return CANDIDATE_WAL_INVALID;
    if (next.id != 0 && size != CANDIDATE_PAGE_OVERFLOW_BYTES) return CANDIDATE_WAL_INVALID;
    uint8_t page[CANDIDATE_PAGE_BYTES] = {0};
    write_le(page + 64, next.id, 8); write_le(page + 72, next.generation, 8);
    write_le(page + 80, size, 4); memcpy(page + 88, bytes, size);
    encode_header(location, 2, 24 + size, page);
    memcpy(out, page, sizeof(page)); return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t decode_version(const uint8_t *p, size_t size,
    uint32_t generation, candidate_page_version_t *out)
{
    if (size < 72 || generation == 0) return CANDIDATE_WAL_CORRUPT;
    uint64_t flags = read_le(p + 64, 4);
    if (flags > 1 || read_le(p + 68, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_page_version_t v = {.transaction_id = read_le(p, 8), .frozen_csn = read_le(p + 8, 8),
        .previous = {{read_le(p + 16, 8), read_le(p + 24, 8)},
                     (uint32_t)read_le(p + 32, 4), (uint32_t)read_le(p + 36, 4)},
        .table_id = read_le(p + 40, 8), .row_id = read_le(p + 48, 8),
        .payload_bytes = read_le(p + 56, 8), .slot_generation = generation, .is_tombstone = flags == 1};
    if (v.transaction_id == 0 || v.table_id == 0 || v.row_id == 0) return CANDIDATE_WAL_CORRUPT;
    if (!is_version_ref(v.previous)) return CANDIDATE_WAL_CORRUPT;
    if (v.is_tombstone) {
        if (size != 72 || v.payload_bytes != 0) return CANDIDATE_WAL_CORRUPT;
    } else {
        if (size < 104) return CANDIDATE_WAL_CORRUPT;
        unsigned tag = (unsigned)read_le(p + 72, 4);
        if (read_le(p + 76, 4) != 0 || tag < 1 || tag > 2) return CANDIDATE_WAL_UNSUPPORTED;
        if (read_le(p + 80, 8) != v.payload_bytes) return CANDIDATE_WAL_CORRUPT;
        v.overflow = (candidate_page_ref_t){read_le(p + 88, 8), read_le(p + 96, 8)};
        if (!is_page_ref(v.overflow)) return CANDIDATE_WAL_CORRUPT;
        if (tag == 1) {
            if (v.overflow.id != 0 || v.payload_bytes > CANDIDATE_PAGE_BYTES) return CANDIDATE_WAL_CORRUPT;
            if (size != 104 + v.payload_bytes) return CANDIDATE_WAL_CORRUPT;
            v.inline_bytes = p + 104;
        } else if (v.overflow.id == 0 || v.payload_bytes == 0 || size != 104) return CANDIDATE_WAL_CORRUPT;
    }
    *out = v; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t decode_versions(const uint8_t *p, candidate_page_info_t *info)
{
    if (info->body_bytes < 8) return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 68, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t count = read_le(p + 64, 4);
    if (count > CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_CORRUPT;
    size_t at = 72 + (size_t)count * 8, end = 64 + info->body_bytes;
    if (at > end) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *slot = p + 72 + i * 8;
        size_t size = (size_t)read_le(slot + 2, 2);
        if (read_le(slot, 2) != at || size > end - at) return CANDIDATE_WAL_CORRUPT;
        candidate_page_version_t version;
        candidate_wal_status_t status = decode_version(p + at, size, (uint32_t)read_le(slot + 4, 4), &version);
        if (status != CANDIDATE_WAL_OK) return status;
        if (is_self_previous(&version, info->location.ref, i)) return CANDIDATE_WAL_CORRUPT;
        at += size;
    }
    if (at != end) return CANDIDATE_WAL_CORRUPT;
    info->slots = (uint32_t)count; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t decode_overflow(const uint8_t *p, const candidate_page_info_t *info)
{
    if (info->body_bytes < 25) return CANDIDATE_WAL_CORRUPT;
    candidate_page_ref_t next = {read_le(p + 64, 8), read_le(p + 72, 8)};
    if (!is_page_ref(next) || is_same_page(next, info->location.ref)) return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 84, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t size = read_le(p + 80, 4);
    if (size == 0 || size > CANDIDATE_PAGE_OVERFLOW_BYTES) return CANDIDATE_WAL_CORRUPT;
    if (info->body_bytes != 24 + size) return CANDIDATE_WAL_CORRUPT;
    if (next.id != 0 && size != CANDIDATE_PAGE_OVERFLOW_BYTES) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_page_decode(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_page_info_t *out)
{
    if (uuid == NULL || bytes == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    if (is_zero(uuid, 16)) return CANDIDATE_WAL_INVALID;
    if (size != CANDIDATE_PAGE_BYTES) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(bytes, uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (memcmp(bytes + 16, "SLPG", 4) != 0 || read_le(bytes + 20, 2) != 1)
        return CANDIDATE_WAL_UNSUPPORTED;
    if (!is_zero(bytes + 56, 8)) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_page_info_t info = {.location = {.ref = {read_le(bytes + 24, 8), read_le(bytes + 32, 8)},
        .lsn = read_le(bytes + 40, 8)}, .type = (uint16_t)read_le(bytes + 22, 2),
        .body_bytes = (uint32_t)read_le(bytes + 48, 4)};
    memcpy(info.location.uuid, uuid, 16);
    if (!is_location(&info.location) || info.body_bytes > CANDIDATE_PAGE_BYTES - 64)
        return CANDIDATE_WAL_CORRUPT;
    if (checksum(bytes) != read_le(bytes + 52, 4)) return CANDIDATE_WAL_CORRUPT;
    if (!is_zero(bytes + 64 + info.body_bytes, CANDIDATE_PAGE_BYTES - 64 - info.body_bytes))
        return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status;
    if (info.type == 1) status = decode_versions(bytes, &info);
    else if (info.type == 2) status = decode_overflow(bytes, &info);
    else return CANDIDATE_WAL_UNSUPPORTED;
    if (status != CANDIDATE_WAL_OK) return status;
    *out = info; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_page_get_version(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_version_ref_t ref, candidate_page_version_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    if (!is_version_ref(ref) || ref.page.id == 0) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode(uuid, bytes, size, &info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 1 || !is_same_page(ref.page, info.location.ref)) return CANDIDATE_WAL_CORRUPT;
    if (ref.slot >= info.slots) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *slot = bytes + 72 + ref.slot * 8;
    if (read_le(slot + 4, 4) != ref.slot_generation) return CANDIDATE_WAL_CORRUPT;
    return decode_version(bytes + read_le(slot, 2), (size_t)read_le(slot + 2, 2), ref.slot_generation, out);
}
