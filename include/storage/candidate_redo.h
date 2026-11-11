#ifndef STORAGE_CANDIDATE_REDO_H
#define STORAGE_CANDIDATE_REDO_H

#include "storage/candidate_control.h"

typedef struct {
    void *context;
    candidate_wal_status_t (*read)(void *, uint64_t, uint8_t[CANDIDATE_PAGE_BYTES], bool *);
    candidate_wal_status_t (*write)(void *, uint64_t, const uint8_t[CANDIDATE_PAGE_BYTES]);
    candidate_wal_status_t (*sync)(void *);
} candidate_redo_target_t;
typedef struct {
    uint8_t uuid[16];
    const candidate_control_state_t *checkpoint;
    candidate_redo_target_t target;
    const candidate_wal_memory_t *memory;
} candidate_redo_options_t;
typedef struct {
    uint64_t groups, pages_written, pages_skipped, wal_end_lsn;
    uint32_t distinct_pages;
    size_t workspace_bytes;
} candidate_redo_info_t;

/* caller가 UUID를 확인한 target의 배타 복구 소유권과 callback 전체 page I/O를 보장한다. */
/* 실패는 부분 data 쓰기를 남길 수 있으며 out은0이다; 재실행 가능성과 APPLIED/DB 활성화는 다르다. */
candidate_wal_status_t candidate_redo_execute(candidate_wal_t *wal,
    const candidate_redo_options_t *options, candidate_redo_info_t *out);

#endif
