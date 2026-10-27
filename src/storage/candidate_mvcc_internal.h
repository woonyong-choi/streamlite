#ifndef STORAGE_CANDIDATE_MVCC_INTERNAL_H
#define STORAGE_CANDIDATE_MVCC_INTERNAL_H

#include <pthread.h>
#include <sys/types.h>

#include "storage/candidate_buffer.h"
#include "storage/candidate_mvcc.h"
#include "candidate_recovery_internal.h"

#define MVCC_ID_BATCH 64u
#define MVCC_EPOCH_OUTCOMES 1024u
#define MVCC_GROUP_HEADROOM 128u
#define MVCC_RECOVERY_HEADROOM_BYTES (2u * CANDIDATE_WAL_SEGMENT_BYTES)
#define MVCC_VERSION_BYTES 104u
#define MVCC_SLOT_BYTES 8u
#define MVCC_STRUCTURE_PAGES (2u * CANDIDATE_STORE_HEIGHT + 2u)
#define MVCC_STRUCTURE_EVENTS (CANDIDATE_STORE_HEIGHT + 3u)
#define MVCC_APPLY_GROUPS (CANDIDATE_MVCC_WRITES + 2u)
#define MVCC_TREE_STACK (CANDIDATE_STORE_HEIGHT * CANDIDATE_STORE_BRANCH_CHILDREN)

typedef struct {
    candidate_mvcc_key_t key;
    uint32_t size;
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
typedef struct { uint64_t transaction_id, snapshot_csn; } mvcc_view_t;
typedef struct { uint32_t slots, body_bytes; } mvcc_usage_t;
typedef struct {
    candidate_store_descriptor_t descriptor;
    uint64_t lsn;
} mvcc_store_t;
/* refs[0]은 root, refs[height-1]은 leaf이며 slots[i]는 refs[i] branch에서 고른 child 순번이다. */
typedef struct {
    candidate_page_ref_t refs[CANDIDATE_STORE_HEIGHT];
    uint64_t lsns[CANDIDATE_STORE_HEIGHT];
    uint32_t slots[CANDIDATE_STORE_HEIGHT], height;
} mvcc_path_t;
typedef struct {
    candidate_store_node_t nodes[CANDIDATE_STORE_HEIGHT], fresh[CANDIDATE_STORE_HEIGHT + 1u], node;
    candidate_store_child_t spill[CANDIDATE_STORE_BRANCH_CHILDREN + 1u];
    candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS];
    candidate_buffer_page_t group[MVCC_STRUCTURE_PAGES];
    candidate_page_ref_t stack[MVCC_TREE_STACK];
    uint8_t pages[MVCC_STRUCTURE_PAGES][CANDIDATE_PAGE_BYTES];
    uint8_t node_page[CANDIDATE_PAGE_BYTES], chain[CANDIDATE_PAGE_BYTES];
    candidate_page_ref_t chain_ref;
} mvcc_work_t;

struct candidate_mvcc {
    candidate_wal_memory_t memory;
    pthread_mutex_t state_lock, io_gate;
    pid_t process_id;
    uint64_t owner_id, owner_epoch, request_epoch, result_table_id, wal_budget_bytes;
    uint64_t next_id, id_end, visible_csn, last_csn, wal_transactions, next_generation, gate_entries, structure_groups;
    candidate_recovery_t *recovery;
    candidate_buffer_t *pool;
    mvcc_table_t *table;
    mvcc_work_t *work;
    candidate_page_ref_t store;
    candidate_wal_status_t fence;
};

candidate_wal_status_t mvcc_gate_lock(candidate_mvcc_t *m);
void mvcc_gate_unlock(candidate_mvcc_t *m);
void mvcc_state_lock(candidate_mvcc_t *m);
void mvcc_state_unlock(candidate_mvcc_t *m);
/* state_lock 아래에서 첫 원인만 남긴다. */
candidate_wal_status_t mvcc_fence(candidate_mvcc_t *m, candidate_wal_status_t status);
/* 실패가 pool 차단을 동반하면 owner도 같은 원인으로 차단한다. */
candidate_wal_status_t mvcc_observe_pool(candidate_mvcc_t *m, candidate_wal_status_t status);
const mvcc_outcome_t *mvcc_find_outcome(const mvcc_table_t *table, uint64_t transaction_id);
candidate_wal_status_t mvcc_find_active(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn, mvcc_active_t **out);
mvcc_reader_t *mvcc_find_reader(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader, candidate_wal_status_t *status);
/* 결과가 불확정인 거래를 UNKNOWN으로 두고 owner를 차단한다. */
candidate_wal_status_t mvcc_settle_unknown(candidate_mvcc_t *m, mvcc_active_t *active, candidate_wal_status_t status);

/* 아래는 io_gate 아래에서 pool의 최신 page를 pin·copy·unpin하며 frame lease를 남기지 않는다. */
candidate_wal_status_t mvcc_copy_page(candidate_mvcc_t *m, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES]);
candidate_wal_status_t mvcc_page_usage(const candidate_mvcc_t *m, const uint8_t *bytes, mvcc_usage_t *out);
/* 아래 둘은 state_lock 아래에서 table 상태로 판정한다. */
bool mvcc_is_visible(const candidate_mvcc_t *m, const candidate_page_version_t *version, mvcc_view_t view, uint64_t *csn);
bool mvcc_is_pending_other(const candidate_mvcc_t *m, const candidate_page_version_t *version, uint64_t own_transaction);
void mvcc_encode_response(uint64_t transaction_id, uint32_t affected_rows, uint8_t out[CANDIDATE_MVCC_RESPONSE_BYTES]);
/* io_gate 아래에서 outcome의 결과 참조를 실제 page에서 읽어 digest를 대조한 사본으로 채운다. */
candidate_wal_status_t mvcc_read_response(candidate_mvcc_t *m, const candidate_outcome_t *outcome,
    candidate_mvcc_response_t *out);

candidate_store_key_t mvcc_store_key(candidate_mvcc_key_t key);
candidate_wal_status_t mvcc_load_node(candidate_mvcc_t *m, candidate_page_ref_t ref, candidate_store_node_t *out, uint64_t *lsn);
candidate_wal_status_t mvcc_load_store(candidate_mvcc_t *m, mvcc_store_t *out);
/* 결과 nodes[0..height-1]은 path의 각 page 사본이다. */
candidate_wal_status_t mvcc_descend(candidate_mvcc_t *m, const mvcc_store_t *store, candidate_mvcc_key_t key, mvcc_path_t *path,
    candidate_store_node_t *nodes);
/* key가 있으면 true와 위치, 없으면 false와 삽입 위치를 at에 둔다. */
bool mvcc_leaf_find(const candidate_store_node_t *leaf, candidate_store_key_t key, uint32_t *at);
candidate_wal_status_t mvcc_lookup(candidate_mvcc_t *m, candidate_mvcc_key_t key, candidate_store_entry_t *out);
/* io_gate 아래에서 head 연쇄를 따라가며 단계마다 state_lock으로 판정한다. */
candidate_wal_status_t mvcc_find_visible(candidate_mvcc_t *m, const candidate_store_entry_t *entry, mvcc_view_t view,
    candidate_mvcc_value_t *out);
candidate_wal_status_t mvcc_find_head(candidate_mvcc_t *m, const candidate_store_entry_t *entry, uint64_t own_transaction,
    candidate_version_ref_t *ref, uint64_t *csn);
/* 열기에서 닿는 모든 head 연쇄의 거래가 결과표나 frozen CSN으로 판정되는지 확인한다. */
candidate_wal_status_t mvcc_check_store(candidate_mvcc_t *m);

candidate_wal_status_t mvcc_create_store(candidate_mvcc_t *m, candidate_page_ref_t *out);
/* apply 전에 append page 공간과 계획 key의 leaf 공간이 확보될 때까지 완성 system group을 쓴다. */
candidate_wal_status_t mvcc_prepare_structure(candidate_mvcc_t *m, const mvcc_active_t *active);
candidate_wal_status_t mvcc_apply(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn);

candidate_wal_status_t mvcc_plan_copy(const candidate_mvcc_t *m, const candidate_mvcc_request_t *request, mvcc_plan_t *out);
uint64_t mvcc_begin_bytes(bool needs_reserve);
uint64_t mvcc_credit_bytes(uint32_t writes);
uint64_t mvcc_terminal_bytes(void);
uint64_t mvcc_group_bytes(uint32_t pages);
candidate_wal_status_t mvcc_log_reserve(candidate_mvcc_t *m);
candidate_wal_status_t mvcc_log_begin(candidate_mvcc_t *m, mvcc_active_t *active, const candidate_outcome_t *outcome);
candidate_wal_status_t mvcc_log_commit(candidate_mvcc_t *m, mvcc_active_t *active, candidate_outcome_t *outcome);
candidate_wal_status_t mvcc_log_abort(candidate_mvcc_t *m, mvcc_active_t *active, candidate_outcome_t *outcome);

#endif
