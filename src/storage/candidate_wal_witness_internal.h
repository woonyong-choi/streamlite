#ifndef STORAGE_CANDIDATE_WAL_WITNESS_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_WITNESS_INTERNAL_H

#include "storage/candidate_wal_witness.h"
#include "storage/candidate_checkpoint.h"

bool candidate_witness_checkpoint_at(const candidate_wal_witness_t *witness, uint32_t index,
    candidate_checkpoint_transaction_t *out);
candidate_wal_status_t candidate_witness_analyze_observed(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_control_state_t *anchor,
    candidate_wal_visitor_t observer, void *context, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_witness_checkpoint_scan(candidate_wal_t *wal,
    candidate_wal_witness_t *witness, const candidate_checkpoint_t *checkpoint,
    candidate_wal_visitor_t visitor, candidate_wal_visitor_t observer, void *context, candidate_wal_info_t *transport);

#endif
