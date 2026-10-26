#include "candidate_wal_read_internal.h"

#include <string.h>

#include "storage/checksum.h"
#include "candidate_wal_bytes_internal.h"

typedef struct {
    const candidate_wal_reader_t *reader;
    const candidate_wal_suffix_t *request;
    candidate_wal_suffix_count_t *count;
    uint64_t at;
} cursor_t;

static uint64_t segment_end(uint64_t lsn)
{
    return (lsn / CANDIDATE_WAL_SEGMENT_BYTES + 1) * CANDIDATE_WAL_SEGMENT_BYTES;
}

void candidate_wal_encode_segment(const uint8_t uuid[16], unsigned id, uint8_t out[64])
{
    memset(out, 0, 64);
    memcpy(out, "SLWAL001", 8);
    write_le(out + 8, 1, 2);
    write_le(out + 10, 64, 2);
    memcpy(out + 16, uuid, 16);
    write_le(out + 32, id, 8);
    write_le(out + 40, (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES, 8);
    write_le(out + 48, crc32c(0, out, 64), 4);
}

candidate_wal_status_t candidate_wal_validate_segment(const uint8_t uuid[16], unsigned id, const uint8_t *p, size_t size)
{
    uint8_t expected[64];
    candidate_wal_encode_segment(uuid, id, expected);
    if (memcmp(p, expected, size < 8 ? size : 8) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 12 && (read_le(p + 8, 2) != 1 || read_le(p + 10, 2) != 64))
        return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 32 && memcmp(p + 16, uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    return memcmp(p, expected, size) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_wal_validate_frame_header(const uint8_t uuid[16], const uint8_t *p, size_t size,
    uint64_t lsn)
{
    if (memcmp(p, "SLWR", size < 4 ? size : 4) != 0) return CANDIDATE_WAL_CORRUPT;
    if (size >= 6 && read_le(p + 4, 2) != 1) return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 8 && (read_le(p + 6, 2) < 1 || read_le(p + 6, 2) > 16))
        return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t total = size >= 12 ? read_le(p + 8, 4) : 96;
    if (total < 96 || total > CANDIDATE_WAL_RECORD_BYTES || total % 8 != 0) return CANDIDATE_WAL_CORRUPT;
    if (total > CANDIDATE_WAL_SEGMENT_BYTES - lsn % CANDIDATE_WAL_SEGMENT_BYTES)
        return CANDIDATE_WAL_CORRUPT;
    if (size >= 16 && ((read_le(p + 12, 4) + 103) & ~(uint64_t)7) != total)
        return CANDIDATE_WAL_CORRUPT;
    if (size >= 24 && read_le(p + 16, 8) != lsn) return CANDIDATE_WAL_CORRUPT;
    if (size >= 40 && read_le(p + 32, 8) >= lsn) return CANDIDATE_WAL_CORRUPT;
    if (size > 40 && memcmp(p + 40, uuid, size < 56 ? size - 40 : 16) != 0)
        return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 64 && read_le(p + 56, 8) == 0) return CANDIDATE_WAL_CORRUPT;
    if (size >= 68 && read_le(p + 64, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (size > 72 && !is_zero(p + 72, size - 72)) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

size_t candidate_wal_frame_bytes(const uint8_t *header) { return (size_t)read_le(header + 8, 4); }

candidate_wal_status_t candidate_wal_check_padding(const uint8_t *p, size_t size)
{
    return is_zero(p, size) ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t read_head(const candidate_wal_reader_t *reader, uint64_t lsn, uint64_t end_lsn,
    size_t *head)
{
    uint64_t room = segment_end(lsn) - lsn, limit = end_lsn - lsn;
    uint64_t size = room < limit ? room : limit;
    *head = size < CANDIDATE_WAL_RECORD_HEADER_BYTES ? (size_t)size : CANDIDATE_WAL_RECORD_HEADER_BYTES;
    return reader->read(reader->context, lsn, reader->buffer, *head);
}

static candidate_wal_status_t decode_frame(const candidate_wal_reader_t *reader, uint64_t lsn, uint64_t end_lsn,
    size_t head, candidate_wal_record_t *out)
{
    candidate_wal_status_t status = candidate_wal_validate_frame_header(reader->uuid, reader->buffer, head, lsn);
    if (status != CANDIDATE_WAL_OK) return status;
    if (head < CANDIDATE_WAL_RECORD_HEADER_BYTES) return CANDIDATE_WAL_CORRUPT;
    size_t total = candidate_wal_frame_bytes(reader->buffer);
    if (total > end_lsn - lsn) return CANDIDATE_WAL_CORRUPT;
    status = reader->read(reader->context, lsn, reader->buffer, total);
    if (status != CANDIDATE_WAL_OK) return status;
    status = candidate_wal_decode(reader->uuid, reader->buffer, total, out);
    if (status == CANDIDATE_WAL_OK && out->lsn != lsn) return CANDIDATE_WAL_CORRUPT;
    return status;
}

candidate_wal_status_t candidate_wal_read_frame(const candidate_wal_reader_t *reader, uint64_t lsn, uint64_t end_lsn,
    candidate_wal_record_t *out)
{
    if (segment_end(lsn) - lsn < CANDIDATE_WAL_RECORD_HEADER_BYTES) return CANDIDATE_WAL_IO;
    size_t head;
    candidate_wal_status_t status = read_head(reader, lsn, end_lsn, &head);
    return status == CANDIDATE_WAL_OK ? decode_frame(reader, lsn, end_lsn, head, out) : status;
}

static bool is_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    if (a->records != b->records || a->valid_end_lsn != b->valid_end_lsn) return false;
    if (a->segments != b->segments || a->highest_owner_epoch != b->highest_owner_epoch) return false;
    return a->has_tail == b->has_tail && a->tail_bytes == b->tail_bytes;
}

static bool is_frame_boundary(const candidate_wal_info_t *cut)
{
    uint64_t offset = cut->valid_end_lsn % CANDIDATE_WAL_SEGMENT_BYTES;
    if (cut->valid_end_lsn < CANDIDATE_WAL_HEADER_BYTES || cut->valid_end_lsn % 8 != 0) return false;
    if (offset != 0 && offset < CANDIDATE_WAL_HEADER_BYTES) return false;
    return (cut->valid_end_lsn - 1) / CANDIDATE_WAL_SEGMENT_BYTES < cut->segments;
}

static bool is_ordered(const candidate_wal_info_t *before, const candidate_wal_info_t *after)
{
    if (before->valid_end_lsn > after->valid_end_lsn || before->records > after->records) return false;
    if (before->segments > after->segments || before->highest_owner_epoch > after->highest_owner_epoch) return false;
    if (before->valid_end_lsn < after->valid_end_lsn) return true;
    return before->records == after->records && before->highest_owner_epoch == after->highest_owner_epoch;
}

candidate_wal_status_t candidate_wal_check_suffix(const candidate_wal_suffix_t *request, const candidate_wal_info_t *current)
{
    if (request->visitor == NULL) return CANDIDATE_WAL_INVALID;
    if (!is_same_cut(&request->after, current)) return CANDIDATE_WAL_STALE;
    bool is_range = is_frame_boundary(&request->before) && is_ordered(&request->before, &request->after);
    return is_range ? CANDIDATE_WAL_OK : CANDIDATE_WAL_INVALID;
}

static candidate_wal_status_t read_segment(const candidate_wal_reader_t *reader, uint64_t lsn, uint64_t end_lsn)
{
    if (end_lsn - lsn < CANDIDATE_WAL_HEADER_BYTES) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status = reader->read(reader->context, lsn, reader->buffer, CANDIDATE_WAL_HEADER_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    unsigned id = (unsigned)(lsn / CANDIDATE_WAL_SEGMENT_BYTES);
    return candidate_wal_validate_segment(reader->uuid, id, reader->buffer, CANDIDATE_WAL_HEADER_BYTES);
}

static candidate_wal_status_t read_padding(const candidate_wal_reader_t *reader, uint64_t lsn, size_t head)
{
    candidate_wal_status_t status = candidate_wal_check_padding(reader->buffer, head);
    uint64_t end = segment_end(lsn);
    for (uint64_t at = lsn + head; status == CANDIDATE_WAL_OK && at < end;) {
        size_t size = end - at < CANDIDATE_WAL_RECORD_BYTES ? (size_t)(end - at) : CANDIDATE_WAL_RECORD_BYTES;
        status = reader->read(reader->context, at, reader->buffer, size);
        if (status == CANDIDATE_WAL_OK) status = candidate_wal_check_padding(reader->buffer, size);
        at += size;
    }
    return status;
}

static candidate_wal_status_t visit_frame(cursor_t *c, size_t head)
{
    candidate_wal_record_t record;
    candidate_wal_status_t status = decode_frame(c->reader, c->at, c->request->after.valid_end_lsn, head, &record);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_suffix_count_t *n = c->count;
    if (n->records == 0 && record.owner_epoch < c->request->before.highest_owner_epoch) {
        n->is_first_behind = true;
        return CANDIDATE_WAL_CORRUPT;
    }
    if (n->records > 0 && record.owner_epoch < n->highest_owner_epoch) return CANDIDATE_WAL_CORRUPT;
    if (n->records == 0) n->first_owner_epoch = record.owner_epoch;
    n->highest_owner_epoch = record.owner_epoch;
    n->records++;
    c->at += candidate_wal_frame_bytes(c->reader->buffer);
    return c->request->visitor(c->request->context, &record) ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STOPPED;
}

static candidate_wal_status_t step(cursor_t *c)
{
    const candidate_wal_reader_t *reader = c->reader;
    uint64_t end_lsn = c->request->after.valid_end_lsn, next = segment_end(c->at);
    if (c->at % CANDIDATE_WAL_SEGMENT_BYTES == 0) {
        candidate_wal_status_t status = read_segment(reader, c->at, end_lsn);
        c->at += CANDIDATE_WAL_HEADER_BYTES;
        return status;
    }
    size_t head;
    candidate_wal_status_t status = read_head(reader, c->at, end_lsn, &head);
    if (status != CANDIDATE_WAL_OK) return status;
    if (reader->buffer[0] != 0 || end_lsn <= next) return visit_frame(c, head);
    status = read_padding(reader, c->at, head);
    c->at = next;
    return status;
}

candidate_wal_status_t candidate_wal_read_suffix(const candidate_wal_reader_t *reader, const candidate_wal_suffix_t *request,
    candidate_wal_suffix_count_t *count)
{
    *count = (candidate_wal_suffix_count_t){0};
    cursor_t c = {reader, request, count, request->before.valid_end_lsn};
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    while (status == CANDIDATE_WAL_OK && c.at < request->after.valid_end_lsn) status = step(&c);
    return status;
}

candidate_wal_status_t candidate_wal_match_suffix(const candidate_wal_suffix_t *request,
    const candidate_wal_suffix_count_t *count)
{
    const candidate_wal_info_t *before = &request->before, *after = &request->after;
    if (count->records != after->records - before->records) return CANDIDATE_WAL_CORRUPT;
    if (count->records == 0)
        return before->highest_owner_epoch == after->highest_owner_epoch ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
    bool is_monotone = count->first_owner_epoch >= before->highest_owner_epoch;
    return is_monotone && count->highest_owner_epoch == after->highest_owner_epoch ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}
