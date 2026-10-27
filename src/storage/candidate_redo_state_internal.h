#ifndef STORAGE_CANDIDATE_REDO_STATE_INTERNAL_H
#define STORAGE_CANDIDATE_REDO_STATE_INTERNAL_H

#include "storage/candidate_allocation.h"
#include "storage/candidate_wal_witness.h"
#include "candidate_redo_internal.h"

#define CANDIDATE_REDO_HISTORY_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)

typedef struct {
    uint64_t id, generation, last_lsn, data_generation, data_lsn;
    unsigned flags;
    uint32_t record_back;
} candidate_redo_history_t;
typedef struct {
    uint64_t lsn, previous_record, group_end;
    uint8_t digest[32];
} candidate_redo_source_t;
typedef struct {
    candidate_redo_history_t *history;
    candidate_redo_source_t *source;
    uint64_t generation, previous_lsn, record_lsn;
    bool has_image, should_write;
    uint8_t bytes[CANDIDATE_PAGE_BYTES];
} candidate_redo_stage_t;
typedef struct {
    uint8_t digest[32];
    uint8_t *scratch;
    candidate_wal_status_t status;
    uint64_t page_records;
} candidate_redo_fingerprint_t;
typedef struct {
    candidate_redo_options_t options;
    candidate_wal_t *wal;
    candidate_redo_info_t info;
    candidate_redo_fingerprint_t fingerprint;
    candidate_redo_history_t history[CANDIDATE_REDO_HISTORY_SLOTS];
    candidate_redo_stage_t staged[CANDIDATE_WITNESS_PAGES];
    uint8_t scratch[CANDIDATE_WAL_RECORD_BYTES];
    uint64_t group_id, transaction_id, owner_epoch, last_lsn, group_lsn;
    uint32_t group_crc, expected_pages, expected_records, group_records, skipped_pages;
    unsigned staged_count;
    bool is_applying, is_preview;
    uint64_t *seeds;
    candidate_redo_source_t *records;
    uint64_t record_count, record_capacity;
} candidate_redo_workspace_t;
struct candidate_redo_preview {
    candidate_wal_memory_t memory;
    candidate_wal_t *wal;
    uint8_t page[CANDIDATE_PAGE_BYTES], known[CANDIDATE_PAGE_BYTES];
    candidate_redo_workspace_t workspace;
};

candidate_wal_status_t candidate_redo_visit_record(candidate_redo_workspace_t *workspace, const candidate_wal_record_t *record);
candidate_redo_history_t *candidate_redo_find_history(candidate_redo_workspace_t *workspace, uint64_t id);
uint32_t candidate_redo_page_crc(const uint8_t *page);

#endif
