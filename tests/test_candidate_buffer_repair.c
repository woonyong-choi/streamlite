#include "test_candidate_buffer_cases.h"

#include <string.h>

#include "../src/storage/candidate_wal_internal.h"

typedef struct {
    candidate_buffer_t *pool;
    candidate_buffer_lease_t lease;
    candidate_page_ref_t ref;
    unsigned calls;
} alias_t;

static alias_t alias;

static uint64_t selected_id(bf_t *b)
{
    candidate_control_info_t info = candidate_control_info(b->control);
    CHECK(info.selected >= 0);
    return info.slots[info.selected].checkpoint_id;
}

static void expect_poisoned(bf_t *b)
{
    candidate_buffer_admission_t a; candidate_buffer_checkpoint_t c; candidate_buffer_flush_t f;
    EXPECT(candidate_buffer_flush(b->pool,4,&f),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_checkpoint(b->pool,4,&c),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_admit(b->pool,candidate_wal_info(b->wal).valid_end_lsn,&a),CANDIDATE_WAL_POISONED);
    CHECK(f.written == 0 && candidate_buffer_info(b->pool).is_fenced);
}

static void expect_rejected_history(bf_t *b)
{
    bf_close(b);
    CHECK(bf_recover(b,9) != CANDIDATE_WAL_OK);
}

static void reenter_release(bf_t *b, size_t size)
{
    (void)b; (void)size;
    candidate_buffer_t *pool = alias.pool;
    candidate_buffer_lease_t lease; uint8_t page[CANDIDATE_PAGE_BYTES]; uint64_t lsn;
    candidate_buffer_flush_t f; candidate_buffer_checkpoint_t c; candidate_buffer_admission_t a;
    candidate_buffer_info_t info = candidate_buffer_info(pool);
    EXPECT(candidate_buffer_pin(pool,alias.ref,&lease),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_copy(pool,&alias.lease,page,&lsn),CANDIDATE_WAL_BUSY);
    lease = alias.lease;
    EXPECT(candidate_buffer_unpin(pool,&lease),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_flush(pool,1,&f),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_checkpoint(pool,1,&c),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_admit(pool,1,&a),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_close(&pool),CANDIDATE_WAL_BUSY); CHECK(pool == alias.pool);
    CHECK(info.resident == 0 && info.dirty == 0 && info.pinned == 0 && info.frames > 0);
    alias.calls++;
}

static void case_close_reentry(void)
{
    bf_t b; bf_boot(&b,"close-reentry",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],11,&a)); bf_commit(&b,&t);
    alias = (alias_t){.pool = b.pool,.ref = bf_ref(&b,b.pages[1])};
    OK(candidate_buffer_pin(b.pool,alias.ref,&alias.lease));
    candidate_buffer_info_t open = candidate_buffer_info(b.pool);
    CHECK(open.resident >= 2 && open.dirty == 1 && open.pinned == 1);
    unsigned releases = b.memory.releases;
    b.memory.is_quarantine = true; b.memory.on_release = reenter_release;
    OK(candidate_buffer_close(&b.pool));
    b.memory.on_release = NULL;
    CHECK(b.pool == NULL && alias.calls == b.memory.releases - releases && alias.calls >= 9);
    printf("CLOSE-REENTRY releases=%u reentries=%u held=%u\n",b.memory.releases - releases,alias.calls,b.memory.held_count);
    bf_release_held(&b);
    CHECK(b.memory.live == 0);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,11,48);
    bf_report(&b,"close-reentry",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_stale_base(void)
{
    bf_t b; bf_boot(&b,"stale-base",4);
    uint64_t p = b.pages[0];
    fx_txn_t first, second; bf_begin(&b,&first); bf_begin(&b,&second); candidate_buffer_admission_t a;
    uint8_t base[CANDIDATE_PAGE_BYTES], stale[CANDIDATE_PAGE_BYTES];
    bf_copy(&b,p,base); bf_with_version(base,first.id,31,48,stale);
    OK(bf_write_version(&b,&second,p,32,&a));
    CHECK(bf_lsn(stale) < a.end_lsn);
    bf_state_t before = bf_take(&b);
    const uint8_t *pages[1] = {stale};
    EXPECT(bf_write(&b,&first,&p,pages,1,&a),CANDIDATE_WAL_STALE);
    bf_state_t after = bf_take(&b); CHECK(bf_same(&before,&after) && !after.is_fenced);
    OK(bf_write_version(&b,&first,p,31,&a));
    uint8_t now[CANDIDATE_PAGE_BYTES]; bf_copy(&b,p,now);
    CHECK(bf_has_body(now,first.id,31,48) && bf_has_body(now,second.id,32,48));
    bf_commit(&b,&first); bf_commit(&b,&second);
    candidate_buffer_flush_t f; OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 1);
    bf_restart(&b,9);
    bf_expect_body(&b,p,first.id,31,48); bf_expect_body(&b,p,second.id,32,48);
    bf_report(&b,"stale-base",CANDIDATE_WAL_STALE);
    bf_free(&b);
}

static candidate_wal_status_t write_ref(bf_t *b, fx_txn_t *t, candidate_page_ref_t ref, uint64_t lsn,
    candidate_buffer_admission_t *a)
{
    static uint8_t page[CANDIDATE_PAGE_BYTES];
    candidate_page_location_t l = fx_location(ref.id,ref.generation,lsn);
    OK(candidate_page_encode_versions(&l,NULL,0,page,CANDIDATE_PAGE_BYTES));
    candidate_buffer_page_t list[1] = {{ref,page}};
    candidate_buffer_group_t group = {.transaction_id = t->id,.previous_lsn = t->last,.pages = list,.count = 1};
    return candidate_buffer_write_group(b->pool,&group,a);
}

static void new_page_producer(const char *label, bool reserves)
{
    bf_t b; bf_boot(&b,label,4);
    uint64_t highwater = candidate_buffer_info(b.pool).anchor.page_highwater;
    candidate_buffer_admission_t a;
    if (reserves) OK(candidate_buffer_admit(b.pool,bf_reserve(&b,2,3),&a));
    candidate_buffer_info_t info = candidate_buffer_info(b.pool);
    fx_txn_t t; bf_begin(&b,&t);
    bf_state_t before = bf_take(&b);
    EXPECT(write_ref(&b,&t,(candidate_page_ref_t){highwater + 1,1},info.observed_end_lsn,&a),CANDIDATE_WAL_UNSUPPORTED);
    EXPECT(write_ref(&b,&t,(candidate_page_ref_t){highwater + 9,1},info.observed_end_lsn,&a),CANDIDATE_WAL_UNSUPPORTED);
    candidate_page_ref_t known = bf_ref(&b,b.pages[0]);
    EXPECT(write_ref(&b,&t,(candidate_page_ref_t){known.id,known.generation + 1},info.observed_end_lsn,&a),CANDIDATE_WAL_UNSUPPORTED);
    bf_state_t after = bf_take(&b); CHECK(bf_same(&before,&after) && !after.is_fenced);
    OK(bf_write_version(&b,&t,b.pages[0],61,&a)); bf_commit(&b,&t);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,61,48);
    bf_report(&b,label,CANDIDATE_WAL_UNSUPPORTED);
    bf_free(&b);
}

static void case_new_page_producer(void) { new_page_producer("new-page-producer",true); }
static void case_new_page_producer_direct(void) { new_page_producer("new-page-producer-direct",false); }

static void new_page_external(const char *label, bool reserves)
{
    bf_t b; bf_boot(&b,label,4);
    candidate_control_state_t old = candidate_buffer_info(b.pool).anchor;
    candidate_buffer_admission_t a;
    if (reserves) OK(candidate_buffer_admit(b.pool,bf_reserve(&b,2,3),&a));
    fx_txn_t t; bf_begin(&b,&t);
    uint64_t end = bf_external_new(&b,&t,old.page_highwater + 1);
    bf_state_t before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    bf_state_t after = bf_take(&b);
    CHECK(after.is_fenced && after.admitted == before.admitted && after.known == before.known && memcmp(after.data,before.data,32) == 0);
    expect_poisoned(&b);
    bf_snapshot_all(&b,"rejected");
    expect_rejected_history(&b);
    bf_report(&b,label,CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_new_page_external(void) { new_page_external("new-page-external",true); }
static void case_new_page_external_direct(void) { new_page_external("new-page-external-direct",false); }

static void case_append_mutate(void)
{
    bf_t b; bf_boot(&b,"append-mutate",4);
    b.is_weak_verifier = true;
    uint64_t old = selected_id(&b);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    uint8_t before_page[CANDIDATE_PAGE_BYTES], after_page[CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[0],before_page); bf_with_version(before_page,t.id,41,48,after_page);
    bf_mutate(&b,MUTATE_IMAGE,32 + bf_body_offset(after_page,t.id));
    bf_state_t before = bf_take(&b);
    const uint8_t *pages[1] = {after_page};
    EXPECT(bf_write(&b,&t,&b.pages[0],pages,1,&a),CANDIDATE_WAL_CORRUPT);
    CHECK(b.mutate.has_fired);
    bf_mutate(&b,MUTATE_OFF,0);
    bf_state_t after = bf_take(&b);
    CHECK(after.is_fenced && after.groups == before.groups && after.admitted == before.admitted && after.dirty == before.dirty);
    CHECK(memcmp(after.data,before.data,32) == 0 && after.wal_end > before.wal_end && b.data_writes == 0);
    expect_poisoned(&b);
    EXPECT(candidate_wal_force(b.wal,candidate_wal_info(b.wal).valid_end_lsn),CANDIDATE_WAL_CORRUPT);
    CHECK(selected_id(&b) == old);
    bf_snapshot_all(&b,"mutated");
    bf_close(&b);
    printf("APPEND-MUTATE durable-alternative recovery=%s\n",bf_status(bf_recover(&b,9)));
    bf_report(&b,"append-mutate",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

static void case_append_correct(void)
{
    bf_t b; bf_boot(&b,"append-correct",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],41,&a)); bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c; OK(candidate_buffer_checkpoint(b.pool,4,&c));
    bf_snapshot_all(&b,"published");
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,41,48);
    candidate_control_info_t info = candidate_control_info(b.control);
    candidate_checkpoint_t *loaded = NULL;
    OK(candidate_checkpoint_load(b.wal,&c.anchor,NULL,&loaded));
    CHECK(candidate_checkpoint_info(loaded).summary.dirty_count == c.dirty_entries);
    candidate_checkpoint_close(&loaded);
    OK(candidate_checkpoint_load(b.wal,&info.slots[info.selected],NULL,&loaded));
    candidate_checkpoint_close(&loaded);
    CHECK(info.slots[info.selected].checkpoint_id >= c.anchor.checkpoint_id);
    printf("APPEND-CORRECT published=%llu selected=%llu\n",(unsigned long long)c.anchor.checkpoint_id,
        (unsigned long long)info.slots[info.selected].checkpoint_id);
    bf_report(&b,"append-correct",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void mutate_checkpoint(const char *label, bf_mutate_mode_t mode)
{
    bf_t b; bf_boot(&b,label,4);
    b.is_weak_verifier = true;
    fx_txn_t t, u; bf_begin(&b,&t); bf_begin(&b,&u); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); OK(bf_write_version(&b,&u,b.pages[1],2,&a)); bf_commit(&b,&t);
    uint64_t old = selected_id(&b);
    bf_mutate(&b,mode,0); bf_trace(&b,true);
    candidate_buffer_checkpoint_t c = {.dirty_entries = 9};
    candidate_wal_status_t status = candidate_buffer_checkpoint(b.pool,0,&c);
    bf_trace(&b,false);
    CHECK(b.mutate.has_fired);
    bf_mutate(&b,MUTATE_OFF,0);
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(c.dirty_entries == 0 && b.counts[EV_CONTROL_WRITE] == 0 && selected_id(&b) == old);
    expect_poisoned(&b);
    bf_snapshot_all(&b,"mutated");
    bf_close(&b);
    bf_open_handles(&b);
    CHECK(selected_id(&b) == old);
    bf_close(&b);
    OK(bf_recover(&b,9));
    bf_reattach(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48); bf_expect_body(&b,b.pages[1],u.id,2,48);
    bf_report(&b,label,status);
    bf_free(&b);
}

static void case_checkpoint_mutate_chunk(void) { mutate_checkpoint("checkpoint-mutate-chunk",MUTATE_CHUNK); }
static void case_checkpoint_mutate_end(void) { mutate_checkpoint("checkpoint-mutate-end",MUTATE_END); }

static void case_checkpoint_dirty_root(void)
{
    bf_t b; bf_boot(&b,"checkpoint-dirty-root",4);
    candidate_control_state_t old = candidate_buffer_info(b.pool).anchor;
    candidate_buffer_admission_t a;
    OK(candidate_buffer_admit(b.pool,bf_reserve(&b,4,2),&a));
    OK(candidate_buffer_admit(b.pool,bf_epoch(&b,old.request_epoch + 1,old.retention_floor),&a));
    fx_txn_t t; bf_begin(&b,&t);
    OK(bf_write_version(&b,&t,b.pages[0],71,&a)); bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,0,&c));
    CHECK(c.writeback.written == 0 && c.remaining_dirty == 3 && c.dirty_entries == 3);
    CHECK(c.anchor.page_highwater == old.page_highwater + 2 && c.anchor.transaction_highwater == old.transaction_highwater + 4);
    CHECK(c.anchor.request_epoch == old.request_epoch + 1);
    bf_snapshot_all(&b,"fuzzy");
    bf_restart(&b,9);
    candidate_control_state_t now = candidate_buffer_info(b.pool).anchor;
    CHECK(now.page_highwater >= c.anchor.page_highwater && now.request_epoch == c.anchor.request_epoch);
    bf_expect_body(&b,b.pages[0],t.id,71,48);
    bf_report(&b,"checkpoint-dirty-root",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void expect_unpublished(bf_t *b, uint64_t old, candidate_wal_status_t status)
{
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(b->counts[EV_CONTROL_WRITE] == 0 && selected_id(b) == old && candidate_buffer_info(b->pool).is_fenced);
    bf_snapshot_all(b,"unpublished");
    expect_rejected_history(b);
}

static void case_admit_dangling_version(void)
{
    bf_t b; bf_boot(&b,"admit-dangling-version",4);
    b.is_weak_verifier = true;
    uint64_t old = selected_id(&b), bogus = candidate_buffer_info(b.pool).anchor.page_highwater + 50;
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a, none; memset(&none,0,sizeof(none));
    uint8_t before_page[CANDIDATE_PAGE_BYTES], pages[1][CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[1],before_page);
    bf_with_previous(before_page,t.id,51,(candidate_version_ref_t){{bogus,1},0,1},pages[0]);
    uint64_t end = bf_external_pages(&b,&t,&b.pages[1],pages,1,true);
    uint8_t control[32], data[32], now[32]; fx_sha256_file(b.fx->control,control); fx_sha256_file(b.fx->data,data);
    bf_trace(&b,true);
    memset(&a,0xa5,sizeof(a));
    candidate_wal_status_t status = candidate_buffer_admit(b.pool,end,&a);
    CHECK(memcmp(&a,&none,sizeof(a)) == 0 && candidate_buffer_info(b.pool).is_fenced);
    CHECK(b.counts[EV_DATA_WRITE] == 0 && b.counts[EV_CONTROL_WRITE] == 0);
    fx_sha256_file(b.fx->control,now); CHECK(memcmp(now,control,32) == 0);
    fx_sha256_file(b.fx->data,now); CHECK(memcmp(now,data,32) == 0);
    bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c = {.dirty_entries = 9,.remaining_dirty = 9,.end_lsn = 9};
    EXPECT(candidate_buffer_checkpoint(b.pool,4,&c),CANDIDATE_WAL_POISONED);
    bf_trace(&b,false);
    CHECK(c.anchor.checkpoint_id == 0 && c.dirty_entries == 0 && c.remaining_dirty == 0 && c.end_lsn == 0 && c.writeback.written == 0);
    CHECK(b.counts[EV_DATA_WRITE] == 0 && b.counts[EV_CONTROL_WRITE] == 0);
    fx_sha256_file(b.fx->control,now); CHECK(memcmp(now,control,32) == 0);
    fx_sha256_file(b.fx->data,now); CHECK(memcmp(now,data,32) == 0);
    printf("ADMIT-DANGLING-VERSION data_writes=%u control_writes=%u\n",b.counts[EV_DATA_WRITE],b.counts[EV_CONTROL_WRITE]);
    expect_unpublished(&b,old,status);
    bf_report(&b,"admit-dangling-version",status);
    bf_free(&b);
}

static void case_admit_dangling_allocation(void)
{
    bf_t b; bf_boot(&b,"admit-dangling-allocation",4);
    b.is_weak_verifier = true;
    uint64_t old = selected_id(&b);
    candidate_buffer_admission_t a, none; memset(&none,0,sizeof(none));
    uint64_t end = bf_alloc_node_ref(&b);
    uint8_t control[32], data[32], now[32]; fx_sha256_file(b.fx->control,control); fx_sha256_file(b.fx->data,data);
    bf_trace(&b,true);
    memset(&a,0xa5,sizeof(a));
    candidate_wal_status_t status = candidate_buffer_admit(b.pool,end,&a);
    CHECK(memcmp(&a,&none,sizeof(a)) == 0 && candidate_buffer_info(b.pool).is_fenced);
    CHECK(b.counts[EV_DATA_WRITE] == 0 && b.counts[EV_CONTROL_WRITE] == 0);
    fx_sha256_file(b.fx->control,now); CHECK(memcmp(now,control,32) == 0);
    fx_sha256_file(b.fx->data,now); CHECK(memcmp(now,data,32) == 0);
    candidate_buffer_checkpoint_t c = {.dirty_entries = 9,.remaining_dirty = 9,.end_lsn = 9};
    EXPECT(candidate_buffer_checkpoint(b.pool,4,&c),CANDIDATE_WAL_POISONED);
    bf_trace(&b,false);
    CHECK(c.anchor.checkpoint_id == 0 && c.dirty_entries == 0 && c.remaining_dirty == 0 && c.end_lsn == 0 && c.writeback.written == 0);
    CHECK(b.counts[EV_DATA_WRITE] == 0 && b.counts[EV_CONTROL_WRITE] == 0);
    fx_sha256_file(b.fx->control,now); CHECK(memcmp(now,control,32) == 0);
    fx_sha256_file(b.fx->data,now); CHECK(memcmp(now,data,32) == 0);
    printf("ADMIT-DANGLING-ALLOCATION data_writes=%u control_writes=%u\n",b.counts[EV_DATA_WRITE],b.counts[EV_CONTROL_WRITE]);
    expect_unpublished(&b,old,status);
    bf_report(&b,"admit-dangling-allocation",status);
    bf_free(&b);
}

static void force_read(const char *label, bf_view_mode_t mode)
{
    bf_t b; bf_boot(&b,label,4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],81,&a)); bf_commit(&b,&t);
    bf_view_set(&b,mode,1);
    candidate_buffer_flush_t f = {.written = 9};
    EXPECT(candidate_buffer_flush(b.pool,4,&f),CANDIDATE_WAL_IO);
    CHECK(f.written == 0 && b.data_writes == 0 && b.view.reads > 0 && candidate_wal_info(b.wal).durable_end_lsn >= t.terminal_end);
    bf_view_set(&b,VIEW_OFF,0);
    expect_poisoned(&b);
    bf_snapshot_all(&b,"failed");
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,81,48);
    bf_report(&b,label,CANDIDATE_WAL_IO);
    bf_free(&b);
}

static void case_force_read_error(void) { force_read("force-read-error",VIEW_ERROR); }
static void case_force_read_eof(void) { force_read("force-read-eof",VIEW_EOF); }

static void case_pinned_wal(void)
{
    bf_t b; bf_boot(&b,"pinned-wal",4);
    OK(candidate_buffer_close(&b.pool));
    EXPECT(candidate_wal_pin(b.wal),CANDIDATE_WAL_INVALID);
    OK(bf_open_pool(&b));
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],91,&a)); bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c; OK(candidate_buffer_checkpoint(b.pool,4,&c));
    OK(candidate_buffer_close(&b.pool));
    OK(bf_open_pool(&b));
    fx_txn_t v; bf_begin(&b,&v);
    uint8_t before_page[CANDIDATE_PAGE_BYTES], after_page[CANDIDATE_PAGE_BYTES];
    bf_copy(&b,b.pages[1],before_page); bf_with_version(before_page,v.id,92,48,after_page);
    bf_mutate(&b,MUTATE_IMAGE,32 + bf_body_offset(after_page,v.id));
    const uint8_t *pages[1] = {after_page};
    EXPECT(bf_write(&b,&v,&b.pages[1],pages,1,&a),CANDIDATE_WAL_CORRUPT);
    CHECK(b.mutate.has_fired);
    bf_mutate(&b,MUTATE_OFF,0);
    CHECK(candidate_buffer_info(b.pool).is_fenced && b.data_writes == 1);
    EXPECT(candidate_wal_force(b.wal,candidate_wal_info(b.wal).valid_end_lsn),CANDIDATE_WAL_CORRUPT);
    expect_poisoned(&b);
    bf_snapshot_all(&b,"mutated");
    bf_close(&b);
    printf("PINNED-WAL durable-alternative recovery=%s\n",bf_status(bf_recover(&b,9)));
    bf_report(&b,"pinned-wal",CANDIDATE_WAL_CORRUPT);
    bf_free(&b);
}

const bf_case_t BF_REPAIR_CASES[] = {
    {"close-reentry",case_close_reentry},{"stale-base",case_stale_base},{"new-page-producer",case_new_page_producer},
    {"new-page-external",case_new_page_external},{"new-page-producer-direct",case_new_page_producer_direct},
    {"new-page-external-direct",case_new_page_external_direct},{"append-mutate",case_append_mutate},{"append-correct",case_append_correct},
    {"checkpoint-mutate-chunk",case_checkpoint_mutate_chunk},{"checkpoint-mutate-end",case_checkpoint_mutate_end},
    {"checkpoint-dirty-root",case_checkpoint_dirty_root},{"admit-dangling-version",case_admit_dangling_version},
    {"admit-dangling-allocation",case_admit_dangling_allocation},{"force-read-error",case_force_read_error},
    {"force-read-eof",case_force_read_eof},{"pinned-wal",case_pinned_wal},{NULL,NULL}
};
