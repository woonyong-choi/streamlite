#include "test_candidate_buffer_cases.h"

#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *name;
    bf_kind_t kind;
    unsigned at;
    uint16_t type, chunk;
} point_t;

static const point_t POINTS[] = {
    {"begin-append",EV_WAL_WRITE,1,12,0},{"begin-force",EV_WAL_SYNC,1,0,0},{"begin-directory",EV_DIRECTORY_SYNC,1,0,0},
    {"data-write",EV_DATA_WRITE,1,0,0},{"data-sync",EV_DATA_SYNC,1,0,0},{"dirty-chunk-append",EV_WAL_WRITE,1,13,1},
    {"transaction-chunk-append",EV_WAL_WRITE,1,13,2},{"end-record-append",EV_WAL_WRITE,1,14,0},{"end-force",EV_WAL_SYNC,2,0,0},
    {"control-write",EV_CONTROL_WRITE,1,0,0},{"control-sync",EV_CONTROL_SYNC,1,0,0},
};

static void arm(bf_t *b, const point_t *point, bf_fault_t mode)
{
    if (point->type != 0) bf_arm_record(b,point->type,point->chunk,point->at,mode);
    else bf_arm(b,point->kind,point->at,mode);
}

static uint64_t selected_checkpoint(bf_t *b)
{
    candidate_control_info_t info = candidate_control_info(b->control);
    CHECK(info.selected >= 0);
    return info.slots[info.selected].checkpoint_id;
}

static const candidate_checkpoint_transaction_t *find_transaction(const candidate_checkpoint_t *c, uint64_t id)
{
    candidate_checkpoint_info_t info = candidate_checkpoint_info(c);
    for (uint32_t i = 0; i < info.summary.transaction_count; i++) {
        const candidate_checkpoint_transaction_t *t = candidate_checkpoint_transaction_at(c,i);
        if (t->id == id) return t;
    }
    return NULL;
}

static void case_fpi(void)
{
    bf_t b; bf_boot(&b,"fpi",4);
    uint64_t p = b.pages[0], q = b.pages[1];
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,p,1,&a)); CHECK(a.images == 1 && a.deltas == 0);
    OK(bf_write_update(&b,&t,p,2,&a)); CHECK(a.images == 0 && a.deltas == 1);
    OK(bf_write_version(&b,&t,q,3,&a)); CHECK(a.images == 1);
    bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,0,&c));
    bf_snapshot_all(&b,"published");
    CHECK(c.dirty_entries == 2 && c.remaining_dirty == 2 && candidate_buffer_info(b.pool).fpi_floor_lsn == c.begin_lsn);
    fx_txn_t u; bf_begin(&b,&u);
    OK(bf_write_version(&b,&u,p,4,&a)); CHECK(a.images == 1 && a.deltas == 0);
    OK(bf_write_update(&b,&u,p,5,&a)); CHECK(a.images == 0 && a.deltas == 1);
    uint8_t before[CANDIDATE_PAGE_BYTES], after[CANDIDATE_PAGE_BYTES];
    bf_copy(&b,q,before); bf_rewrite_version(before,t.id,6,after);
    uint32_t at = 64; while (before[at] == after[at]) at++;
    uint32_t last = CANDIDATE_PAGE_BYTES - 1; while (before[last] == after[last]) last--;
    bf_state_t s0 = bf_take(&b);
    uint64_t end = bf_external_delta(&b,&u,q,at,after + at,last + 1 - at);
    EXPECT(candidate_buffer_admit(b.pool,end,&a),CANDIDATE_WAL_CORRUPT);
    bf_state_t s1 = bf_take(&b);
    CHECK(s1.is_fenced && memcmp(s0.data,s1.data,32) == 0 && s1.admitted == s0.admitted && b.data_writes == 0);
    bf_commit(&b,&u);
    bf_restart(&b,9);
    bf_expect_body(&b,p,t.id,2,48); bf_expect_body(&b,p,u.id,5,48); bf_expect_body(&b,q,t.id,6,48);
    bf_report(&b,"fpi",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static unsigned next_event(const bf_t *b, unsigned from, bf_kind_t kind)
{
    unsigned i = bf_find(b,from,kind);
    CHECK(i != UINT32_MAX);
    return i;
}

static void check_order(const bf_t *b, const candidate_buffer_checkpoint_t *c)
{
    unsigned begin = next_event(b,0,EV_WAL_WRITE), begin_sync = next_event(b,begin,EV_WAL_SYNC);
    unsigned write = next_event(b,begin_sync,EV_DATA_WRITE), data_sync = next_event(b,write,EV_DATA_SYNC);
    unsigned chunk = next_event(b,data_sync,EV_WAL_WRITE), end_sync = next_event(b,chunk,EV_WAL_SYNC);
    unsigned control = next_event(b,end_sync,EV_CONTROL_WRITE), control_sync = next_event(b,control,EV_CONTROL_SYNC);
    CHECK(b->events[begin].offset == c->begin_lsn % CANDIDATE_WAL_SEGMENT_BYTES && bf_find(b,begin + 1,EV_WAL_WRITE) == chunk);
    CHECK(bf_find(b,begin_sync + 1,EV_WAL_SYNC) == end_sync);
    unsigned last = chunk;
    for (unsigned i = chunk; i < end_sync; i++) if (b->events[i].kind == EV_WAL_WRITE) last = i;
    CHECK(b->events[last].offset + b->events[last].size == c->end_lsn % CANDIDATE_WAL_SEGMENT_BYTES);
    CHECK(b->events[control].offset == (uint64_t)c->anchor.slot * CANDIDATE_CONTROL_BYTES);
    CHECK(bf_find(b,0,EV_DATA_WRITE) > begin_sync && bf_find(b,data_sync,EV_DATA_WRITE) == UINT32_MAX);
    printf("ORDER begin=%u begin_sync=%u write=%u data_sync=%u chunk=%u end_sync=%u control=%u control_sync=%u events=%u\n",
        begin,begin_sync,write,data_sync,chunk,end_sync,control,control_sync,b->event_count);
}

static void case_checkpoint_order(void)
{
    bf_t b; bf_boot(&b,"checkpoint-order",4);
    fx_txn_t t, u; bf_begin(&b,&t); bf_begin(&b,&u); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); OK(bf_write_version(&b,&u,b.pages[1],2,&a)); bf_commit(&b,&t);
    uint64_t before = selected_checkpoint(&b);
    candidate_control_state_t current = candidate_buffer_info(b.pool).anchor;
    OK(bf_prove(&b,&current,"live-before"));
    bf_trace(&b,true);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,4,&c));
    bf_trace(&b,false);
    bf_dump(&b,"checkpoint-order");
    CHECK(c.writeback.written == 1 && c.writeback.unresolved == 1 && c.dirty_entries == 2 && c.active == 1 && c.remaining_dirty == 1);
    CHECK(c.anchor.checkpoint_id == before + 1 && selected_checkpoint(&b) == before + 1 && c.anchor.redo_begin <= a.begin_lsn);
    check_order(&b,&c);
    bf_snapshot_all(&b,"checkpointed");
    printf("CHECKPOINT %s id=%llu begin=%llu end=%llu redo=%llu dirty=%u transactions=%u active=%u\n",b.tag,
        (unsigned long long)c.anchor.checkpoint_id,(unsigned long long)c.begin_lsn,(unsigned long long)c.end_lsn,
        (unsigned long long)c.anchor.redo_begin,c.dirty_entries,c.transactions,c.active);
    bf_expect_body(&b,b.pages[0],t.id,1,48); bf_expect_absent(&b,b.pages[1],u.id);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],t.id,1,48); bf_expect_body(&b,b.pages[1],u.id,2,48);
    bf_report(&b,"checkpoint-order",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void commit_during_write(bf_t *b, uint64_t id, unsigned index)
{
    (void)id; (void)index;
    b->on_write = NULL;
    bf_commit(b,b->hook_context);
}

static void case_checkpoint_active(void)
{
    bf_t b; bf_boot(&b,"checkpoint-active",4);
    fx_txn_t v, w, t; bf_begin(&b,&v); bf_begin(&b,&w); bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&w,b.pages[0],3,&a));
    OK(bf_write_version(&b,&t,b.pages[1],4,&a)); bf_commit(&b,&t);
    b.on_write = commit_during_write; b.hook_context = &w;
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,4,&c));
    bf_snapshot_all(&b,"published");
    CHECK(b.on_write == NULL && w.terminal_end > c.begin_lsn && w.terminal_end < c.end_lsn);
    CHECK(c.active == 2 && c.writeback.written == 1 && c.dirty_entries == 2 && c.remaining_dirty == 1);
    candidate_checkpoint_t *loaded = NULL;
    OK(candidate_checkpoint_load(b.wal,&c.anchor,NULL,&loaded));
    const candidate_checkpoint_transaction_t *nv = find_transaction(loaded,v.id), *nw = find_transaction(loaded,w.id);
    const candidate_checkpoint_transaction_t *nt = find_transaction(loaded,t.id);
    CHECK(nv != NULL && nv->state == CANDIDATE_CHECKPOINT_ACTIVE && nv->first_lsn == v.first && nv->last_lsn == v.first);
    CHECK(nw != NULL && nw->state == CANDIDATE_CHECKPOINT_ACTIVE && nw->terminal_end_lsn == 0);
    CHECK(nt != NULL && nt->state == CANDIDATE_CHECKPOINT_COMMIT && nt->terminal_end_lsn == t.terminal_end && nt->csn == t.csn);
    CHECK(c.anchor.redo_begin <= v.first && c.anchor.redo_begin <= w.first);
    printf("ACTIVE redo=%llu v_first=%llu w_first=%llu transactions=%u\n",(unsigned long long)c.anchor.redo_begin,
        (unsigned long long)v.first,(unsigned long long)w.first,candidate_checkpoint_info(loaded).summary.transaction_count);
    candidate_checkpoint_close(&loaded);
    candidate_buffer_flush_t f;
    OK(candidate_buffer_flush(b.pool,4,&f)); CHECK(f.written == 1 && candidate_buffer_info(b.pool).dirty == 0);
    bf_restart(&b,9);
    bf_expect_body(&b,b.pages[0],w.id,3,48); bf_expect_body(&b,b.pages[1],t.id,4,48);
    bf_report(&b,"checkpoint-active",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_checkpoint_metadata(void)
{
    bf_t b; bf_boot(&b,"checkpoint-metadata",4);
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    OK(bf_write_version(&b,&t,b.pages[0],1,&a)); bf_commit(&b,&t);
    candidate_control_state_t old = candidate_buffer_info(b.pool).anchor;
    uint64_t end = bf_reserve(&b,5,3);
    OK(candidate_buffer_admit(b.pool,end,&a)); CHECK(a.images == 2 && a.pages == 2);
    end = bf_epoch(&b,old.request_epoch + 1,old.retention_floor);
    OK(candidate_buffer_admit(b.pool,end,&a)); CHECK(a.images == 1);
    uint64_t id = old.page_highwater + 1;
    end = bf_alloc(&b,id);
    OK(candidate_buffer_admit(b.pool,end,&a)); CHECK(a.images == 3);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b.pool,8,&c));
    bf_snapshot_all(&b,"published");
    CHECK(c.anchor.transaction_highwater == old.transaction_highwater + 5 && c.anchor.page_highwater == old.page_highwater + 3);
    CHECK(c.anchor.request_epoch == old.request_epoch + 1 && c.anchor.retention_floor == old.retention_floor);
    CHECK(c.anchor.allocation_root.id == old.allocation_root.id && c.anchor.outcome_root.id == old.outcome_root.id);
    CHECK(c.writeback.written >= 4 && c.remaining_dirty == 0);
    printf("METADATA txh=%llu ph=%llu epoch=%llu floor=%llu gc=%llu id=%llu\n",(unsigned long long)c.anchor.transaction_highwater,
        (unsigned long long)c.anchor.page_highwater,(unsigned long long)c.anchor.request_epoch,
        (unsigned long long)c.anchor.retention_floor,(unsigned long long)c.anchor.gc_horizon,(unsigned long long)id);
    bf_restart(&b,9);
    candidate_control_state_t now = candidate_buffer_info(b.pool).anchor;
    CHECK(now.transaction_highwater >= c.anchor.transaction_highwater && now.page_highwater >= c.anchor.page_highwater);
    CHECK(now.request_epoch == c.anchor.request_epoch && bf_generation(&b,id) == 1);
    bf_expect_body(&b,b.pages[0],t.id,1,48);
    bf_report(&b,"checkpoint-metadata",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_checkpoint_root(void)
{
    bf_t b; bf_boot(&b,"checkpoint-root",4);
    candidate_control_state_t anchor = candidate_buffer_info(b.pool).anchor;
    fx_txn_t t; bf_begin(&b,&t); candidate_buffer_admission_t a;
    uint8_t root[1][CANDIDATE_PAGE_BYTES]; bf_copy(&b,anchor.outcome_root.id,root[0]);
    candidate_catalog_root_t catalog; OK(candidate_catalog_decode_root(fx_uuid,root[0],CANDIDATE_PAGE_BYTES,&catalog));
    catalog.visible_csn = catalog.transaction_highwater;
    candidate_page_location_t l = fx_location(anchor.outcome_root.id,anchor.outcome_root.generation,bf_lsn(root[0]));
    OK(candidate_catalog_encode_root(&l,&catalog,root[0],CANDIDATE_PAGE_BYTES));
    uint64_t end = bf_external_pages(&b,&t,&anchor.outcome_root.id,root,1,true);
    candidate_wal_status_t admitted = candidate_buffer_admit(b.pool,end,&a);
    bf_commit(&b,&t);
    candidate_buffer_checkpoint_t c;
    candidate_wal_status_t status = admitted == CANDIDATE_WAL_OK ? candidate_buffer_checkpoint(b.pool,8,&c) : admitted;
    printf("ROOT admitted=%s checkpoint=%s\n",bf_status(admitted),bf_status(status));
    CHECK(status == CANDIDATE_WAL_UNSUPPORTED || (admitted == CANDIDATE_WAL_CORRUPT && candidate_buffer_info(b.pool).is_fenced));
    CHECK(selected_checkpoint(&b) == anchor.checkpoint_id);
    bf_snapshot_all(&b,"final");
    bf_report(&b,"checkpoint-root",status);
    bf_free(&b);
}

static void prepare_checkpoint(bf_t *b, fx_txn_t *t, fx_txn_t *u)
{
    candidate_buffer_admission_t a;
    bf_begin(b,t); bf_begin(b,u);
    OK(bf_write_version(b,t,b->pages[0],1,&a)); OK(bf_write_version(b,u,b->pages[1],2,&a)); bf_commit(b,t);
}

static void finish_failed(bf_t *b, const fx_txn_t *t, const fx_txn_t *u, uint64_t expected, const char *label)
{
    bf_restart(b,9);
    printf("FAULT %s selected=%llu expected=%llu\n",label,(unsigned long long)candidate_buffer_info(b->pool).anchor.checkpoint_id,
        (unsigned long long)expected);
    bf_expect_body(b,b->pages[0],t->id,1,48); bf_expect_body(b,b->pages[1],u->id,2,48);
}

static void checkpoint_fault(const point_t *point)
{
    char label[64]; CHECK(snprintf(label,sizeof(label),"fault-%s",point->name) > 0);
    bf_t b; bf_boot(&b,label,4);
    fx_txn_t t, u; prepare_checkpoint(&b,&t,&u);
    uint64_t old = selected_checkpoint(&b);
    bf_trace(&b,true); arm(&b,point,FAULT_ERROR);
    candidate_buffer_checkpoint_t c = {.dirty_entries = 9};
    candidate_wal_status_t status = candidate_buffer_checkpoint(b.pool,4,&c);
    bf_trace(&b,false);
    bf_dump(&b,label);
    bf_snapshot_all(&b,"failed");
    EXPECT(status,CANDIDATE_WAL_IO);
    CHECK(c.dirty_entries == 0 && candidate_buffer_info(b.pool).is_fenced && b.trigger.has_fired);
    bool is_written = point->kind == EV_CONTROL_SYNC;
    bf_close(&b);
    bf_open_handles(&b);
    uint64_t selected = selected_checkpoint(&b);
    CHECK(selected == (is_written ? old + 1 : old));
    finish_failed(&b,&t,&u,selected,label);
    bf_report(&b,label,status);
    bf_free(&b);
}

static bool is_published(const point_t *point, bf_fault_t mode)
{
    if (point->kind == EV_CONTROL_SYNC) return true;
    return point->kind == EV_CONTROL_WRITE && mode == FAULT_CRASH_AFTER;
}

static void checkpoint_crash(const point_t *point, bf_fault_t mode)
{
    static const char *MODES[] = {"error","short","before","after","torn"};
    char label[64]; CHECK(snprintf(label,sizeof(label),"crash-%s-%s",point->name,MODES[mode]) > 0);
    bf_t b; bf_boot(&b,label,4);
    uint64_t old = selected_checkpoint(&b), first = b.next_transaction;
    bf_close(&b);
    fflush(stdout);
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        bf_open_handles(&b); OK(bf_open_pool(&b));
        fx_txn_t t, u; prepare_checkpoint(&b,&t,&u); CHECK(t.id == first && u.id == first + 1);
        bf_snapshot_all(&b,"prepared");
        bf_trace(&b,true); arm(&b,point,mode);
        candidate_buffer_checkpoint_t c;
        candidate_wal_status_t status = candidate_buffer_checkpoint(b.pool,4,&c);
        printf("CRASH-MISSED %s status=%s fired=%d\n",label,bf_status(status),b.trigger.has_fired);
        fflush(stdout);
        _exit(3);
    }
    int code; CHECK(waitpid(child,&code,0) == child && WIFEXITED(code));
    b.changes++;
    int expected_code = mode == FAULT_CRASH_BEFORE ? 77 : mode == FAULT_CRASH_AFTER ? 78 : 79;
    printf("CRASH %s exit=%d\n",label,WEXITSTATUS(code));
    bf_snapshot_all(&b,"crashed");
    CHECK(WEXITSTATUS(code) == expected_code);
    bf_open_handles(&b);
    uint64_t selected = selected_checkpoint(&b);
    CHECK(selected == (is_published(point,mode) ? old + 1 : old));
    fx_txn_t t = {.id = first}, u = {.id = first + 1};
    finish_failed(&b,&t,&u,selected,label);
    bf_report(&b,label,CANDIDATE_WAL_OK);
    bf_free(&b);
}

static bool has_wal_tail(bf_t *b)
{
    candidate_wal_info_t info;
    candidate_wal_status_t status = candidate_wal_scan(b->wal,NULL,NULL,&info);
    return status == CANDIDATE_WAL_OK && info.has_tail;
}

static void case_checkpoint_slots(void)
{
    bf_t b; bf_boot(&b,"checkpoint-slots",4);
    fx_txn_t t[4]; candidate_buffer_admission_t a; candidate_buffer_checkpoint_t c[3];
    for (unsigned i = 0; i < 2; i++) {
        bf_begin(&b,&t[i]); OK(bf_write_version(&b,&t[i],b.pages[i],20 + i,&a)); bf_commit(&b,&t[i]);
        OK(candidate_buffer_checkpoint(b.pool,4,&c[i]));
    }
    candidate_control_info_t info = candidate_control_info(b.control);
    CHECK(info.valid_mask == 3 && c[0].anchor.slot != c[1].anchor.slot && info.selected == (int)c[1].anchor.slot);
    CHECK(c[1].anchor.generation == c[0].anchor.generation + 1);
    bf_snapshot_all(&b,"published");
    uint64_t third = b.next_transaction;
    bf_close(&b);
    fflush(stdout);
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        bf_open_handles(&b); OK(bf_open_pool(&b));
        bf_begin(&b,&t[2]); OK(bf_write_version(&b,&t[2],b.pages[2 % b.page_count],22,&a)); bf_commit(&b,&t[2]);
        bf_trace(&b,true); bf_arm(&b,EV_CONTROL_WRITE,1,FAULT_TORN);
        candidate_buffer_checkpoint(b.pool,4,&c[2]);
        _exit(3);
    }
    int code; CHECK(waitpid(child,&code,0) == child && WIFEXITED(code));
    b.changes++;
    printf("SLOTS torn exit=%d\n",WEXITSTATUS(code));
    bf_snapshot_all(&b,"torn");
    CHECK(WEXITSTATUS(code) == 79);
    t[2] = (fx_txn_t){.id = third}; b.next_transaction = third + 1; b.fx->highest_csn++;
    bf_open_handles(&b);
    info = candidate_control_info(b.control);
    CHECK(info.valid_mask == (1u << c[1].anchor.slot) && info.selected == (int)c[1].anchor.slot && !has_wal_tail(&b));
    CHECK(info.slots[info.selected].checkpoint_id == c[1].anchor.checkpoint_id);
    printf("SLOTS first=%u second=%u torn_mask=%u\n",c[0].anchor.slot,c[1].anchor.slot,info.valid_mask);
    bf_close(&b);
    OK(bf_recover(&b,9));
    bf_reattach(&b,9);
    bf_begin(&b,&t[3]); OK(bf_write_version(&b,&t[3],b.pages[0],23,&a)); bf_commit(&b,&t[3]);
    candidate_buffer_checkpoint_t last;
    OK(candidate_buffer_checkpoint(b.pool,4,&last));
    CHECK(candidate_control_info(b.control).valid_mask == 3 && last.anchor.checkpoint_id > c[1].anchor.checkpoint_id);
    bf_restart(&b,10);
    bf_expect_body(&b,b.pages[0],t[0].id,20,48); bf_expect_body(&b,b.pages[1],t[1].id,21,48);
    bf_expect_body(&b,b.pages[2 % b.page_count],t[2].id,22,48); bf_expect_body(&b,b.pages[0],t[3].id,23,48);
    bf_report(&b,"checkpoint-slots",CANDIDATE_WAL_OK);
    bf_free(&b);
}

static void case_faults(void)
{
    for (unsigned i = 0; i < sizeof(POINTS) / sizeof(POINTS[0]); i++) checkpoint_fault(&POINTS[i]);
}

static void case_crashes(void)
{
    for (unsigned i = 0; i < sizeof(POINTS) / sizeof(POINTS[0]); i++) {
        checkpoint_crash(&POINTS[i],FAULT_CRASH_BEFORE);
        checkpoint_crash(&POINTS[i],FAULT_CRASH_AFTER);
        if (POINTS[i].kind == EV_WAL_WRITE || POINTS[i].kind == EV_DATA_WRITE || POINTS[i].kind == EV_CONTROL_WRITE)
            checkpoint_crash(&POINTS[i],FAULT_TORN);
    }
}

const bf_case_t BF_CHECKPOINT_CASES[] = {
    {"fpi",case_fpi},{"checkpoint-order",case_checkpoint_order},{"checkpoint-active",case_checkpoint_active},
    {"checkpoint-metadata",case_checkpoint_metadata},{"checkpoint-root",case_checkpoint_root},{"checkpoint-faults",case_faults},
    {"checkpoint-crashes",case_crashes},{"checkpoint-slots",case_checkpoint_slots},{NULL,NULL}
};
