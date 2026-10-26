#include "test_candidate_recovery_closure_fixture.h"

#include <string.h>

#include "../src/storage/candidate_wal_witness_data_internal.h"
#include "../src/storage/candidate_wal_witness_state_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define PREVIEW CANDIDATE_RECOVERY_VIEW_PREVIEW
#define ACTIVE_LIMIT 4096u
#define NEW_OWNER 8u
#define LIVE_FIRST_ID 40u
#define LIVE_ABORTS 10u

static const scen_t EXPIRED = {.requests = 4,.req = vf_expired,.root_epoch = 2,.root_floor = 2};

static uint8_t *copy_bytes(const void *p, size_t bytes)
{
    uint8_t *copy = malloc(bytes == 0 ? 1 : bytes); CHECK(copy != NULL);
    if (bytes > 0) memcpy(copy,p,bytes);
    return copy;
}

void cf_source_take(const candidate_recovery_view_t *v, cf_source_t *s)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(w);
    size_t event = sizeof(candidate_witness_system_event_t);
    memset(s,0,sizeof(*s));
    s->closure = candidate_recovery_view_closure(v); s->identity = candidate_identity_info(v->identity);
    s->view_bytes = sizeof(*v); s->view = copy_bytes(v,s->view_bytes);
    s->witness_bytes = sizeof(*w); s->witness = copy_bytes(w,s->witness_bytes);
    s->admission_bytes = candidate_admissions_resident_bytes(v->admissions); s->admissions = copy_bytes(v->admissions,s->admission_bytes);
    s->graph_bytes = sizeof(*v->graph); s->graph = copy_bytes(v->graph,s->graph_bytes);
    s->event_bytes = (size_t)system.events * event;
    s->events = malloc(s->event_bytes == 0 ? 1 : s->event_bytes); CHECK(s->events != NULL);
    for (uint32_t i = 0; i < system.events; i++) memcpy(s->events + (size_t)i * event,candidate_wal_witness_system_at(w,i),event);
}

bool cf_source_same(const candidate_recovery_view_t *v, const cf_source_t *s)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(w);
    candidate_recovery_closure_t closure = candidate_recovery_view_closure(v);
    candidate_identity_info_t identity = candidate_identity_info(v->identity);
    size_t event = sizeof(candidate_witness_system_event_t);
    bool is_same = memcmp(&closure,&s->closure,sizeof(closure)) == 0 && memcmp(&identity,&s->identity,sizeof(identity)) == 0;
    is_same = is_same && memcmp(v,s->view,s->view_bytes) == 0 && memcmp(w,s->witness,s->witness_bytes) == 0;
    is_same = is_same && memcmp(v->admissions,s->admissions,s->admission_bytes) == 0;
    is_same = is_same && memcmp(v->graph,s->graph,s->graph_bytes) == 0 && (size_t)system.events * event == s->event_bytes;
    for (uint32_t i = 0; is_same && i < system.events; i++)
        is_same = memcmp(candidate_wal_witness_system_at(w,i),s->events + (size_t)i * event,event) == 0;
    return is_same;
}

void cf_source_free(cf_source_t *s)
{
    free(s->view); free(s->witness); free(s->admissions); free(s->graph); free(s->events);
    memset(s,0,sizeof(*s));
}

static void run_row(const cf_row_group_t *g, const cf_row_t *row)
{
    char tag[96]; CHECK(snprintf(tag,sizeof(tag),"closure-%s-%s",g->name,row->name) > 0);
    cf_t c; cf_boot(&c,tag,g->mode);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = (void *)1;
    OK(cf_open(&c,&v0)); g->setup(&c); cf_flush(&c); OK(cf_continue(&c,v0,&v1));
    if (g->expect_setup != NULL) g->expect_setup(&c,v1);
    cf_source_t before; cf_source_take(v1,&before);
    row->act(&c); cf_flush(&c);
    size_t live = c.f.memory.live;
    candidate_wal_status_t status = cf_continue(&c,v1,&v2);
    bool is_same = cf_source_same(v1,&before), is_released = v2 != NULL || c.f.memory.live == live;
    printf("ROW %s status=%s expected=%s out_null=%d source_same=%d failure_released=%d\n",tag,vf_status(status),
        vf_status(row->expected),v2 == NULL,is_same,is_released);
    CHECK(status == row->expected && (status == CANDIDATE_WAL_OK) == (v2 != NULL) && is_same && is_released);
    if (v2 != NULL) { cf_print(tag,&c,v2); if (row->expect != NULL) row->expect(&c,v2); }
    cf_source_free(&before);
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

void cf_run_rows(const cf_row_group_t *g)
{
    for (unsigned i = 0; i < g->count; i++) run_row(g,&g->rows[i]);
}

static cf_carried_t carried;
static fx_txn_t pending_txn, admitted_active, plain_active;
static cf_group_t pending;

static void carry_setup(cf_t *c)
{
    cf_carry_start(c,&carried);
    for (unsigned i = 0; i < 6; i++) cf_unit(c,i % 2 == 0,i % 2 == 1);
}

static void expect_carried(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t t = {0}; bool found = cf_semantic_of(v,carried.t.id,&t);
    candidate_semantic_group_t g = {0}; bool has_group = false;
    for (uint32_t i = 0; !has_group && candidate_witness_group_at(candidate_recovery_view_witness(v),i,&g); i++)
        has_group = g.transaction_id == carried.t.id;
    printf("CARRIED id=%llu found=%d limit=%llu chunk_bytes=%llu chunks=%u groups=%u crc=%u owner=%llu group=%d\n",
        (unsigned long long)carried.t.id,found,(unsigned long long)t.result_limit,(unsigned long long)t.chunk_bytes,
        t.value.result_chunks,t.value.groups,t.value.chain_crc,(unsigned long long)t.owner_epoch,has_group);
    CHECK(found && t.result_limit == CF_CARRIED_BYTES + 16 && t.chunk_bytes == 65536 && t.value.result_chunks == 1);
    CHECK(t.value.groups == 1 && t.value.chain_crc == carried.t.crc && t.owner_epoch == c->f.owner && has_group);
    cf_expect_resident(c,v,0);
}

static void abort_carried(cf_t *c) { cf_carry_abort(c,&carried); }

static void expect_carried_abort(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t t = {0}; candidate_semantic_admission_t a = {0};
    bool found = cf_semantic_of(v,carried.t.id,&t) && cf_admission_of(v,carried.t.id,&a);
    printf("CARRIED_ABORT found=%d state=%d chunks=%u chunk_bytes=%llu result_bytes=%llu has_result=%d result_page=%llu "
        "terminal_end=%llu epoch=%llu floor=%llu\n",found,(int)t.value.state,t.value.result_chunks,(unsigned long long)t.chunk_bytes,
        (unsigned long long)t.value.result_bytes,a.has_result,(unsigned long long)a.result.page.id,
        (unsigned long long)t.value.terminal_end_lsn,(unsigned long long)c->epoch,(unsigned long long)c->floor);
    CHECK(found && t.value.state == CANDIDATE_WITNESS_ABORT && t.value.result_chunks == 2 && t.chunk_bytes == CF_CARRIED_BYTES);
    CHECK(memcmp(t.value.result_digest,carried.digest,32) == 0 && a.has_result && a.result.page.id == carried.ref.page.id);
    CHECK(t.value.terminal_end_lsn == carried.t.terminal_end && candidate_recovery_view_closure(v).unfinished == 0);
    cf_expect_resident(c,v,0);
}

static void chunk_wrong_offset(cf_t *c) { cf_chunk_t chunk = cf_carry_chunk(&carried,1); chunk.offset = 0; cf_chunk(c,&carried.t,&chunk); }

static void chunk_wrong_digest(cf_t *c) { cf_chunk_t chunk = cf_carry_chunk(&carried,1); chunk.digest[0] ^= 1; cf_chunk(c,&carried.t,&chunk); }

static void abort_wrong_crc(cf_t *c) { cf_carry_last(c,&carried); carried.t.crc ^= 1; cf_end(c,&carried.t,false); }

static void open_pending(cf_t *c) { cf_admit(c,&pending_txn,c->next_id++,64,NULL); cf_group_open(c,&pending_txn,&pending); }

static void expect_pending(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    CHECK(w.incomplete_group_lsn != 0 && w.unfinished == 1);
    cf_expect_resident(c,v,0);
}

static void close_pending_abort(cf_t *c) { cf_group_close(c,&pending_txn,&pending); cf_end(c,&pending_txn,false); }

static void expect_pending_abort(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t t = {0}; bool found = cf_semantic_of(v,pending_txn.id,&t);
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    printf("PENDING_ABORT found=%d state=%d groups=%u incomplete=%llu\n",found,(int)t.value.state,t.value.groups,
        (unsigned long long)w.incomplete_group_lsn);
    CHECK(found && t.value.state == CANDIDATE_WITNESS_ABORT && t.value.groups == 1 && w.incomplete_group_lsn == 0);
    cf_expect_resident(c,v,0);
}

static void interleave_pending(cf_t *c) { fx_txn_t t; cf_begin(c,&t,c->next_id++); }

void cf_dependency_rows(void)
{
    static const cf_row_t carried_rows[] = {{"abort-valid",abort_carried,CANDIDATE_WAL_OK,expect_carried_abort},
        {"chunk-offset-wrong",chunk_wrong_offset,CANDIDATE_WAL_CORRUPT,NULL},
        {"chunk-digest-wrong",chunk_wrong_digest,CANDIDATE_WAL_CORRUPT,NULL},
        {"terminal-crc-wrong",abort_wrong_crc,CANDIDATE_WAL_CORRUPT,NULL}};
    static const cf_row_t pending_rows[] = {{"pending-abort-valid",close_pending_abort,CANDIDATE_WAL_OK,expect_pending_abort},
        {"pending-interleave",interleave_pending,CANDIDATE_WAL_CORRUPT,NULL}};
    const cf_row_group_t a = {"carried",PREVIEW,carry_setup,expect_carried,carried_rows,sizeof(carried_rows) / sizeof(carried_rows[0])};
    const cf_row_group_t b = {"pending",PREVIEW,open_pending,expect_pending,pending_rows,2};
    cf_run_rows(&a); cf_run_rows(&b);
}

static void active_setup(cf_t *c)
{
    cf_admit(c,&admitted_active,c->next_id++,ACTIVE_LIMIT,NULL); cf_delta(c,&admitted_active,8);
    cf_begin(c,&plain_active,c->next_id++); cf_delta(c,&plain_active,8);
    for (unsigned i = 0; i < 6; i++) cf_unit(c,false,i % 2 == 0);
    for (unsigned i = 0; i < 2; i++) cf_unit(c,true,false);
}

static void print_active(const char *tag, const candidate_semantic_transaction_t *a, const candidate_semantic_transaction_t *p,
    const candidate_witness_info_t *w, const cf_t *c)
{
    printf("ACTIVE %s admitted=%llu state=%d last=%llu/%llu owner=%llu limit=%llu chunk_bytes=%llu groups=%u plain=%llu "
        "state=%d last=%llu/%llu groups=%u highest_csn=%llu/%llu complete_groups=%u\n",tag,
        (unsigned long long)admitted_active.id,(int)a->value.state,(unsigned long long)a->value.last_lsn,
        (unsigned long long)admitted_active.last,(unsigned long long)a->owner_epoch,(unsigned long long)a->result_limit,
        (unsigned long long)a->chunk_bytes,a->value.groups,(unsigned long long)plain_active.id,(int)p->value.state,
        (unsigned long long)p->value.last_lsn,(unsigned long long)plain_active.last,p->value.groups,
        (unsigned long long)w->highest_csn,(unsigned long long)c->f.built.fx->highest_csn,w->complete_groups);
}

static void expect_active(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t a = {0}, p = {0};
    bool found = cf_semantic_of(v,admitted_active.id,&a) && cf_semantic_of(v,plain_active.id,&p);
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    print_active("setup",&a,&p,&w,c);
    CHECK(found && a.value.last_lsn == admitted_active.last && a.owner_epoch == c->f.owner && a.result_limit == ACTIVE_LIMIT);
    CHECK(a.value.groups == 1 && p.value.last_lsn == plain_active.last && p.value.groups == 1 && !p.value.has_admission);
    CHECK(w.highest_csn == c->f.built.fx->highest_csn && w.complete_groups == 3);
    cf_expect_resident(c,v,1);
}

static void extend_valid(cf_t *c) { cf_delta(c,&admitted_active,8); }

static void extend_forked(cf_t *c) { admitted_active.last = admitted_active.first; cf_delta(c,&admitted_active,8); }

static void extend_new_owner(cf_t *c) { cf_reown(c,NEW_OWNER); cf_delta(c,&admitted_active,8); }

static void abort_new_owner(cf_t *c) { cf_reown(c,NEW_OWNER); cf_end(c,&admitted_active,false); }

static void active_chunk(cf_t *c, uint64_t total)
{
    cf_chunk_t chunk = {.ref = {{c->delta_page,1},0,1},.total = total,.length = total,.count = 1};
    fx_payload(5,32,chunk.digest);
    cf_chunk(c,&admitted_active,&chunk);
}

static void chunk_at_limit(cf_t *c) { active_chunk(c,ACTIVE_LIMIT); }

static void chunk_over_limit(cf_t *c) { active_chunk(c,ACTIVE_LIMIT + 1); }

static void commit_next_csn(cf_t *c) { cf_end(c,&plain_active,true); }

static void commit_repeated_csn(cf_t *c) { c->f.built.fx->highest_csn--; cf_end(c,&plain_active,true); }

static void commit_wrong_dependency(cf_t *c) { plain_active.group_ends[0] += 8; cf_end(c,&plain_active,true); }

static void expect_extended(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t a = {0}, p = {0};
    bool found = cf_semantic_of(v,admitted_active.id,&a) && cf_semantic_of(v,plain_active.id,&p);
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    print_active("extended",&a,&p,&w,c);
    CHECK(found && a.value.last_lsn == admitted_active.last && a.value.groups == 2 && a.value.state == CANDIDATE_WITNESS_UNFINISHED);
    cf_expect_resident(c,v,1);
}

static void expect_owner_abort(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t a = {0}, p = {0};
    bool found = cf_semantic_of(v,admitted_active.id,&a) && cf_semantic_of(v,plain_active.id,&p);
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    print_active("new-owner-abort",&a,&p,&w,c);
    CHECK(found && a.value.state == CANDIDATE_WITNESS_ABORT && a.owner_epoch == c->f.owner);
    CHECK(candidate_recovery_view_info(v).cut.highest_owner_epoch == NEW_OWNER);
    cf_expect_resident(c,v,1);
}

static void expect_limit(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_semantic_transaction_t a = {0}; candidate_semantic_admission_t m = {0};
    bool found = cf_semantic_of(v,admitted_active.id,&a) && cf_admission_of(v,admitted_active.id,&m);
    printf("ACTIVE limit found=%d chunk_bytes=%llu chunks=%u limit=%llu/%llu has_result=%d\n",found,(unsigned long long)a.chunk_bytes,
        a.value.result_chunks,(unsigned long long)a.result_limit,(unsigned long long)m.result_limit,m.has_result);
    CHECK(found && a.chunk_bytes == ACTIVE_LIMIT && a.value.result_chunks == 1 && a.value.result_bytes == ACTIVE_LIMIT);
    CHECK(m.has_result && m.result_limit == ACTIVE_LIMIT && a.value.state == CANDIDATE_WITNESS_UNFINISHED);
    cf_expect_resident(c,v,1);
}

static void expect_committed(const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_witness_info_t w = candidate_wal_witness_info(candidate_recovery_view_witness(v));
    printf("ACTIVE committed plain=%llu csn=%llu highest_csn=%llu resident=%d\n",(unsigned long long)plain_active.id,
        (unsigned long long)plain_active.csn,(unsigned long long)w.highest_csn,
        candidate_wal_witness_find(candidate_recovery_view_witness(v),plain_active.id) != NULL);
    CHECK(w.highest_csn == plain_active.csn && plain_active.csn == c->f.built.fx->highest_csn && c->last_plain == plain_active.id);
    cf_expect_resident(c,v,0);
}

void cf_active_rows(void)
{
    static const cf_row_t rows[] = {{"previous-valid",extend_valid,CANDIDATE_WAL_OK,expect_extended},
        {"previous-forked",extend_forked,CANDIDATE_WAL_CORRUPT,NULL},
        {"owner-abort-valid",abort_new_owner,CANDIDATE_WAL_OK,expect_owner_abort},
        {"owner-extend-wrong",extend_new_owner,CANDIDATE_WAL_CORRUPT,NULL},
        {"limit-valid",chunk_at_limit,CANDIDATE_WAL_OK,expect_limit},
        {"limit-over",chunk_over_limit,CANDIDATE_WAL_CORRUPT,NULL},
        {"csn-valid",commit_next_csn,CANDIDATE_WAL_OK,expect_committed},
        {"csn-repeat",commit_repeated_csn,CANDIDATE_WAL_CORRUPT,NULL},
        {"dependency-wrong",commit_wrong_dependency,CANDIDATE_WAL_CORRUPT,NULL}};
    const cf_row_group_t g = {"active",PREVIEW,active_setup,expect_active,rows,sizeof(rows) / sizeof(rows[0])};
    cf_run_rows(&g);
}

static uint32_t slot_of(const vf_t *f) { return f->built.txn[0].result.slot; }

static void edit_frozen_valid(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].frozen_csn = f->built.txn[0].csn;
}

static void edit_frozen_wrong(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].frozen_csn = f->built.txn[0].csn + 1;
}

static void edit_unknown_txn(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].transaction_id = 4000;
}

static void edit_retired_txn(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].transaction_id = LIVE_FIRST_ID + 3;
}

typedef struct { const char *name, *kind; vf_edit_t edit; candidate_wal_status_t closure, legacy; } live_row_t;

static void expect_live_closure(vf_t *f, const candidate_recovery_view_t *v)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    const candidate_witness_transaction_t *expired = candidate_wal_witness_find(w,f->built.txn[0].id);
    CHECK(expired != NULL && expired->state == CANDIDATE_WITNESS_COMMIT && expired->csn == f->built.txn[0].csn);
    for (unsigned n = 0; n < LIVE_ABORTS; n++) CHECK(candidate_wal_witness_find(w,LIVE_FIRST_ID + n) == NULL);
    for (unsigned n = 1; n < f->built.spec.requests; n++) {
        candidate_catalog_query_t q = scen_query(&f->built,n); const candidate_outcome_t *e = NULL;
        candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e); vf_expect_answer(f,n,lookup,e);
        CHECK(candidate_wal_witness_find(w,f->built.txn[n].id) != NULL);
    }
    candidate_catalog_query_t q = scen_query(&f->built,0); const candidate_outcome_t *e = (void *)1;
    CHECK(candidate_recovery_view_lookup(v,&q,&e) == CANDIDATE_CATALOG_OUTSIDE_RETENTION && e == NULL);
}

static void run_live_row(const live_row_t *row)
{
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"closure-live-%s",row->name) > 0);
    vf_t f; vf_boot(&f,tag,&EXPIRED,true);
    candidate_recovery_view_request_t request = vf_request(&f,RESOLVED,0);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = (void *)1;
    OK(candidate_recovery_view_open(f.wal,&request,&v0));
    CHECK(LIVE_FIRST_ID + LIVE_ABORTS <= f.anchor.transaction_highwater && LIVE_FIRST_ID > f.built.carrier_txn);
    for (unsigned n = 0; n < LIVE_ABORTS; n++) vf_abort_resultless(&f,LIVE_FIRST_ID + n);
    OK(candidate_wal_force(f.wal,candidate_wal_info(f.wal).valid_end_lsn));
    OK(candidate_recovery_view_continue(f.wal,&request,v0,&v1));
    expect_live_closure(&f,v1);
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v1);
    if (row->edit != NULL) vf_edit_versions(&f,f.built.txn[0].result.page.id,row->edit,&f);
    cf_source_t before; cf_source_take(v1,&before);
    candidate_wal_status_t status = candidate_recovery_view_continue(f.wal,&request,v1,&v2);
    bool is_same = cf_source_same(v1,&before);
    candidate_recovery_view_t *full = NULL; candidate_wal_status_t legacy = candidate_recovery_view_open(f.wal,&request,&full);
    printf("LIVE %s kind=%s closure=%s legacy_full=%s source_same=%d resident_txns=%u history_txns=%llu retired=%llu\n",row->name,
        row->kind,vf_status(status),vf_status(legacy),is_same,k.transactions,(unsigned long long)k.history.transactions,
        (unsigned long long)(k.history.transactions - k.transactions));
    CHECK(status == row->closure && legacy == row->legacy && (status == CANDIDATE_WAL_OK) == (v2 != NULL) && is_same);
    CHECK(k.history.transactions >= k.transactions + LIVE_ABORTS);
    if (v2 != NULL) expect_live_closure(&f,v2);
    cf_source_free(&before);
    candidate_recovery_view_close(&full); candidate_recovery_view_close(&v2);
    candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    CHECK(f.memory.live == 0);
    vf_free(&f);
}

void cf_live_rows(void)
{
    static const live_row_t rows[] = {{"healthy","healthy",NULL,CANDIDATE_WAL_OK,CANDIDATE_WAL_OK},
        {"frozen-valid","healthy",edit_frozen_valid,CANDIDATE_WAL_OK,CANDIDATE_WAL_OK},
        {"frozen-wrong","corrupt-both",edit_frozen_wrong,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
        {"unknown-txn","corrupt-both",edit_unknown_txn,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
        {"retired-txn-tamper","malformed-same-lsn-data-tamper:closure-tightened-legacy-weaker",edit_retired_txn,
            CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_OK}};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) run_live_row(&rows[i]);
}
