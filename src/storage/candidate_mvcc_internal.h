#ifndef STORAGE_CANDIDATE_MVCC_INTERNAL_H
#define STORAGE_CANDIDATE_MVCC_INTERNAL_H

#include <pthread.h>
#include <sys/types.h>

#include "storage/candidate_buffer.h"
#include "storage/candidate_mvcc.h"
#include "candidate_recovery_internal.h"

#define MVCC_PAGES (CANDIDATE_MVCC_ROWS + 1u)
#define MVCC_PLAN_PAGES (CANDIDATE_MVCC_WRITES + 1u)
#define MVCC_ID_BATCH 64u
#define MVCC_EPOCH_OUTCOMES 1024u
#define MVCC_GROUP_HEADROOM 128u
#define MVCC_RECOVERY_HEADROOM_BYTES (2u * CANDIDATE_WAL_SEGMENT_BYTES)
#define MVCC_VERSION_BYTES 104u
#define MVCC_SLOT_BYTES 8u

typedef struct {
    candidate_mvcc_key_t key;
    uint32_t page, size;
    uint8_t bytes[CANDIDATE_MVCC_PAYLOAD_BYTES];
} mvcc_write_t;
typedef struct {
    uint8_t nonce[16], digest[32];
    uint32_t count;
    mvcc_write_t writes[CANDIDATE_MVCC_WRITES];
} mvcc_plan_t;
typedef struct {
    uint64_t first_lsn, last_lsn, records;
    uint32_t crc;
} mvcc_chain_t;
typedef struct {
    candidate_outcome_t outcome;
    candidate_mvcc_state_t state;
} mvcc_outcome_t;
typedef struct {
    uint64_t generation, snapshot_csn, group_id, group_end, wal_credit;
    uint32_t outcome;
    mvcc_plan_t plan;
    mvcc_chain_t chain;
    candidate_version_ref_t versions[CANDIDATE_MVCC_WRITES], result;
    bool is_applied;
} mvcc_active_t;
typedef struct { uint64_t generation, csn; } mvcc_reader_t;
typedef struct {
    mvcc_outcome_t outcomes[CANDIDATE_MVCC_HISTORY];
    mvcc_active_t active[CANDIDATE_MVCC_TRANSACTIONS];
    mvcc_reader_t readers[CANDIDATE_MVCC_READERS];
    uint32_t outcome_count, active_count, reader_count;
} mvcc_table_t;
typedef struct {
    candidate_mvcc_key_t key;
    uint32_t page;
} mvcc_row_t;
typedef struct { uint64_t transaction_id, snapshot_csn; } mvcc_view_t;
typedef struct { uint32_t slots, body_bytes; } mvcc_usage_t;

struct candidate_mvcc {
    candidate_wal_memory_t memory;
    pthread_mutex_t state_lock, io_gate;
    pid_t process_id;
    uint64_t owner_id, owner_epoch, request_epoch, result_table_id, wal_budget_bytes;
    uint64_t next_id, id_end, visible_csn, last_csn, wal_transactions, next_generation, gate_entries;
    candidate_recovery_t *recovery;
    candidate_buffer_t *pool;
    mvcc_table_t *table;
    uint8_t *scratch;
    mvcc_row_t rows[CANDIDATE_MVCC_ROWS];
    candidate_page_ref_t pages[MVCC_PAGES];
    uint32_t row_count, page_count, result_page;
    candidate_wal_status_t fence;
};

candidate_wal_status_t mvcc_gate_lock(candidate_mvcc_t *m);
void mvcc_gate_unlock(candidate_mvcc_t *m);
void mvcc_state_lock(candidate_mvcc_t *m);
void mvcc_state_unlock(candidate_mvcc_t *m);
/* state_lock 아래에서 첫 원인만 남긴다. */
candidate_wal_status_t mvcc_fence(candidate_mvcc_t *m, candidate_wal_status_t status);
const mvcc_outcome_t *mvcc_find_outcome(const mvcc_table_t *table, uint64_t transaction_id);
const mvcc_row_t *mvcc_find_row(const candidate_mvcc_t *m, candidate_mvcc_key_t key);

/* io_gate 아래에서 pool의 최신 page를 pin·copy·unpin하며 frame lease를 남기지 않는다. */
candidate_wal_status_t mvcc_page_copy(candidate_mvcc_t *m, uint32_t page, uint8_t out[CANDIDATE_PAGE_BYTES]);
candidate_wal_status_t mvcc_page_usage(const candidate_mvcc_t *m, const uint8_t *bytes, mvcc_usage_t *out);
/* 아래 둘은 state_lock 아래에서 table 상태로 slot을 뒤에서부터 판정한다. */
candidate_wal_status_t mvcc_find_visible(const candidate_mvcc_t *m, const uint8_t *bytes, candidate_mvcc_key_t key,
    mvcc_view_t view, candidate_mvcc_value_t *out);
candidate_wal_status_t mvcc_find_head(const candidate_mvcc_t *m, const uint8_t *bytes, candidate_mvcc_key_t key,
    uint64_t own_transaction, candidate_version_ref_t *ref, uint64_t *csn);
candidate_wal_status_t mvcc_check_page(const candidate_mvcc_t *m, const uint8_t *bytes);
candidate_wal_status_t mvcc_merge_page(const candidate_mvcc_t *m, const uint8_t *base, uint32_t page,
    mvcc_active_t *active, uint8_t out[CANDIDATE_PAGE_BYTES]);
void mvcc_encode_response(uint64_t transaction_id, uint32_t affected_rows, uint8_t out[CANDIDATE_MVCC_RESPONSE_BYTES]);
/* io_gate 아래에서 outcome의 결과 참조를 실제 page에서 읽어 digest를 대조한 사본으로 채운다. */
candidate_wal_status_t mvcc_read_response(candidate_mvcc_t *m, const candidate_outcome_t *outcome,
    candidate_mvcc_response_t *out);

candidate_wal_status_t mvcc_plan_copy(const candidate_mvcc_t *m, const candidate_mvcc_request_t *request, mvcc_plan_t *out);
uint32_t mvcc_plan_pages(const candidate_mvcc_t *m, const mvcc_plan_t *plan, uint32_t pages[MVCC_PLAN_PAGES]);
uint64_t mvcc_begin_bytes(bool needs_reserve);
uint64_t mvcc_credit_bytes(uint32_t pages);
uint64_t mvcc_group_bytes(uint32_t pages);
candidate_wal_status_t mvcc_log_reserve(candidate_mvcc_t *m);
candidate_wal_status_t mvcc_log_begin(candidate_mvcc_t *m, mvcc_active_t *active, const candidate_outcome_t *outcome);
candidate_wal_status_t mvcc_log_commit(candidate_mvcc_t *m, mvcc_active_t *active, candidate_outcome_t *outcome);
candidate_wal_status_t mvcc_log_abort(candidate_mvcc_t *m, mvcc_active_t *active, candidate_outcome_t *outcome);

#endif
