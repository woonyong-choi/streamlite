#ifndef STORAGE_CANDIDATE_WAL_SYSTEM_H
#define STORAGE_CANDIDATE_WAL_SYSTEM_H

#include "storage/candidate_wal.h"

#define CANDIDATE_SYSTEM_EVENTS 4096u
typedef struct {
    uint16_t type;
    uint64_t group_id, end_lsn, owner_epoch;
    union {
        struct { uint64_t page_id, previous_generation, next_generation, release_horizon; } allocation;
        struct { uint64_t previous_transaction, next_transaction, previous_page, next_page; } reserve;
        struct { uint64_t previous_epoch, next_epoch, previous_floor, next_floor; } epoch;
    } value;
} candidate_witness_system_event_t;
typedef struct {
    uint64_t transaction_highwater, page_highwater, current_epoch, retention_floor;
    uint32_t events, system_groups;
    bool has_reservation, has_epoch;
} candidate_witness_system_info_t;

#endif
