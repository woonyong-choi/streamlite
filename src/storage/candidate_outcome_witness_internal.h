#ifndef STORAGE_CANDIDATE_OUTCOME_WITNESS_INTERNAL_H
#define STORAGE_CANDIDATE_OUTCOME_WITNESS_INTERNAL_H

#include "storage/candidate_outcome_witness.h"
#include "storage/candidate_semantic_seed.h"

typedef struct candidate_admissions candidate_admissions_t;

candidate_wal_status_t candidate_admissions_create(const candidate_wal_memory_t *memory,
    const candidate_admissions_t *source, candidate_admissions_t **out);
bool candidate_admissions_observe(void *admissions, const candidate_wal_record_t *record);
candidate_wal_status_t candidate_admissions_status(const candidate_admissions_t *admissions);
uint32_t candidate_admissions_count(const candidate_admissions_t *admissions);
bool candidate_admissions_at(const candidate_admissions_t *admissions, uint32_t index,
    candidate_semantic_admission_t *out);
size_t candidate_admissions_resident_bytes(const candidate_admissions_t *admissions);
/* witness에 남은 거래의 admission만 순서대로 남긴다. */
void candidate_admissions_retain(candidate_admissions_t *admissions, const candidate_wal_witness_t *witness);
void candidate_admissions_close(candidate_admissions_t **admissions);

/* candidate_outcome_witness_load와 같은 root·coverage·entry·본문 대조이며 TAIL·force 검사는 caller가 먼저 한다. */
candidate_wal_status_t candidate_outcome_check_resolved(const candidate_catalog_t *catalog,
    const candidate_catalog_load_t *load, const candidate_wal_witness_t *history,
    candidate_admissions_t *admissions, candidate_outcome_witness_info_t *info);
/* 재구성 전 preview와 같은 bounds·저장 entry·본문 대조이며 coverage와 ADMITTED 종결은 요구하지 않는다. */
candidate_wal_status_t candidate_outcome_check_preview(const candidate_catalog_t *catalog,
    const candidate_catalog_load_t *load, const candidate_control_state_t *anchor,
    const candidate_wal_witness_t *history, candidate_admissions_t *admissions, uint64_t owner_epoch);

#endif
