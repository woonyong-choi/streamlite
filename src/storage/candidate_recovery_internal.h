#ifndef STORAGE_CANDIDATE_RECOVERY_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_INTERNAL_H

#include "storage/candidate_recovery.h"
#include "storage/candidate_wal_witness.h"

typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    const candidate_control_state_t *anchor;
    const candidate_wal_memory_t *memory;
    bool allow_group_tail;
} candidate_terminal_options_t;

candidate_wal_status_t candidate_terminal_prepare(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_terminal_resolve(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_terminal_info_t *out);
candidate_wal_status_t candidate_terminal_repair_wal(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_wal_info_t *out);

candidate_wal_status_t candidate_recovery_validate(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);

#endif
