#ifndef TEST_CANDIDATE_RECOVERY_CLOSURE_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_CLOSURE_FIXTURE_H

#include "test_candidate_recovery_view_fixture.h"
#include "../src/storage/candidate_recovery_view_internal.h"
#include "../src/storage/candidate_wal_witness_state_internal.h"

#define CF_CHURN_PAGES 55u
#define CF_EPOCHS 32u
#define CF_FIRST_ID 1000u
#define CF_TRANSACTION_HIGHWATER (1u << 20)
#define CF_PAD_RESERVE (2u * 8744u)
#define CF_ENVELOPE_BYTES (128u * 1024u * 1024u)
#define CF_CARRIED_BYTES 70000u

typedef struct {
    uint64_t transactions, commits, aborts, groups, system_groups, events, max_started;
    uint64_t admitted[CF_EPOCHS];
} cf_tally_t;
typedef void (*cf_hook_t)(void *context);
typedef struct {
    uint64_t deny_before, reads, read_bytes, lowest_lsn, denied, fail_from, failed, hook_from, hook_call;
    cf_hook_t on_read;
    void *context;
    bool is_armed;
} cf_io_t;
typedef struct {
    uint64_t records, frame_bytes, header_bytes, padding_bytes;
} cf_suffix_t;
typedef struct {
    uint64_t id, epoch, limit, owner, terminal_end, result_bytes;
    uint32_t chunks;
    uint8_t nonce[16], operation[32], result_digest[32];
    candidate_version_ref_t result;
    bool is_terminal;
} cf_admission_t;
typedef struct {
    vf_t f;
    candidate_recovery_view_mode_t mode;
    uint64_t next_id, epoch, floor, transaction_highwater, page_highwater;
    uint64_t epoch_root, delta_page, churn_first, last_churn_end, last_reserve_end;
    uint64_t generation[CF_CHURN_PAGES];
    bool is_churn_free;
    uint8_t delta_body[CANDIDATE_PAGE_BYTES - 64];
    cf_tally_t tally;
    cf_admission_t *ledger;
    uint32_t ledger_count, ledger_capacity;
    uint64_t last_plain;
} cf_t;
typedef struct {
    candidate_version_ref_t ref;
    uint64_t total, offset, length;
    uint32_t index, count;
    uint8_t digest[32];
} cf_chunk_t;
typedef struct { uint64_t id; uint32_t crc; } cf_group_t;
typedef struct {
    fx_txn_t t;
    uint8_t digest[32];
    candidate_version_ref_t ref;
    bool is_active;
} cf_carried_t;
typedef struct {
    size_t allocator_peak, wal_resident, stack_bound, total;
} cf_budget_t;
typedef struct {
    candidate_recovery_closure_t closure;
    candidate_identity_info_t identity;
    uint8_t *view, *witness, *admissions, *graph, *events;
    size_t view_bytes, witness_bytes, admission_bytes, graph_bytes, event_bytes;
} cf_source_t;
typedef struct {
    const char *name;
    void (*act)(cf_t *c);
    candidate_wal_status_t expected;
    void (*expect)(const cf_t *c, const candidate_recovery_view_t *v);
} cf_row_t;
typedef struct {
    const char *name;
    candidate_recovery_view_mode_t mode;
    void (*setup)(cf_t *c);
    void (*expect_setup)(const cf_t *c, const candidate_recovery_view_t *v);
    const cf_row_t *rows;
    unsigned count;
} cf_row_group_t;

extern cf_io_t cf_io;

void cf_boot(cf_t *c, const char *tag, candidate_recovery_view_mode_t mode);
void cf_free(cf_t *c);
void cf_reown(cf_t *c, uint64_t owner);
candidate_recovery_view_request_t cf_request(cf_t *c);
candidate_wal_status_t cf_open(cf_t *c, candidate_recovery_view_t **out);
candidate_wal_status_t cf_continue(cf_t *c, const candidate_recovery_view_t *source, candidate_recovery_view_t **out);
void cf_arm(uint64_t deny_before);
void cf_disarm(void);
void cf_begin(cf_t *c, fx_txn_t *t, uint64_t id);
void cf_admit(cf_t *c, fx_txn_t *t, uint64_t id, uint64_t limit, const uint8_t *nonce);
void cf_end(cf_t *c, fx_txn_t *t, bool is_commit);
void cf_delta(cf_t *c, fx_txn_t *t, uint32_t size);
void cf_chunk(cf_t *c, fx_txn_t *t, const cf_chunk_t *chunk);
void cf_group_open(cf_t *c, fx_txn_t *t, cf_group_t *g);
void cf_group_close(cf_t *c, fx_txn_t *t, const cf_group_t *g);
void cf_append_raw(cf_t *c, uint16_t type, uint64_t transaction_id, const uint8_t *body, size_t size);
uint64_t cf_unit(cf_t *c, bool is_admitted, bool is_commit);
cf_chunk_t cf_carry_chunk(const cf_carried_t *k, uint32_t index);
void cf_carry_start(cf_t *c, cf_carried_t *k);
void cf_carry_last(cf_t *c, cf_carried_t *k);
void cf_carry_abort(cf_t *c, cf_carried_t *k);
uint64_t cf_room(const cf_t *c);
uint64_t cf_churn_bytes(const cf_t *c);
void cf_churn(cf_t *c);
void cf_epoch(cf_t *c, uint64_t next_floor);
void cf_reserve(cf_t *c, uint64_t previous_transaction, uint64_t next_transaction, uint64_t next_page);
void cf_pad(cf_t *c);
void cf_pad_to(cf_t *c, uint64_t room);
void cf_cross_segment(cf_t *c);
void cf_filler(cf_t *c, uint64_t bytes);
void cf_flush(cf_t *c);
uint64_t cf_window_admissions(const cf_t *c);
uint64_t cf_suffix_records(const candidate_wal_info_t *before, const candidate_wal_info_t *after);
uint64_t cf_suffix_bytes(const candidate_wal_info_t *before, const candidate_wal_info_t *after);
cf_suffix_t cf_suffix_of(cf_t *c, const candidate_wal_info_t *before, const candidate_wal_info_t *after);
cf_suffix_t cf_expect_reads(const char *tag, cf_t *c, const candidate_wal_info_t *before, const candidate_wal_info_t *after);
bool cf_semantic_of(const candidate_recovery_view_t *v, uint64_t id, candidate_semantic_transaction_t *out);
bool cf_admission_of(const candidate_recovery_view_t *v, uint64_t id, candidate_semantic_admission_t *out);
void cf_expect_history(const cf_t *c, const candidate_recovery_closure_t *closure);
void cf_expect_resident(const cf_t *c, const candidate_recovery_view_t *v, uint32_t plain_active);
void cf_expect_rebuild(cf_t *c);
void cf_expect_budget(const char *tag, const cf_t *c, size_t floor_bytes);
void cf_expect_outcomes(cf_t *c, const candidate_recovery_view_t *v);
void cf_print(const char *tag, const cf_t *c, const candidate_recovery_view_t *v);

void cf_source_take(const candidate_recovery_view_t *v, cf_source_t *s);
bool cf_source_same(const candidate_recovery_view_t *v, const cf_source_t *s);
void cf_source_free(cf_source_t *s);
void cf_run_rows(const cf_row_group_t *g);
void cf_dependency_rows(void);
void cf_active_rows(void);
void cf_live_rows(void);

#endif
