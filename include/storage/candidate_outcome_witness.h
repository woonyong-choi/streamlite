#ifndef STORAGE_CANDIDATE_OUTCOME_WITNESS_H
#define STORAGE_CANDIDATE_OUTCOME_WITNESS_H

#include "storage/candidate_catalog.h"
#include "storage/candidate_control.h"

typedef struct candidate_outcome_witness candidate_outcome_witness_t;
typedef struct {
    candidate_catalog_load_t catalog;
    const candidate_control_state_t *checkpoint;
} candidate_outcome_witness_options_t;
typedef struct {
    uint32_t admitted, committed, aborted;
    uint64_t result_bytes, wal_end_lsn;
    size_t resident_bytes;
} candidate_outcome_witness_info_t;

/* 안정된 복원 target과 WAL을 caller가 보유한다; 성공은 결과 대조이며 활성화/APPLIED가 아니다. */
/* 전체 WAL force가 선행되어야 하며 stale metadata는 STALE, 불완전 tail은 TAIL, 실패 out은 NULL이다. */
/* ABORT의 NULL 결과는 조각을 버린 final descriptor이며, 보존 결과는 완전한 chunk/본문을 대조한다. */
candidate_wal_status_t candidate_outcome_witness_load(candidate_wal_t *wal,
    const candidate_outcome_witness_options_t *options, candidate_outcome_witness_t **out);
/* MATCH는 대조한 상태를 빌리며 부재/만료를 NOT_APPLIED나 자동 재실행 권한으로 바꾸지 않는다. */
candidate_catalog_lookup_t candidate_outcome_witness_lookup(const candidate_outcome_witness_t *witness,
    const candidate_catalog_query_t *query, const candidate_outcome_t **out);
candidate_outcome_witness_info_t candidate_outcome_witness_info(const candidate_outcome_witness_t *witness);
void candidate_outcome_witness_close(candidate_outcome_witness_t **witness);

#endif
