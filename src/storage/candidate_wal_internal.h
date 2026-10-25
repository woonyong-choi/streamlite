#ifndef STORAGE_CANDIDATE_WAL_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_INTERNAL_H

#include "storage/candidate_wal.h"

typedef struct {
    uint64_t position;
    unsigned segments;
} candidate_wal_cursor_t;

candidate_wal_cursor_t candidate_wal_cursor(const candidate_wal_t *wal);
candidate_wal_status_t candidate_wal_place(const candidate_wal_t *wal, candidate_wal_cursor_t *cursor,
    size_t payload_bytes, uint64_t *lsn);
candidate_wal_status_t candidate_wal_read_at(candidate_wal_t *wal, uint64_t lsn, candidate_wal_record_t *out);
candidate_wal_status_t candidate_wal_pin(candidate_wal_t *wal);
void candidate_wal_reject(candidate_wal_t *wal);

#endif
