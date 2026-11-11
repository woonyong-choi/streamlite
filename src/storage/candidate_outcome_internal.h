#ifndef STORAGE_CANDIDATE_OUTCOME_INTERNAL_H
#define STORAGE_CANDIDATE_OUTCOME_INTERNAL_H

#include "storage/candidate_outcome_witness.h"

candidate_wal_status_t candidate_outcome_preview(candidate_wal_t *wal,
    const candidate_outcome_witness_options_t *options, uint64_t owner_epoch);

#endif
