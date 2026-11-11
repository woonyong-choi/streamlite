#include "test_candidate_buffer_cases.h"

#include <string.h>

#define FRAME_BEGIN 112u
#define FRAME_IMAGE 8320u
#define FRAME_END 120u

static void case_shared_leaf(void)
{
    bf_t b; bf_boot(&b,"shared-leaf",4);
    uint64_t p = b.pages[0], q = b.pages[1];
    fx_txn_t a, c, d; candidate_buffer_admission_t r;
    bf_begin(&b,&a); bf_begin(&b,&c); bf_begin(&b,&d);
    OK(bf_write_version(&b,&a,p,1,&r));
    OK(bf_write_version(&b,&c,p,2,&r));
    OK(bf_write_version(&b,&d,q,3,&r));
    bf_commit(&b,&c); bf_commit(&b,&d);
    OK(candidate_wal_force(b.wal,c.terminal_end));
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,4,&f));
    CHECK(f.written == 1 && f.unresolved == 1);
    bf_expect_body(&b,q,d.id,3,48); bf_expect_absent(&b,p,c.id); bf_expect_absent(&b,p,a.id);
    candidate_buffer_checkpoint_t cp;
    OK(candidate_buffer_checkpoint(b.pool,4,&cp));
    CHECK(cp.dirty_entries == 1 && cp.active == 1 && cp.remaining_dirty == 1 && cp.writeback.unresolved == 1);
    bf_abort(&b,&a);
    OK(candidate_buffer_flush(b.pool,4,&f));
    CHECK(f.written == 1 && f.unresolved == 0);
    bf_expect_body(&b,p,a.id,1,48); bf_expect_body(&b,p,c.id,2,48);
    bf_restart(&b,9);
    bf_expect_body(&b,p,a.id,1,48); bf_expect_body(&b,p,c.id,2,48); bf_expect_body(&b,q,d.id,3,48);
    bf_report(&b,"shared-leaf",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_membership(void)
{
    bf_t b; bf_boot(&b,"membership",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    uint64_t first = bf_external(&b,&t,&b.pages[0],1,1,true);
    uint64_t second = bf_external(&b,&t,&b.pages[1],1,2,true);
    bf_state_t before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,second,&a),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_buffer_admit(b.pool,first + 8,&a),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_buffer_admit(b.pool,first - FRAME_END,&a),CANDIDATE_WAL_INVALID);
    candidate_buffer_checkpoint_t c;
    EXPECT(candidate_buffer_checkpoint(b.pool,4,&c),CANDIDATE_WAL_BUSY);
    bf_state_t after = bf_take(&b); CHECK(bf_same(&before,&after));
    OK(candidate_buffer_admit(b.pool,first,&a)); CHECK(a.end_lsn == first && a.images == 1);
    EXPECT(candidate_buffer_admit(b.pool,first,&a),CANDIDATE_WAL_INVALID);
    OK(candidate_buffer_admit(b.pool,second,&a)); CHECK(a.end_lsn == second);
    uint64_t pending = bf_external(&b,&t,&b.pages[0],1,3,false);
    before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,pending,&a),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_buffer_checkpoint(b.pool,4,&c),CANDIDATE_WAL_BUSY);
    EXPECT(bf_write_update(&b,&t,b.pages[1],4,&a),CANDIDATE_WAL_BUSY);
    after = bf_take(&b); CHECK(bf_same(&before,&after));
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 0 && f.unresolved == 2);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48); bf_expect_body(&b,b.pages[1],t.id,2,48);
    bf_report(&b,"membership",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void expect_rejected_history(bf_t *b)
{
    bf_snapshot_all(b,"rejected");
    bf_close(b);
    candidate_wal_status_t status = bf_recover(b,9);
    CHECK(status != CANDIDATE_WAL_OK);
}

static void case_atomic(void)
{
    bf_t b; bf_boot(&b,"atomic",4);
    fx_txn_t t; bf_begin(&b,&t);
    uint8_t before_pages[2][CANDIDATE_PAGE_BYTES], pages[2][CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[0],before_pages[0]); bf_copy(&b,b.pages[1],before_pages[1]);
    bf_with_version(before_pages[0],t.id,1,48,pages[0]); bf_with_version(before_pages[1],999999,2,48,pages[1]);
    uint64_t end = bf_external_pages(&b,&t,b.pages,pages,2,true);
    bf_state_t before = bf_take(&b); candidate_buffer_admission_t a;
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    bf_state_t after = bf_take(&b);
    CHECK(after.is_fenced && after.admitted == before.admitted && after.groups == before.groups && after.dirty == 0);
    CHECK(memcmp(after.data,before.data,32) == 0 && after.known == before.known);
    EXPECT(candidate_buffer_flush(b.pool,4,&(candidate_buffer_flush_t){0}),CANDIDATE_WAL_POISONED);
    expect_rejected_history(&b);
    bf_report(&b,"atomic",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void check_durable(bf_t *b, uint64_t id, unsigned index)
{
    (void)id; (void)index;
    const fx_txn_t *t = b->hook_context;
    CHECK(candidate_wal_info(b->wal).durable_end_lsn >= t->terminal_end);
}

static void case_terminal(void)
{
    bf_t b; bf_boot(&b,"terminal",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a));
    bf_commit(&b,&t);
    CHECK(candidate_wal_info(b.wal).durable_end_lsn < t.terminal_end);
    b.on_write = check_durable; b.hook_context = &t;
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,1,&f));
    CHECK(f.written == 1 && f.forced_lsn == t.terminal_end && b.data_writes == 1);
    b.on_write = NULL;
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48);
    bf_report(&b,"terminal",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_unknown_version(void)
{
    bf_t b; bf_boot(&b,"unknown-version",4);
    fx_txn_t u; bf_begin(&b,&u); candidate_buffer_admission_t a;
    uint8_t before_page[CANDIDATE_PAGE_BYTES], pages[1][CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[1],before_page); bf_with_version(before_page,999999,4,48,pages[0]);
    uint64_t end = bf_external_pages(&b,&u,&b.pages[1],pages,1,true);
    bf_state_t s0 = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    bf_state_t s1 = bf_take(&b); CHECK(s1.is_fenced && s1.admitted == s0.admitted && memcmp(s0.data,s1.data,32) == 0);
    expect_rejected_history(&b);
    bf_report(&b,"unknown-version",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_frozen_mismatch(void)
{
    bf_t b; bf_boot(&b,"frozen-mismatch",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); bf_commit(&b,&t);
    fx_txn_t v; bf_begin(&b,&v);
    uint8_t before_page[CANDIDATE_PAGE_BYTES], pages[1][CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[1],before_page); bf_with_frozen(before_page,t.id,t.csn + 7,5,48,pages[0]);
    uint64_t end = bf_external_pages(&b,&v,&b.pages[1],pages,1,true);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    CHECK(candidate_buffer_info(b.pool).is_fenced && b.data_writes == 0);
    expect_rejected_history(&b);
    bf_report(&b,"frozen-mismatch",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_wal_force(void)
{
    bf_t b; bf_boot(&b,"wal-force",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); bf_commit(&b,&t);
    bf_trace(&b,true); bf_arm(&b,EV_WAL_SYNC,1,FAULT_ERROR);
    candidate_buffer_flush_t f = {.written = 9};
    candidate_wal_status_t status = candidate_buffer_flush(b.pool,4,&f);
    bf_trace(&b,false);
    EXPECT(status,CANDIDATE_WAL_IO);
    CHECK(f.written == 0 && b.counts[EV_DATA_WRITE] == 0 && b.counts[EV_DATA_SYNC] == 0);
    candidate_buffer_info_t info = candidate_buffer_info(b.pool);
    CHECK(info.is_fenced && info.dirty == 1);
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_POISONED);
    bf_expect_absent(&b,b.pages[0],t.id);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48);
    bf_report(&b,"wal-force",status);
    bf_free(&b);
}

static void data_fault(const char *label, bf_kind_t kind, bf_fault_t mode)
{
    bf_t b; bf_boot(&b,label,4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); bf_commit(&b,&t);
    bf_trace(&b,true); bf_arm(&b,kind,1,mode);
    candidate_buffer_flush_t f = {.written = 9};
    candidate_wal_status_t status = candidate_buffer_flush(b.pool,4,&f);
    bf_trace(&b,false);
    candidate_buffer_info_t info = candidate_buffer_info(b.pool);
    EXPECT(status,CANDIDATE_WAL_IO);
    CHECK(f.written == 0 && info.is_fenced && info.dirty == 1 && info.pages_written == 0);
    CHECK(bf_find(&b,0,EV_WAL_SYNC) < bf_find(&b,0,EV_DATA_WRITE));
    bf_dump(&b,label);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(&b,b.pages[0],page,&present);
    printf("TORN %s has_version=%d\n",label,bf_has_version(page,t.id));
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_POISONED);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48);
    bf_report(&b,label,status);
    bf_free(&b);
}

static void case_data_enospc(void) { data_fault("data-enospc",EV_DATA_WRITE,FAULT_ERROR); }
static void case_data_short(void) { data_fault("data-short",EV_DATA_WRITE,FAULT_SHORT); }
static void case_data_sync(void) { data_fault("data-sync",EV_DATA_SYNC,FAULT_ERROR); }

typedef struct {
    fx_txn_t *u;
    const uint8_t *pages[4];
    uint64_t ids[4], begins[2], ends[2];
} redirty_t;

static void redirty(bf_t *b, uint64_t id, unsigned index)
{
    redirty_t *r = b->hook_context; candidate_buffer_admission_t a;
    if (index == 0) {
        CHECK(id == r->ids[0]);
        const uint8_t *pages[2] = {r->pages[1],r->pages[0]}; uint64_t ids[2] = {r->ids[1],r->ids[0]};
        OK(bf_write(b,r->u,ids,pages,2,&a)); r->begins[0] = a.begin_lsn; r->ends[0] = a.end_lsn;
    }
    if (index == 1) {
        CHECK(id == r->ids[1]);
        static uint8_t fresh[CANDIDATE_PAGE_BYTES];
        memcpy(fresh,r->pages[3],CANDIDATE_PAGE_BYTES); bf_set_lsn(fresh,r->ends[0]);
        const uint8_t *pages[2] = {r->pages[2],fresh}; uint64_t ids[2] = {r->ids[2],r->ids[0]};
        OK(bf_write(b,r->u,ids,pages,2,&a)); r->begins[1] = a.begin_lsn;
        CHECK(a.images == 1 && a.deltas == 1);
    }
    candidate_buffer_flush_t f;
    EXPECT(candidate_buffer_flush(b->pool,1,&f),CANDIDATE_WAL_BUSY);
}

static void expect_dirty(const candidate_checkpoint_t *checkpoint, uint64_t id, uint64_t rec_lsn)
{
    candidate_checkpoint_info_t info = candidate_checkpoint_info(checkpoint);
    for (uint32_t i = 0; i < info.summary.dirty_count; i++) {
        const candidate_checkpoint_dirty_t *d = candidate_checkpoint_dirty_at(checkpoint,i);
        if (d->ref.id == id) { CHECK(d->rec_lsn == rec_lsn && rec_lsn != 0); return; }
    }
    CHECK(false);
}

static void case_redirty(void)
{
    bf_t b; bf_boot(&b,"redirty",8);
    uint64_t *p = b.pages; CHECK(b.page_count >= 3);
    fx_txn_t t, u; bf_begin(&b,&t); bf_begin(&b,&u); candidate_buffer_admission_t a;
    for (unsigned i = 0; i < 3; i++) OK(bf_write_version(&b,&t,p[i],1 + i,&a));
    bf_commit(&b,&t);
    static uint8_t base[3][CANDIDATE_PAGE_BYTES], next[4][CANDIDATE_PAGE_BYTES];
    for (unsigned i = 0; i < 3; i++) { bf_copy(&b,p[i],base[i]); bf_with_version(base[i],u.id,40 + i,48,next[i]); }
    bf_rewrite_version(next[0],u.id,43,next[3]);
    redirty_t r = {.u = &u,.pages = {next[0],next[1],next[2],next[3]},.ids = {p[0],p[1],p[2],0}};
    b.on_write = redirty; b.hook_context = &r;
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,4,&f));
    b.on_write = NULL;
    CHECK(f.written == 3 && f.redirtied == 3 && candidate_buffer_info(b.pool).dirty == 3 && b.data_writes == 3);
    for (unsigned i = 0; i < 3; i++) CHECK(b.written_ids[i] == p[i] && memcmp(b.written[i],base[i],CANDIDATE_PAGE_BYTES) == 0);
    for (unsigned i = 0; i < 3; i++) { bf_expect_body(&b,p[i],t.id,1 + i,48); bf_expect_absent(&b,p[i],u.id); }
    OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 0 && f.unresolved == 3);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,0,&c));
    CHECK(c.dirty_entries == 3 && c.active == 1 && c.remaining_dirty == 3);
    candidate_checkpoint_t *loaded = NULL;
    OK(candidate_checkpoint_load(b.wal,&c.anchor,NULL,&loaded));
    expect_dirty(loaded,p[0],r.begins[0]); expect_dirty(loaded,p[1],r.begins[0]); expect_dirty(loaded,p[2],r.begins[1]);
    CHECK(c.anchor.redo_begin <= r.begins[0]);
    candidate_checkpoint_close(&loaded);
    bf_commit(&b,&u);
    OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 3 && candidate_buffer_info(b.pool).dirty == 0);
    bf_restart(&b,9);
    for (unsigned i = 0; i < 3; i++) bf_expect_body(&b,p[i],t.id,1 + i,48);
    bf_expect_body(&b,p[0],u.id,43,48); bf_expect_body(&b,p[1],u.id,41,48); bf_expect_body(&b,p[2],u.id,42,48);
    bf_report(&b,"redirty",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static uint64_t image_record(const candidate_buffer_admission_t *a) { return a->begin_lsn + FRAME_BEGIN; }

static void expect_poisoned(bf_t *b)
{
    candidate_buffer_admission_t a; candidate_buffer_checkpoint_t c; candidate_buffer_flush_t f;
    EXPECT(candidate_buffer_flush(b->pool,4,&f),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_checkpoint(b->pool,4,&c),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_admit(b->pool,candidate_wal_info(b->wal).valid_end_lsn,&a),CANDIDATE_WAL_POISONED);
    CHECK(f.written == 0 && candidate_buffer_info(b->pool).is_fenced);
}

static void case_source_view(void)
{
    bf_t b; bf_boot(&b,"source-view",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],5,&a)); bf_commit(&b,&t);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bf_copy(&b,b.pages[0],page);
    bf_view_altered(&b,image_record(&a),32 + bf_body_offset(page,t.id),true);
    bf_view_set(&b,VIEW_ALTERED,0);
    candidate_buffer_flush_t f = {.written = 9};
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_CORRUPT);
    CHECK(f.written == 0 && b.data_writes == 0 && b.view.reads > 0);
    bf_view_set(&b,VIEW_OFF,0);
    EXPECT(candidate_wal_force(b.wal,candidate_wal_info(b.wal).valid_end_lsn),CANDIDATE_WAL_CORRUPT);
    expect_poisoned(&b);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,5,48);
    bf_report(&b,"source-view",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_source_admit_view(void)
{
    bf_t b; bf_boot(&b,"source-admit-view",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    uint8_t before_page[CANDIDATE_PAGE_BYTES], expected[CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[0],before_page); bf_with_version(before_page,t.id,6,48,expected);
    uint64_t end = bf_external(&b,&t,b.pages,1,6,true);
    bf_view_altered(&b,end - FRAME_END - FRAME_IMAGE,32 + bf_body_offset(expected,t.id),true);
    bf_view_set(&b,VIEW_ALTERED,0);
    bf_state_t before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    bf_state_t after = bf_take(&b);
    CHECK(after.is_fenced && after.admitted == before.admitted && after.groups == before.groups && b.data_writes == 0);
    bf_view_set(&b,VIEW_OFF,0);
    EXPECT(candidate_wal_force(b.wal,candidate_wal_info(b.wal).valid_end_lsn),CANDIDATE_WAL_CORRUPT);
    expect_poisoned(&b);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,6,48);
    bf_report(&b,"source-admit-view",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void source_failure(const char *label, bf_view_mode_t mode, candidate_wal_status_t expected)
{
    bf_t b; bf_boot(&b,label,4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],7,&a)); bf_commit(&b,&t);
    if (mode == VIEW_ALTERED) {
        uint8_t page[CANDIDATE_PAGE_BYTES]; bf_copy(&b,b.pages[0],page);
        bf_view_altered(&b,image_record(&a),32 + bf_body_offset(page,t.id),false);
    }
    bf_view_set(&b,mode,0);
    candidate_buffer_flush_t f = {.written = 9};
    EXPECT(candidate_buffer_flush(b.pool,4,&f),expected);
    CHECK(f.written == 0 && b.data_writes == 0 && b.view.reads > 0);
    bf_view_set(&b,VIEW_OFF,0);
    expect_poisoned(&b);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,7,48);
    bf_report(&b,label,expected);
    bf_free(&b);
}

static void case_source_malformed(void) { source_failure("source-malformed",VIEW_ALTERED,CANDIDATE_WAL_CORRUPT); }
static void case_source_read_error(void) { source_failure("source-read-error",VIEW_ERROR,CANDIDATE_WAL_IO); }
static void case_source_read_eof(void) { source_failure("source-read-eof",VIEW_EOF,CANDIDATE_WAL_IO); }

static void case_source_retry(void)
{
    bf_t b; bf_boot(&b,"source-retry",4);
    OK(candidate_buffer_close(&b.pool));
    candidate_buffer_options_t o = bf_options(&b); o.frames = 1;
    OK(candidate_buffer_open(&o,&b.pool));
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a; candidate_buffer_flush_t f;
    uint64_t end = bf_external(&b,&t,&b.pages[1],1,8,true);
    EXPECT(candidate_buffer_admit(b.pool,end + 8,&a),CANDIDATE_WAL_INVALID);
    candidate_buffer_lease_t lease;
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&lease));
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_BUSY);
    b.memory.calls = 0; b.memory.fail_at = 1;
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_NOMEM);
    b.memory.fail_at = 0;
    CHECK(!candidate_buffer_info(b.pool).is_fenced);
    OK(candidate_buffer_unpin(b.pool,&lease));
    OK(candidate_buffer_admit(b.pool,end,&a)); bf_commit(&b,&t);
    OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 1);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[1],t.id,8,48);
    bf_report(&b,"source-retry",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_force_view(void)
{
    bf_t b; bf_boot(&b,"force-view",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],9,&a)); bf_commit(&b,&t);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bf_copy(&b,b.pages[0],page);
    bf_view_altered(&b,image_record(&a),32 + bf_body_offset(page,t.id),true);
    bf_view_set(&b,VIEW_ALTERED,1);
    candidate_buffer_flush_t f = {.written = 9};
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_CORRUPT);
    CHECK(f.written == 0 && b.data_writes == 0 && b.view.reads > 0 && candidate_wal_info(b.wal).durable_end_lsn >= t.terminal_end);
    bf_view_set(&b,VIEW_OFF,0);
    expect_poisoned(&b);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,9,48);
    bf_report(&b,"force-view",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_force_append(void)
{
    bf_t b; bf_boot(&b,"force-append",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],10,&a)); bf_commit(&b,&t);
    b.is_injecting = true;
    candidate_buffer_flush_t f = {.written = 9};
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_CORRUPT);
    CHECK(!b.is_injecting && f.written == 0 && b.data_writes == 0);
    expect_poisoned(&b);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,10,48);
    bf_report(&b,"force-append",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

const bf_case_t BF_FLUSH_CASES[] = {
    {"shared-leaf",case_shared_leaf},{"membership",case_membership},{"atomic",case_atomic},{"terminal",case_terminal},
    {"unknown-version",case_unknown_version},{"frozen-mismatch",case_frozen_mismatch},{"wal-force",case_wal_force},
    {"data-enospc",case_data_enospc},{"data-short",case_data_short},{"data-sync",case_data_sync},{"redirty",case_redirty},
    {"source-view",case_source_view},{"source-admit-view",case_source_admit_view},{"source-malformed",case_source_malformed},
    {"source-read-error",case_source_read_error},{"source-read-eof",case_source_read_eof},{"source-retry",case_source_retry},
    {"force-view",case_force_view},{"force-append",case_force_append},{NULL,NULL}
};
