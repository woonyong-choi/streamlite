#include "test_candidate_recovery_closure_fixture.h"

#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"
#include "../src/storage/candidate_wal_witness_state_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define PREVIEW CANDIDATE_RECOVERY_VIEW_PREVIEW
#define CYCLE_UNITS 1200u
#define MAX_CYCLES 12u
#define LEGACY_LIMIT 4096u
#define RETAINED_IDS 4u
#define RETIRED_IDS 10u
#define RETIRED_FIRST (CF_FIRST_ID + RETAINED_IDS)

unsigned checks;

static const char *mode_name(candidate_recovery_view_mode_t mode) { return mode == RESOLVED ? "resolved" : "preview"; }

static cf_carried_t carried;

static uint64_t admitted_total(const cf_tally_t *t)
{
    uint64_t total = 0;
    for (unsigned e = 0; e < CF_EPOCHS; e++) total += t->admitted[e];
    return total;
}

static bool exceeds_limits(const cf_t *c)
{
    const cf_tally_t *t = &c->tally;
    bool has_admissions = c->mode == RESOLVED || admitted_total(t) > LEGACY_LIMIT;
    bool has_terminals = t->commits + t->aborts > LEGACY_LIMIT && has_admissions;
    return t->transactions > LEGACY_LIMIT && t->groups > LEGACY_LIMIT && t->events > LEGACY_LIMIT && has_terminals;
}

static void fill_units(cf_t *c)
{
    for (unsigned i = 0; i < CYCLE_UNITS; i++) {
        bool is_plain_commit = c->mode == PREVIEW && i % 4 == 3;
        cf_unit(c,c->mode == PREVIEW && !is_plain_commit,is_plain_commit);
    }
}

static void fill_cycle(cf_t *c, unsigned cycle, cf_carried_t *k)
{
    bool is_preview = c->mode == PREVIEW;
    if (is_preview && cycle == 2) cf_carry_start(c,k);
    if (is_preview && cycle == 3) cf_carry_abort(c,k);
    fill_units(c);
    if (is_preview) cf_epoch(c,cycle == 1 ? c->floor : c->epoch);
    while (cf_churn_bytes(c) + CF_PAD_RESERVE <= cf_room(c)) cf_churn(c);
    cf_pad(c);
}

static void continue_cycle(cf_t *c, candidate_recovery_view_t **view, unsigned cycle)
{
    candidate_wal_info_t before = candidate_recovery_view_info(*view).cut, after = candidate_wal_info(c->f.wal);
    vf_reset_memory(&c->f); size_t held = c->f.memory.live;
    candidate_recovery_view_t *next = NULL;
    cf_arm(before.valid_end_lsn);
    candidate_wal_status_t status = cf_continue(c,*view,&next);
    cf_disarm();
    size_t resident = candidate_recovery_view_info(next).resident_bytes;
    uint64_t records = cf_suffix_records(&before,&after), bytes = cf_suffix_bytes(&before,&after);
    printf("CYCLE %s %u status=%s suffix_records=%llu suffix_bytes=%llu wal_reads=%llu wal_read_bytes=%llu lowest_read=%llu "
        "denied=%llu held=%zu new_resident=%zu calls=%u\n",mode_name(c->mode),cycle,vf_status(status),
        (unsigned long long)records,(unsigned long long)bytes,(unsigned long long)cf_io.reads,(unsigned long long)cf_io.read_bytes,
        (unsigned long long)cf_io.lowest_lsn,(unsigned long long)cf_io.denied,held,resident,c->f.memory.calls);
    OK(status);
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"cycle-%s-%u",mode_name(c->mode),cycle) > 0);
    cf_suffix_t suffix = cf_expect_reads(tag,c,&before,&after);
    CHECK(records > 0 && suffix.padding_bytes == 0 && suffix.header_bytes == (cycle == 1 ? 0 : CANDIDATE_WAL_HEADER_BYTES));
    cf_expect_budget(tag,c,held + resident);
    candidate_recovery_view_close(view); *view = next;
}

static bool is_same_transaction(const candidate_witness_transaction_t *a, const candidate_witness_transaction_t *b)
{
    bool is_same_chain = a->transaction_id == b->transaction_id && a->first_lsn == b->first_lsn && a->last_lsn == b->last_lsn &&
        a->records == b->records && a->chain_crc == b->chain_crc && a->groups == b->groups;
    bool is_same_terminal = a->state == b->state && a->csn == b->csn && a->terminal_end_lsn == b->terminal_end_lsn;
    bool is_same_request = a->has_admission == b->has_admission && a->request_epoch == b->request_epoch &&
        memcmp(a->request_nonce,b->request_nonce,16) == 0 && memcmp(a->operation_digest,b->operation_digest,32) == 0;
    bool is_same_result = a->result_bytes == b->result_bytes && a->result_chunks == b->result_chunks &&
        memcmp(a->result_digest,b->result_digest,32) == 0;
    return is_same_chain && is_same_terminal && is_same_request && is_same_result;
}

static void expect_same_admissions(const cf_t *c, const candidate_recovery_view_t *full, const candidate_recovery_view_t *continued)
{
    unsigned compared = 0;
    for (uint32_t i = 0; i < c->ledger_count; i++) {
        uint64_t id = c->ledger[i].id;
        const candidate_witness_transaction_t *a = candidate_wal_witness_find(candidate_recovery_view_witness(full),id);
        const candidate_witness_transaction_t *b = candidate_wal_witness_find(candidate_recovery_view_witness(continued),id);
        candidate_semantic_admission_t x = {0}, y = {0};
        CHECK(a != NULL && b != NULL && is_same_transaction(a,b));
        CHECK(cf_admission_of(full,id,&x) && cf_admission_of(continued,id,&y));
        CHECK(x.owner_epoch == y.owner_epoch && x.result_limit == y.result_limit && x.has_result == y.has_result);
        compared++;
    }
    printf("DIFFERENTIAL admissions_compared=%u ledger=%u\n",compared,c->ledger_count);
}

static void expect_full_open_matches(cf_t *c, const candidate_recovery_view_t *continued)
{
    candidate_recovery_view_t *full = NULL; OK(cf_open(c,&full));
    candidate_recovery_view_info_t a = candidate_recovery_view_info(full), b = candidate_recovery_view_info(continued);
    candidate_recovery_closure_t fa = candidate_recovery_view_closure(full), fb = candidate_recovery_view_closure(continued);
    printf("DIFFERENTIAL %s full_txns=%u closure_txns=%u full_groups=%u closure_groups=%u full_events=%u closure_events=%u "
        "live=%u/%u versions=%llu/%llu committed=%u/%u aborted=%u/%u admitted=%u/%u full_resident=%zu closure_resident=%zu\n",
        mode_name(c->mode),fa.transactions,fb.transactions,fa.groups,fb.groups,fa.events,fb.events,a.live_pages,b.live_pages,
        (unsigned long long)a.versions,(unsigned long long)b.versions,a.outcomes.committed,b.outcomes.committed,
        a.outcomes.aborted,b.outcomes.aborted,a.outcomes.admitted,b.outcomes.admitted,a.resident_bytes,b.resident_bytes);
    CHECK(a.live_pages == b.live_pages && a.free_pages == b.free_pages && a.versions == b.versions);
    CHECK(a.outcomes.committed == b.outcomes.committed && a.outcomes.aborted == b.outcomes.aborted);
    CHECK(a.outcomes.admitted == b.outcomes.admitted && a.outcomes.result_bytes == b.outcomes.result_bytes);
    CHECK(a.cut.valid_end_lsn == b.cut.valid_end_lsn && a.cut.records == b.cut.records);
    CHECK(fa.history.transactions == fb.history.transactions && fa.history.groups == fb.history.groups);
    CHECK(fa.history.events == fb.history.events && fa.history.commits == fb.history.commits);
    CHECK(fa.history.aborts == fb.history.aborts && fa.history.max_started_id == fb.history.max_started_id);
    CHECK(fa.transactions > fb.transactions && fa.groups > fb.groups && fa.events > fb.events);
    CHECK(fa.admissions == fb.admissions && fa.tombstones == 0 && fb.tombstones == cf_window_admissions(c));
    expect_same_admissions(c,full,continued);
    candidate_recovery_view_close(&full);
}

static void expect_catalog_resident(const cf_t *c, const candidate_recovery_view_t *v)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    for (unsigned n = 0; n < c->f.built.spec.requests; n++) {
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(w,c->f.built.txn[n].id);
        CHECK(t != NULL && t->csn == c->f.built.txn[n].csn && t->has_admission);
    }
}

static void expect_cycle(cf_t *c, const candidate_recovery_view_t *v, size_t *baseline)
{
    candidate_recovery_closure_t closure = candidate_recovery_view_closure(v);
    size_t resident = candidate_recovery_view_info(v).resident_bytes - closure.identity_bytes;
    if (*baseline == 0) *baseline = resident;
    cf_expect_history(c,&closure);
    uint32_t active = carried.is_active ? 1 : 0;
    CHECK(closure.groups == 1 + active && closure.events == CF_CHURN_PAGES && closure.started_intervals == 2);
    CHECK(resident == *baseline);
    cf_expect_resident(c,v,0);
    expect_catalog_resident(c,v);
    if (c->mode == RESOLVED) cf_expect_outcomes(c,v);
}

static void run_cycles(candidate_recovery_view_mode_t mode)
{
    cf_t c; cf_boot(&c,mode == RESOLVED ? "closure-cycles-resolved" : "closure-cycles-preview",mode);
    candidate_recovery_view_t *view = NULL; OK(cf_open(&c,&view));
    size_t baseline = 0; unsigned cycle = 0; carried = (cf_carried_t){0};
    while (!exceeds_limits(&c)) {
        CHECK(++cycle <= MAX_CYCLES);
        fill_cycle(&c,cycle,&carried); cf_flush(&c);
        continue_cycle(&c,&view,cycle);
        if (cycle == 1) expect_full_open_matches(&c,view);
        if (cycle == 1 && mode == PREVIEW) cf_expect_rebuild(&c);
        expect_cycle(&c,view,&baseline);
        char tag[64]; CHECK(snprintf(tag,sizeof(tag),"cycle-%s-%u",mode_name(mode),cycle) > 0); cf_print(tag,&c,view);
    }
    vf_reset_memory(&c.f); candidate_recovery_view_t *full = (void *)1; candidate_wal_status_t legacy = cf_open(&c,&full);
    printf("CYCLES %s cycles=%u txns=%llu terminals=%llu commits=%llu groups=%llu events=%llu admissions=%llu ledger=%u "
        "legacy_full_open=%s steady_resident_without_identity=%zu\n",mode_name(mode),cycle,(unsigned long long)c.tally.transactions,
        (unsigned long long)(c.tally.commits + c.tally.aborts),(unsigned long long)c.tally.commits,(unsigned long long)c.tally.groups,
        (unsigned long long)c.tally.events,(unsigned long long)admitted_total(&c.tally),c.ledger_count,vf_status(legacy),baseline);
    CHECK(cycle >= 2 && legacy == CANDIDATE_WAL_LIMIT && full == NULL);
    if (mode == PREVIEW) CHECK(c.tally.commits > c.f.built.spec.requests && c.ledger_count + c.f.built.spec.requests > LEGACY_LIMIT);
    CHECK(!carried.is_active);
    candidate_recovery_view_close(&view); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

static void cycles_resolved(void) { run_cycles(RESOLVED); }

static void cycles_preview(void) { run_cycles(PREVIEW); }

static void nonce_of(uint64_t id, uint64_t epoch, uint8_t out[16])
{
    uint64_t a = id * 7919 + epoch, b = 0xa5a5a5a5u ^ id;
    for (unsigned i = 0; i < 8; i++) { out[i] = (uint8_t)(a >> (8 * i)); out[8 + i] = (uint8_t)(b >> (8 * i)); }
}

static void identity_setup(cf_t *c)
{
    for (unsigned i = 0; i < RETAINED_IDS; i++) cf_unit(c,true,false);
    for (unsigned i = 0; i < RETIRED_IDS; i++) cf_unit(c,false,i % 2 == 0);
}

static void expect_identity_setup(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    cf_expect_history(c,&k); cf_expect_resident(c,v,0);
    for (unsigned i = 0; i < RETIRED_IDS; i++) CHECK(candidate_wal_witness_find(w,RETIRED_FIRST + i) == NULL);
    for (unsigned i = 0; i < RETAINED_IDS; i++) CHECK(candidate_wal_witness_find(w,CF_FIRST_ID + i) != NULL);
    CHECK(k.started_intervals == 2 && k.history.max_started_id == RETIRED_FIRST + RETIRED_IDS - 1);
}

static void begin_retired(cf_t *c) { fx_txn_t t; cf_begin(c,&t,RETIRED_FIRST + 4); }

static void admit_retired_id(cf_t *c) { fx_txn_t t; uint8_t n[16]; nonce_of(9001,c->epoch,n); cf_admit(c,&t,RETIRED_FIRST + 7,64,n); }

static void begin_resident(cf_t *c) { fx_txn_t t; cf_begin(c,&t,2); }

static void begin_retained(cf_t *c) { fx_txn_t t; cf_begin(c,&t,CF_FIRST_ID + 1); }

static void begin_unused_gaps(cf_t *c)
{
    static const uint64_t ids[] = {5000,500,RETIRED_FIRST + RETIRED_IDS};
    for (unsigned i = 0; i < 3; i++) { fx_txn_t t; cf_begin(c,&t,ids[i]); cf_end(c,&t,false); }
    c->next_id = RETIRED_FIRST + RETIRED_IDS + 1;
}

static void expect_gaps(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); cf_expect_history(c,&k); cf_expect_resident(c,v,0);
    CHECK(k.started_intervals == 4 && k.history.max_started_id == 5000);
}

static void admit_retained_nonce(cf_t *c) { fx_txn_t t; uint8_t n[16]; nonce_of(CF_FIRST_ID + 2,c->epoch,n); cf_admit(c,&t,2000,64,n); }

static void admit_fresh_nonce(cf_t *c) { fx_txn_t t; cf_admit(c,&t,2001,64,NULL); cf_end(c,&t,false); }

static void expect_fresh(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); cf_expect_history(c,&k); cf_expect_resident(c,v,0);
    CHECK(candidate_wal_witness_find(candidate_recovery_view_witness(v),2001) != NULL);
}

static void reserve_below_started(cf_t *c) { cf_reserve(c,RETIRED_FIRST + 1,c->transaction_highwater + 16,c->page_highwater + 8); }

static void raise_floor(cf_t *c) { cf_epoch(c,2); }

static void expect_floor(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); cf_expect_history(c,&k); cf_expect_resident(c,v,0);
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(candidate_recovery_view_witness(v));
    for (unsigned i = 0; i < RETAINED_IDS; i++) CHECK(candidate_wal_witness_find(candidate_recovery_view_witness(v),CF_FIRST_ID + i) == NULL);
    CHECK(k.tombstones == 0 && s.current_epoch == 2 && s.retention_floor == 2 && k.transactions == c->f.built.spec.requests);
}

static void expired_epoch_refused(void)
{
    cf_t c; cf_boot(&c,"closure-identity-expired-epoch",PREVIEW);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = NULL, *v3 = (void *)1;
    OK(cf_open(&c,&v0)); identity_setup(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1));
    raise_floor(&c); cf_flush(&c); OK(cf_continue(&c,v1,&v2)); expect_floor(&c,v2);
    cf_source_t before; cf_source_take(v2,&before); size_t live = c.f.memory.live;
    fx_txn_t t; uint8_t n[16]; nonce_of(9002,1,n);
    c.epoch = 1; cf_admit(&c,&t,2002,64,n); c.epoch = 2; cf_flush(&c);
    candidate_wal_status_t status = cf_continue(&c,v2,&v3);
    bool is_same = cf_source_same(v2,&before);
    printf("ROW closure-identity-expired-epoch status=%s out_null=%d source_same=%d live_same=%d\n",vf_status(status),v3 == NULL,
        is_same,c.f.memory.live == live);
    CHECK(status == CANDIDATE_WAL_CORRUPT && v3 == NULL && is_same && c.f.memory.live == live);
    cf_source_free(&before);
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    cf_free(&c);
}

static void identity_rows(void)
{
    static const cf_row_t rows[] = {{"retired-begin",begin_retired,CANDIDATE_WAL_CORRUPT,NULL},
        {"retired-admit",admit_retired_id,CANDIDATE_WAL_CORRUPT,NULL},{"resident-begin",begin_resident,CANDIDATE_WAL_CORRUPT,NULL},
        {"retained-begin",begin_retained,CANDIDATE_WAL_CORRUPT,NULL},{"unused-gaps",begin_unused_gaps,CANDIDATE_WAL_OK,expect_gaps},
        {"retained-nonce-duplicate",admit_retained_nonce,CANDIDATE_WAL_CORRUPT,NULL},
        {"fresh-nonce",admit_fresh_nonce,CANDIDATE_WAL_OK,expect_fresh},
        {"reservation-below-started",reserve_below_started,CANDIDATE_WAL_CORRUPT,NULL},
        {"floor-advance",raise_floor,CANDIDATE_WAL_OK,expect_floor}};
    const cf_row_group_t g = {"identity",PREVIEW,identity_setup,expect_identity_setup,rows,sizeof(rows) / sizeof(rows[0])};
    cf_run_rows(&g);
    expired_epoch_refused();
}

static void churn_twice(cf_t *c)
{
    cf_churn(c); cf_churn(c);
    for (unsigned i = 0; i < 5; i++) cf_unit(c,true,false);
}

static void count_events(const cf_t *c, const candidate_recovery_view_t *v, unsigned *reservations, unsigned *transitions)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(w);
    for (uint32_t i = 0; i < s.events; i++) {
        const candidate_witness_system_event_t *e = candidate_wal_witness_system_at(w,i);
        if (e->type == 16) {
            CHECK(e->value.reserve.previous_page == c->f.anchor.page_highwater && e->value.reserve.next_page == c->page_highwater);
            CHECK(e->owner_epoch == c->f.owner && e->end_lsn == c->last_reserve_end); (*reservations)++;
            continue;
        }
        uint64_t page = e->value.allocation.page_id; CHECK(c->churn_first <= page && page < c->churn_first + CF_CHURN_PAGES);
        CHECK(e->type == 6 && e->value.allocation.next_generation == c->generation[page - c->churn_first]);
        CHECK(e->end_lsn == c->last_churn_end); (*transitions)++;
    }
}

static void expect_reservation(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); cf_expect_history(c,&k); cf_expect_resident(c,v,0);
    unsigned reservations = 0, transitions = 0; count_events(c,v,&reservations,&transitions);
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(candidate_recovery_view_witness(v));
    printf("RESERVATION events=%u reservations=%u transitions=%u page_highwater=%llu transaction_highwater=%llu max_started=%llu "
        "highest_owner=%llu\n",k.events,reservations,transitions,(unsigned long long)s.page_highwater,
        (unsigned long long)s.transaction_highwater,(unsigned long long)k.history.max_started_id,
        (unsigned long long)candidate_recovery_view_info(v).cut.highest_owner_epoch);
    CHECK(reservations == 1 && transitions == CF_CHURN_PAGES && k.events == CF_CHURN_PAGES + 1);
    CHECK(s.page_highwater == c->page_highwater && s.transaction_highwater == c->transaction_highwater);
    CHECK(k.history.max_started_id == c->tally.max_started && candidate_recovery_view_info(v).cut.highest_owner_epoch == c->f.owner);
}

static void reserve_after_churn(cf_t *c) { cf_reserve(c,c->transaction_highwater,c->transaction_highwater + 16,c->page_highwater + 8); }

static void reservation_rows(void)
{
    static const cf_row_t rows[] = {{"reserve-retained",reserve_after_churn,CANDIDATE_WAL_OK,expect_reservation}};
    const cf_row_group_t g = {"reservation",PREVIEW,churn_twice,NULL,rows,1};
    cf_run_rows(&g);
    cf_t c; cf_boot(&c,"closure-reservation-highwater",PREVIEW);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = NULL, *v3 = (void *)1;
    OK(cf_open(&c,&v0)); churn_twice(&c); reserve_after_churn(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1));
    uint64_t highwater = c.transaction_highwater; fx_txn_t t;
    cf_begin(&c,&t,highwater); cf_end(&c,&t,false); cf_flush(&c); OK(cf_continue(&c,v1,&v2));
    cf_source_t before; cf_source_take(v2,&before);
    cf_begin(&c,&t,highwater + 1); cf_flush(&c);
    candidate_wal_status_t status = cf_continue(&c,v2,&v3);
    bool is_same = cf_source_same(v2,&before);
    printf("ROW closure-reservation-highwater highwater=%llu at_highwater=OK above=%s source_same=%d\n",(unsigned long long)highwater,
        vf_status(status),is_same);
    CHECK(status == CANDIDATE_WAL_CORRUPT && v3 == NULL && is_same);
    cf_source_free(&before);
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    cf_free(&c);
}

static void prefix_reads_stay_after_cut(void)
{
    cf_t c; cf_boot(&c,"closure-prefix",RESOLVED);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = NULL, *full = (void *)1;
    OK(cf_open(&c,&v0));
    candidate_wal_info_t empty = candidate_recovery_view_info(v0).cut;
    cf_arm(empty.valid_end_lsn); OK(cf_continue(&c,v0,&v1)); cf_disarm();
    CHECK(cf_io.reads == 0 && cf_io.denied == 0);
    for (unsigned i = 0; i < 20; i++) cf_unit(&c,false,false);
    cf_churn(&c); cf_churn(&c); cf_flush(&c);
    candidate_wal_info_t before = candidate_recovery_view_info(v1).cut, after = candidate_wal_info(c.f.wal);
    cf_arm(before.valid_end_lsn);
    candidate_wal_status_t status = cf_continue(&c,v1,&v2);
    cf_disarm();
    uint64_t records = cf_suffix_records(&before,&after), bytes = cf_suffix_bytes(&before,&after);
    printf("PREFIX continue=%s cut=%llu suffix_records=%llu suffix_bytes=%llu reads=%llu read_bytes=%llu lowest=%llu denied=%llu\n",
        vf_status(status),(unsigned long long)before.valid_end_lsn,(unsigned long long)records,(unsigned long long)bytes,
        (unsigned long long)cf_io.reads,(unsigned long long)cf_io.read_bytes,(unsigned long long)cf_io.lowest_lsn,
        (unsigned long long)cf_io.denied);
    OK(status);
    cf_suffix_t suffix = cf_expect_reads("prefix",&c,&before,&after);
    CHECK(suffix.header_bytes == 0 && suffix.padding_bytes == 0);
    cf_expect_outcomes(&c,v2);
    cf_arm(before.valid_end_lsn);
    candidate_wal_status_t legacy = cf_open(&c,&full);
    cf_disarm();
    vf_reset_memory(&c.f); unsigned loads = c.f.loads; candidate_recovery_view_t *next = (void *)1;
    candidate_wal_status_t poisoned = cf_continue(&c,v2,&next);
    printf("PREFIX calibration full_open=%s denied=%llu out_null=%d next_continue=%s next_calls=%u next_loads=%u\n",vf_status(legacy),
        (unsigned long long)cf_io.denied,full == NULL,vf_status(poisoned),c.f.memory.calls,c.f.loads - loads);
    CHECK(legacy != CANDIDATE_WAL_OK && cf_io.denied > 0 && full == NULL);
    CHECK(poisoned == CANDIDATE_WAL_POISONED && next == NULL && c.f.memory.calls == 0 && c.f.loads == loads);
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    cf_free(&c);
}

static bool is_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    bool is_same_end = a->valid_end_lsn == b->valid_end_lsn && a->records == b->records && a->segments == b->segments;
    return is_same_end && a->highest_owner_epoch == b->highest_owner_epoch && a->has_tail == b->has_tail;
}

static void expect_crossed(cf_t *c, const candidate_recovery_view_t *v, const candidate_wal_info_t *before)
{
    candidate_wal_info_t after = candidate_wal_info(c->f.wal), cut = candidate_recovery_view_info(v).cut;
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v);
    cf_suffix_t suffix = cf_expect_reads("segments-crossed",c,before,&after);
    CHECK(is_same_cut(&cut,&after) && after.segments == before->segments + 1 && suffix.padding_bytes > 0);
    cf_expect_history(c,&k); cf_expect_resident(c,v,0); cf_expect_outcomes(c,v);
}

static void segment_boundaries_continue_suffix(void)
{
    cf_t c; cf_boot(&c,"closure-segments",RESOLVED);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = NULL, *v3 = (void *)1;
    OK(cf_open(&c,&v0));
    for (unsigned i = 0; i < 50; i++) cf_unit(&c,false,false);
    cf_pad(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1));
    candidate_wal_info_t boundary = candidate_recovery_view_info(v1).cut;
    for (unsigned i = 0; i < 50; i++) cf_unit(&c,false,false);
    cf_flush(&c); cf_arm(boundary.valid_end_lsn); OK(cf_continue(&c,v1,&v2)); cf_disarm();
    uint64_t lowest = cf_io.lowest_lsn;
    candidate_wal_info_t inside = candidate_recovery_view_info(v2).cut;
    cf_cross_segment(&c); cf_flush(&c);
    cf_source_t held; cf_source_take(v2,&held);
    cf_arm(inside.valid_end_lsn); candidate_wal_status_t crossed = cf_continue(&c,v2,&v3); cf_disarm();
    bool is_same = cf_source_same(v2,&held);
    printf("SEGMENTS boundary=%llu boundary_segments=%u boundary_lowest=%llu inside=%llu crossed=%s segments=%u source_same=%d\n",
        (unsigned long long)boundary.valid_end_lsn,boundary.segments,(unsigned long long)lowest,
        (unsigned long long)inside.valid_end_lsn,vf_status(crossed),candidate_wal_info(c.f.wal).segments,is_same);
    CHECK(boundary.valid_end_lsn % CANDIDATE_WAL_SEGMENT_BYTES == 0 && lowest == boundary.valid_end_lsn);
    OK(crossed); CHECK(v3 != NULL && is_same);
    expect_crossed(&c,v3,&inside);
    cf_source_free(&held);
    candidate_recovery_view_close(&v3);
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    cf_free(&c);
    cf_boot(&c,"closure-checkpoint-suffix",RESOLVED);
    v3 = (void *)1; OK(cf_open(&c,&v0)); OK(cf_continue(&c,v0,&v1));
    uint8_t body[16] = {0}; cf_append_raw(&c,12,0,body,sizeof(body)); cf_flush(&c);
    candidate_wal_status_t checkpoint = cf_continue(&c,v1,&v3), handle = candidate_wal_check_uuid(c.f.wal,fx_uuid);
    printf("SEGMENTS checkpoint_suffix=%s out_null=%d handle_after=%s\n",vf_status(checkpoint),v3 == NULL,vf_status(handle));
    CHECK(checkpoint == CANDIDATE_WAL_UNSUPPORTED && v3 == NULL && handle == CANDIDATE_WAL_OK);
    candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    cf_free(&c);
}

static bool is_same_closure(const candidate_recovery_closure_t *a, const candidate_recovery_closure_t *b)
{
    return memcmp(a,b,sizeof(*a)) == 0;
}

static cf_t *hook_fixture;
static const candidate_recovery_view_t *hook_source;
static candidate_wal_status_t inner_status;
static candidate_recovery_closure_t inner_closure;

static void reenter(vf_t *f)
{
    (void)f; candidate_recovery_view_t *inner = NULL;
    inner_status = cf_continue(hook_fixture,hook_source,&inner);
    inner_closure = candidate_recovery_view_closure(inner);
    candidate_recovery_view_close(&inner);
}

static void append_begin(vf_t *f)
{
    uint8_t body[16] = {0}; uint64_t end;
    candidate_wal_record_t r = {.type = 2,.transaction_id = CF_TRANSACTION_HIGHWATER - 1,.payload = body,.payload_bytes = sizeof(body)};
    OK(candidate_wal_append(f->wal,&r,&end));
}

static candidate_wal_status_t refuse(cf_t *c, const candidate_recovery_view_t *source, candidate_recovery_view_request_t *request)
{
    vf_reset_memory(&c->f); unsigned loads = c->f.loads; candidate_recovery_view_t *out = (void *)1;
    candidate_wal_status_t status = candidate_recovery_view_continue(c->f.wal,request,source,&out);
    CHECK(out == NULL && c->f.memory.calls == 0 && c->f.loads == loads);
    return status;
}

static void expect_refusals(cf_t *c, const candidate_recovery_view_t *v1)
{
    candidate_recovery_view_request_t r = cf_request(c); r.load.uuid[0] ^= 1;
    candidate_wal_status_t uuid = refuse(c,v1,&r);
    r = cf_request(c); r.anchor.generation++; candidate_wal_status_t anchor = refuse(c,v1,&r);
    r = cf_request(c); r.mode = PREVIEW; r.owner_epoch = 8; candidate_wal_status_t mode = refuse(c,v1,&r);
    r = cf_request(c); candidate_wal_status_t no_source = candidate_recovery_view_continue(c->f.wal,&r,NULL,NULL);
    printf("FAULT uuid=%s anchor=%s mode=%s null_out=%s\n",vf_status(uuid),vf_status(anchor),vf_status(mode),vf_status(no_source));
    CHECK(uuid == CANDIDATE_WAL_UNSUPPORTED && anchor == CANDIDATE_WAL_UNSUPPORTED && mode == CANDIDATE_WAL_INVALID);
    CHECK(no_source == CANDIDATE_WAL_INVALID);
}

static void expect_callbacks(cf_t *c, const candidate_recovery_view_t *v1, const candidate_recovery_closure_t *base)
{
    candidate_recovery_view_request_t r = cf_request(c); candidate_recovery_view_t *v = NULL;
    vf_reset_memory(&c->f); c->f.memory.mutate_request = &r; c->f.memory.mutate_memory = &c->f.allocator;
    OK(candidate_recovery_view_continue(c->f.wal,&r,v1,&v));
    candidate_recovery_closure_t mutated = candidate_recovery_view_closure(v);
    CHECK(r.mode == 0 && c->f.allocator.allocate == NULL && is_same_closure(&mutated,base));
    candidate_recovery_view_close(&v); vf_reset_memory(&c->f);
    hook_fixture = c; hook_source = v1; c->f.on_load = reenter; c->f.hook_at = c->f.loads + 1;
    OK(cf_continue(c,v1,&v));
    candidate_recovery_closure_t outer = candidate_recovery_view_closure(v);
    printf("FAULT inputs_mutated=OK reentry_outer=OK reentry_inner=%s same=%d\n",vf_status(inner_status),
        is_same_closure(&outer,&inner_closure));
    CHECK(inner_status == CANDIDATE_WAL_OK && is_same_closure(&outer,base) && is_same_closure(&inner_closure,base));
    unsigned loads = c->f.loads; c->f.is_loader_closed = true;
    candidate_recovery_view_info_t info = candidate_recovery_view_info(v); candidate_recovery_closure_t closed = candidate_recovery_view_closure(v);
    candidate_catalog_query_t q = scen_query(&c->f.built,0); const candidate_outcome_t *e = NULL;
    candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e);
    printf("FAULT loader_closed lookup=%u loads=%u\n",lookup,c->f.loads - loads);
    CHECK(c->f.loads == loads && info.resident_bytes > 0 && is_same_closure(&closed,base));
    vf_expect_answer(&c->f,0,lookup,e);
    c->f.is_loader_closed = false; candidate_recovery_view_close(&v);
}

static void expect_handle_faults(cf_t *c, const candidate_recovery_view_t *v1)
{
    size_t base = c->f.memory.live; candidate_recovery_view_t *v = (void *)1;
    c->f.on_load = append_begin; c->f.hook_at = c->f.loads + 1;
    candidate_wal_status_t cut = cf_continue(c,v1,&v);
    CHECK(cut == CANDIDATE_WAL_STALE && v == NULL && c->f.memory.live == base);
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) { alarm(10); v = (void *)1; candidate_wal_status_t s = cf_continue(c,v1,&v); _exit(s == CANDIDATE_WAL_FORKED && v == NULL ? 0 : 1); }
    int exit_status; CHECK(waitpid(child,&exit_status,0) == child);
    candidate_wal_reject(c->f.wal);
    candidate_recovery_view_request_t r = cf_request(c); candidate_wal_status_t poisoned = refuse(c,v1,&r);
    printf("FAULT cut_change=%s fork_child_exit=%d poisoned=%s\n",vf_status(cut),WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1,
        vf_status(poisoned));
    CHECK(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0 && poisoned == CANDIDATE_WAL_POISONED);
}

static void fault_refusals_keep_source(void)
{
    cf_t c; cf_boot(&c,"closure-faults",RESOLVED);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v = NULL;
    OK(cf_open(&c,&v0));
    for (unsigned i = 0; i < 10; i++) cf_unit(&c,false,false);
    cf_churn(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1)); candidate_recovery_view_close(&v0);
    for (unsigned i = 0; i < 10; i++) cf_unit(&c,false,false);
    cf_flush(&c);
    cf_source_t held; cf_source_take(v1,&held);
    OK(cf_continue(&c,v1,&v)); candidate_recovery_closure_t base = candidate_recovery_view_closure(v); candidate_recovery_view_close(&v);
    expect_refusals(&c,v1); expect_callbacks(&c,v1,&base); expect_handle_faults(&c,v1);
    CHECK(cf_source_same(v1,&held));
    cf_source_free(&held);
    candidate_recovery_view_close(&v1); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

static void allocation_failures_mode(candidate_recovery_view_mode_t mode)
{
    bool is_preview = mode == PREVIEW;
    cf_t c; cf_boot(&c,is_preview ? "closure-oom-preview" : "closure-oom-resolved",mode);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v = NULL;
    OK(cf_open(&c,&v0));
    for (unsigned i = 0; i < 20; i++) cf_unit(&c,is_preview,false);
    cf_churn(&c); cf_churn(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1)); candidate_recovery_view_close(&v0);
    for (unsigned i = 0; i < 20; i++) cf_unit(&c,is_preview && i % 2 == 0,is_preview && i % 2 == 1);
    cf_churn(&c); cf_flush(&c);
    cf_source_t held; cf_source_take(v1,&held);
    vf_reset_memory(&c.f); size_t base = c.f.memory.live;
    OK(cf_continue(&c,v1,&v)); unsigned calls = c.f.memory.calls;
    size_t resident = candidate_recovery_view_info(v).resident_bytes;
    cf_expect_budget(is_preview ? "oom-preview" : "oom-resolved",&c,base + resident);
    cf_expect_resident(&c,v,0);
    if (mode == RESOLVED) cf_expect_outcomes(&c,v);
    candidate_recovery_view_close(&v);
    unsigned failures = 0;
    for (unsigned k = 1; k <= calls; k++) {
        vf_reset_memory(&c.f); c.f.memory.fail_at = k; v = (void *)1;
        candidate_wal_status_t status = cf_continue(&c,v1,&v);
        if (status != CANDIDATE_WAL_NOMEM) printf("OOM %s k=%u status=%s\n",mode_name(mode),k,vf_status(status));
        CHECK(status == CANDIDATE_WAL_NOMEM && v == NULL && c.f.memory.live == base);
        failures++;
    }
    vf_reset_memory(&c.f);
    bool is_same = cf_source_same(v1,&held);
    printf("OOM %s calls=%u failures=%u held=%zu resident=%zu source_same=%d\n",mode_name(mode),calls,failures,base,resident,is_same);
    CHECK(failures == calls && calls > 0 && is_same);
    if (mode == RESOLVED) cf_expect_outcomes(&c,v1);
    cf_source_free(&held);
    candidate_recovery_view_close(&v1); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

static void allocation_failures_keep_source(void)
{
    allocation_failures_mode(RESOLVED);
    allocation_failures_mode(PREVIEW);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;

static const test_case_t CASES[] = {{"cycles-resolved",cycles_resolved},{"cycles-preview",cycles_preview},
    {"identity",identity_rows},{"reservation",reservation_rows},{"dependency",cf_dependency_rows},{"active",cf_active_rows},
    {"live",cf_live_rows},{"prefix",prefix_reads_stay_after_cut},{"segments",segment_boundaries_continue_suffix},
    {"faults",fault_refusals_keep_source},{"oom",allocation_failures_keep_source}};

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(1200);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        if (!is_all && strcmp(selected,CASES[i].name) != 0) continue;
        printf("CASE %s\n",CASES[i].name); CASES[i].run(); has_match = true;
    }
    CHECK(has_match);
    printf("PASS recovery closure case=%s checks=%u; owned in-memory closure continuation only, not durable seeds or WAL reuse\n",
        selected,checks);
    return 0;
}
