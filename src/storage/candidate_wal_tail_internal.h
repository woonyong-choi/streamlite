#ifndef STORAGE_CANDIDATE_WAL_TAIL_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_TAIL_INTERNAL_H

#include "storage/candidate_wal.h"

typedef struct {
    uint64_t begin_lsn, end_lsn, tail_bytes;
    uint8_t digest[32];
    bool has_tail;
} candidate_wal_tail_proof_t;

typedef struct {
    uint8_t digest[32], group_digest[32];
    uint64_t end_lsn, tail_bytes, group_lsn;
    unsigned segments;
    bool has_tail;
} candidate_wal_view_t;

candidate_wal_status_t candidate_wal_tail_hash(uint8_t digest[32], const candidate_wal_record_t *record);
candidate_wal_status_t candidate_wal_view_add(candidate_wal_view_t *view, const candidate_wal_record_t *record);
bool candidate_wal_view_same(const candidate_wal_view_t *left, const candidate_wal_view_t *right);
candidate_wal_status_t candidate_wal_tail_check(candidate_wal_t *wal, const candidate_wal_tail_proof_t *proof);
/* 배타 복구자가 같은 의미 scan에서 만든 증거만 받으며 final 미완료 그룹을 재검증한다. */
candidate_wal_status_t candidate_wal_discard_group_tail(candidate_wal_t *wal, const candidate_wal_tail_proof_t *proof);

#endif
