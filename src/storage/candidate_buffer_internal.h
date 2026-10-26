#ifndef STORAGE_CANDIDATE_BUFFER_INTERNAL_H
#define STORAGE_CANDIDATE_BUFFER_INTERNAL_H

#include <sys/types.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_buffer.h"
#include "storage/candidate_checkpoint.h"
#include "storage/candidate_wal_witness.h"

#define BUFFER_DIRECTORY_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)
#define BUFFER_NO_FRAME UINT32_MAX
#define BUFFER_SCRATCH_BYTES CANDIDATE_WAL_RECORD_BYTES
#ifndef BUFFER_COUNTER_LIMIT
#define BUFFER_COUNTER_LIMIT UINT64_MAX
#endif

typedef struct {
    candidate_wal_memory_t base;
    size_t limit, used, peak, borrowed;
    bool is_refused;
} buffer_meter_t;

typedef struct {
    uint64_t id, generation, last_lsn, image_lsn;
    uint32_t frame;
} buffer_page_t;

typedef struct {
    candidate_page_ref_t ref;
    uint64_t page_lsn, rec_lsn, pending_rec_lsn, frame_generation, dirty_generation;
    uint64_t contributors[CANDIDATE_BUFFER_CONTRIBUTORS];
    uint32_t contributor_count, pins;
    bool is_dirty, is_inflight, is_referenced;
} buffer_frame_t;

typedef struct {
    uint64_t lease_id;
    uint32_t slot;
} buffer_lease_t;

typedef struct {
    uint64_t end_lsn, records, tail_bytes;
    uint8_t digest[32];
} buffer_cut_t;

typedef struct {
    uint16_t type;
    uint64_t lsn, transaction_id, previous_lsn, owner_epoch;
    size_t offset, bytes;
} buffer_record_t;

typedef struct {
    uint64_t id, generation, previous_lsn;
    uint32_t record;
    bool is_image;
} buffer_change_t;

typedef struct {
    uint64_t group_id, transaction_id, begin_lsn, end_lsn;
    uint32_t records, pages;
    size_t used;
    buffer_record_t list[CANDIDATE_WITNESS_RECORDS];
    buffer_change_t changes[CANDIDATE_BUFFER_GROUP_PAGES];
    uint32_t slots[CANDIDATE_BUFFER_GROUP_PAGES];
} buffer_capture_t;

typedef struct {
    buffer_cut_t cut;
    candidate_witness_info_t witness;
    candidate_witness_system_info_t system;
    uint64_t last_group_end, max_group_id, last_checkpoint_begin, highest_owner;
    uint32_t unadmitted_groups;
} buffer_observation_t;

typedef struct {
    uint32_t slot;
    uint64_t id, frame_generation, dirty_generation, required_lsn;
} buffer_snapshot_t;

typedef struct {
    buffer_snapshot_t *list;
    uint8_t *bytes;
    uint32_t capacity;
    size_t list_size, bytes_size;
} buffer_batch_t;

typedef enum { BUFFER_IDLE, BUFFER_ADMIT, BUFFER_PIN, BUFFER_FLUSH, BUFFER_CHECKPOINT, BUFFER_CLOSE } buffer_operation_t;

struct candidate_buffer {
    candidate_buffer_options_t options;
    buffer_meter_t meter;
    candidate_wal_memory_t memory;
    candidate_control_state_t anchor;
    buffer_observation_t observed;
    pid_t process_id;
    uint64_t pool_id, lease_counter, baseline_lsn, admitted_lsn, fpi_floor_lsn;
    uint64_t groups_admitted, pages_written, data_syncs, evictions;
    uint32_t frame_count, lease_capacity, clock_hand, known_pages, active_leases, transaction_count;
    buffer_frame_t *frames;
    uint8_t *bytes;
    buffer_page_t *directory;
    buffer_lease_t *leases;
    candidate_checkpoint_transaction_t *transactions;
    uint8_t *staging, *install, *scratch;
    size_t staging_bytes, frame_bytes;
    buffer_operation_t operation;
    bool is_writing, is_fenced;
};

typedef struct {
    buffer_observation_t observed;
    buffer_capture_t *capture;
    candidate_wal_witness_t *witness;
    uint64_t target_end;
    bool is_opening;
} buffer_scan_t;

candidate_wal_status_t candidate_buffer_enter(candidate_buffer_t *pool, buffer_operation_t operation);
void candidate_buffer_leave(candidate_buffer_t *pool, buffer_operation_t previous);
candidate_wal_status_t candidate_buffer_result(candidate_buffer_t *pool, candidate_wal_status_t status);
candidate_wal_status_t candidate_buffer_fence(candidate_buffer_t *pool, candidate_wal_status_t status);
void candidate_buffer_count(uint64_t *counter, uint64_t amount);
void *candidate_buffer_allocate(candidate_buffer_t *pool, size_t size);
void candidate_buffer_release(candidate_buffer_t *pool, void *bytes, size_t size);

buffer_page_t *candidate_buffer_page(candidate_buffer_t *pool, uint64_t id);
candidate_wal_status_t candidate_buffer_add_page(candidate_buffer_t *pool, uint64_t id, buffer_page_t **out);
uint8_t *candidate_buffer_frame_bytes(candidate_buffer_t *pool, uint32_t slot);
const candidate_checkpoint_transaction_t *candidate_buffer_transaction(const candidate_buffer_t *pool, uint64_t id);
candidate_wal_status_t candidate_buffer_check_page(const uint8_t uuid[16], const uint8_t *bytes, candidate_page_info_t *info);
candidate_wal_status_t candidate_buffer_prove(candidate_buffer_t *pool, const candidate_control_state_t *anchor);
candidate_wal_status_t candidate_buffer_load(candidate_buffer_t *pool, const buffer_page_t *page, uint8_t *out);
candidate_wal_status_t candidate_buffer_plan_frames(candidate_buffer_t *pool, const uint64_t *ids, uint32_t count,
    uint32_t *slots);
void candidate_buffer_take_frame(candidate_buffer_t *pool, uint32_t slot, buffer_page_t *page);

candidate_wal_status_t candidate_buffer_cut_add(candidate_buffer_t *pool, buffer_cut_t *cut, const candidate_wal_record_t *record);
candidate_wal_status_t candidate_buffer_scan(candidate_buffer_t *pool, buffer_scan_t *scan);
candidate_wal_status_t candidate_buffer_source_failure(candidate_buffer_t *pool, candidate_wal_status_t status);
candidate_wal_status_t candidate_buffer_verify_cut(candidate_buffer_t *pool, const buffer_cut_t *expected);
candidate_wal_status_t candidate_buffer_commit_scan(candidate_buffer_t *pool, buffer_scan_t *scan);
void candidate_buffer_end_scan(buffer_scan_t *scan);
candidate_wal_status_t candidate_buffer_check_capture(candidate_buffer_t *pool, const buffer_scan_t *scan,
    candidate_buffer_admission_t *out);
candidate_wal_status_t candidate_buffer_check_versions(const uint8_t uuid[16], const uint8_t *bytes,
    const candidate_wal_witness_t *witness);
candidate_wal_status_t candidate_buffer_check_capacity(candidate_buffer_t *pool, buffer_capture_t *capture);
candidate_wal_status_t candidate_buffer_install(candidate_buffer_t *pool, const buffer_scan_t *scan);
candidate_wal_status_t candidate_buffer_admit_group(candidate_buffer_t *pool, uint64_t end_lsn, candidate_buffer_admission_t *out);

candidate_wal_status_t candidate_buffer_prepare_writeback(candidate_buffer_t *pool, uint32_t max_pages, buffer_batch_t *batch);
void candidate_buffer_release_writeback(candidate_buffer_t *pool, buffer_batch_t *batch);
candidate_wal_status_t candidate_buffer_writeback(candidate_buffer_t *pool, buffer_batch_t *batch, const buffer_cut_t *expected,
    candidate_buffer_flush_t *out);

#endif
