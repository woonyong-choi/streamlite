#ifndef STORAGE_CANDIDATE_WAL_WITNESS_STATE_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_WITNESS_STATE_INTERNAL_H

#include "storage/candidate_semantic_seed.h"
#include "candidate_wal_identity_internal.h"

candidate_wal_status_t candidate_witness_clone(const candidate_wal_witness_t *source,
    const candidate_wal_memory_t *memory, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_witness_apply(candidate_wal_witness_t *witness, const candidate_wal_record_t *record);
void candidate_witness_settle(candidate_wal_witness_t *witness, const candidate_wal_info_t *transport);
bool candidate_witness_transaction_at(const candidate_wal_witness_t *witness, uint32_t index,
    candidate_semantic_transaction_t *out);
bool candidate_witness_group_at(const candidate_wal_witness_t *witness, uint32_t index,
    candidate_semantic_group_t *out);

/* reducer 전에 resident가 아닌 기록된 시작 ID와 기록된 최대 시작 ID보다 작은 예약 previous를 거절한다. */
candidate_wal_status_t candidate_witness_check_retired(const candidate_wal_witness_t *witness,
    const candidate_wal_identity_t *identity, const candidate_wal_record_t *record);
/* reducer가 받아들인 ADMIT의 (epoch, nonce)가 identity에 이미 있으면 거절한다. */
candidate_wal_status_t candidate_witness_check_nonce(const candidate_wal_identity_t *identity,
    const candidate_wal_record_t *record);
bool candidate_witness_index(const candidate_wal_witness_t *witness, uint64_t transaction_id, uint32_t *index);
/* 모든 resident 시작 ID와 허용 epoch nonce를 identity에 먼저 기록하며 그 실패 시 witness와 identity는 그대로다. */
/* 그 뒤 keep 또는 미종결 거래, 미종결 거래의 group과 마지막 group, system 보존 event만 남긴다. */
candidate_wal_status_t candidate_witness_retain(candidate_wal_witness_t *witness, const bool *keep,
    uint64_t max_allocated_id, candidate_wal_identity_t *identity);

#endif
