#ifndef STORAGE_CANDIDATE_OUTCOME_WITNESS_INTERNAL_H
#define STORAGE_CANDIDATE_OUTCOME_WITNESS_INTERNAL_H

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
void candidate_admissions_close(candidate_admissions_t **admissions);

#endif
