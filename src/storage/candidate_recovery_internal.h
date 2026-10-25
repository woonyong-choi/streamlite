#ifndef STORAGE_CANDIDATE_RECOVERY_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_INTERNAL_H

#include "storage/candidate_recovery.h"

candidate_wal_status_t candidate_recovery_validate(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);

#endif
