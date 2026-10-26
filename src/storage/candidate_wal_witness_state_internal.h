#ifndef STORAGE_CANDIDATE_WAL_WITNESS_STATE_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_WITNESS_STATE_INTERNAL_H

#include "storage/candidate_semantic_seed.h"

candidate_wal_status_t candidate_witness_clone(const candidate_wal_witness_t *source,
    const candidate_wal_memory_t *memory, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_witness_apply(candidate_wal_witness_t *witness, const candidate_wal_record_t *record);
void candidate_witness_settle(candidate_wal_witness_t *witness, const candidate_wal_info_t *transport);
bool candidate_witness_transaction_at(const candidate_wal_witness_t *witness, uint32_t index,
    candidate_semantic_transaction_t *out);
bool candidate_witness_group_at(const candidate_wal_witness_t *witness, uint32_t index,
    candidate_semantic_group_t *out);

#endif
