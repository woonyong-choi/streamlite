#ifndef STORAGE_CANDIDATE_RECOVERY_VIEW_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_VIEW_INTERNAL_H

#include "storage/candidate_allocation.h"
#include "candidate_outcome_witness_internal.h"
#include "candidate_recovery_internal.h"
#include "candidate_wal_identity_internal.h"

#define CANDIDATE_RECOVERY_HISTORY_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)

typedef void (*candidate_recovery_mark_t)(void *context, const candidate_witness_transaction_t *transaction);
typedef struct { candidate_page_ref_t ref; uint64_t lsn; } candidate_recovery_page_t;
typedef struct {
    candidate_catalog_load_t load;
    candidate_allocation_t *allocation;
    const candidate_wal_witness_t *witness;
    candidate_recovery_info_t info;
    candidate_wal_status_t status;
    uint32_t count, pending_count;
    bool allows_unfinished;
    candidate_recovery_mark_t mark;
    void *mark_context;
    candidate_page_ref_t pending[CANDIDATE_WITNESS_PAGES];
    candidate_recovery_page_t pages[CANDIDATE_RECOVERY_HISTORY_SLOTS];
} candidate_recovery_graph_t;
struct candidate_recovery_view {
    candidate_wal_memory_t memory;
    candidate_recovery_view_info_t info;
    candidate_recovery_graph_t *graph;
    candidate_admissions_t *admissions;
    candidate_wal_witness_t *witness;
    candidate_catalog_t *catalog;
    candidate_wal_identity_t *identity;
};
typedef struct {
    const candidate_wal_witness_t *witness;
    bool keep[CANDIDATE_WITNESS_TRANSACTIONS];
} candidate_recovery_marks_t;
typedef struct {
    uint64_t transactions, commits, aborts, groups, system_groups, events, max_started_id;
} candidate_recovery_history_t;
typedef struct {
    candidate_recovery_history_t history;
    uint32_t transactions, unfinished, commits, aborts, groups, system_groups, events, admissions;
    uint32_t started_intervals, tombstones;
    size_t identity_bytes;
} candidate_recovery_closure_t;

/* source와 같은 anchor·mode의 request로 source cut 뒤 기록만 읽어 공통 graph/outcome 검증을 통과한 closure를 소유한다. */
/* 같은 anchor가 아니면 UNSUPPORTED, mode가 다르면 INVALID다; suffix는 다음 segment로 이어 읽고 cut 뒤 checkpoint 기록은 WAL을 poison하지 않는 UNSUPPORTED다. */
/* source는 바꾸지 않고 loader는 호출 동안만 빌리며 WAL prefix 동일성은 caller 책임이고 실패 시 *out은 NULL이다. */
candidate_wal_status_t candidate_recovery_view_continue(candidate_wal_t *wal,
    const candidate_recovery_view_request_t *request, const candidate_recovery_view_t *source,
    candidate_recovery_view_t **out);
/* history의 system_groups는 reducer 누계이고 나머지는 은퇴 계수와 남은 closure 계수의 합이다. */
candidate_recovery_closure_t candidate_recovery_view_closure(const candidate_recovery_view_t *view);
/* 반환 witness는 view가 소유하며 view를 닫기 전까지만 유효하다. */
const candidate_wal_witness_t *candidate_recovery_view_witness(const candidate_recovery_view_t *view);

candidate_wal_status_t candidate_recovery_view_check_request(const candidate_recovery_view_request_t *request);
candidate_wal_status_t candidate_recovery_graph_clone(const candidate_recovery_graph_t *source,
    const candidate_wal_memory_t *memory, candidate_recovery_graph_t **out);
bool candidate_recovery_view_observe(void *view, const candidate_wal_record_t *record);
candidate_wal_status_t candidate_recovery_graph_check(candidate_recovery_graph_t *graph,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load, const candidate_wal_witness_t *witness);
candidate_wal_status_t candidate_recovery_view_verify(candidate_recovery_view_t *view, candidate_wal_t *wal,
    const candidate_catalog_load_t *load);
candidate_wal_status_t candidate_recovery_view_check_cut(const candidate_recovery_view_t *view, candidate_wal_t *wal,
    const uint8_t uuid[16]);
void candidate_recovery_view_settle(candidate_recovery_view_t *view);
void candidate_recovery_mark_live(void *marks, const candidate_witness_transaction_t *transaction);
candidate_wal_status_t candidate_recovery_view_compact(candidate_recovery_view_t *view, candidate_recovery_marks_t *marks);

#endif
