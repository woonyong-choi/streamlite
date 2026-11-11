#ifndef STORAGE_CANDIDATE_WAL_WITNESS_DATA_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_WITNESS_DATA_INTERNAL_H

#include "storage/candidate_wal_witness.h"
#include "candidate_wal_system_internal.h"

typedef struct {
    candidate_witness_transaction_t value;
    uint64_t owner_epoch;
    uint64_t result_limit;
    uint64_t chunk_bytes;
} transaction_t;

typedef struct { uint64_t id, transaction_id, end_lsn; } group_t;
typedef struct { uint64_t id, generation, image_lsn; bool has_image; } page_t;
typedef struct {
    uint64_t id, transaction_id, first_lsn, last_lsn, owner_epoch;
    uint32_t expected_pages, expected_records, records, pages, crc;
    page_t seen[CANDIDATE_WITNESS_PAGES];
} pending_group_t;

struct candidate_wal_witness {
    candidate_witness_info_t info;
    candidate_wal_memory_t memory;
    candidate_wal_status_t status;
    uint8_t uuid[16];
    unsigned active;
    transaction_t transactions[CANDIDATE_WITNESS_TRANSACTIONS];
    group_t groups[CANDIDATE_WITNESS_GROUPS];
    pending_group_t pending;
    candidate_wal_system_t *system;
};

#endif
