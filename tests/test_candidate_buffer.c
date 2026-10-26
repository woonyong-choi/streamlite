#include "test_candidate_buffer_cases.h"

#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef BUFFER_COUNTER_LIMIT
#define BUFFER_COUNTER_LIMIT UINT64_MAX
#endif
#define BATCH_ENTRY_BYTES 40u

unsigned checks;

bf_state_t bf_take(bf_t *b)
{
    candidate_buffer_info_t i = candidate_buffer_info(b->pool);
    bf_state_t s; memset(&s,0,sizeof(s));
    s.admitted = i.admitted_lsn; s.groups = i.groups_admitted; s.written = i.pages_written;
    s.wal_end = candidate_wal_info(b->wal).valid_end_lsn; s.known = i.known_pages; s.dirty = i.dirty; s.resident = i.resident;
    s.pinned = i.pinned; s.leases = i.leases; s.requested = i.requested_bytes; s.is_fenced = i.is_fenced;
    fx_sha256_file(b->fx->data,s.data);
    return s;
}

bool bf_same(const bf_state_t *a, const bf_state_t *b)
{
    bool is_same_history = a->admitted == b->admitted && a->groups == b->groups && a->wal_end == b->wal_end;
    bool is_same_frames = a->known == b->known && a->dirty == b->dirty && a->resident == b->resident;
    bool is_same_holds = a->pinned == b->pinned && a->leases == b->leases && a->requested == b->requested;
    bool is_same_output = a->written == b->written && a->is_fenced == b->is_fenced && memcmp(a->data,b->data,32) == 0;
    if (is_same_history && is_same_frames && is_same_holds && is_same_output) return true;
    printf("STATE admitted=%llu/%llu groups=%llu/%llu written=%llu/%llu wal=%llu/%llu known=%llu/%llu dirty=%u/%u resident=%u/%u "
        "pinned=%u/%u leases=%u/%u requested=%zu/%zu fenced=%d/%d data=%d\n",(unsigned long long)a->admitted,(unsigned long long)b->admitted,
        (unsigned long long)a->groups,(unsigned long long)b->groups,(unsigned long long)a->written,(unsigned long long)b->written,
        (unsigned long long)a->wal_end,(unsigned long long)b->wal_end,(unsigned long long)a->known,(unsigned long long)b->known,
        a->dirty,b->dirty,a->resident,b->resident,a->pinned,b->pinned,a->leases,b->leases,a->requested,b->requested,a->is_fenced,
        b->is_fenced,memcmp(a->data,b->data,32) == 0);
    return false;
}

static candidate_wal_status_t open_with(bf_t *b, candidate_buffer_options_t *o)
{
    return candidate_buffer_open(o,&b->pool);
}

static void expect_refused_open(bf_t *b, candidate_buffer_options_t *o, candidate_wal_status_t expected)
{
    size_t live = b->memory.live;
    EXPECT(open_with(b,o),expected);
    CHECK(b->pool == NULL && b->memory.live == live);
}

static void check_frame_edges(bf_t *b, size_t per_frame)
{
    candidate_buffer_options_t o = bf_options(b); o.frames = 0; o.leases = 0;
    OK(open_with(b,&o));
    candidate_buffer_info_t d = candidate_buffer_info(b->pool);
    CHECK(d.frames == CANDIDATE_BUFFER_FRAMES && d.frame_bytes == CANDIDATE_BUFFER_FRAMES * per_frame);
    CHECK(d.staging_bytes == CANDIDATE_BUFFER_STAGING_BUDGET && d.peak_requested_bytes + d.borrowed_bytes <= CANDIDATE_BUFFER_TOTAL_BUDGET);
    printf("BUDGET default frames=%u frame_bytes=%zu requested=%zu open_peak=%zu borrowed=%zu\n",d.frames,d.frame_bytes,
        d.requested_bytes,d.peak_requested_bytes,d.borrowed_bytes);
    OK(candidate_buffer_close(&b->pool));
    uint32_t max = (uint32_t)(CANDIDATE_BUFFER_FRAME_BUDGET / per_frame);
    o.frames = max;
    OK(open_with(b,&o));
    candidate_buffer_info_t m = candidate_buffer_info(b->pool);
    CHECK(m.frame_bytes <= CANDIDATE_BUFFER_FRAME_BUDGET && m.peak_requested_bytes + m.borrowed_bytes <= CANDIDATE_BUFFER_TOTAL_BUDGET);
    printf("BUDGET max frames=%u frame_bytes=%zu requested=%zu open_peak=%zu\n",m.frames,m.frame_bytes,m.requested_bytes,
        m.peak_requested_bytes);
    size_t steady = m.requested_bytes;
    candidate_buffer_checkpoint_t c; OK(candidate_buffer_checkpoint(b->pool,0,&c));
    m = candidate_buffer_info(b->pool);
    CHECK(m.peak_requested_bytes + m.borrowed_bytes <= CANDIDATE_BUFFER_TOTAL_BUDGET && m.requested_bytes == steady);
    printf("BUDGET max checkpoint_peak=%zu borrowed=%zu total=%zu budget=%u\n",m.peak_requested_bytes,m.borrowed_bytes,
        m.peak_requested_bytes + m.borrowed_bytes,CANDIDATE_BUFFER_TOTAL_BUDGET);
    OK(candidate_buffer_close(&b->pool));
    OK(open_with(b,&o));
    m = candidate_buffer_info(b->pool);
    OK(candidate_buffer_close(&b->pool));
    o.total_budget_bytes = m.peak_requested_bytes + m.borrowed_bytes;
    OK(open_with(b,&o)); OK(candidate_buffer_close(&b->pool));
    o.total_budget_bytes--;
    expect_refused_open(b,&o,CANDIDATE_WAL_LIMIT);
    o = bf_options(b); o.frames = max + 1;
    expect_refused_open(b,&o,CANDIDATE_WAL_LIMIT);
    o = bf_options(b); o.frame_budget_bytes = CANDIDATE_BUFFER_FRAME_BUDGET + 1;
    expect_refused_open(b,&o,CANDIDATE_WAL_INVALID);
    o = bf_options(b); o.staging_budget_bytes = CANDIDATE_BUFFER_STAGING_BUDGET + 1;
    expect_refused_open(b,&o,CANDIDATE_WAL_INVALID);
    o = bf_options(b); o.total_budget_bytes = CANDIDATE_BUFFER_TOTAL_BUDGET + 1;
    expect_refused_open(b,&o,CANDIDATE_WAL_INVALID);
}

static bool has_size(const bf_t *b, size_t size)
{
    CHECK(b->memory.size_count <= BF_SIZES);
    for (unsigned i = 0; i < b->memory.size_count; i++) if (b->memory.sizes[i] == size) return true;
    return false;
}

static size_t flush_peak(bf_t *b, uint32_t pages)
{
    fx_txn_t t; bf_begin(b,&t); candidate_buffer_admission_t a;
    for (uint32_t i = 0; i < pages; i++) OK(bf_write_version(b,&t,b->pages[i],60 + i,&a));
    bf_commit(b,&t);
    candidate_buffer_flush_t f;
    b->memory.size_count = 0;
    OK(candidate_buffer_flush(b->pool,pages,&f)); CHECK(f.written == pages);
    CHECK(has_size(b,(size_t)pages * CANDIDATE_PAGE_BYTES) && has_size(b,(size_t)pages * BATCH_ENTRY_BYTES));
    return candidate_buffer_info(b->pool).peak_requested_bytes;
}

static void check_staging(bf_t *b, fx_txn_t *t, size_t group_bytes)
{
    candidate_buffer_options_t o = bf_options(b); o.staging_budget_bytes = group_bytes - 1;
    OK(open_with(b,&o));
    fx_txn_t refused; bf_begin(b,&refused); candidate_buffer_admission_t a;
    uint8_t page[CANDIDATE_PAGE_BYTES], next[CANDIDATE_PAGE_BYTES];
    bf_copy(b,b->pages[0],page); bf_with_version(page,refused.id,1,48,next);
    const uint8_t *pages[1] = {next};
    bf_state_t before = bf_take(b);
    EXPECT(bf_write(b,&refused,&b->pages[0],pages,1,&a),CANDIDATE_WAL_LIMIT);
    bf_state_t after = bf_take(b); CHECK(bf_same(&before,&after));
    bf_abort(b,&refused);
    OK(candidate_buffer_close(&b->pool));
    o.staging_budget_bytes = group_bytes;
    OK(open_with(b,&o));
    bf_begin(b,t);
    size_t live = b->memory.live;
    OK(bf_write_version(b,t,b->pages[0],1,&a));
    CHECK(a.images == 1 && a.deltas == 0 && b->memory.live == live);
    OK(bf_write_update(b,t,b->pages[0],2,&a));
    CHECK(a.images == 0 && a.deltas == 1 && b->memory.live == live);
}

static void case_budget(void)
{
    bf_t b; bf_boot(&b,"budget",4);
    candidate_buffer_info_t base = candidate_buffer_info(b.pool);
    size_t open_peak = base.peak_requested_bytes, frame_bytes = base.frame_bytes, borrowed = base.borrowed_bytes;
    size_t group_bytes = 16 + 32 + CANDIDATE_PAGE_BYTES + 24;
    CHECK(borrowed == candidate_wal_info(b.wal).resident_bytes + candidate_control_info(b.control).resident_bytes);
    CHECK(base.budget_bytes == CANDIDATE_BUFFER_TOTAL_BUDGET && base.requested_bytes == b.memory.live);
    size_t steady = base.requested_bytes;
    size_t one = flush_peak(&b,1), two = flush_peak(&b,2);
    printf("BUDGET flush1_peak=%zu flush2_peak=%zu batch_bytes=%u\n",one,two,CANDIDATE_PAGE_BYTES + BATCH_ENTRY_BYTES);
    CHECK(one <= two && two + borrowed <= CANDIDATE_BUFFER_TOTAL_BUDGET);
    CHECK(candidate_buffer_info(b.pool).requested_bytes == steady && b.memory.live == steady);
    printf("BUDGET steady=%zu open_peak=%zu flush1_peak=%zu flush2_peak=%zu borrowed=%zu\n",steady,open_peak,one,two,borrowed);
    bf_restart(&b,9);
    OK(candidate_buffer_close(&b.pool));
    candidate_buffer_options_t o = bf_options(&b); o.frame_budget_bytes = frame_bytes;
    OK(open_with(&b,&o)); OK(candidate_buffer_close(&b.pool));
    o.frame_budget_bytes = frame_bytes - 1;
    expect_refused_open(&b,&o,CANDIDATE_WAL_LIMIT);
    o = bf_options(&b);
    OK(open_with(&b,&o));
    open_peak = candidate_buffer_info(b.pool).peak_requested_bytes; borrowed = candidate_buffer_info(b.pool).borrowed_bytes;
    OK(candidate_buffer_close(&b.pool));
    o.total_budget_bytes = open_peak + borrowed;
    OK(open_with(&b,&o));
    CHECK(candidate_buffer_info(b.pool).peak_requested_bytes == open_peak);
    OK(candidate_buffer_close(&b.pool));
    o.total_budget_bytes = open_peak + borrowed - 1;
    expect_refused_open(&b,&o,CANDIDATE_WAL_LIMIT);
    check_frame_edges(&b,frame_bytes / 8);
    fx_txn_t t;
    check_staging(&b,&t,group_bytes);
    bf_commit(&b,&t);
    candidate_buffer_flush_t f; OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 1);
    OK(candidate_buffer_close(&b.pool));
    o = bf_options(&b); o.staging_budget_bytes = group_bytes - 1;
    OK(open_with(&b,&o));
    fx_txn_t u; bf_begin(&b,&u); candidate_buffer_admission_t a;
    uint64_t end = bf_external(&b,&u,&b.pages[1],1,5,true);
    bf_state_t before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_LIMIT);
    bf_state_t after = bf_take(&b); CHECK(bf_same(&before,&after));
    OK(candidate_buffer_close(&b.pool));
    expect_refused_open(&b,&o,CANDIDATE_WAL_UNSUPPORTED);
    bf_restart(&b,10);
    bf_expect_body(&b,b.pages[0],t.id,2,48); bf_expect_body(&b,b.pages[1],u.id,5,48);
    bf_report(&b,"budget",CANDIDATE_WAL_OK);
    bf_free(&b);
}

typedef candidate_wal_status_t (*op_t)(bf_t *b, void *context);

static unsigned oom_loop(bf_t *b, op_t op, void *context)
{
    for (unsigned k = 1; k < 64; k++) {
        bf_state_t before = bf_take(b); size_t live = b->memory.live;
        b->memory.calls = 0; b->memory.fail_at = k;
        candidate_wal_status_t status = op(b,context);
        b->memory.fail_at = 0;
        if (status == CANDIDATE_WAL_OK) return k;
        EXPECT(status,CANDIDATE_WAL_NOMEM);
        bf_state_t after = bf_take(b);
        if (!bf_same(&before,&after) || b->memory.live != live)
            printf("OOM changed k=%u fenced=%d wal=%llu->%llu dirty=%u->%u written=%llu->%llu requested=%zu->%zu live=%zu->%zu\n",k,
                after.is_fenced,(unsigned long long)before.wal_end,(unsigned long long)after.wal_end,before.dirty,after.dirty,
                (unsigned long long)before.written,(unsigned long long)after.written,before.requested,after.requested,live,b->memory.live);
        CHECK(bf_same(&before,&after) && b->memory.live == live);
    }
    CHECK(false);
    return 0;
}

static candidate_wal_status_t op_admit(bf_t *b, void *context)
{
    candidate_buffer_admission_t a; return candidate_buffer_admit(b->pool,*(uint64_t *)context,&a);
}

static candidate_wal_status_t op_flush(bf_t *b, void *context)
{
    (void)context; candidate_buffer_flush_t f; return candidate_buffer_flush(b->pool,4,&f);
}

static candidate_wal_status_t op_checkpoint(bf_t *b, void *context)
{
    (void)context; candidate_buffer_checkpoint_t c; return candidate_buffer_checkpoint(b->pool,4,&c);
}

static candidate_wal_status_t op_pin(bf_t *b, void *context)
{
    candidate_buffer_lease_t lease;
    candidate_wal_status_t status = candidate_buffer_pin(b->pool,bf_ref(b,*(uint64_t *)context),&lease);
    if (status == CANDIDATE_WAL_OK) OK(candidate_buffer_unpin(b->pool,&lease));
    return status;
}

static unsigned oom_checkpoint(bf_t *b)
{
    unsigned fenced = 0;
    for (unsigned k = 1; k < 512; k++) {
        bf_state_t before = bf_take(b); size_t live = b->memory.live;
        uint64_t selected = candidate_control_info(b->control).slots[candidate_control_info(b->control).selected].checkpoint_id;
        b->memory.calls = 0; b->memory.fail_at = k;
        candidate_wal_status_t status = op_checkpoint(b,NULL);
        b->memory.fail_at = 0;
        if (status == CANDIDATE_WAL_OK) { printf("OOM checkpoint fenced_points=%u\n",fenced); return k; }
        EXPECT(status,CANDIDATE_WAL_NOMEM);
        bf_state_t after = bf_take(b);
        uint64_t now = candidate_control_info(b->control).slots[candidate_control_info(b->control).selected].checkpoint_id;
        CHECK(now == selected);
        if (bf_same(&before,&after) && b->memory.live == live) continue;
        CHECK(after.is_fenced && after.wal_end > before.wal_end && after.admitted == before.admitted);
        fenced++;
        bf_restart(b,b->owner + 1);
        fx_txn_t t; bf_begin(b,&t); candidate_buffer_admission_t a;
        OK(bf_write_version(b,&t,b->pages[k % 2],200 + k,&a)); bf_commit(b,&t);
    }
    CHECK(false);
    return 0;
}

static void case_oom(void)
{
    bf_t b; bf_boot(&b,"oom",4);
    OK(candidate_buffer_close(&b.pool));
    unsigned k = 1;
    for (;; k++) {
        candidate_buffer_options_t o = bf_options(&b);
        b.memory.calls = 0; b.memory.fail_at = k; size_t live = b.memory.live;
        candidate_wal_status_t status = candidate_buffer_open(&o,&b.pool);
        b.memory.fail_at = 0;
        if (status == CANDIDATE_WAL_OK) break;
        EXPECT(status,CANDIDATE_WAL_NOMEM); CHECK(b.pool == NULL && b.memory.live == live);
    }
    printf("OOM open first_success=%u\n",k);
    printf("OOM pin first_success=%u\n",oom_loop(&b,op_pin,&b.pages[1]));
    fx_txn_t t; bf_begin(&b,&t);
    uint64_t end = bf_external(&b,&t,b.pages,1,3,true);
    printf("OOM admit first_success=%u\n",oom_loop(&b,op_admit,&end));
    bf_commit(&b,&t);
    printf("OOM checkpoint first_success=%u\n",oom_checkpoint(&b));
    fx_txn_t u; bf_begin(&b,&u); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&u,b.pages[1],4,&a)); bf_commit(&b,&u);
    printf("OOM flush first_success=%u\n",oom_loop(&b,op_flush,NULL));
    fx_txn_t v; bf_begin(&b,&v);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bf_copy(&b,b.pages[0],page);
    unsigned write_k = 1;
    for (;; write_k++) {
        bf_state_t before = bf_take(&b);
        b.memory.calls = 0; b.memory.fail_at = write_k;
        candidate_wal_status_t status = bf_write_version(&b,&v,b.pages[0],9,&a);
        b.memory.fail_at = 0;
        EXPECT(status,CANDIDATE_WAL_NOMEM);
        bf_state_t after = bf_take(&b);
        if (after.is_fenced) { CHECK(after.wal_end > before.wal_end); break; }
        CHECK(bf_same(&before,&after));
    }
    printf("OOM write fenced_after_append_at=%u\n",write_k);
    EXPECT(candidate_buffer_flush(b.pool,4,&(candidate_buffer_flush_t){0}),CANDIDATE_WAL_POISONED);
    bf_snapshot_all(&b,"write-fenced");
    bf_restart(&b,b.owner + 1);
    bf_expect_body(&b,b.pages[0],v.id,9,48); bf_expect_body(&b,b.pages[1],u.id,4,48); bf_expect_body(&b,b.pages[0],t.id,3,48);
    bf_report(&b,"oom",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_counters(void)
{
    bf_t b; bf_boot(&b,"counters",8);
    candidate_buffer_info_t first = candidate_buffer_info(b.pool);
    candidate_buffer_lease_t l0, l1;
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0));
    candidate_buffer_lease_t closed = l0;
    OK(candidate_buffer_unpin(b.pool,&l0)); CHECK(l0.lease_id == 0);
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0));
    CHECK(l0.lease_id == closed.lease_id + 1 && l0.pool_id == first.pool_id);
    closed = l0;
    OK(candidate_buffer_close(&b.pool));
    OK(bf_open_pool(&b));
    candidate_buffer_info_t second = candidate_buffer_info(b.pool);
    CHECK(second.pool_id > first.pool_id);
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l1)); OK(candidate_buffer_unpin(b.pool,&l1));
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l1));
    CHECK(l1.pool_id == second.pool_id);
    CHECK(l1.lease_id == closed.lease_id && l1.slot == closed.slot && l1.frame_generation == closed.frame_generation);
    uint8_t page[CANDIDATE_PAGE_BYTES]; uint64_t lsn;
    EXPECT(candidate_buffer_copy(b.pool,&closed,page,&lsn),CANDIDATE_WAL_STALE);
    OK(candidate_buffer_copy(b.pool,&l1,page,&lsn));
    OK(candidate_buffer_unpin(b.pool,&l1));
    printf("COUNTERS pool_ids=%llu,%llu lease=%llu\n",(unsigned long long)first.pool_id,(unsigned long long)second.pool_id,
        (unsigned long long)l1.lease_id);
    bf_snapshot_all(&b,"final");
    bf_report(&b,"counters",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static unsigned exhaust_leases(bf_t *b)
{
    unsigned issued = 0;
    for (;;) {
        candidate_buffer_lease_t lease;
        candidate_wal_status_t status = candidate_buffer_pin(b->pool,bf_ref(b,b->pages[0]),&lease);
        if (status == CANDIDATE_WAL_LIMIT) return issued;
        OK(status); issued++;
        CHECK(lease.lease_id == issued);
        OK(candidate_buffer_unpin(b->pool,&lease));
    }
}

static unsigned exhaust_frames(bf_t *b)
{
    candidate_buffer_options_t o = bf_options(b); o.frames = 1;
    OK(candidate_buffer_close(&b->pool)); OK(open_with(b,&o));
    unsigned loads = 0;
    for (unsigned i = 0;; i++) {
        fx_txn_t t; bf_begin(b,&t); candidate_buffer_admission_t a;
        uint8_t page[CANDIDATE_PAGE_BYTES], next[CANDIDATE_PAGE_BYTES]; bool present;
        bf_read_data(b,b->pages[i % 2],page,&present); CHECK(present);
        bf_with_version(page,t.id,90 + i,48,next);
        const uint8_t *pages[1] = {next};
        bf_state_t before = bf_take(b);
        candidate_wal_status_t status = bf_write(b,&t,&b->pages[i % 2],pages,1,&a);
        if (status != CANDIDATE_WAL_OK) {
            EXPECT(status,CANDIDATE_WAL_BUSY);
            bf_state_t after = bf_take(b); CHECK(bf_same(&before,&after));
            bf_abort(b,&t);
            return loads;
        }
        loads++;
        bf_commit(b,&t);
        candidate_buffer_flush_t f; OK(candidate_buffer_flush(b->pool,1,&f)); CHECK(f.written == 1);
    }
}

static unsigned exhaust_dirty(bf_t *b, fx_txn_t *t)
{
    candidate_buffer_admission_t a; uint8_t page[CANDIDATE_PAGE_BYTES], next[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(b,b->pages[2],page,&present); CHECK(present);
    for (unsigned i = 0;; i++) {
        if (i == 0) bf_with_version(page,t->id,70,48,next); else bf_rewrite_version(page,t->id,70 + i,next);
        bf_state_t before = bf_take(b);
        const uint8_t *pages[1] = {next};
        candidate_wal_status_t status = bf_write(b,t,&b->pages[2],pages,1,&a);
        memcpy(page,next,CANDIDATE_PAGE_BYTES);
        if (status == CANDIDATE_WAL_OK) bf_set_lsn(page,a.end_lsn);
        if (status != CANDIDATE_WAL_OK) {
            EXPECT(status,CANDIDATE_WAL_LIMIT);
            bf_state_t after = bf_take(b); CHECK(bf_same(&before,&after));
            return i;
        }
    }
}

static void case_counter_limit(void)
{
    bf_t b; bf_boot(&b,"counter-limit",8);
    CHECK(BUFFER_COUNTER_LIMIT < 64);
    unsigned leases = exhaust_leases(&b);
    CHECK(leases == BUFFER_COUNTER_LIMIT);
    unsigned loads = exhaust_frames(&b);
    CHECK(loads == BUFFER_COUNTER_LIMIT);
    OK(candidate_buffer_close(&b.pool)); OK(bf_open_pool(&b));
    fx_txn_t t; bf_begin(&b,&t);
    unsigned writes = exhaust_dirty(&b,&t);
    CHECK(writes == BUFFER_COUNTER_LIMIT);
    bf_commit(&b,&t);
    candidate_buffer_flush_t f; OK(candidate_buffer_flush(b.pool,8,&f)); CHECK(f.written == 1);
    OK(candidate_buffer_close(&b.pool));
    unsigned opened = 3;
    for (;;) {
        candidate_wal_status_t status = bf_open_pool(&b);
        if (status != CANDIDATE_WAL_OK) { EXPECT(status,CANDIDATE_WAL_LIMIT); CHECK(b.pool == NULL); break; }
        opened++;
        CHECK(candidate_buffer_info(b.pool).pool_id == opened);
        OK(candidate_buffer_close(&b.pool));
    }
    CHECK(opened == BUFFER_COUNTER_LIMIT);
    printf("COUNTER-LIMIT limit=%u leases=%u loads=%u writes=%u pools=%u\n",(unsigned)BUFFER_COUNTER_LIMIT,leases,loads,writes,opened);
    bf_close(&b);
    OK(bf_recover(&b,9));
    b.owner = 9; b.fx->owner = 9; bf_open_handles(&b);
    bf_expect_body(&b,b.pages[2],t.id,70 + writes - 1,48);
    bf_report(&b,"counter-limit",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_pinned(void)
{
    bf_t b; bf_boot(&b,"pinned",8);
    CHECK(b.page_count >= 3);
    OK(candidate_buffer_close(&b.pool));
    candidate_buffer_options_t o = bf_options(&b); o.frames = 2; o.leases = 2;
    OK(open_with(&b,&o));
    candidate_buffer_lease_t l0, l1, l2;
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0));
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[1]),&l1));
    EXPECT(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[2]),&l2),CANDIDATE_WAL_LIMIT);
    fx_txn_t t; bf_begin(&b,&t);
    uint64_t end = bf_external(&b,&t,&b.pages[2],1,1,true);
    bf_state_t before = bf_take(&b); candidate_buffer_admission_t a;
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_BUSY);
    bf_state_t after = bf_take(&b); CHECK(bf_same(&before,&after));
    OK(candidate_buffer_unpin(b.pool,&l1));
    OK(candidate_buffer_admit(b.pool,end,&a));
    bf_commit(&b,&t);
    fx_txn_t u; bf_begin(&b,&u);
    OK(bf_write_version(&b,&u,b.pages[0],2,&a));
    bf_commit(&b,&u);
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,4,&f));
    CHECK(f.written == 1 && f.pinned == 1);
    bf_expect_absent(&b,b.pages[0],u.id); bf_expect_body(&b,b.pages[2],t.id,1,48);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,4,&c));
    CHECK(c.dirty_entries == 1 && c.remaining_dirty == 1 && c.writeback.pinned == 1);
    OK(candidate_buffer_unpin(b.pool,&l0));
    fx_txn_t v; bf_begin(&b,&v);
    uint64_t three[3] = {b.pages[0],b.pages[1],b.pages[2]};
    end = bf_external(&b,&v,three,3,7,true);
    before = bf_take(&b);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_LIMIT);
    after = bf_take(&b); CHECK(bf_same(&before,&after));
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],u.id,2,48); bf_expect_body(&b,b.pages[0],v.id,7,48);
    bf_expect_body(&b,b.pages[1],v.id,8,48); bf_expect_body(&b,b.pages[2],v.id,9,48);
    bf_report(&b,"pinned",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void reenter(bf_t *b, uint64_t id, unsigned index)
{
    (void)id; (void)index;
    b->on_write = NULL;
    candidate_buffer_flush_t f; candidate_buffer_lease_t lease; candidate_buffer_t *pool = b->pool;
    EXPECT(candidate_buffer_flush(pool,1,&f),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_buffer_close(&pool),CANDIDATE_WAL_BUSY); CHECK(pool == b->pool);
    EXPECT(candidate_buffer_pin(pool,(candidate_page_ref_t){b->pages[0],1},&lease),CANDIDATE_WAL_BUSY);
    candidate_buffer_checkpoint_t c;
    EXPECT(candidate_buffer_checkpoint(pool,1,&c),CANDIDATE_WAL_BUSY);
}

static void case_lease(void)
{
    bf_t b; bf_boot(&b,"lease",8);
    OK(candidate_buffer_close(&b.pool));
    candidate_buffer_options_t o = bf_options(&b); o.frames = 2;
    OK(open_with(&b,&o));
    candidate_buffer_lease_t l0; uint8_t page[CANDIDATE_PAGE_BYTES]; uint64_t lsn;
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0));
    candidate_buffer_lease_t saved = l0;
    OK(candidate_buffer_unpin(b.pool,&l0));
    EXPECT(candidate_buffer_copy(b.pool,&saved,page,&lsn),CANDIDATE_WAL_STALE);
    EXPECT(candidate_buffer_unpin(b.pool,&saved),CANDIDATE_WAL_STALE);
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0)); saved = l0;
    OK(candidate_buffer_unpin(b.pool,&l0));
    for (unsigned i = 1; i < 3; i++) { candidate_buffer_lease_t l; OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[i]),&l)); OK(candidate_buffer_unpin(b.pool,&l)); }
    CHECK(candidate_buffer_info(b.pool).evictions >= 1);
    OK(candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0));
    CHECK(l0.frame_generation != saved.frame_generation || l0.slot != saved.slot);
    candidate_buffer_lease_t forged = l0; forged.pool_id++;
    EXPECT(candidate_buffer_copy(b.pool,&forged,page,&lsn),CANDIDATE_WAL_STALE);
    forged = l0; forged.frame_generation++;
    EXPECT(candidate_buffer_copy(b.pool,&forged,page,&lsn),CANDIDATE_WAL_STALE);
    EXPECT(candidate_buffer_pin(b.pool,(candidate_page_ref_t){b.pages[0],99},&forged),CANDIDATE_WAL_INVALID);
    OK(candidate_buffer_copy(b.pool,&l0,page,&lsn));
    OK(candidate_buffer_unpin(b.pool,&l0));
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    uint64_t end = bf_external(&b,&t,&b.pages[1],1,1,true);
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        candidate_buffer_flush_t f; candidate_buffer_checkpoint_t c;
        bool is_forked = candidate_buffer_flush(b.pool,1,&f) == CANDIDATE_WAL_FORKED &&
            candidate_buffer_pin(b.pool,bf_ref(&b,b.pages[0]),&l0) == CANDIDATE_WAL_FORKED &&
            candidate_buffer_admit(b.pool,end,&a) == CANDIDATE_WAL_FORKED &&
            candidate_buffer_checkpoint(b.pool,1,&c) == CANDIDATE_WAL_FORKED;
        _exit(is_forked ? 0 : 1);
    }
    int code; CHECK(waitpid(child,&code,0) == child && WIFEXITED(code) && WEXITSTATUS(code) == 0);
    OK(candidate_buffer_admit(b.pool,end,&a)); bf_commit(&b,&t);
    b.on_write = reenter;
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,1,&f)); CHECK(f.written == 1 && b.on_write == NULL);
    bf_expect_body(&b,b.pages[1],t.id,1,48);
    bf_report(&b,"lease",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void write_page(bf_t *b, uint64_t id, const uint8_t *page)
{
    b->changes++;
    CHECK(pwrite(b->data_fd,page,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)) == CANDIDATE_PAGE_BYTES);
}

static void expect_tampered(bf_t *b, const uint8_t *page, const uint8_t *changed, candidate_wal_status_t expected, const char *label)
{
    write_page(b,b->pages[1],changed);
    candidate_wal_status_t status = bf_open_pool(b);
    printf("REOPEN %s status=%s\n",label,bf_status(status));
    EXPECT(status,expected);
    if (b->pool != NULL) OK(candidate_buffer_close(&b->pool));
    write_page(b,b->pages[1],page);
}

static size_t first_body(const uint8_t *page)
{
    candidate_page_info_t info; OK(candidate_page_decode(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info)); CHECK(info.slots > 0);
    uint32_t generation = (uint32_t)page[76] | (uint32_t)page[77] << 8 | (uint32_t)page[78] << 16 | (uint32_t)page[79] << 24;
    candidate_page_version_t v;
    OK(candidate_page_get_version(fx_uuid,page,CANDIDATE_PAGE_BYTES,(candidate_version_ref_t){info.location.ref,0,generation},&v));
    CHECK(v.inline_bytes != NULL && v.payload_bytes > 0);
    return (size_t)(v.inline_bytes - page);
}

static void case_reopen(void)
{
    bf_t b; bf_boot(&b,"reopen",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],11,&a));
    OK(candidate_buffer_close(&b.pool));
    EXPECT(bf_open_pool(&b),CANDIDATE_WAL_UNSUPPORTED);
    bf_commit(&b,&t);
    EXPECT(bf_open_pool(&b),CANDIDATE_WAL_UNSUPPORTED);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,11,48);
    OK(candidate_buffer_close(&b.pool));
    uint8_t page[CANDIDATE_PAGE_BYTES], changed[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(&b,b.pages[1],page,&present); CHECK(present);
    memcpy(changed,page,CANDIDATE_PAGE_BYTES); changed[first_body(page)] ^= 1;
    expect_tampered(&b,page,changed,CANDIDATE_WAL_UNSUPPORTED,"crc");
    bf_set_lsn(changed,bf_lsn(page) + 8);
    expect_tampered(&b,page,changed,CANDIDATE_WAL_CORRUPT,"future-lsn");
    bf_set_lsn(changed,bf_lsn(page));
    expect_tampered(&b,page,changed,CANDIDATE_WAL_OK,"trusted-same-lsn");
    OK(bf_open_pool(&b));
    bf_report(&b,"reopen",CANDIDATE_WAL_OK);
    bf_free(&b);
}

const bf_case_t BF_CORE_CASES[] = {
    {"budget",case_budget},{"oom",case_oom},{"counters",case_counters},{"counter-limit",case_counter_limit},
    {"pinned",case_pinned},{"lease",case_lease},{"reopen",case_reopen},{NULL,NULL}
};

static const bf_case_t *find_case(const char *name)
{
    const bf_case_t *tables[] = {BF_CORE_CASES,BF_FLUSH_CASES,BF_CHECKPOINT_CASES,BF_REPAIR_CASES,BF_SOURCE_CASES};
    for (unsigned t = 0; t < 5; t++)
        for (const bf_case_t *c = tables[t]; c->name != NULL; c++) if (strcmp(c->name,name) == 0) return c;
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr,"usage: %s CASE\n",argv[0]); return 2; }
    const bf_case_t *c = find_case(argv[1]);
    if (c == NULL) { fprintf(stderr,"unknown case %s\n",argv[1]); return 2; }
    bool is_limited = BUFFER_COUNTER_LIMIT != UINT64_MAX;
    if (is_limited != (strcmp(c->name,"counter-limit") == 0)) {
        fprintf(stderr,"case %s requires the %s counter build\n",c->name,is_limited ? "default" : "limited");
        return 2;
    }
    c->run();
    printf("PASS %s checks=%u\n",c->name,checks);
    return 0;
}
