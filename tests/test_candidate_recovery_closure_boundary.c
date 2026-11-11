#include "test_candidate_recovery_closure_boundary_fixture.h"

#include <string.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_outcome_rebuild.h"
#include "../src/storage/candidate_wal_internal.h"
#include "../src/storage/candidate_wal_witness_data_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define PREVIEW CANDIDATE_RECOVERY_VIEW_PREVIEW
#define FOREIGN_ID CF_FIRST_ID
#define ABORT_LIMIT 64u

unsigned checks;

typedef struct {
    candidate_recovery_closure_t closure;
    candidate_recovery_view_info_t info;
} snapshot_t;
typedef struct {
    candidate_wal_status_t status;
    bool is_out_null;
    unsigned calls, loads, source_loads;
    candidate_wal_info_t out_cut;
    uint8_t out_anchor_uuid[16];
} foreign_t;
typedef struct {
    candidate_witness_transaction_t transaction;
    candidate_semantic_admission_t admission;
    bool has_transaction, has_admission;
} retained_t;

static const char *uuid_text(const uint8_t uuid[16], char out[33])
{
    bf_format_uuid(uuid,out);
    return out;
}

static snapshot_t snapshot_of(const candidate_recovery_view_t *v)
{
    return (snapshot_t){candidate_recovery_view_closure(v),candidate_recovery_view_info(v)};
}

static bool is_same_snapshot(const snapshot_t *a, const snapshot_t *b)
{
    bool is_same_closure = memcmp(&a->closure,&b->closure,sizeof(a->closure)) == 0;
    bool is_same_cut = a->info.cut.valid_end_lsn == b->info.cut.valid_end_lsn && a->info.cut.records == b->info.cut.records;
    bool is_same_anchor = memcmp(&a->info.anchor,&b->info.anchor,sizeof(a->info.anchor)) == 0;
    return is_same_closure && is_same_cut && is_same_anchor && a->info.resident_bytes == b->info.resident_bytes;
}

static bool is_same_info(const candidate_recovery_view_info_t *a, const candidate_recovery_view_info_t *b)
{
    bool is_same_cut = a->cut.valid_end_lsn == b->cut.valid_end_lsn && a->cut.records == b->cut.records &&
        a->cut.segments == b->cut.segments;
    bool is_same_graph = a->live_pages == b->live_pages && a->free_pages == b->free_pages && a->versions == b->versions;
    bool is_same_outcomes = a->outcomes.committed == b->outcomes.committed && a->outcomes.aborted == b->outcomes.aborted &&
        a->outcomes.result_bytes == b->outcomes.result_bytes;
    return is_same_cut && is_same_graph && is_same_outcomes && a->cut.highest_owner_epoch == b->cut.highest_owner_epoch;
}

static bool is_same_anchor_except_uuid(const candidate_control_state_t *a, const candidate_control_state_t *b)
{
    candidate_control_state_t renamed = *a; memcpy(renamed.uuid,b->uuid,16);
    return memcmp(&renamed,b,sizeof(renamed)) == 0 && memcmp(a->uuid,b->uuid,16) != 0;
}

static void expect_pages_namespaced(bf_db_t *b)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; unsigned loads = b->loads;
    OK(bf_load(b,b->anchor.outcome_root,page));
    candidate_page_info_t info;
    candidate_wal_status_t own = candidate_page_decode_header(b->uuid,page,sizeof(page),&info);
    candidate_wal_status_t foreign = candidate_page_decode_header(fx_uuid,page,sizeof(page),&info);
    candidate_wal_status_t wal_own = candidate_wal_check_uuid(b->wal,b->uuid), wal_foreign = candidate_wal_check_uuid(b->wal,fx_uuid);
    printf("NAMESPACE b_page_own=%s b_page_with_a=%s b_wal_own=%s b_wal_with_a=%s\n",vf_status(own),vf_status(foreign),
        vf_status(wal_own),vf_status(wal_foreign));
    CHECK(own == CANDIDATE_WAL_OK && foreign == CANDIDATE_WAL_UNSUPPORTED && b->loads == loads + 1);
    CHECK(wal_own == CANDIDATE_WAL_OK && wal_foreign == CANDIDATE_WAL_UNSUPPORTED);
}

static void expect_b_lookups(cf_t *a, const candidate_recovery_view_t *vb)
{
    for (unsigned n = 0; n < a->f.built.spec.requests; n++) {
        candidate_catalog_query_t q = scen_query(&a->f.built,n); memcpy(q.uuid,bf_uuid_b,16);
        const candidate_outcome_t *e = NULL;
        candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(vb,&q,&e);
        vf_expect_answer(&a->f,n,lookup,e);
    }
}

static void print_pair(const char *tag, const candidate_recovery_view_t *va, const candidate_recovery_view_t *vb)
{
    candidate_recovery_view_info_t ia = candidate_recovery_view_info(va), ib = candidate_recovery_view_info(vb);
    candidate_recovery_closure_t ka = candidate_recovery_view_closure(va), kb = candidate_recovery_view_closure(vb);
    char ua[33], ub[33];
    printf("PAIR %s uuid_a=%s uuid_b=%s root=%llu/%llu checkpoint=%llu..%llu/%llu..%llu end=%llu/%llu records=%llu/%llu "
        "segments=%u/%u owner=%llu/%llu live=%u/%u versions=%llu/%llu committed=%u/%u aborted=%u/%u txns=%u/%u groups=%u/%u "
        "events=%u/%u\n",tag,uuid_text(ia.anchor.uuid,ua),uuid_text(ib.anchor.uuid,ub),(unsigned long long)ia.anchor.outcome_root.id,
        (unsigned long long)ib.anchor.outcome_root.id,(unsigned long long)ia.anchor.checkpoint_begin,
        (unsigned long long)ia.anchor.checkpoint_end,(unsigned long long)ib.anchor.checkpoint_begin,
        (unsigned long long)ib.anchor.checkpoint_end,(unsigned long long)ia.cut.valid_end_lsn,(unsigned long long)ib.cut.valid_end_lsn,
        (unsigned long long)ia.cut.records,(unsigned long long)ib.cut.records,ia.cut.segments,ib.cut.segments,
        (unsigned long long)ia.cut.highest_owner_epoch,(unsigned long long)ib.cut.highest_owner_epoch,ia.live_pages,ib.live_pages,
        (unsigned long long)ia.versions,(unsigned long long)ib.versions,ia.outcomes.committed,ib.outcomes.committed,
        ia.outcomes.aborted,ib.outcomes.aborted,ka.transactions,kb.transactions,ka.groups,kb.groups,ka.events,kb.events);
}

static void open_pair(cf_t *a, bf_db_t *b, candidate_recovery_view_t **va, candidate_recovery_view_t **vb)
{
    candidate_recovery_view_request_t rb = bf_request(b,&b->anchor,RESOLVED,0);
    unsigned loads = b->loads;
    candidate_wal_status_t sa = cf_open(a,va), sb = candidate_recovery_view_open(b->wal,&rb,vb);
    printf("PAIR_OPEN a=%s b=%s expected=OK/OK b_loads=%u\n",vf_status(sa),vf_status(sb),b->loads - loads);
    OK(sa); OK(sb);
    CHECK(b->loads > loads && is_same_anchor_except_uuid(&a->f.anchor,&b->anchor));
}

static void healthy_pair_opens_both_namespaces(void)
{
    cf_t a; bf_db_t b; cf_boot(&a,"boundary-healthy-pair",RESOLVED); bf_clone(&a,&b);
    candidate_recovery_view_t *va = NULL, *vb = NULL;

    open_pair(&a,&b,&va,&vb);
    print_pair("healthy",va,vb);

    candidate_recovery_view_info_t ia = candidate_recovery_view_info(va), ib = candidate_recovery_view_info(vb);
    candidate_recovery_closure_t ka = candidate_recovery_view_closure(va), kb = candidate_recovery_view_closure(vb);
    CHECK(is_same_info(&ia,&ib) && memcmp(&ka,&kb,sizeof(ka)) == 0);
    CHECK(memcmp(ia.anchor.uuid,fx_uuid,16) == 0 && memcmp(ib.anchor.uuid,bf_uuid_b,16) == 0);
    expect_pages_namespaced(&b);
    expect_b_lookups(&a,vb); cf_expect_outcomes(&a,va);
    candidate_recovery_view_close(&vb); candidate_recovery_view_close(&va);
    CHECK(b.memory.live == 0 && a.f.memory.live == 0);
    bf_free(&b); cf_free(&a);
}

static foreign_t continue_foreign(cf_t *a, bf_db_t *b, const candidate_recovery_view_t *source)
{
    candidate_recovery_view_request_t r = bf_request(b,&a->f.anchor,RESOLVED,0);
    b->memory.calls = 0;
    unsigned loads = b->loads, source_loads = a->f.loads;
    candidate_recovery_view_t *out = (void *)1;
    foreign_t f = {.status = candidate_recovery_view_continue(b->wal,&r,source,&out)};
    f.calls = b->memory.calls; f.loads = b->loads - loads; f.source_loads = a->f.loads - source_loads;
    f.is_out_null = out == NULL;
    if (out == (void *)1) out = NULL;
    candidate_recovery_view_info_t info = candidate_recovery_view_info(out);
    f.out_cut = info.cut; memcpy(f.out_anchor_uuid,info.anchor.uuid,16);
    candidate_recovery_view_close(&out);
    return f;
}

static void print_foreign(const char *tag, const foreign_t *f, const bf_db_t *b, const candidate_recovery_view_t *source)
{
    candidate_recovery_view_info_t s = candidate_recovery_view_info(source); candidate_wal_info_t t = candidate_wal_info(b->wal);
    char anchor[33], load[33], out[33];
    printf("FOREIGN %s status=%s expected=UNSUPPORTED out_null=%d expected_out_null=1 calls=%u expected_calls=0 b_loads=%u "
        "expected_b_loads=0 a_loads=%u request_anchor_uuid=%s request_load_uuid=%s wal_uuid=%s out_anchor_uuid=%s "
        "source_cut=%llu/%llu b_cut=%llu/%llu out_cut=%llu/%llu b_live=%zu\n",tag,vf_status(f->status),f->is_out_null,f->calls,
        f->loads,f->source_loads,uuid_text(s.anchor.uuid,anchor),uuid_text(b->uuid,load),load,uuid_text(f->out_anchor_uuid,out),
        (unsigned long long)s.cut.valid_end_lsn,(unsigned long long)s.cut.records,(unsigned long long)t.valid_end_lsn,
        (unsigned long long)t.records,(unsigned long long)f->out_cut.valid_end_lsn,(unsigned long long)f->out_cut.records,
        b->memory.live);
}

static void expect_foreign_refused(const foreign_t *f, const snapshot_t *before, const snapshot_t *after)
{
    CHECK(is_same_snapshot(before,after));
    CHECK(f->status == CANDIDATE_WAL_UNSUPPORTED && f->is_out_null);
    CHECK(f->calls == 0 && f->loads == 0 && f->source_loads == 0);
}

static void foreign_empty_suffix_is_refused(void)
{
    cf_t a; bf_db_t b; cf_boot(&a,"boundary-foreign-empty",RESOLVED); bf_clone(&a,&b);
    candidate_recovery_view_t *source = NULL, *vb = NULL, *vb_next = NULL;
    open_pair(&a,&b,&source,&vb);
    candidate_recovery_view_request_t rb = bf_request(&b,&b.anchor,RESOLVED,0);
    candidate_wal_status_t own = candidate_recovery_view_continue(b.wal,&rb,vb,&vb_next);
    printf("FOREIGN_CONTROL empty b_source_b_request=%s expected=OK\n",vf_status(own));
    OK(own); candidate_recovery_view_close(&vb_next);
    snapshot_t before = snapshot_of(source);

    foreign_t f = continue_foreign(&a,&b,source);

    snapshot_t after = snapshot_of(source);
    print_foreign("empty",&f,&b,source);
    candidate_recovery_view_close(&vb); candidate_recovery_view_close(&source);
    expect_foreign_refused(&f,&before,&after);
    CHECK(b.memory.live == 0 && a.f.memory.live == 0);
    bf_free(&b); cf_free(&a);
}

static void foreign_abort_suffix_is_refused(void)
{
    cf_t a; bf_db_t b; cf_boot(&a,"boundary-foreign-suffix",RESOLVED); bf_clone(&a,&b);
    candidate_recovery_view_t *source = NULL, *vb = NULL, *vb_next = NULL;
    open_pair(&a,&b,&source,&vb);
    CHECK(candidate_wal_witness_find(candidate_recovery_view_witness(source),FOREIGN_ID) == NULL);
    uint64_t end = bf_abort(&b,FOREIGN_ID);
    candidate_recovery_view_request_t rb = bf_request(&b,&b.anchor,RESOLVED,0);
    candidate_wal_status_t own = candidate_recovery_view_continue(b.wal,&rb,vb,&vb_next);
    candidate_recovery_closure_t base = candidate_recovery_view_closure(vb), next = candidate_recovery_view_closure(vb_next);
    printf("FOREIGN_CONTROL suffix b_source_b_request=%s expected=OK cut=%llu b_end=%llu history_txns=%llu->%llu aborts=%llu->%llu\n",
        vf_status(own),(unsigned long long)candidate_recovery_view_info(vb_next).cut.valid_end_lsn,(unsigned long long)end,
        (unsigned long long)base.history.transactions,(unsigned long long)next.history.transactions,
        (unsigned long long)base.history.aborts,(unsigned long long)next.history.aborts);
    OK(own);
    CHECK(candidate_recovery_view_info(vb_next).cut.valid_end_lsn == end && next.history.transactions == base.history.transactions + 1);
    CHECK(next.history.aborts == base.history.aborts + 1 && next.history.max_started_id == FOREIGN_ID);
    candidate_recovery_view_close(&vb_next);
    snapshot_t before = snapshot_of(source);

    foreign_t f = continue_foreign(&a,&b,source);

    snapshot_t after = snapshot_of(source);
    print_foreign("suffix",&f,&b,source);
    candidate_recovery_view_close(&vb); candidate_recovery_view_close(&source);
    expect_foreign_refused(&f,&before,&after);
    CHECK(b.memory.live == 0 && a.f.memory.live == 0);
    bf_free(&b); cf_free(&a);
}

static uint64_t append_admitted_abort(cf_t *c, fx_txn_t *t)
{
    uint64_t id = c->next_id++;
    cf_admit(c,t,id,ABORT_LIMIT,NULL); cf_end(c,t,false); cf_flush(c);
    printf("UNPUBLISHED_ABORT transaction=%llu epoch=%llu floor=%llu first=%llu last=%llu records=%llu terminal_end=%llu\n",
        (unsigned long long)id,(unsigned long long)t->epoch,(unsigned long long)c->floor,(unsigned long long)t->first,
        (unsigned long long)t->last,(unsigned long long)t->records,(unsigned long long)t->terminal_end);
    return id;
}

static bool rebuild_entry(cf_t *c, uint64_t id, candidate_outcome_t *out)
{
    candidate_recovery_view_request_t r = cf_request(c);
    candidate_outcome_rebuild_options_t o = {.source = {.catalog = r.load,.checkpoint = &c->f.anchor},.owner_epoch = c->f.owner + 1};
    candidate_outcome_rebuild_t *rebuild = NULL;
    candidate_wal_status_t status = candidate_outcome_rebuild_load(c->f.wal,&o,&rebuild);
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(rebuild);
    bool is_found = false;
    for (uint32_t i = 0; !is_found && i < info.count; i++) {
        const candidate_outcome_t *e = candidate_outcome_rebuild_at(rebuild,i);
        if (e->transaction_id == id) { *out = *e; is_found = true; }
    }
    printf("REBUILD status=%s expected=OK entries=%u current_epoch=%llu floor=%llu transaction=%llu found=%d\n",vf_status(status),
        info.count,(unsigned long long)info.current_epoch,(unsigned long long)info.retention_floor,(unsigned long long)id,is_found);
    candidate_outcome_rebuild_close(&rebuild);
    OK(status);
    return is_found;
}

static void expect_projected_abort(const cf_t *c, const fx_txn_t *t, const candidate_outcome_t *e)
{
    uint8_t empty[32]; OK(candidate_digest_sha256(NULL,0,empty));
    printf("PROJECTED transaction=%llu state=%d epoch=%llu csn=%llu terminal_end=%llu limit=%llu owner=%llu result=%llu bytes=%llu "
        "chunks=%u\n",(unsigned long long)e->transaction_id,(int)e->state,(unsigned long long)e->epoch,(unsigned long long)e->csn,
        (unsigned long long)e->terminal_end_lsn,(unsigned long long)e->declared_result_limit,(unsigned long long)e->admitting_owner_epoch,
        (unsigned long long)e->result.page.id,(unsigned long long)e->result_bytes,e->result_chunks);
    CHECK(e->state == CANDIDATE_OUTCOME_ABORTED && e->csn == 0 && e->terminal_end_lsn == t->terminal_end);
    CHECK(e->epoch == t->epoch && c->floor <= e->epoch && memcmp(e->nonce,t->nonce,16) == 0);
    CHECK(memcmp(e->operation_digest,t->operation,32) == 0 && e->declared_result_limit == ABORT_LIMIT);
    CHECK(e->admitting_owner_epoch == c->f.owner && e->result.page.id == 0 && e->result_bytes == 0);
    CHECK(e->result_chunks == 0 && memcmp(e->result_digest,empty,32) == 0);
}

static retained_t retained_of(const candidate_recovery_view_t *v, uint64_t id)
{
    retained_t r = {0};
    if (v == NULL) return r;
    const candidate_witness_transaction_t *t = candidate_wal_witness_find(candidate_recovery_view_witness(v),id);
    if (t != NULL) { r.transaction = *t; r.has_transaction = true; }
    candidate_semantic_admission_t a;
    for (uint32_t i = 0; !r.has_admission && candidate_admissions_at(v->admissions,i,&a); i++)
        if (a.transaction_id == id) { r.admission = a; r.has_admission = true; }
    return r;
}

static void print_retained(const char *tag, const retained_t *r)
{
    const candidate_witness_transaction_t *t = &r->transaction;
    printf("RETAINED %s witness=%d admission=%d state=%d admitted=%d epoch=%llu csn=%llu terminal_end=%llu records=%llu "
        "admission_owner=%llu admission_limit=%llu has_result=%d\n",tag,r->has_transaction,r->has_admission,(int)t->state,
        t->has_admission,(unsigned long long)t->request_epoch,(unsigned long long)t->csn,(unsigned long long)t->terminal_end_lsn,
        (unsigned long long)t->records,(unsigned long long)r->admission.owner_epoch,(unsigned long long)r->admission.result_limit,
        r->admission.has_result);
}

static void expect_retained_as_projected(const retained_t *r, const candidate_outcome_t *e)
{
    const candidate_witness_transaction_t *t = &r->transaction;
    CHECK(r->has_transaction && r->has_admission);
    CHECK(t->state == CANDIDATE_WITNESS_ABORT && t->has_admission && t->request_epoch == e->epoch);
    CHECK(memcmp(t->request_nonce,e->nonce,16) == 0 && memcmp(t->operation_digest,e->operation_digest,32) == 0);
    CHECK(t->csn == e->csn && t->terminal_end_lsn == e->terminal_end_lsn);
    CHECK(r->admission.owner_epoch == e->admitting_owner_epoch && r->admission.result_limit == e->declared_result_limit);
    CHECK(!r->admission.has_result);
}

static void unpublished_abort_survives_compaction(void)
{
    cf_t c; cf_boot(&c,"boundary-unpublished-abort",PREVIEW);
    fx_txn_t t; uint64_t id = append_admitted_abort(&c,&t);
    candidate_outcome_t projected = {0};
    CHECK(rebuild_entry(&c,id,&projected));
    expect_projected_abort(&c,&t,&projected);
    candidate_recovery_view_t *v0 = NULL, *v1 = (void *)1;
    OK(cf_open(&c,&v0));
    retained_t initial = retained_of(v0,id); print_retained("initial",&initial);
    expect_retained_as_projected(&initial,&projected);
    snapshot_t before = snapshot_of(v0);

    candidate_wal_status_t status = cf_continue(&c,v0,&v1);

    if (v1 == (void *)1) v1 = NULL;
    snapshot_t after = snapshot_of(v0);
    retained_t compacted = retained_of(v1,id);
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v1);
    printf("UNPUBLISHED continue=%s expected=OK epoch=%llu floor=%llu resident_txns=%u admissions=%u tombstones=%u "
        "expected_witness=1 expected_admission=1\n",vf_status(status),(unsigned long long)c.epoch,(unsigned long long)c.floor,
        k.transactions,k.admissions,k.tombstones);
    print_retained("compacted",&compacted);
    candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    OK(status);
    CHECK(is_same_snapshot(&before,&after) && c.f.memory.live == 0);
    expect_retained_as_projected(&compacted,&projected);
    cf_free(&c);
}

static void expired_abort_may_retire(void)
{
    cf_t c; cf_boot(&c,"boundary-expired-abort",PREVIEW);
    fx_txn_t t; uint64_t id = append_admitted_abort(&c,&t);
    candidate_outcome_t projected = {0};
    CHECK(rebuild_entry(&c,id,&projected));
    candidate_recovery_view_t *v0 = NULL, *v1 = (void *)1;
    OK(cf_open(&c,&v0));
    cf_epoch(&c,c.epoch + 1); cf_flush(&c);
    candidate_outcome_t expired = {0};
    bool is_rebuilt = rebuild_entry(&c,id,&expired);

    candidate_wal_status_t status = cf_continue(&c,v0,&v1);

    if (v1 == (void *)1) v1 = NULL;
    retained_t r = retained_of(v1,id);
    printf("EXPIRED continue=%s expected=OK abort_epoch=%llu epoch=%llu floor=%llu rebuild_found=%d expected_rebuild_found=0\n",
        vf_status(status),(unsigned long long)t.epoch,(unsigned long long)c.epoch,(unsigned long long)c.floor,is_rebuilt);
    print_retained("expired",&r);
    candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    OK(status);
    CHECK(t.epoch < c.floor && !is_rebuilt && c.f.memory.live == 0);
    cf_free(&c);
}

static uint64_t append_plain_abort(cf_t *c)
{
    fx_txn_t t; uint64_t id = c->next_id++;
    cf_begin(c,&t,id); cf_end(c,&t,false); cf_flush(c);
    printf("PLAIN_ABORT transaction=%llu first=%llu terminal_end=%llu records=%llu\n",(unsigned long long)id,
        (unsigned long long)t.first,(unsigned long long)t.terminal_end,(unsigned long long)t.records);
    return id;
}

static void print_vacated_slot(const candidate_recovery_view_t *v)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v);
    uint32_t at = w->info.transactions;
    CHECK(at < CANDIDATE_WITNESS_TRANSACTIONS);
    const candidate_witness_transaction_t *s = &w->transactions[at].value;
    printf("VACATED_SLOT index=%u id=%llu state=%d first=%llu last=%llu records=%llu crc=%u has_begin=%d has_admission=%d\n",at,
        (unsigned long long)s->transaction_id,(int)s->state,(unsigned long long)s->first_lsn,(unsigned long long)s->last_lsn,
        (unsigned long long)s->records,s->chain_crc,s->has_begin,s->has_admission);
}

static void retired_slot_accepts_next_transaction(void)
{
    cf_t c; cf_boot(&c,"boundary-slot-reuse",RESOLVED);
    uint64_t retired = append_plain_abort(&c);
    candidate_recovery_view_t *v0 = NULL, *v1 = NULL, *v2 = (void *)1;
    OK(cf_open(&c,&v0)); OK(cf_continue(&c,v0,&v1));
    candidate_recovery_closure_t k1 = candidate_recovery_view_closure(v1);
    bool is_retired = candidate_wal_witness_find(candidate_recovery_view_witness(v1),retired) == NULL;
    printf("SLOT_SOURCE retired=%llu retired_from_witness=%d resident_txns=%u history_txns=%llu aborts=%llu max_started=%llu\n",
        (unsigned long long)retired,is_retired,k1.transactions,(unsigned long long)k1.history.transactions,
        (unsigned long long)k1.history.aborts,(unsigned long long)k1.history.max_started_id);
    CHECK(is_retired);
    print_vacated_slot(v1);
    uint64_t next = append_plain_abort(&c);
    snapshot_t before = snapshot_of(v1);

    candidate_wal_status_t status = cf_continue(&c,v1,&v2);

    if (v2 == (void *)1) v2 = NULL;
    snapshot_t after = snapshot_of(v1);
    candidate_recovery_closure_t k2 = candidate_recovery_view_closure(v2);
    printf("SLOT_REUSE next=%llu continue=%s expected=OK out_null=%d history_txns=%llu expected_history_txns=%llu aborts=%llu "
        "expected_aborts=%llu max_started=%llu expected_max_started=%llu source_same=%d\n",(unsigned long long)next,vf_status(status),
        v2 == NULL,(unsigned long long)k2.history.transactions,(unsigned long long)(k1.history.transactions + 1),
        (unsigned long long)k2.history.aborts,(unsigned long long)(k1.history.aborts + 1),(unsigned long long)k2.history.max_started_id,
        (unsigned long long)next,is_same_snapshot(&before,&after));
    candidate_recovery_view_close(&v2); candidate_recovery_view_close(&v1); candidate_recovery_view_close(&v0);
    CHECK(is_same_snapshot(&before,&after) && c.f.memory.live == 0);
    OK(status);
    CHECK(k2.history.transactions == k1.history.transactions + 1 && k2.history.aborts == k1.history.aborts + 1);
    CHECK(k2.history.max_started_id == next && next > retired);
    cf_free(&c);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;

static const test_case_t CASES[] = {{"healthy-pair",healthy_pair_opens_both_namespaces},
    {"foreign-empty",foreign_empty_suffix_is_refused},{"foreign-suffix",foreign_abort_suffix_is_refused},
    {"unpublished-abort",unpublished_abort_survives_compaction},{"expired-abort",expired_abort_may_retire},
    {"slot-reuse",retired_slot_accepts_next_transaction}};

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
    printf("PASS recovery closure boundary case=%s checks=%u\n",selected,checks);
    return 0;
}
