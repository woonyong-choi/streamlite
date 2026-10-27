#include "test_candidate_recovery_physical_fixture.h"

#include <string.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define PREVIEW CANDIDATE_RECOVERY_VIEW_PREVIEW
#define PAGE_BYTES CANDIDATE_PAGE_BYTES

unsigned checks;

static uint32_t load_seeded(pf_t *p, const candidate_recovery_view_t *view, const pf_snapshot_t *expected, cf_io_t *io)
{
    candidate_physical_seed_t seed = pf_seed_of(view);
    candidate_redo_seed_t request = pf_seed(p,0); candidate_redo_preview_t *preview = NULL;
    cf_arm(seed.before.valid_end_lsn);
    candidate_wal_status_t status = candidate_redo_seeded_open(p->c.f.wal,view,&request,&preview);
    uint32_t same = status == CANDIDATE_WAL_OK ? pf_expect_loads(preview,view,expected) : 0;
    *io = cf_io; cf_disarm();
    candidate_redo_info_t info = candidate_redo_preview_info(preview);
    printf("SEEDED status=%s r=%llu cut=%llu current=%llu page_records=%llu groups=%llu live=%u same=%u reads=%llu read_bytes=%llu "
        "lowest=%llu denied=%llu workspace=%zu\n",vf_status(status),(unsigned long long)seed.before.valid_end_lsn,
        (unsigned long long)seed.after.valid_end_lsn,(unsigned long long)candidate_wal_info(p->c.f.wal).valid_end_lsn,
        (unsigned long long)seed.page_records,(unsigned long long)info.groups,info.distinct_pages,same,
        (unsigned long long)io->reads,(unsigned long long)io->read_bytes,(unsigned long long)io->lowest_lsn,
        (unsigned long long)io->denied,info.workspace_bytes);
    candidate_redo_preview_close(&preview);
    OK(status);
    return same;
}

static candidate_physical_page_t page_of(const candidate_recovery_view_t *view, uint64_t id)
{
    candidate_physical_page_t page = {0};
    for (uint32_t i = 0; candidate_recovery_physical_page_at(view,i,&page); i++) if (page.ref.id == id) return page;
    CHECK(false);
    return page;
}

static candidate_redo_options_t legacy_options(pf_t *p)
{
    candidate_redo_options_t o = {.checkpoint = &p->c.f.anchor,.target = pf_target(p)}; memcpy(o.uuid,fx_uuid,16);
    return o;
}

static void seeded_preview_reads_only_retained_range(void)
{
    pf_t p; pf_build(&p,"physical-range",PREVIEW);
    candidate_physical_seed_t s = pf_seed_of(p.source);
    printf("RANGE source r=%llu records_before=%llu page_records=%llu b=%llu\n",(unsigned long long)s.before.valid_end_lsn,
        (unsigned long long)s.before.records,(unsigned long long)s.page_records,(unsigned long long)s.after.valid_end_lsn);
    CHECK(s.before.valid_end_lsn == p.refresh_lsn && s.before.records > 0 && s.before.valid_end_lsn < p.b.valid_end_lsn);
    CHECK(s.after.valid_end_lsn == p.b.valid_end_lsn && s.page_records > 0);
    pf_extend(&p);

    cf_io_t io; uint32_t same = load_seeded(&p,p.source,&p.at_b,&io);
    CHECK(same == p.at_b.count && io.denied == 0 && io.reads > 0 && io.lowest_lsn == s.before.valid_end_lsn);
    CHECK(candidate_wal_info(p.c.f.wal).valid_end_lsn > p.b.valid_end_lsn);
    candidate_physical_seed_t n = pf_seed_of(p.next);
    CHECK(n.before.valid_end_lsn == s.before.valid_end_lsn && n.before.valid_end_lsn < p.b.valid_end_lsn);
    CHECK(n.after.valid_end_lsn == p.t.valid_end_lsn && n.page_records > s.page_records);
    cf_source_t before; cf_source_take(p.next,&before);
    same = load_seeded(&p,p.next,&p.at_t,&io);
    bool is_same_source = cf_source_same(p.next,&before); cf_source_free(&before);
    CHECK(same == p.at_t.count && io.denied == 0 && io.lowest_lsn == n.before.valid_end_lsn && is_same_source);
    CHECK(io.read_bytes >= n.after.valid_end_lsn - n.before.valid_end_lsn);
    candidate_recovery_closure_t closure = candidate_recovery_view_closure(p.next);
    cf_expect_history(&p.c,&closure);

    candidate_redo_options_t o = legacy_options(&p); candidate_redo_preview_t *legacy = (void *)1;
    cf_arm(n.before.valid_end_lsn);
    candidate_wal_status_t status = candidate_redo_preview_open(p.c.f.wal,&o,&legacy);
    io = cf_io; cf_disarm();
    printf("LEGACY_CONTROL status=%s denied=%llu reads=%llu below=%llu\n",vf_status(status),(unsigned long long)io.denied,
        (unsigned long long)io.reads,(unsigned long long)n.before.valid_end_lsn);
    CHECK(status != CANDIDATE_WAL_OK && legacy == NULL && io.denied > 0);
    pf_free(&p);
}

static void expect_expired_versions(pf_t *p, candidate_redo_preview_t *preview)
{
    const built_t *b = &p->c.f.built; uint8_t page[PAGE_BYTES];
    for (unsigned i = 0; i < 2; i++) {
        const fx_txn_t *t = &b->txn[i];
        OK(candidate_redo_preview_load(preview,t->result.page,page));
        candidate_page_version_t v; OK(candidate_page_get_version(fx_uuid,page,PAGE_BYTES,t->result,&v));
        const candidate_witness_transaction_t *w = candidate_wal_witness_find(candidate_recovery_view_witness(p->next),t->id);
        printf("EXPIRED txn=%llu version_txn=%llu frozen=%llu state=%d csn=%llu/%llu epoch=%llu floor=%llu\n",(unsigned long long)t->id,
            (unsigned long long)v.transaction_id,(unsigned long long)v.frozen_csn,w == NULL ? -1 : (int)w->state,
            (unsigned long long)(w == NULL ? 0 : w->csn),(unsigned long long)t->csn,(unsigned long long)t->epoch,
            (unsigned long long)p->c.floor);
        CHECK(v.transaction_id == t->id && v.frozen_csn == 0 && w != NULL && w->state == CANDIDATE_WITNESS_COMMIT);
        CHECK(w->csn == t->csn && w->has_admission && w->request_epoch == t->epoch && t->epoch < p->c.floor);
    }
}

static void canonical_authority_keeps_free_realloc_and_versions(void)
{
    pf_t p; pf_build(&p,"physical-authority",PREVIEW); pf_extend(&p);
    fx_t *fx = p.c.f.built.fx; candidate_physical_page_t page; uint32_t matched = 0, free_pages = 0;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(p.next,i,&page); i++) {
        const candidate_allocation_entry_t *e = fx_alloc_find(fx,page.ref.id);
        bool is_same = e != NULL && e->state == page.state && e->ref.generation == page.ref.generation;
        matched += is_same && e->transition_lsn == page.transition_lsn; free_pages += page.state == CANDIDATE_ALLOCATION_FREE;
    }
    candidate_physical_page_t freed = page_of(p.next,p.free_id), moved = page_of(p.next,p.realloc_id);
    candidate_physical_page_t version = page_of(p.next,p.c.f.built.txn[0].result.page.id);
    printf("AUTHORITY matched=%u/%u free=%u freed=%d/%llu@%llu moved=%d/%llu@%llu image=%llu version_end=%llu b=%llu\n",matched,
        fx->alloc_count,free_pages,(int)freed.state,(unsigned long long)freed.ref.generation,(unsigned long long)freed.transition_lsn,
        (int)moved.state,(unsigned long long)moved.ref.generation,(unsigned long long)moved.transition_lsn,
        (unsigned long long)moved.image_lsn,(unsigned long long)version.end_lsn,(unsigned long long)p.b.valid_end_lsn);
    CHECK(matched == fx->alloc_count && free_pages == 1 && pf_live_pages(p.next) == p.at_t.count);
    CHECK(freed.state == CANDIDATE_ALLOCATION_FREE && freed.ref.generation == 1 && freed.transition_lsn == p.free_end);
    CHECK(freed.transition_lsn < p.b.valid_end_lsn && freed.end_lsn == p.refresh_end);
    CHECK(moved.state == CANDIDATE_ALLOCATION_LIVE && moved.ref.generation == 2 && moved.transition_lsn == p.realloc_end);
    CHECK(moved.image_lsn == p.realloc_begin && moved.end_lsn == p.realloc_end && moved.end_lsn < p.b.valid_end_lsn);
    CHECK(version.end_lsn == p.refresh_end && version.image_lsn == p.refresh_lsn && version.transition_lsn == 0);

    candidate_redo_seed_t request = pf_seed(&p,0); candidate_redo_preview_t *preview = NULL;
    OK(candidate_redo_seeded_open(p.c.f.wal,p.next,&request,&preview));
    uint8_t expected[PAGE_BYTES], loaded[PAGE_BYTES];
    pf_expected_empty(p.realloc_id,2,p.realloc_end,expected);
    OK(candidate_redo_preview_load(preview,moved.ref,loaded));
    CHECK(memcmp(loaded,expected,PAGE_BYTES) == 0 && memcmp(loaded,pf_snapshot_page(&p.at_t,p.realloc_id),PAGE_BYTES) == 0);
    CHECK(candidate_redo_preview_load(preview,freed.ref,loaded) == CANDIDATE_WAL_CORRUPT);
    expect_expired_versions(&p,preview);
    candidate_redo_preview_close(&preview);
    pf_free(&p);
}

static void damage(pf_t *p, uint64_t torn, uint64_t forged, uint64_t absent)
{
    pf_tear(p,torn); pf_forge_body(p,forged);
    p->target = (pf_target_t){.absent_id = absent};
    uint8_t page[PAGE_BYTES]; candidate_page_info_t a, b; const uint8_t *original = pf_snapshot_page(&p->at_t,forged);
    pf_read_data(p,forged,page);
    OK(candidate_page_decode_header(fx_uuid,page,PAGE_BYTES,&a)); OK(candidate_page_decode_header(fx_uuid,original,PAGE_BYTES,&b));
    CHECK(memcmp(&a.location,&b.location,sizeof(a.location)) == 0 && memcmp(page,original,PAGE_BYTES) != 0);
}

static void seeded_execute_restores_actual_bodies(void)
{
    pf_t p; pf_build(&p,"physical-restore",RESOLVED); pf_extend(&p);
    fx_t *fx = p.c.f.built.fx; uint64_t torn = p.c.delta_page, absent = p.c.f.built.txn[0].result.page.id;
    uint64_t forged = p.c.f.anchor.outcome_root.id;
    uint8_t expected[PAGE_BYTES], whole[PAGE_BYTES];
    pf_expected_patch(&p,PF_SECOND_NONCE,fx->pages[torn].lsn,expected);
    CHECK(memcmp(expected,pf_snapshot_page(&p.at_t,torn),PAGE_BYTES) == 0);
    damage(&p,torn,forged,absent);
    candidate_redo_options_t o = legacy_options(&p); candidate_redo_preview_t *legacy = NULL;
    OK(candidate_redo_preview_open(p.c.f.wal,&o,&legacy));
    OK(candidate_redo_preview_load(legacy,(candidate_page_ref_t){torn,1},whole)); CHECK(memcmp(whole,expected,PAGE_BYTES) == 0);
    OK(candidate_redo_preview_load(legacy,(candidate_page_ref_t){absent,1},whole));
    CHECK(memcmp(whole,pf_snapshot_page(&p.at_t,absent),PAGE_BYTES) == 0);
    candidate_redo_preview_close(&legacy);
    p.target = (pf_target_t){.absent_id = absent};

    candidate_redo_seed_t request = pf_seed(&p,0); candidate_redo_info_t info;
    OK(candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info));
    uint32_t live = pf_live_pages(p.next), restored = pf_expect_data(&p,&p.at_t);
    printf("RESTORE written=%llu skipped=%llu live=%u restored=%u/%u syncs=%u workspace=%zu\n",(unsigned long long)info.pages_written,
        (unsigned long long)info.pages_skipped,live,restored,p.at_t.count,p.target.syncs,info.workspace_bytes);
    CHECK(info.pages_written == 3 && info.pages_skipped == live - 3 && info.distinct_pages == live && p.target.syncs == 1);
    CHECK(restored == p.at_t.count && p.target.absent_id == 0);
    pf_read_data(&p,torn,whole); CHECK(memcmp(whole,expected,PAGE_BYTES) == 0);
    p.target = (pf_target_t){0};
    OK(candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info));
    CHECK(info.pages_written == 0 && info.pages_skipped == live && p.target.writes == 0);
    pf_free(&p);
}

typedef struct { const char *name; void (*calibrate)(pf_t *p); void (*malform)(pf_t *p); } malformed_row_t;

static void patch_exact(pf_t *p) { pf_group_t g; pf_patch(p,0x33,&g); }

static void patch_stale_previous(pf_t *p)
{
    p->c.f.built.fx->pages[p->c.delta_page].lsn = p->refresh_end;
    pf_group_t g; pf_patch(p,0x44,&g);
}

static void image_exact(pf_t *p) { pf_image(p,p->c.f.built.fx->pages[p->c.delta_page].lsn); }

static void image_restarts_generation(pf_t *p) { pf_image(p,0); }

static void run_malformed(const malformed_row_t *row)
{
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"physical-malformed-%s",row->name) > 0);
    pf_t p; pf_build(&p,tag,RESOLVED);
    row->calibrate(&p); cf_flush(&p.c);
    candidate_recovery_view_t *next = NULL; candidate_wal_status_t calibrated = cf_continue(&p.c,p.source,&next);
    candidate_recovery_view_close(&next);
    row->malform(&p); cf_flush(&p.c);
    candidate_witness_options_t wo = {.memory = &p.c.f.allocator,.enable_system_groups = true}; memcpy(wo.uuid,fx_uuid,16);
    candidate_wal_witness_t *witness = NULL;
    candidate_wal_status_t analyzed = candidate_wal_witness_analyze_checkpoint(p.c.f.wal,&wo,&p.c.f.anchor,&witness);
    candidate_wal_witness_close(&witness);
    candidate_recovery_view_t *full = (void *)1; next = (void *)1;
    candidate_wal_status_t continued = cf_continue(&p.c,p.source,&next), opened = cf_open(&p.c,&full);
    printf("MALFORMED %s calibrated=%s witness=%s continue=%s open=%s\n",row->name,vf_status(calibrated),vf_status(analyzed),
        vf_status(continued),vf_status(opened));
    OK(calibrated); OK(analyzed);
    CHECK(continued == CANDIDATE_WAL_CORRUPT && next == NULL && opened == CANDIDATE_WAL_CORRUPT && full == NULL);
    pf_free(&p);
}

static void malformed_page_chain_refused(void)
{
    static const malformed_row_t rows[] = {{"previous",patch_exact,patch_stale_previous},
        {"generation",image_exact,image_restarts_generation}};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) run_malformed(&rows[i]);
}

typedef struct {
    pf_t *p;
    const candidate_recovery_view_t *view;
    candidate_wal_suffix_t range;
    candidate_wal_status_t read, visit, open;
    unsigned calls;
} reentry_t;

static bool accept_record(void *context, const candidate_wal_record_t *r) { (void)context; (void)r; return true; }

static void reenter(void *context)
{
    reentry_t *e = context; candidate_wal_t *wal = e->p->c.f.wal; candidate_wal_record_t r;
    e->calls++;
    e->read = candidate_wal_read_at(wal,e->range.before.valid_end_lsn,&r);
    e->visit = candidate_wal_visit_range(wal,&e->range);
    candidate_redo_seed_t request = pf_seed(e->p,0); candidate_redo_preview_t *inner = NULL;
    e->open = candidate_redo_seeded_open(wal,e->view,&request,&inner);
    candidate_redo_preview_close(&inner);
}

static candidate_wal_status_t open_once(pf_t *p, const candidate_recovery_view_t *view, const candidate_redo_seed_t *request)
{
    candidate_redo_preview_t *preview = (void *)1;
    candidate_wal_status_t status = candidate_redo_seeded_open(p->c.f.wal,view,request,&preview);
    CHECK((status == CANDIDATE_WAL_OK) == (preview != NULL));
    candidate_redo_preview_close(&preview);
    return status;
}

static void seeded_source_changes_refuse(void)
{
    pf_t p; pf_build(&p,"physical-source",RESOLVED);
    candidate_physical_seed_t s = pf_seed_of(p.source);
    pf_plain(&p); cf_flush(&p.c);
    candidate_redo_seed_t request = pf_seed(&p,0); candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t cut = candidate_redo_seeded_execute(p.c.f.wal,p.source,&request,&info);
    CHECK(cut == CANDIDATE_WAL_STALE && info.groups == 0 && p.target.reads == 0 && p.target.writes == 0);

    reentry_t e = {.p = &p,.view = p.source,.range = {.before = s.before,.after = s.after,.visitor = accept_record}};
    cf_arm(0); cf_io.hook_from = s.before.valid_end_lsn; cf_io.on_read = reenter; cf_io.context = &e;
    candidate_redo_preview_t *preview = NULL;
    candidate_wal_status_t status = candidate_redo_seeded_open(p.c.f.wal,p.source,&request,&preview);
    uint32_t same = status == CANDIDATE_WAL_OK ? pf_expect_loads(preview,p.source,&p.at_b) : 0;
    cf_disarm(); candidate_redo_preview_close(&preview);
    printf("SOURCE cut=%s reentry=%s calls=%u read=%s visit=%s open=%s same=%u\n",vf_status(cut),vf_status(status),e.calls,
        vf_status(e.read),vf_status(e.visit),vf_status(e.open),same);
    OK(status); CHECK(e.calls == 1 && same == p.at_b.count);
    CHECK(e.read == CANDIDATE_WAL_INVALID && e.visit == CANDIDATE_WAL_INVALID && e.open == CANDIDATE_WAL_INVALID);

    pf_replace_segment(&p);
    candidate_wal_status_t replaced = open_once(&p,p.source,&request), after_replace = candidate_wal_check_uuid(p.c.f.wal,fx_uuid);
    cf_reown(&p.c,p.c.f.owner);
    candidate_wal_status_t reopened = open_once(&p,p.source,&request);
    pf_rewrite_payload(&p,&p.first_patch);
    candidate_wal_status_t rewritten = open_once(&p,p.source,&request);
    candidate_wal_status_t after_rewrite = candidate_wal_check_uuid(p.c.f.wal,fx_uuid);
    printf("SOURCE replaced=%s after=%s reopened=%s rewritten=%s after=%s\n",vf_status(replaced),vf_status(after_replace),
        vf_status(reopened),vf_status(rewritten),vf_status(after_rewrite));
    CHECK(replaced == CANDIDATE_WAL_IO && after_replace == CANDIDATE_WAL_POISONED && reopened == CANDIDATE_WAL_OK);
    CHECK(rewritten == CANDIDATE_WAL_CORRUPT && after_rewrite == CANDIDATE_WAL_POISONED);
    pf_free(&p);
}

static void expect_failed_opens(pf_t *p, candidate_redo_seed_t *request, unsigned calls, size_t base)
{
    vf_t *f = &p->c.f;
    for (unsigned k = 1; k <= calls; k++) {
        candidate_redo_preview_t *preview = (void *)1; candidate_redo_info_t info = {.groups = 9};
        vf_reset_memory(f); f->memory.fail_at = k;
        candidate_wal_status_t opened = candidate_redo_seeded_open(f->wal,p->next,request,&preview);
        vf_reset_memory(f); f->memory.fail_at = k;
        candidate_wal_status_t executed = candidate_redo_seeded_execute(f->wal,p->next,request,&info);
        if (opened != CANDIDATE_WAL_NOMEM || executed != CANDIDATE_WAL_NOMEM)
            printf("BUDGET_NOMEM k=%u open=%s execute=%s\n",k,vf_status(opened),vf_status(executed));
        CHECK(opened == CANDIDATE_WAL_NOMEM && preview == NULL && executed == CANDIDATE_WAL_NOMEM && info.groups == 0);
        CHECK(f->memory.live == base && p->target.writes == 0);
    }
    vf_reset_memory(f);
}

static void seeded_budget_counts_source_and_next(void)
{
    pf_t p; pf_build(&p,"physical-budget",RESOLVED); pf_extend(&p);
    vf_t *f = &p.c.f;
    size_t source = candidate_recovery_view_info(p.source).resident_bytes, next = candidate_recovery_view_info(p.next).resident_bytes;
    size_t wal = candidate_wal_info(f->wal).resident_bytes;
    vf_reset_memory(f); size_t base = f->memory.live;
    candidate_redo_seed_t request = pf_seed(&p,source); candidate_redo_preview_t *preview = NULL;
    OK(candidate_redo_seeded_open(f->wal,p.next,&request,&preview));
    size_t work = candidate_redo_preview_info(preview).workspace_bytes; unsigned calls = f->memory.calls;
    candidate_redo_preview_close(&preview);
    size_t total = next + source + wal + work;
    request.budget_bytes = total; candidate_wal_status_t exact = open_once(&p,p.next,&request);
    vf_reset_memory(f); request.budget_bytes = total - 1;
    candidate_wal_status_t below = open_once(&p,p.next,&request);
    unsigned below_calls = f->memory.calls;
    printf("BUDGET source=%zu next=%zu wal=%zu work=%zu total=%zu limit=%u base=%zu calls=%u exact=%s below=%s below_calls=%u\n",
        source,next,wal,work,total,CANDIDATE_REDO_BUDGET_BYTES,base,calls,vf_status(exact),vf_status(below),below_calls);
    CHECK(base == source + next && calls == 3 && total <= CANDIDATE_REDO_BUDGET_BYTES);
    CHECK(exact == CANDIDATE_WAL_OK && below == CANDIDATE_WAL_LIMIT && below_calls == 0);

    request.budget_bytes = 0;
    cf_source_t kept_source, kept_next; cf_source_take(p.source,&kept_source); cf_source_take(p.next,&kept_next);
    pf_tear(&p,p.c.delta_page); p.target = (pf_target_t){0};
    expect_failed_opens(&p,&request,calls,base);
    candidate_redo_info_t info;
    OK(candidate_redo_seeded_execute(f->wal,p.next,&request,&info));
    bool is_same = cf_source_same(p.source,&kept_source) && cf_source_same(p.next,&kept_next);
    cf_source_free(&kept_source); cf_source_free(&kept_next);
    CHECK(info.pages_written == 1 && pf_expect_data(&p,&p.at_t) == p.at_t.count && is_same && f->memory.live == base);
    pf_free(&p);
}

static void preview_reads_and_resolved_writes(void)
{
    pf_t q; pf_build(&q,"physical-modes-preview",PREVIEW);
    pf_plain(&q);
    candidate_wal_info_t before = candidate_wal_info(q.c.f.wal);
    cf_io_t io; uint32_t same = load_seeded(&q,q.source,&q.at_b,&io);
    candidate_redo_seed_t request = pf_seed(&q,0); candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t refused = candidate_redo_seeded_execute(q.c.f.wal,q.source,&request,&info);
    candidate_wal_info_t after = candidate_wal_info(q.c.f.wal);
    printf("MODES preview same=%u execute=%s durable=%llu->%llu valid=%llu target=%u/%u/%u\n",same,vf_status(refused),
        (unsigned long long)before.durable_end_lsn,(unsigned long long)after.durable_end_lsn,(unsigned long long)after.valid_end_lsn,
        q.target.reads,q.target.writes,q.target.syncs);
    CHECK(same == q.at_b.count && refused == CANDIDATE_WAL_UNSUPPORTED && info.groups == 0);
    CHECK(q.target.reads == 0 && q.target.writes == 0 && q.target.syncs == 0);
    CHECK(before.durable_end_lsn < before.valid_end_lsn && after.durable_end_lsn == before.durable_end_lsn);
    pf_free(&q);

    pf_t p; pf_build(&p,"physical-modes-resolved",RESOLVED);
    pf_tear(&p,p.c.delta_page);
    request = pf_seed(&p,0);
    OK(candidate_redo_seeded_execute(p.c.f.wal,p.source,&request,&info));
    printf("MODES resolved written=%llu durable_at_write=%llu cut=%llu syncs=%u\n",(unsigned long long)info.pages_written,
        (unsigned long long)p.target.durable_at_write,(unsigned long long)p.b.valid_end_lsn,p.target.syncs);
    CHECK(info.pages_written == 1 && p.target.writes == 1 && p.target.syncs == 1);
    CHECK(p.target.durable_at_write >= p.b.valid_end_lsn && pf_expect_data(&p,&p.at_b) == p.at_b.count);
    pf_free(&p);
}

static void write_and_sync_failures_retry_same_seed(void)
{
    pf_t p; pf_build(&p,"physical-failures",RESOLVED); pf_extend(&p);
    uint64_t torn = p.c.delta_page, absent = p.c.f.built.txn[0].result.page.id, forged = p.c.f.anchor.outcome_root.id;
    CHECK(forged < absent && absent < torn);
    damage(&p,torn,forged,absent); p.target.fail_write_at = 2;
    candidate_redo_seed_t request = pf_seed(&p,0); candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t failed_write = candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info);
    uint32_t restored = pf_expect_data(&p,&p.at_t); uint64_t groups = info.groups; pf_target_t first = p.target;
    p.target = (pf_target_t){.absent_id = first.absent_id};
    candidate_wal_status_t retried = candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info);
    uint64_t retried_written = info.pages_written; uint32_t retried_restored = pf_expect_data(&p,&p.at_t);
    printf("FAILURES write=%s writes=%u syncs=%u restored=%u/%u retry=%s written=%llu restored=%u\n",vf_status(failed_write),
        first.writes,first.syncs,restored,p.at_t.count,vf_status(retried),(unsigned long long)retried_written,retried_restored);
    CHECK(failed_write == CANDIDATE_WAL_IO && groups == 0 && first.writes == 2 && first.syncs == 0 && first.absent_id == absent);
    CHECK(restored == p.at_t.count - 1 && retried == CANDIDATE_WAL_OK && retried_written == 2 && retried_restored == p.at_t.count);

    pf_tear(&p,torn); p.target = (pf_target_t){.should_fail_sync = true}; info.groups = 9;
    candidate_wal_status_t failed_sync = candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info);
    groups = info.groups; first = p.target; p.target = (pf_target_t){0};
    candidate_wal_status_t resynced = candidate_redo_seeded_execute(p.c.f.wal,p.next,&request,&info);
    printf("FAILURES sync=%s writes=%u syncs=%u retry=%s written=%llu syncs=%u\n",vf_status(failed_sync),first.writes,first.syncs,
        vf_status(resynced),(unsigned long long)info.pages_written,p.target.syncs);
    CHECK(failed_sync == CANDIDATE_WAL_IO && groups == 0 && first.writes == 1 && first.syncs == 1);
    CHECK(resynced == CANDIDATE_WAL_OK && info.pages_written == 0 && p.target.syncs == 1);
    CHECK(pf_expect_data(&p,&p.at_t) == p.at_t.count);
    pf_free(&p);
}

static bool is_empty_info(const candidate_redo_info_t *info)
{
    return info->groups == 0 && info->pages_written == 0 && info->pages_skipped == 0 && info->wal_end_lsn == 0 &&
        info->distinct_pages == 0 && info->workspace_bytes == 0;
}

typedef struct { pf_t *p; unsigned calls, append_at; uint64_t appended; } cut_memory_t;

static void *cut_allocate(void *context, size_t size)
{
    cut_memory_t *m = context; candidate_wal_t *wal = m->p->c.f.wal;
    if (++m->calls == m->append_at) {
        pf_plain(m->p); OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));
        m->appended = candidate_wal_info(wal).valid_end_lsn;
    }
    return m->p->c.f.allocator.allocate(m->p->c.f.allocator.context,size);
}

static void cut_release(void *context, void *ptr, size_t size)
{
    cut_memory_t *m = context; m->p->c.f.allocator.release(m->p->c.f.allocator.context,ptr,size);
}

static void allocation_append_makes_cut_stale(void)
{
    pf_t p; pf_build(&p,"physical-cut-alloc",RESOLVED);
    pf_tear(&p,p.c.delta_page);
    cut_memory_t m = {.p = &p,.append_at = 1}; candidate_wal_memory_t memory = {&m,cut_allocate,cut_release};
    candidate_redo_seed_t request = pf_seed(&p,0); request.memory = &memory;
    candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t status = candidate_redo_seeded_execute(p.c.f.wal,p.source,&request,&info);
    uint64_t current = candidate_wal_info(p.c.f.wal).valid_end_lsn;
    printf("CUT_ALLOC status=%s b=%llu appended=%llu current=%llu calls=%u groups=%llu written=%llu skipped=%llu "
        "target=%u/%u/%u\n",vf_status(status),(unsigned long long)p.b.valid_end_lsn,(unsigned long long)m.appended,
        (unsigned long long)current,m.calls,(unsigned long long)info.groups,(unsigned long long)info.pages_written,
        (unsigned long long)info.pages_skipped,p.target.reads,p.target.writes,p.target.syncs);
    CHECK(m.calls >= 1 && m.appended > p.b.valid_end_lsn && current == m.appended);
    CHECK(status == CANDIDATE_WAL_STALE && is_empty_info(&info) && p.target.writes == 0 && p.target.syncs == 0);
    pf_free(&p);
}

static void restore_read_append_keeps_newer_page(void)
{
    pf_t p; pf_build(&p,"physical-cut-restore",RESOLVED);
    uint64_t id = p.c.delta_page; uint32_t live = pf_live_pages(p.source), rank = 0, at = 0;
    candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(p.source,i,&page); i++) {
        if (page.state != CANDIDATE_ALLOCATION_LIVE) continue;
        rank++; if (page.ref.id == id) at = rank;
    }
    CHECK(at > 0 && pf_expect_data(&p,&p.at_b) == p.at_b.count);
    p.target = (pf_target_t){.patch_after = live};
    candidate_redo_seed_t request = pf_seed(&p,0); candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t status = candidate_redo_seeded_execute(p.c.f.wal,p.source,&request,&info);
    uint64_t current = candidate_wal_info(p.c.f.wal).valid_end_lsn, newer = p.target.patched.end;
    uint8_t expected[PAGE_BYTES], actual[PAGE_BYTES];
    pf_expected_patch(&p,PF_LATE_NONCE,newer,expected); pf_read_data(&p,id,actual);
    bool is_newer = memcmp(actual,expected,PAGE_BYTES) == 0, is_old = memcmp(actual,pf_snapshot_page(&p.at_b,id),PAGE_BYTES) == 0;
    printf("CUT_RESTORE status=%s b=%llu newer=%llu current=%llu page=%llu rank=%u/%u groups=%llu written=%llu skipped=%llu "
        "target=%u/%u/%u is_newer=%d is_old=%d\n",vf_status(status),(unsigned long long)p.b.valid_end_lsn,
        (unsigned long long)newer,(unsigned long long)current,(unsigned long long)id,at,live,(unsigned long long)info.groups,
        (unsigned long long)info.pages_written,(unsigned long long)info.pages_skipped,p.target.reads,p.target.writes,
        p.target.syncs,is_newer,is_old);
    CHECK(p.target.patch_after == 0 && newer > p.b.valid_end_lsn && current >= newer);
    CHECK(status == CANDIDATE_WAL_STALE && is_empty_info(&info) && p.target.writes == 0 && p.target.syncs == 0);
    CHECK(is_newer && !is_old);
    pf_free(&p);
}

static void early_semantic_corrupt_poisons_source(void)
{
    pf_t p; pf_build(&p,"physical-early",RESOLVED);
    candidate_redo_seed_t request = pf_seed(&p,0);
    candidate_wal_status_t control = open_once(&p,p.source,&request), control_after = candidate_wal_check_uuid(p.c.f.wal,fx_uuid);
    printf("EARLY control=%s after=%s\n",vf_status(control),vf_status(control_after));
    OK(control); OK(control_after);

    pf_rewrite_records(&p,&p.first_patch,1);
    candidate_redo_preview_t *preview = (void *)1;
    candidate_wal_status_t opened = candidate_redo_seeded_open(p.c.f.wal,p.source,&request,&preview);
    bool is_null = preview == NULL; candidate_redo_preview_close(&preview);
    candidate_wal_status_t after = candidate_wal_check_uuid(p.c.f.wal,fx_uuid);
    candidate_redo_info_t info = {.groups = 9};
    candidate_wal_status_t executed = candidate_redo_seeded_execute(p.c.f.wal,p.source,&request,&info);
    pf_target_t first = p.target;
    pf_rewrite_records(&p,&p.first_patch,-1);
    candidate_wal_status_t restored = open_once(&p,p.source,&request), restored_after = candidate_wal_check_uuid(p.c.f.wal,fx_uuid);
    printf("EARLY open=%s null=%d after=%s execute=%s groups=%llu target=%u/%u/%u restored=%s after=%s\n",vf_status(opened),
        is_null,vf_status(after),vf_status(executed),(unsigned long long)info.groups,first.reads,first.writes,first.syncs,
        vf_status(restored),vf_status(restored_after));
    CHECK(opened == CANDIDATE_WAL_CORRUPT && is_null && after == CANDIDATE_WAL_POISONED);
    CHECK(executed != CANDIDATE_WAL_OK && is_empty_info(&info) && first.writes == 0 && first.syncs == 0);
    CHECK(restored == CANDIDATE_WAL_POISONED && restored_after == CANDIDATE_WAL_POISONED);
    pf_free(&p);
}

#define SETTLE_GROUP_BYTES ((size_t)(CANDIDATE_WITNESS_GROUPS + 1u) * sizeof(candidate_physical_cut_t))

typedef struct {
    pf_t *p;
    candidate_wal_memory_t memory;
    void *captured, *released;
    unsigned allocs, releases, triggers;
    uint64_t appended;
    bool is_armed;
} settle_memory_t;

typedef struct {
    candidate_wal_status_t status, wal;
    uint64_t cut, current;
    size_t live_before, live_after;
    bool is_null, is_same, is_source;
} settle_run_t;

static void *settle_allocate(void *context, size_t size)
{
    settle_memory_t *m = context; void *ptr = m->p->c.f.allocator.allocate(m->p->c.f.allocator.context,size);
    if (ptr != NULL && size == SETTLE_GROUP_BYTES) { m->allocs++; m->captured = ptr; }
    return ptr;
}

static void settle_release(void *context, void *ptr, size_t size)
{
    settle_memory_t *m = context; candidate_wal_t *wal = m->p->c.f.wal;
    if (ptr != NULL && ptr == m->captured && size == SETTLE_GROUP_BYTES) {
        m->releases++; m->released = ptr;
        if (m->is_armed) {
            m->is_armed = false; m->triggers++;
            pf_plain(m->p); OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));
            m->appended = candidate_wal_info(wal).valid_end_lsn;
        }
    }
    m->p->c.f.allocator.release(m->p->c.f.allocator.context,ptr,size);
}

static void settle_start(settle_memory_t *m, pf_t *p, bool is_armed)
{
    *m = (settle_memory_t){.p = p,.memory = {m,settle_allocate,settle_release},.is_armed = is_armed};
}

static settle_run_t settle_run(const char *tag, pf_t *p, settle_memory_t *m, bool is_continue)
{
    candidate_wal_t *wal = p->c.f.wal; cf_source_t kept; cf_source_take(p->source,&kept);
    candidate_recovery_view_request_t request = cf_request(&p->c); request.load.memory = &m->memory;
    settle_run_t r = {.live_before = p->c.f.memory.live}; candidate_recovery_view_t *view = (void *)1;
    r.status = is_continue ? candidate_recovery_view_continue(wal,&request,p->source,&view) :
        candidate_recovery_view_open(wal,&request,&view);
    candidate_wal_info_t cut = candidate_recovery_view_info(view).cut;
    r.is_null = view == NULL; r.cut = cut.valid_end_lsn; r.current = candidate_wal_info(wal).valid_end_lsn;
    r.is_same = !r.is_null && cut.valid_end_lsn == p->b.valid_end_lsn && cut.records == p->b.records &&
        cut.highest_owner_epoch == p->b.highest_owner_epoch;
    candidate_recovery_view_close(&view);
    r.live_after = p->c.f.memory.live; r.is_source = cf_source_same(p->source,&kept); cf_source_free(&kept);
    r.wal = candidate_wal_check_uuid(wal,fx_uuid);
    printf("RELEASE_%s %s status=%s b=%llu t=%llu current=%llu viewcut=%llu is_null=%d is_same=%d allocs=%u releases=%u "
        "triggers=%u captured=%d released=%d live=%zu->%zu source=%d wal=%s\n",tag,is_continue ? "continue" : "open",
        vf_status(r.status),(unsigned long long)p->b.valid_end_lsn,(unsigned long long)m->appended,
        (unsigned long long)r.current,(unsigned long long)r.cut,r.is_null,r.is_same,m->allocs,m->releases,m->triggers,
        m->captured != NULL,m->released != NULL && m->released == m->captured,r.live_before,r.live_after,r.is_source,
        vf_status(r.wal));
    return r;
}

static void run_release(const char *tag, bool is_continue)
{
    pf_t p; pf_build(&p,tag,RESOLVED);
    uint64_t b = p.b.valid_end_lsn;
    settle_memory_t control; settle_start(&control,&p,false);
    settle_run_t c = settle_run("CONTROL",&p,&control,is_continue);
    settle_memory_t mutate; settle_start(&mutate,&p,true);
    settle_run_t m = settle_run("MUTATE",&p,&mutate,is_continue);
    bool is_control_bound = control.allocs == 1 && control.releases == 1 && control.captured != NULL &&
        control.released == control.captured && control.triggers == 0;
    bool is_mutate_bound = mutate.allocs == 1 && mutate.releases == 1 && mutate.captured != NULL &&
        mutate.released == mutate.captured && mutate.triggers == 1 && !mutate.is_armed;
    pf_free(&p);
    OK(c.status); CHECK(!c.is_null && c.is_same && c.current == b && is_control_bound);
    CHECK(c.live_after == c.live_before && c.is_source && c.wal == CANDIDATE_WAL_OK);
    CHECK(is_mutate_bound && mutate.appended > b && m.current == mutate.appended);
    CHECK(m.live_after == m.live_before && m.is_source && m.wal == CANDIDATE_WAL_OK);
    CHECK(m.status == CANDIDATE_WAL_STALE && m.is_null);
}

static void view_release_open_refuses_stale(void) { run_release("physical-release-open",false); }

static void view_release_continue_refuses_stale(void) { run_release("physical-release-continue",true); }

typedef struct { const char *name; void (*run)(void); } test_case_t;

static const test_case_t CASES[] = {{"range",seeded_preview_reads_only_retained_range},
    {"authority",canonical_authority_keeps_free_realloc_and_versions},{"restore",seeded_execute_restores_actual_bodies},
    {"malformed",malformed_page_chain_refused},{"source",seeded_source_changes_refuse},
    {"budget",seeded_budget_counts_source_and_next},{"modes",preview_reads_and_resolved_writes},
    {"failures",write_and_sync_failures_retry_same_seed},{"callback-cut-alloc",allocation_append_makes_cut_stale},
    {"callback-cut-restore",restore_read_append_keeps_newer_page},{"early-corrupt",early_semantic_corrupt_poisons_source},
    {"view-release-open",view_release_open_refuses_stale},{"view-release-continue",view_release_continue_refuses_stale}};

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(900);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        if (!is_all && strcmp(selected,CASES[i].name) != 0) continue;
        printf("CASE %s\n",CASES[i].name); CASES[i].run(); has_match = true;
    }
    CHECK(has_match);
    printf("PASS recovery physical case=%s checks=%u; local seeded preview/redo candidate, not buffer, durable seed, reader or reuse\n",
        selected,checks);
    return 0;
}
