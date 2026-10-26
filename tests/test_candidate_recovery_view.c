#include "test_candidate_recovery_view_fixture.h"

#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define PREVIEW CANDIDATE_RECOVERY_VIEW_PREVIEW
#define WITNESS_RESIDENT_BYTES 1681736u

unsigned checks;

static const scen_t CLEAN = {.requests = 3,.req = vf_clean};
static const scen_t FREED = {.requests = 3,.req = vf_clean,.root_epoch = 1,.root_floor = 1,.epoch_to = 2,.floor_to = 1,
    .free_epoch_root = true};
static const scen_t EXPIRED = {.requests = 4,.req = vf_expired,.root_epoch = 2,.root_floor = 2};

static const char *mode_name(candidate_recovery_view_mode_t mode) { return mode == RESOLVED ? "resolved" : "preview"; }

static uint64_t owner_of(candidate_recovery_view_mode_t mode) { return mode == PREVIEW ? 8 : 0; }

static candidate_wal_status_t open_view(vf_t *f, candidate_recovery_view_mode_t mode, candidate_recovery_view_t **out)
{
    candidate_recovery_view_request_t request = vf_request(f,mode,owner_of(mode));
    return candidate_recovery_view_open(f->wal,&request,out);
}

static candidate_wal_status_t try_view(vf_t *f, candidate_recovery_view_mode_t mode)
{
    candidate_recovery_view_t *v = (void *)1;
    candidate_wal_status_t status = open_view(f,mode,&v);
    CHECK((status == CANDIDATE_WAL_OK) == (v != NULL));
    candidate_recovery_view_close(&v);
    return status;
}

static candidate_page_result_t *read_body(vf_t *f, const candidate_outcome_t *e)
{
    if (e->result.page.id == 0) return NULL;
    candidate_result_read_t r = {.ref = e->result,.transaction_id = e->transaction_id,.result_bytes = e->result_bytes,
        .load = vf_load,.context = f,.memory = &f->allocator};
    memcpy(r.uuid,fx_uuid,16); memcpy(r.digest,e->result_digest,32);
    candidate_page_result_t *body = NULL; OK(candidate_page_read_result(&r,&body));
    return body;
}

static void expect_recovery_answers(vf_t *f, candidate_recovery_t *r)
{
    for (unsigned i = 0; i < f->built.spec.requests; i++) {
        candidate_catalog_query_t q = scen_query(&f->built,i); candidate_recovery_answer_t a;
        OK(candidate_recovery_query(r,&q,&a));
        vf_expect_answer(f,i,a.lookup,&a.outcome); vf_expect_body(f,i,a.body);
        candidate_page_result_close(&a.body);
    }
}

static void resolved_view_matches_legacy_outcomes(void)
{
    vf_t f; vf_boot(&f,"resolved",&CLEAN,true);
    candidate_recovery_info_t graph; candidate_outcome_witness_info_t outcomes = {0};
    OK(vf_legacy(&f,RESOLVED,0,&graph,&outcomes));
    vf_reset_memory(&f); f.loads = 0;
    candidate_recovery_view_t *v = NULL; OK(open_view(&f,RESOLVED,&v));
    candidate_recovery_view_info_t i = candidate_recovery_view_info(v);
    printf("RESOLVED live=%u free=%u versions=%llu committed=%u aborted=%u admitted=%u result_bytes=%llu cut_end=%llu "
        "records=%llu resident=%zu peak=%zu calls=%u loads=%u\n",i.live_pages,i.free_pages,(unsigned long long)i.versions,
        i.outcomes.committed,i.outcomes.aborted,i.outcomes.admitted,(unsigned long long)i.outcomes.result_bytes,
        (unsigned long long)i.cut.valid_end_lsn,(unsigned long long)i.cut.records,i.resident_bytes,f.memory.peak,f.memory.calls,f.loads);
    CHECK(i.mode == RESOLVED && i.live_pages == graph.live_pages && i.free_pages == graph.free_pages && i.versions == graph.versions);
    CHECK(i.cut.valid_end_lsn == graph.wal_end_lsn && i.cut.valid_end_lsn == candidate_wal_info(f.wal).valid_end_lsn);
    CHECK(i.outcomes.committed == outcomes.committed && i.outcomes.aborted == outcomes.aborted);
    CHECK(i.outcomes.admitted == outcomes.admitted && i.outcomes.result_bytes == outcomes.result_bytes);
    CHECK(i.outcomes.committed == 2 && i.outcomes.aborted == 1 && i.resident_bytes == f.memory.live);
    for (unsigned n = 0; n < f.built.spec.requests; n++) {
        candidate_catalog_query_t q = scen_query(&f.built,n); const candidate_outcome_t *e = NULL;
        candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e);
        vf_expect_answer(&f,n,lookup,e);
        candidate_page_result_t *body = read_body(&f,e); vf_expect_body(&f,n,body); candidate_page_result_close(&body);
    }
    candidate_catalog_query_t q = scen_query(&f.built,0); const candidate_outcome_t *e = (void *)1;
    q.operation_digest[0] ^= 1; CHECK(candidate_recovery_view_lookup(v,&q,&e) == CANDIDATE_CATALOG_CONTENT_MISMATCH && e == NULL);
    q = scen_query(&f.built,0); q.nonce[15] ^= 0xff; CHECK(candidate_recovery_view_lookup(v,&q,&e) == CANDIDATE_CATALOG_NOT_FOUND);
    candidate_recovery_view_close(&v); CHECK(v == NULL && f.memory.live == 0);
    vf_free(&f);
}

static void recovery_query_reads_view_bodies(void)
{
    vf_t f; vf_boot(&f,"recovery",&CLEAN,true);
    candidate_recovery_view_t *v = NULL; OK(open_view(&f,RESOLVED,&v));
    candidate_recovery_view_info_t view = candidate_recovery_view_info(v); candidate_recovery_view_close(&v);
    vf_close(&f); vf_reset_memory(&f);
    candidate_recovery_options_t o = vf_options(&f,8); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&o,&r));
    candidate_recovery_info_t info = candidate_recovery_info(r);
    printf("RECOVERY live=%u versions=%llu wal_end=%llu redo_written=%llu resident=%zu view_resident=%zu live_bytes=%zu peak=%zu\n",
        info.live_pages,(unsigned long long)info.versions,(unsigned long long)info.wal_end_lsn,
        (unsigned long long)info.redo.pages_written,info.resident_bytes,view.resident_bytes,f.memory.live,f.memory.peak);
    CHECK(info.live_pages == view.live_pages && info.free_pages == view.free_pages && info.versions == view.versions);
    CHECK(info.wal_end_lsn == view.cut.valid_end_lsn && info.anchor.checkpoint_id == f.anchor.checkpoint_id);
    CHECK(info.resident_bytes == f.memory.live && info.resident_bytes > view.resident_bytes);
    expect_recovery_answers(&f,r); CHECK(f.memory.live == info.resident_bytes);
    OK(candidate_recovery_close(&r)); CHECK(f.memory.live == 0);
    vf_free(&f);
}

static void stale_admitted_preview_accepts_resolved_refuses(void)
{
    vf_t f; vf_boot(&f,"preview",&CLEAN,true);
    OK(try_view(&f,RESOLVED)); OK(try_view(&f,PREVIEW));
    vf_mark_admitted(&f,0);
    candidate_wal_status_t legacy_preview = vf_legacy(&f,PREVIEW,8,NULL,NULL), legacy_resolved = vf_legacy(&f,RESOLVED,0,NULL,NULL);
    vf_reset_memory(&f);
    candidate_wal_status_t resolved = try_view(&f,RESOLVED);
    CHECK(f.memory.live == 0);
    candidate_recovery_view_t *v = NULL; candidate_wal_status_t preview = open_view(&f,PREVIEW,&v);
    printf("PREVIEW stale-admitted legacy_preview=%s view_preview=%s legacy_resolved=%s view_resolved=%s\n",vf_status(legacy_preview),
        vf_status(preview),vf_status(legacy_resolved),vf_status(resolved));
    CHECK(legacy_preview == CANDIDATE_WAL_OK && preview == CANDIDATE_WAL_OK);
    CHECK(legacy_resolved == CANDIDATE_WAL_STALE && resolved == CANDIDATE_WAL_STALE);
    candidate_recovery_view_info_t i = candidate_recovery_view_info(v);
    CHECK(i.mode == PREVIEW && i.owner_epoch == 8 && i.outcomes.committed == 0 && i.resident_bytes == f.memory.live);
    candidate_catalog_query_t q = scen_query(&f.built,0); const candidate_outcome_t *e = (void *)1;
    CHECK(candidate_recovery_view_lookup(v,&q,&e) == CANDIDATE_CATALOG_UNAVAILABLE && e == NULL);
    candidate_recovery_view_close(&v); CHECK(f.memory.live == 0);
    vf_close(&f);
    candidate_recovery_options_t o = vf_options(&f,8); candidate_recovery_t *r = (void *)1;
    candidate_wal_status_t opened = candidate_recovery_open(&o,&r);
    CHECK(opened == CANDIDATE_WAL_STALE && r == NULL && f.memory.live == 0);
    o = vf_options(&f,9); OK(candidate_recovery_recover(&o,&r));
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    printf("PREVIEW_RECOVER open=%s published=%d outcomes=%u checkpoint=%llu\n",vf_status(opened),p.has_publication,p.outcomes,
        (unsigned long long)candidate_recovery_info(r).anchor.checkpoint_id);
    expect_recovery_answers(&f,r);
    OK(candidate_recovery_close(&r)); CHECK(f.memory.live == 0);
    vf_free(&f);
}

static uint32_t slot_of(const vf_t *f) { return f->built.txn[0].result.slot; }

static void edit_frozen(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].frozen_csn = f->built.txn[0].csn + 1;
}

static void edit_unknown_txn(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].transaction_id = 4000;
}

static void edit_frozen_valid(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count); v[slot_of(f)].frozen_csn = f->built.txn[0].csn;
}

static bool is_same_ref(candidate_version_ref_t a, candidate_version_ref_t b)
{
    return a.page.id == b.page.id && a.page.generation == b.page.generation && a.slot == b.slot && a.slot_generation == b.slot_generation;
}

static void edit_cycle(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; candidate_version_ref_t a = f->built.txn[0].result, b = f->built.txn[1].result;
    CHECK(a.page.id == l->ref.id && b.page.id == l->ref.id && a.slot != b.slot && a.slot < count && b.slot < count);
    v[a.slot].previous = b; v[b.slot].previous = a;
}

static void expect_cycle(vf_t *f)
{
    candidate_version_ref_t a = f->built.txn[0].result, b = f->built.txn[1].result;
    candidate_page_version_t va = {0}, vb = {0};
    candidate_wal_status_t sa = vf_read_version(f,a,&va), sb = vf_read_version(f,b,&vb);
    printf("CYCLE a=%llu/%u txn=%llu previous=%llu/%u codec=%s b=%llu/%u txn=%llu previous=%llu/%u codec=%s\n",
        (unsigned long long)a.page.id,a.slot,(unsigned long long)va.transaction_id,(unsigned long long)va.previous.page.id,
        va.previous.slot,vf_status(sa),(unsigned long long)b.page.id,b.slot,(unsigned long long)vb.transaction_id,
        (unsigned long long)vb.previous.page.id,vb.previous.slot,vf_status(sb));
    CHECK(sa == CANDIDATE_WAL_OK && sb == CANDIDATE_WAL_OK && !is_same_ref(a,b));
    CHECK(is_same_ref(va.previous,b) && is_same_ref(vb.previous,a));
    CHECK(va.transaction_id == f->built.txn[0].id && vb.transaction_id == f->built.txn[1].id);
}

static void edit_free(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count && f->built.epoch_root_id != 0);
    const candidate_allocation_entry_t *entry = fx_alloc_find(f->built.fx,f->built.epoch_root_id);
    CHECK(entry != NULL && entry->state == CANDIDATE_ALLOCATION_FREE);
    v[slot_of(f)].previous = (candidate_version_ref_t){{f->built.epoch_root_id,1},0,1};
}

static void edit_generation(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    (void)v; (void)count; (void)context; l->ref.generation++;
}

static void edit_lsn(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    (void)v; (void)count; (void)context; l->lsn += 8;
}

static void edit_body(candidate_page_location_t *l, candidate_page_version_t *v, uint32_t count, void *context)
{
    static uint8_t body[64];
    vf_t *f = context; (void)l; CHECK(slot_of(f) < count);
    candidate_page_version_t *e = &v[slot_of(f)];
    CHECK(e->inline_bytes != NULL && e->payload_bytes <= sizeof(body));
    memcpy(body,e->inline_bytes,e->payload_bytes); body[0] ^= 1; e->inline_bytes = body;
}

typedef struct {
    const char *name;
    const scen_t *spec;
    vf_edit_t edit;
    bool is_recovered;
    candidate_wal_status_t graph;
} mutation_t;

static void graph_mutations_refuse_like_legacy(void)
{
    static const mutation_t rows[] = {{"baseline",&CLEAN,NULL,true,CANDIDATE_WAL_OK},
        {"baseline-freed",&FREED,NULL,false,CANDIDATE_WAL_OK},{"frozen",&CLEAN,edit_frozen,true,CANDIDATE_WAL_CORRUPT},
        {"selected-unknown-txn",&CLEAN,edit_unknown_txn,true,CANDIDATE_WAL_CORRUPT},
        {"previous-cycle",&CLEAN,edit_cycle,true,CANDIDATE_WAL_CORRUPT},
        {"free-reference",&FREED,edit_free,false,CANDIDATE_WAL_CORRUPT},
        {"generation",&CLEAN,edit_generation,true,CANDIDATE_WAL_CORRUPT},
        {"page-lsn",&CLEAN,edit_lsn,true,CANDIDATE_WAL_CORRUPT},{"result-body",&CLEAN,edit_body,true,CANDIDATE_WAL_OK}};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        vf_t f; vf_boot(&f,rows[i].name,rows[i].spec,rows[i].is_recovered);
        candidate_wal_status_t before_r = vf_graph(&f,RESOLVED), before_p = vf_graph(&f,PREVIEW);
        CHECK(before_p == CANDIDATE_WAL_OK && (!rows[i].is_recovered || before_r == CANDIDATE_WAL_OK));
        if (rows[i].edit != NULL) vf_edit_versions(&f,f.built.txn[0].result.page.id,rows[i].edit,&f);
        if (rows[i].edit == edit_cycle) expect_cycle(&f);
        candidate_wal_status_t gr = vf_graph(&f,RESOLVED), gp = vf_graph(&f,PREVIEW);
        candidate_wal_status_t lr = vf_legacy(&f,RESOLVED,0,NULL,NULL), lp = vf_legacy(&f,PREVIEW,8,NULL,NULL);
        vf_reset_memory(&f);
        candidate_wal_status_t vr = try_view(&f,RESOLVED), vp = try_view(&f,PREVIEW);
        printf("GRAPH %s before_graph_resolved=%s before_graph_preview=%s graph_resolved=%s graph_preview=%s "
            "legacy_resolved=%s view_resolved=%s legacy_preview=%s view_preview=%s live=%zu\n",rows[i].name,vf_status(before_r),
            vf_status(before_p),vf_status(gr),vf_status(gp),vf_status(lr),vf_status(vr),vf_status(lp),vf_status(vp),f.memory.live);
        CHECK(vr == lr && vp == lp && f.memory.live == 0);
        if (rows[i].edit != NULL) CHECK(vr == CANDIDATE_WAL_CORRUPT && vp == CANDIDATE_WAL_CORRUPT);
        else CHECK(vp == CANDIDATE_WAL_OK && (!rows[i].is_recovered || vr == CANDIDATE_WAL_OK));
        if (rows[i].edit != NULL) CHECK(gr == rows[i].graph && gp == rows[i].graph);
        else CHECK(gp == CANDIDATE_WAL_OK && (!rows[i].is_recovered || gr == CANDIDATE_WAL_OK));
        vf_free(&f);
    }
}

typedef struct { const char *name; vf_edit_t edit; candidate_wal_status_t expected; } live_row_t;

static void nonselected_live_version_reaches_graph(void)
{
    static const live_row_t rows[] = {{"live-healthy",NULL,CANDIDATE_WAL_OK},{"live-frozen-valid",edit_frozen_valid,CANDIDATE_WAL_OK},
        {"live-unknown-txn",edit_unknown_txn,CANDIDATE_WAL_CORRUPT},{"live-frozen-wrong",edit_frozen,CANDIDATE_WAL_CORRUPT}};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        vf_t f; vf_boot(&f,rows[i].name,&EXPIRED,true);
        vf_expect_absent(&f,0);
        if (rows[i].edit != NULL) vf_edit_versions(&f,f.built.txn[0].result.page.id,rows[i].edit,&f);
        candidate_page_version_t live = {0}; candidate_wal_status_t codec = vf_read_version(&f,f.built.txn[0].result,&live);
        candidate_wal_status_t gr = vf_graph(&f,RESOLVED), gp = vf_graph(&f,PREVIEW);
        candidate_wal_status_t lr = vf_legacy(&f,RESOLVED,0,NULL,NULL), lp = vf_legacy(&f,PREVIEW,8,NULL,NULL);
        vf_reset_memory(&f);
        candidate_recovery_view_t *v = NULL; candidate_wal_status_t vr = open_view(&f,RESOLVED,&v), vp = try_view(&f,PREVIEW);
        candidate_recovery_view_info_t info = candidate_recovery_view_info(v);
        printf("NONSELECTED %s txn=%llu csn=%llu version_txn=%llu frozen=%llu codec=%s graph_resolved=%s graph_preview=%s "
            "legacy_resolved=%s view_resolved=%s legacy_preview=%s view_preview=%s versions=%llu committed=%u aborted=%u\n",
            rows[i].name,(unsigned long long)f.built.txn[0].id,(unsigned long long)f.built.txn[0].csn,
            (unsigned long long)live.transaction_id,(unsigned long long)live.frozen_csn,vf_status(codec),vf_status(gr),vf_status(gp),
            vf_status(lr),vf_status(vr),vf_status(lp),vf_status(vp),(unsigned long long)info.versions,info.outcomes.committed,
            info.outcomes.aborted);
        CHECK((vr == CANDIDATE_WAL_OK) == (v != NULL) && live.transaction_id != 0);
        CHECK(codec == CANDIDATE_WAL_OK && gr == rows[i].expected && gp == rows[i].expected);
        CHECK(lr == gr && lp == gp && vr == lr && vp == lp);
        if (vr == CANDIDATE_WAL_OK) {
            CHECK(info.versions == 3 && info.outcomes.committed == 2 && info.outcomes.aborted == 1);
            for (unsigned n = 1; n < f.built.spec.requests; n++) {
                candidate_catalog_query_t q = scen_query(&f.built,n); const candidate_outcome_t *e = NULL;
                candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e); vf_expect_answer(&f,n,lookup,e);
            }
            candidate_catalog_query_t q = scen_query(&f.built,0); const candidate_outcome_t *e = (void *)1;
            CHECK(candidate_recovery_view_lookup(v,&q,&e) == CANDIDATE_CATALOG_OUTSIDE_RETENTION && e == NULL);
        }
        candidate_recovery_view_close(&v); CHECK(f.memory.live == 0);
        vf_free(&f);
    }
}

static void append_record(vf_t *f)
{
    uint8_t body[16] = {0}; uint64_t end;
    candidate_wal_record_t r = {.type = 2,.transaction_id = 4001,.payload = body,.payload_bytes = sizeof(body)};
    OK(candidate_wal_append(f->wal,&r,&end));
}

static void rescan(vf_t *f)
{
    candidate_wal_info_t info; OK(candidate_wal_scan(f->wal,NULL,NULL,&info));
}

static void callback_cut_change_refuses_view(void)
{
    for (unsigned m = 0; m < 2; m++) {
        candidate_recovery_view_mode_t mode = m == 0 ? RESOLVED : PREVIEW;
        vf_t f; vf_boot(&f,"cut",&CLEAN,true);
        candidate_wal_info_t before = candidate_wal_info(f.wal);
        f.on_load = append_record; f.hook_at = 1;
        candidate_wal_status_t status = try_view(&f,mode);
        candidate_wal_info_t after = candidate_wal_info(f.wal);
        printf("CUT %s append status=%s end=%llu->%llu records=%llu->%llu live=%zu\n",mode_name(mode),vf_status(status),
            (unsigned long long)before.valid_end_lsn,(unsigned long long)after.valid_end_lsn,(unsigned long long)before.records,
            (unsigned long long)after.records,f.memory.live);
        CHECK(status == CANDIDATE_WAL_STALE && f.memory.live == 0 && f.on_load == NULL);
        CHECK(after.valid_end_lsn > before.valid_end_lsn && after.records == before.records + 1);
        vf_free(&f);
    }
    vf_t f; vf_boot(&f,"rescan",&CLEAN,true);
    f.on_load = rescan; f.hook_at = 1;
    candidate_wal_status_t status = try_view(&f,RESOLVED);
    printf("CUT resolved rescan status=%s\n",vf_status(status));
    CHECK(status == CANDIDATE_WAL_OK && f.on_load == NULL && f.memory.live == 0);
    vf_free(&f);
}

static candidate_wal_status_t inner_status;
static bool is_inner_null;
static unsigned inner_calls;

static void reenter(vf_t *f)
{
    candidate_recovery_view_request_t request = vf_request(f,RESOLVED,0);
    unsigned calls = f->memory.calls; candidate_recovery_view_t *inner = (void *)1;
    inner_status = candidate_recovery_view_open(f->wal,&request,&inner);
    is_inner_null = inner == NULL; inner_calls = f->memory.calls - calls;
}

static void reject_handle(vf_t *f) { candidate_wal_reject(f->wal); }

static void replace_segment(vf_t *f)
{
    char path[320], held[320];
    CHECK(snprintf(path,sizeof(path),"%s.wal.0000000000000000",f->built.fx->wal) > 0);
    CHECK(snprintf(held,sizeof(held),"%s.held",f->built.fx->wal) > 0);
    CHECK(rename(path,held) == 0);
    int fd = open(path,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(fd >= 0); CHECK(close(fd) == 0);
    printf("HELD %s replaced=%s\n",held,path);
}

static void expect_refusal(const char *tag, bool is_pinned, vf_hook_t hook, candidate_wal_status_t first, candidate_wal_status_t next)
{
    vf_t f; vf_boot(&f,tag,&CLEAN,true);
    if (is_pinned) OK(candidate_wal_pin(f.wal));
    f.on_load = hook; f.hook_at = 1;
    candidate_wal_status_t a = try_view(&f,RESOLVED);
    vf_reset_memory(&f); unsigned loads = f.loads;
    candidate_wal_status_t b = try_view(&f,PREVIEW);
    printf("ACCESS %s pinned=%d first=%s next=%s next_calls=%u next_loads=%u live=%zu\n",tag,is_pinned,vf_status(a),vf_status(b),
        f.memory.calls,f.loads - loads,f.memory.live);
    CHECK(a == first && b == next);
    CHECK(f.memory.calls == 0 && f.loads == loads && f.memory.live == 0);
    vf_free(&f);
}

static void handle_access_refusals_keep_out_null(void)
{
    vf_t f; vf_boot(&f,"uuid",&CLEAN,true);
    candidate_recovery_view_request_t request = vf_request(&f,RESOLVED,0); request.load.uuid[0] ^= 1;
    vf_reset_memory(&f); uint64_t bytes = vf_io.read_bytes; candidate_recovery_view_t *v = (void *)1;
    candidate_wal_status_t status = candidate_recovery_view_open(f.wal,&request,&v);
    printf("ACCESS uuid status=%s calls=%u loads=%u wal_bytes=%llu\n",vf_status(status),f.memory.calls,f.loads,
        (unsigned long long)(vf_io.read_bytes - bytes));
    CHECK(status == CANDIDATE_WAL_UNSUPPORTED && v == NULL);
    CHECK(f.memory.calls == 0 && f.loads == 0 && vf_io.read_bytes == bytes);
    vf_io.on_read = reenter; status = try_view(&f,RESOLVED);
    printf("ACCESS reentry outer=%s inner=%s inner_null=%d inner_calls=%u\n",vf_status(status),vf_status(inner_status),
        is_inner_null,inner_calls);
    CHECK(status == CANDIDATE_WAL_OK && vf_io.on_read == NULL);
    CHECK(inner_status == CANDIDATE_WAL_INVALID && is_inner_null && inner_calls == 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        alarm(10); v = (void *)1; candidate_wal_status_t s = open_view(&f,RESOLVED,&v);
        _exit(s == CANDIDATE_WAL_FORKED && v == NULL ? 0 : 1);
    }
    int exit_status; CHECK(waitpid(child,&exit_status,0) == child);
    printf("ACCESS fork child_exit=%d\n",WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1);
    CHECK(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0);
    status = try_view(&f,RESOLVED); CHECK(status == CANDIDATE_WAL_OK);
    vf_free(&f);
    expect_refusal("rejected-unpinned",false,reject_handle,CANDIDATE_WAL_POISONED,CANDIDATE_WAL_POISONED);
    expect_refusal("rejected-pinned",true,reject_handle,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT);
    expect_refusal("path-binding",false,replace_segment,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED);
}

static void expect_closed_handle(vf_t *f, const char *tag, candidate_wal_status_t expected)
{
    vf_reset_memory(f); unsigned loads = f->loads; uint64_t bytes = vf_io.read_bytes;
    candidate_wal_status_t a = try_view(f,RESOLVED), b = try_view(f,PREVIEW);
    printf("FAULT %s view_resolved=%s view_preview=%s calls=%u loads=%u wal_bytes=%llu live=%zu\n",tag,vf_status(a),vf_status(b),
        f->memory.calls,f->loads - loads,(unsigned long long)(vf_io.read_bytes - bytes),f->memory.live);
    CHECK(a == expected && b == expected && f->memory.calls == 0 && f->loads == loads);
    CHECK(vf_io.read_bytes == bytes && f->memory.live == 0);
}

static void fsync_failure_poisons_view(void)
{
    vf_t f; vf_boot(&f,"fsync-eio",&CLEAN,true);
    OK(try_view(&f,RESOLVED)); OK(try_view(&f,PREVIEW));
    candidate_wal_info_t durable = candidate_wal_info(f.wal);
    vf_io.fsyncs = 0; vf_io.fsync_failures = 0; vf_io.should_fail_fsync = true;
    candidate_wal_status_t noop = candidate_wal_force(f.wal,durable.valid_end_lsn);
    unsigned noop_fsyncs = vf_io.fsyncs;
    append_record(&f);
    candidate_wal_info_t pending = candidate_wal_info(f.wal);
    candidate_wal_status_t forced = candidate_wal_force(f.wal,pending.valid_end_lsn);
    vf_io.should_fail_fsync = false;
    candidate_wal_info_t after = candidate_wal_info(f.wal); unsigned fsyncs = vf_io.fsyncs;
    candidate_wal_status_t again = candidate_wal_force(f.wal,pending.valid_end_lsn);
    printf("FAULT fsync-eio noop_force=%s noop_fsyncs=%u durable=%llu pending_end=%llu force=%s fsync_calls=%u fsync_failures=%u "
        "durable_after=%llu again=%s again_fsyncs=%u\n",vf_status(noop),noop_fsyncs,(unsigned long long)durable.durable_end_lsn,
        (unsigned long long)pending.valid_end_lsn,vf_status(forced),fsyncs,vf_io.fsync_failures,
        (unsigned long long)after.durable_end_lsn,vf_status(again),vf_io.fsyncs - fsyncs);
    CHECK(noop == CANDIDATE_WAL_OK && noop_fsyncs == 0 && durable.durable_end_lsn == durable.valid_end_lsn);
    CHECK(pending.valid_end_lsn > durable.valid_end_lsn && pending.durable_end_lsn == durable.durable_end_lsn);
    CHECK(forced == CANDIDATE_WAL_IO && fsyncs == 1 && vf_io.fsync_failures == 1 && after.durable_end_lsn == pending.durable_end_lsn);
    CHECK(again == CANDIDATE_WAL_POISONED && vf_io.fsyncs == fsyncs);
    expect_closed_handle(&f,"fsync-eio",CANDIDATE_WAL_POISONED);
    vf_free(&f);
}

typedef struct { uint64_t lsn, bytes; unsigned type; } payload_at_t;

static bool find_payload(void *context, const candidate_wal_record_t *r)
{
    payload_at_t *at = context;
    if (at->lsn == 0 && r->payload_bytes > 0) *at = (payload_at_t){r->lsn,r->payload_bytes,r->type};
    return true;
}

static void malformed_read_rejects_view(bool is_pinned)
{
    const char *tag = is_pinned ? "malformed-pinned" : "malformed-unpinned";
    candidate_wal_status_t next = is_pinned ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_POISONED;
    vf_t f; vf_boot(&f,tag,&CLEAN,true);
    if (is_pinned) OK(candidate_wal_pin(f.wal));
    OK(try_view(&f,RESOLVED)); OK(try_view(&f,PREVIEW));
    payload_at_t at = {0}; candidate_wal_info_t info; OK(candidate_wal_scan(f.wal,find_payload,&at,&info));
    candidate_wal_record_t r; OK(candidate_wal_read_at(f.wal,at.lsn,&r));
    CHECK(at.lsn != 0 && r.lsn == at.lsn && r.type == at.type && r.payload_bytes == at.bytes);
    uint64_t offset = at.lsn + CANDIDATE_WAL_RECORD_HEADER_BYTES; vf_flip_wal_byte(&f,offset);
    candidate_wal_status_t first = candidate_wal_read_at(f.wal,at.lsn,&r), repeat = candidate_wal_read_at(f.wal,at.lsn,&r);
    candidate_wal_status_t scanned = candidate_wal_scan(f.wal,NULL,NULL,&info);
    printf("FAULT %s probe=read_at lsn=%llu type=%u payload=%llu flipped=%llu first=%s repeat=%s scan=%s\n",tag,
        (unsigned long long)at.lsn,at.type,(unsigned long long)at.bytes,(unsigned long long)offset,vf_status(first),
        vf_status(repeat),vf_status(scanned));
    CHECK(first == CANDIDATE_WAL_CORRUPT && repeat == next && scanned == next);
    expect_closed_handle(&f,tag,next);
    vf_free(&f);
}

static void real_faults_refuse_view(void)
{
    fsync_failure_poisons_view();
    malformed_read_rejects_view(true);
    malformed_read_rejects_view(false);
}

static bool is_same_wal_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    if (a->records != b->records || a->valid_end_lsn != b->valid_end_lsn || a->segments != b->segments) return false;
    return a->highest_owner_epoch == b->highest_owner_epoch && a->has_tail == b->has_tail && a->tail_bytes == b->tail_bytes;
}

static void force_turns_stale_resolved_ok(void)
{
    vf_t f; vf_boot(&f,"durable",&CLEAN,true);
    OK(try_view(&f,RESOLVED)); OK(try_view(&f,PREVIEW));
    candidate_wal_info_t base = candidate_wal_info(f.wal);
    vf_abort_resultless(&f,f.built.carrier_txn + 1);
    candidate_wal_info_t pending = candidate_wal_info(f.wal);
    CHECK(base.durable_end_lsn == base.valid_end_lsn && pending.records == base.records + 2);
    CHECK(pending.durable_end_lsn == base.durable_end_lsn && pending.durable_end_lsn < pending.valid_end_lsn);
    for (unsigned phase = 0; phase < 2; phase++) {
        candidate_wal_status_t forced = CANDIDATE_WAL_OK; vf_io.fsyncs = 0;
        if (phase == 1) forced = candidate_wal_force(f.wal,pending.valid_end_lsn);
        candidate_wal_info_t before = candidate_wal_info(f.wal);
        candidate_wal_status_t lr = vf_legacy(&f,RESOLVED,0,NULL,NULL), lp = vf_legacy(&f,PREVIEW,8,NULL,NULL);
        vf_reset_memory(&f);
        candidate_recovery_view_t *v = NULL; candidate_wal_status_t vr = open_view(&f,RESOLVED,&v), vp = try_view(&f,PREVIEW);
        candidate_recovery_view_info_t i = candidate_recovery_view_info(v); candidate_wal_info_t after = candidate_wal_info(f.wal);
        printf("DURABLE %s force=%s fsyncs=%u end=%llu durable=%llu records=%llu segments=%u owner=%llu legacy_resolved=%s "
            "view_resolved=%s legacy_preview=%s view_preview=%s view_end=%llu view_records=%llu\n",phase == 0 ? "unforced" : "forced",
            vf_status(forced),vf_io.fsyncs,(unsigned long long)before.valid_end_lsn,(unsigned long long)before.durable_end_lsn,
            (unsigned long long)before.records,before.segments,(unsigned long long)before.highest_owner_epoch,vf_status(lr),
            vf_status(vr),vf_status(lp),vf_status(vp),(unsigned long long)i.cut.valid_end_lsn,(unsigned long long)i.cut.records);
        CHECK(is_same_wal_cut(&before,&pending) && is_same_wal_cut(&after,&pending) && after.durable_end_lsn == before.durable_end_lsn);
        CHECK(vr == lr && vp == lp && (vr == CANDIDATE_WAL_OK) == (v != NULL));
        if (phase == 0) CHECK(vr == CANDIDATE_WAL_STALE && before.durable_end_lsn == pending.durable_end_lsn);
        else {
            CHECK(forced == CANDIDATE_WAL_OK && vf_io.fsyncs > 0 && vr == CANDIDATE_WAL_OK && before.durable_end_lsn == pending.valid_end_lsn);
            CHECK(i.cut.valid_end_lsn == pending.valid_end_lsn && i.cut.records == pending.records && i.cut.segments == pending.segments);
            CHECK(i.cut.highest_owner_epoch == pending.highest_owner_epoch && i.outcomes.committed == 2 && i.outcomes.aborted == 1);
            for (unsigned n = 0; n < f.built.spec.requests; n++) {
                candidate_catalog_query_t q = scen_query(&f.built,n); const candidate_outcome_t *e = NULL;
                candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e); vf_expect_answer(&f,n,lookup,e);
            }
        }
        candidate_recovery_view_close(&v); CHECK(f.memory.live == 0);
    }
    vf_free(&f);
}

static void allocation_failures_leave_held_view(void)
{
    vf_t f; vf_boot(&f,"oom",&CLEAN,true);
    candidate_recovery_view_t *held = NULL; OK(open_view(&f,RESOLVED,&held));
    candidate_recovery_view_info_t kept = candidate_recovery_view_info(held);
    size_t base = f.memory.live; CHECK(base == kept.resident_bytes);
    for (unsigned m = 0; m < 2; m++) {
        candidate_recovery_view_mode_t mode = m == 0 ? RESOLVED : PREVIEW;
        vf_reset_memory(&f); candidate_recovery_view_t *v = NULL; OK(open_view(&f,mode,&v));
        unsigned calls = f.memory.calls; size_t peak = f.memory.peak - base, resident = candidate_recovery_view_info(v).resident_bytes;
        CHECK(f.memory.live == base + resident); candidate_recovery_view_close(&v); CHECK(f.memory.live == base);
        for (unsigned k = 1; k <= calls; k++) {
            vf_reset_memory(&f); f.memory.fail_at = k; v = (void *)1;
            candidate_wal_status_t status = open_view(&f,mode,&v);
            if (status != CANDIDATE_WAL_NOMEM) printf("OOM %s k=%u status=%s\n",mode_name(mode),k,vf_status(status));
            CHECK(status == CANDIDATE_WAL_NOMEM && v == NULL && f.memory.live == base);
        }
        printf("OOM %s calls=%u resident=%zu peak_requested=%zu held=%zu\n",mode_name(mode),calls,resident,peak,base);
    }
    candidate_recovery_view_info_t now = candidate_recovery_view_info(held);
    CHECK(now.resident_bytes == kept.resident_bytes && now.live_pages == kept.live_pages && now.versions == kept.versions);
    candidate_catalog_query_t q = scen_query(&f.built,0); const candidate_outcome_t *e = NULL;
    candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(held,&q,&e);
    vf_expect_answer(&f,0,lookup,e);
    candidate_recovery_view_close(&held); CHECK(f.memory.live == 0);
    vf_free(&f);
}

static bool is_same_info(const candidate_recovery_info_t *a, const candidate_recovery_info_t *b)
{
    if (a->anchor.checkpoint_id != b->anchor.checkpoint_id || a->anchor.owner_epoch != b->anchor.owner_epoch) return false;
    if (a->live_pages != b->live_pages || a->free_pages != b->free_pages || a->versions != b->versions) return false;
    if (a->wal_end_lsn != b->wal_end_lsn || a->redo.pages_written != b->redo.pages_written) return false;
    return a->resident_bytes == b->resident_bytes;
}

static bool is_same_receipt(candidate_recovery_t *r, const candidate_recovery_publication_t *p, const candidate_recovery_extent_t *e)
{
    candidate_recovery_publication_t now = candidate_recovery_publication(r); candidate_recovery_extent_t extent = candidate_recovery_publication_extent(r);
    if (now.checkpoint_id != p->checkpoint_id || now.outcomes != p->outcomes || now.nodes != p->nodes) return false;
    if (now.has_publication != p->has_publication || now.page_highwater_after != p->page_highwater_after) return false;
    if (now.wal_bytes_appended != p->wal_bytes_appended || now.protected_pages != p->protected_pages) return false;
    return memcmp(&extent,e,sizeof(extent)) == 0;
}

static void failed_verify_keeps_recovery_owner(void)
{
    vf_t f; vf_boot(&f,"owner",&CLEAN,false); vf_close(&f); vf_reset_memory(&f);
    candidate_recovery_options_t o = vf_options(&f,8); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_recover(&o,&r));
    candidate_recovery_info_t before = candidate_recovery_info(r);
    candidate_recovery_publication_t p = candidate_recovery_publication(r); candidate_recovery_extent_t e = candidate_recovery_publication_extent(r);
    CHECK(before.resident_bytes == f.memory.live && p.has_publication);
    unsigned failures = 0; candidate_wal_status_t status = CANDIDATE_WAL_NOMEM;
    for (unsigned k = 1; status != CANDIDATE_WAL_OK; k++) {
        CHECK(k < 4096);
        vf_reset_memory(&f); f.memory.fail_at = k;
        status = candidate_recovery_verify(r,&before.anchor);
        f.memory.fail_at = 0;
        candidate_recovery_info_t now = candidate_recovery_info(r);
        CHECK(is_same_receipt(r,&p,&e));
        if (status == CANDIDATE_WAL_OK) break;
        if (status != CANDIDATE_WAL_NOMEM) printf("OWNER k=%u status=%s\n",k,vf_status(status));
        CHECK(status == CANDIDATE_WAL_NOMEM && is_same_info(&now,&before) && f.memory.live == before.resident_bytes);
        expect_recovery_answers(&f,r); CHECK(f.memory.live == before.resident_bytes);
        failures++;
    }
    candidate_recovery_info_t after = candidate_recovery_info(r);
    printf("OWNER failures=%u resident=%zu->%zu live=%zu redo_written=%llu checkpoint=%llu publication=%d extent_alloc=%llu\n",failures,
        before.resident_bytes,after.resident_bytes,f.memory.live,(unsigned long long)after.redo.pages_written,
        (unsigned long long)after.anchor.checkpoint_id,p.has_publication,(unsigned long long)e.allocated_pages);
    CHECK(failures > 0 && after.anchor.checkpoint_id == before.anchor.checkpoint_id && after.live_pages == before.live_pages);
    CHECK(after.versions == before.versions && after.wal_end_lsn == before.wal_end_lsn && after.resident_bytes == f.memory.live);
    expect_recovery_answers(&f,r);
    OK(candidate_recovery_close(&r)); CHECK(f.memory.live == 0);
    vf_free(&f);
}

static candidate_recovery_view_request_t *loader_request;
static unsigned zero_calls, zero_load;

static void zero_request(vf_t *f) { zero_calls++; zero_load = f->loads; memset(loader_request,0,sizeof(*loader_request)); }

static void callback_input_mutation_keeps_snapshot(void)
{
    vf_t f; vf_boot(&f,"inputs",&CLEAN,true);
    for (unsigned m = 0; m < 2; m++) {
        candidate_recovery_view_mode_t mode = m == 0 ? RESOLVED : PREVIEW;
        candidate_recovery_view_t *v = NULL; vf_reset_memory(&f); OK(open_view(&f,mode,&v));
        candidate_recovery_view_info_t base = candidate_recovery_view_info(v); candidate_recovery_view_close(&v);
        candidate_recovery_view_request_t request = vf_request(&f,mode,owner_of(mode));
        vf_reset_memory(&f); f.memory.mutate_request = &request; f.memory.mutate_memory = &f.allocator;
        OK(candidate_recovery_view_open(f.wal,&request,&v));
        candidate_recovery_view_info_t allocated = candidate_recovery_view_info(v);
        CHECK(request.mode == 0 && f.allocator.allocate == NULL);
        candidate_recovery_view_close(&v); CHECK(f.memory.live == 0);
        vf_reset_memory(&f); request = vf_request(&f,mode,owner_of(mode));
        unsigned armed = f.loads + 1; zero_calls = 0; zero_load = 0;
        loader_request = &request; f.on_load = zero_request; f.hook_at = armed;
        OK(candidate_recovery_view_open(f.wal,&request,&v));
        candidate_recovery_view_info_t loaded = candidate_recovery_view_info(v);
        CHECK(zero_calls == 1 && zero_load == armed && request.mode == 0 && f.on_load == NULL);
        candidate_recovery_view_close(&v); CHECK(f.memory.live == 0);
        printf("INPUTS %s base_versions=%llu allocated_versions=%llu loaded_versions=%llu committed=%u/%u/%u resident=%zu/%zu/%zu "
            "loader_hook_calls=%u loader_hook_load=%u\n",mode_name(mode),(unsigned long long)base.versions,
            (unsigned long long)allocated.versions,(unsigned long long)loaded.versions,base.outcomes.committed,
            allocated.outcomes.committed,loaded.outcomes.committed,base.resident_bytes,allocated.resident_bytes,
            loaded.resident_bytes,zero_calls,zero_load);
        CHECK(allocated.versions == base.versions && loaded.versions == base.versions && allocated.mode == mode && loaded.mode == mode);
        CHECK(allocated.outcomes.committed == base.outcomes.committed && loaded.outcomes.committed == base.outcomes.committed);
        CHECK(allocated.resident_bytes == base.resident_bytes && loaded.resident_bytes == base.resident_bytes);
        CHECK(allocated.anchor.checkpoint_id == f.anchor.checkpoint_id && loaded.anchor.checkpoint_id == f.anchor.checkpoint_id);
    }
    vf_free(&f);
}

static void closed_loader_metadata_needs_no_loads(void)
{
    vf_t f; vf_boot(&f,"metadata",&CLEAN,true);
    for (unsigned m = 0; m < 2; m++) {
        candidate_recovery_view_mode_t mode = m == 0 ? RESOLVED : PREVIEW;
        vf_reset_memory(&f); f.loads = 0;
        candidate_recovery_view_t *v = NULL; OK(open_view(&f,mode,&v));
        unsigned loads = f.loads; f.is_loader_closed = true;
        candidate_recovery_view_info_t i = candidate_recovery_view_info(v);
        candidate_recovery_info_t target = {.anchor = {.checkpoint_id = 4242},.redo = {.pages_written = 77},.resident_bytes = 99};
        candidate_recovery_view_fill(v,&target);
        candidate_catalog_query_t q = scen_query(&f.built,0); const candidate_outcome_t *e = (void *)1;
        candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e);
        candidate_recovery_view_close(&v);
        printf("METADATA %s loads_open=%u loads_after=%u lookup=%u live=%zu resident=%zu\n",mode_name(mode),loads,f.loads - loads,
            lookup,f.memory.live,i.resident_bytes);
        CHECK(f.loads == loads && f.memory.live == 0 && i.mode == mode && i.resident_bytes > 0);
        CHECK(target.anchor.checkpoint_id == 4242 && target.redo.pages_written == 77 && target.resident_bytes == 99);
        CHECK(target.live_pages == i.live_pages && target.versions == i.versions && target.wal_end_lsn == i.cut.valid_end_lsn);
        if (mode == RESOLVED) CHECK(lookup == CANDIDATE_CATALOG_MATCH && e != NULL);
        else CHECK(lookup == CANDIDATE_CATALOG_UNAVAILABLE && e == NULL);
        f.is_loader_closed = false;
    }
    vf_free(&f);
}

static void view_observes_one_semantic_scan(void)
{
    vf_t f; vf_boot(&f,"observe",&CLEAN,true);
    vf_io.read_bytes = 0; candidate_checkpoint_t *checkpoint = NULL;
    OK(candidate_checkpoint_load(f.wal,&f.anchor,&f.allocator,&checkpoint)); candidate_checkpoint_close(&checkpoint);
    uint64_t checkpoint_bytes = vf_io.read_bytes;
    vf_io.read_bytes = 0; candidate_witness_options_t wo = {.memory = &f.allocator,.enable_system_groups = true}; memcpy(wo.uuid,fx_uuid,16);
    candidate_wal_witness_t *witness = NULL; OK(candidate_wal_witness_analyze_checkpoint(f.wal,&wo,&f.anchor,&witness));
    uint64_t analyze_bytes = vf_io.read_bytes; size_t witness_bytes = candidate_wal_witness_info(witness).resident_bytes;
    candidate_wal_witness_close(&witness);
    uint64_t scan_bytes = analyze_bytes - checkpoint_bytes;
    CHECK(checkpoint_bytes > 0 && scan_bytes > 0 && witness_bytes == WITNESS_RESIDENT_BYTES);
    for (unsigned m = 0; m < 2; m++) {
        candidate_recovery_view_mode_t mode = m == 0 ? RESOLVED : PREVIEW;
        vf_io.read_bytes = 0; f.loads = 0;
        OK(try_view(&f,mode)); uint64_t view_bytes = vf_io.read_bytes; unsigned view_loads = f.loads;
        vf_io.read_bytes = 0; f.loads = 0;
        OK(vf_legacy(&f,mode,owner_of(mode),NULL,NULL)); uint64_t legacy_bytes = vf_io.read_bytes; unsigned legacy_loads = f.loads;
        printf("OBSERVE %s checkpoint_bytes=%llu semantic_scan_bytes=%llu view_wal_bytes=%llu view_semantic_scans=%llu "
            "legacy_wal_bytes=%llu legacy_semantic_scans=%llu view_page_loads=%u legacy_page_loads=%u witness_bytes=%zu; "
            "redo/pin/recovery file reads are outside this measure\n",mode_name(mode),(unsigned long long)checkpoint_bytes,
            (unsigned long long)scan_bytes,(unsigned long long)view_bytes,(unsigned long long)((view_bytes - checkpoint_bytes) / scan_bytes),
            (unsigned long long)legacy_bytes,(unsigned long long)((legacy_bytes - 2 * checkpoint_bytes) / scan_bytes),view_loads,
            legacy_loads,witness_bytes);
        CHECK(view_bytes == analyze_bytes && legacy_bytes == 2 * analyze_bytes && view_loads == legacy_loads);
    }
    CHECK(f.memory.live == 0);
    vf_free(&f);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;

static const test_case_t CASES[] = {{"resolved",resolved_view_matches_legacy_outcomes},
    {"recovery",recovery_query_reads_view_bodies},{"preview",stale_admitted_preview_accepts_resolved_refuses},
    {"graph",graph_mutations_refuse_like_legacy},{"nonselected",nonselected_live_version_reaches_graph},
    {"cut",callback_cut_change_refuses_view},{"access",handle_access_refusals_keep_out_null},
    {"faults",real_faults_refuse_view},{"durable",force_turns_stale_resolved_ok},
    {"oom",allocation_failures_leave_held_view},{"owner",failed_verify_keeps_recovery_owner},
    {"inputs",callback_input_mutation_keeps_snapshot},{"metadata",closed_loader_metadata_needs_no_loads},
    {"observe",view_observes_one_semantic_scan}};

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(600);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        if (!is_all && strcmp(selected,CASES[i].name) != 0) continue;
        printf("CASE %s\n",CASES[i].name); CASES[i].run(); has_match = true;
    }
    CHECK(has_match);
    printf("PASS recovery view case=%s checks=%u; acceptance of the internal owned view, not pruning or product activation\n",
        selected,checks);
    return 0;
}
