#ifndef STORAGE_CANDIDATE_WAL_SYSTEM_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_SYSTEM_INTERNAL_H

#include "storage/candidate_wal_system.h"
#include "storage/candidate_control.h"

typedef struct candidate_wal_system candidate_wal_system_t;
candidate_wal_status_t candidate_system_create(const uint8_t uuid[16],
    const candidate_wal_memory_t *memory, candidate_wal_system_t **out);
void candidate_system_close(candidate_wal_system_t **system);
size_t candidate_system_resident_bytes(const candidate_wal_system_t *system);
void candidate_system_begin(candidate_wal_system_t *system);
candidate_wal_status_t candidate_system_bind_checkpoint(candidate_wal_system_t *system,
    const candidate_control_state_t *anchor);
candidate_wal_status_t candidate_system_image(candidate_wal_system_t *system, const candidate_wal_record_t *record);
candidate_wal_status_t candidate_system_event(candidate_wal_system_t *system, const candidate_wal_record_t *record);
candidate_wal_status_t candidate_system_finish(candidate_wal_system_t *system, uint64_t group,
    uint64_t end_lsn, uint64_t owner_epoch, uint64_t minimum_active_epoch, bool is_system);
candidate_witness_system_info_t candidate_system_info(const candidate_wal_system_t *system);
const candidate_witness_system_event_t *candidate_system_at(const candidate_wal_system_t *system, uint32_t index);

#endif
