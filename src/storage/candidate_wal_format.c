#include "storage/candidate_wal.h"

#include <string.h>

#include "storage/checksum.h"

static uint64_t read_le(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static uint32_t frame_crc(const uint8_t *p, size_t size)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0, p, 68);
    crc = crc32c(crc, zero, 4);
    return crc32c(crc, p + 72, size - 72);
}

candidate_wal_status_t candidate_wal_encode(const uint8_t uuid[16], const candidate_wal_record_t *r,
                                          uint8_t *out, size_t capacity, size_t *written)
{
    if (uuid == NULL || r == NULL || out == NULL || written == NULL) return CANDIDATE_WAL_INVALID;
    if (is_zero(uuid, 16) || r->owner_epoch == 0) return CANDIDATE_WAL_INVALID;
    if (r->type < 1 || r->type > 16) return CANDIDATE_WAL_UNSUPPORTED;
    if (r->lsn < 64 || r->lsn % 8 != 0 || r->previous_lsn >= r->lsn) return CANDIDATE_WAL_INVALID;
    if (r->payload_bytes > CANDIDATE_WAL_RECORD_BYTES - 96) return CANDIDATE_WAL_LIMIT;
    if (r->payload_bytes > 0 && r->payload == NULL) return CANDIDATE_WAL_INVALID;
    size_t total = (96 + r->payload_bytes + 7) & ~(size_t)7;
    if (r->lsn > UINT64_MAX - total) return CANDIDATE_WAL_LIMIT;
    uint64_t segment_offset = r->lsn % CANDIDATE_WAL_SEGMENT_BYTES;
    if (segment_offset < 64 || total > CANDIDATE_WAL_SEGMENT_BYTES - segment_offset)
        return CANDIDATE_WAL_INVALID;
    if (capacity < total) return CANDIDATE_WAL_LIMIT;
    memset(out, 0, total);
    memcpy(out, "SLWR", 4);
    write_le(out + 4, 1, 2);
    write_le(out + 6, r->type, 2);
    write_le(out + 8, total, 4);
    write_le(out + 12, r->payload_bytes, 4);
    write_le(out + 16, r->lsn, 8);
    write_le(out + 24, r->transaction_id, 8);
    write_le(out + 32, r->previous_lsn, 8);
    memcpy(out + 40, uuid, 16);
    write_le(out + 56, r->owner_epoch, 8);
    if (r->payload_bytes > 0) memcpy(out + 80, r->payload, r->payload_bytes);
    write_le(out + total - 16, total, 4);
    memcpy(out + total - 12, "SLWE", 4);
    write_le(out + total - 8, r->lsn, 8);
    write_le(out + 68, frame_crc(out, total), 4);
    *written = total;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_decode(const uint8_t uuid[16], const uint8_t *p, size_t size,
                                          candidate_wal_record_t *out)
{
    if (uuid == NULL || p == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    if (size < 96 || size > CANDIDATE_WAL_RECORD_BYTES || size % 8 != 0) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(p, "SLWR", 4) != 0) return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 4, 2) != 1) return CANDIDATE_WAL_UNSUPPORTED;
    unsigned type = (unsigned)read_le(p + 6, 2);
    if (type < 1 || type > 16 || read_le(p + 64, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (memcmp(p + 40, uuid, 16) != 0 || is_zero(uuid, 16)) return CANDIDATE_WAL_UNSUPPORTED;
    size_t payload = (size_t)read_le(p + 12, 4);
    if (read_le(p + 8, 4) != size || payload > size - 96) return CANDIDATE_WAL_CORRUPT;
    if (((payload + 103) & ~(size_t)7) != size) return CANDIDATE_WAL_CORRUPT;
    if (!is_zero(p + 72, 8) || !is_zero(p + 80 + payload, size - 96 - payload))
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + size - 16, 4) != size || memcmp(p + size - 12, "SLWE", 4) != 0)
        return CANDIDATE_WAL_CORRUPT;
    uint64_t lsn = read_le(p + 16, 8);
    if (lsn > UINT64_MAX - size) return CANDIDATE_WAL_CORRUPT;
    uint64_t offset = lsn % CANDIDATE_WAL_SEGMENT_BYTES;
    if (offset < 64 || lsn % 8 != 0 || size > CANDIDATE_WAL_SEGMENT_BYTES - offset)
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + size - 8, 8) != lsn || read_le(p + 32, 8) >= lsn)
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 56, 8) == 0 || read_le(p + 68, 4) != frame_crc(p, size))
        return CANDIDATE_WAL_CORRUPT;
    *out = (candidate_wal_record_t){(uint16_t)type, lsn, read_le(p + 24, 8), read_le(p + 32, 8),
                                    read_le(p + 56, 8), p + 80, payload};
    return CANDIDATE_WAL_OK;
}
