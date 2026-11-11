#include "test_candidate_mvcc_fixture.h"

#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RESULT_TABLE MF_RESULT_TABLE
#define ROW_A 0u
#define ROW_B 1u
#define ROW_NEW 2u

unsigned checks;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned phase;
} step_t;
typedef struct {
    candidate_mvcc_t *m;
    step_t *step;
    candidate_mvcc_txn_t txn;
    candidate_mvcc_txn_info_t info;
    candidate_mvcc_response_t response;
    uint8_t value[MF_PAYLOAD];
    uint64_t applied_at, committed_at, abort_at;
} worker_t;

static uint64_t order;

static uint64_t next_order(void) { return __atomic_add_fetch(&order,1,__ATOMIC_SEQ_CST); }

static void step_wait(step_t *s, unsigned phase)
{
    CHECK(pthread_mutex_lock(&s->lock) == 0);
    while (s->phase < phase) CHECK(pthread_cond_wait(&s->cond,&s->lock) == 0);
    CHECK(pthread_mutex_unlock(&s->lock) == 0);
}

static void step_set(step_t *s, unsigned phase)
{
    CHECK(pthread_mutex_lock(&s->lock) == 0);
    s->phase = phase;
    CHECK(pthread_cond_broadcast(&s->cond) == 0);
    CHECK(pthread_mutex_unlock(&s->lock) == 0);
}

static candidate_mvcc_value_t read_reader(candidate_mvcc_t *m, const candidate_mvcc_reader_t *r, unsigned row)
{
    candidate_mvcc_value_t v; EXPECT(candidate_mvcc_reader_read(m,r,mf_key(row),&v),CANDIDATE_WAL_OK); return v;
}

static candidate_mvcc_value_t read_own(candidate_mvcc_t *m, const candidate_mvcc_txn_t *t, unsigned row)
{
    candidate_mvcc_value_t v; EXPECT(candidate_mvcc_read(m,t,mf_key(row),&v),CANDIDATE_WAL_OK); return v;
}

static void expect_bytes(const candidate_mvcc_value_t *v, const uint8_t *bytes)
{
    CHECK(v->is_found && v->size == MF_PAYLOAD && memcmp(v->bytes,bytes,MF_PAYLOAD) == 0);
}

static void begin_write(candidate_mvcc_t *m, uint64_t nonce, unsigned row, const uint8_t *bytes, candidate_mvcc_txn_t *out)
{
    candidate_mvcc_write_t w = mf_write(row,bytes,MF_PAYLOAD);
    candidate_mvcc_request_t r = mf_request(nonce,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,out),CANDIDATE_WAL_OK);
}

static candidate_mvcc_answer_t query_write(candidate_mvcc_t *m, uint64_t nonce, unsigned row, const uint8_t *bytes)
{
    candidate_mvcc_write_t w = mf_write(row,bytes,MF_PAYLOAD);
    candidate_mvcc_request_t r = mf_request(nonce,&w,1);
    candidate_mvcc_answer_t a; EXPECT(candidate_mvcc_query(m,&r,&a),CANDIDATE_WAL_OK); return a;
}

static void print_txn(const char *label, const candidate_mvcc_txn_info_t *i)
{
    printf("TXN %s id=%" PRIu64 " state=%d snapshot=%" PRIu64 " first=%" PRIu64 " last=%" PRIu64 " records=%" PRIu64
        " crc=%08x group=%" PRIu64 "@%" PRIu64 " row=%" PRIu64 "/%" PRIu64 ":%u.%u result=%" PRIu64 ":%u.%u\n",label,
        i->transaction_id,(int)i->state,i->snapshot_csn,i->first_lsn,i->last_lsn,i->records,i->chain_crc,i->group_id,i->group_end_lsn,
        i->versions[0].page.id,i->versions[0].page.generation,i->versions[0].slot,i->versions[0].slot_generation,
        i->result.page.id,i->result.slot,i->result.slot_generation);
}

static void expect_response(const candidate_mvcc_response_t *r, uint64_t transaction_id, uint64_t affected)
{
    CHECK(r->state == CANDIDATE_MVCC_COMMITTED && r->transaction_id == transaction_id && r->csn != 0);
    CHECK(r->size == CANDIDATE_MVCC_RESPONSE_BYTES && memcmp(r->bytes,"SLMR",4) == 0 && r->affected_rows == affected);
    printf("RESPONSE id=%" PRIu64 " csn=%" PRIu64 " end=%" PRIu64 " affected=%" PRIu64 " ref=%" PRIu64 ":%u.%u\n",r->transaction_id,
        r->csn,r->terminal_end_lsn,r->affected_rows,r->result.page.id,r->result.slot,r->result.slot_generation);
}

static void expect_chain(const mf_t *f, uint64_t transaction_id, uint16_t terminal, mf_chain_t *out)
{
    mf_wal_t wal; mf_scan_wal(f,transaction_id,out,&wal);
    CHECK(out->count >= 5 && out->types[0] == 1 && out->types[1] == 2 && out->types[2] == 3);
    bool has_group_end = false;
    for (unsigned i = 0; i < out->count; i++) has_group_end = has_group_end || out->types[i] == 8;
    CHECK(has_group_end && wal.reserves >= 1 && transaction_id <= wal.highwater);
    if (terminal == 0) CHECK(out->terminal_end == 0 && out->types[out->count - 1] == 8);
    else CHECK(out->types[out->count - 1] == terminal && out->terminal_end != 0);
    if (terminal == 10) CHECK(out->count >= 2 && out->types[out->count - 2] == 9 && out->csn != 0);
    printf("ORACLE txn=%" PRIu64 " records=%u first=%" PRIu64 " last=%" PRIu64 " terminal=%u csn=%" PRIu64 " end=%" PRIu64
        " reserves=%u highwater=%" PRIu64 "\n",transaction_id,out->count,out->lsns[0],out->lsns[out->count - 1],terminal,out->csn,
        out->terminal_end,wal.reserves,wal.highwater);
}

static void expect_prior(const mf_t *f, candidate_version_ref_t ref, unsigned row)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_slot_t prior;
    mf_read_version(f,ref,page,&prior);
    CHECK(prior.transaction_id == f->seed_transaction && prior.row_id == mf_key(row).row_id);
    CHECK(memcmp(prior.bytes,f->initial[row],MF_PAYLOAD) == 0);
}

static void expect_result_slot(const mf_t *f, candidate_version_ref_t ref, uint64_t transaction_id)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_slot_t s;
    mf_read_version(f,ref,page,&s);
    CHECK(s.transaction_id == transaction_id && s.table_id == RESULT_TABLE && s.row_id == transaction_id);
}

static void expect_pages(const mf_t *f, const candidate_mvcc_txn_info_t *a, const candidate_mvcc_txn_info_t *b, bool has_result_a)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_slot_t va, vb;
    mf_read_version(f,a->versions[0],page,&va);
    CHECK(va.transaction_id == a->transaction_id && va.table_id == 1 && va.row_id == 3 && va.frozen_csn == 0 && va.generation == 1);
    expect_prior(f,va.previous,ROW_A);
    mf_read_version(f,b->versions[0],page,&vb);
    CHECK(vb.transaction_id == b->transaction_id && vb.table_id == 1 && vb.row_id == 4 && vb.frozen_csn == 0 && vb.generation == 1);
    expect_prior(f,vb.previous,ROW_B);
    CHECK(a->versions[0].page.id == b->versions[0].page.id && a->versions[0].slot != b->versions[0].slot);
    expect_result_slot(f,b->result,b->transaction_id);
    if (has_result_a) expect_result_slot(f,a->result,a->transaction_id);
    printf("SLOTS append=%" PRIu64 " a=%" PRIu64 ":%u b=%" PRIu64 ":%u results=%" PRIu64 ":%u,%" PRIu64 ":%u\n",a->versions[0].page.id,
        a->transaction_id,a->versions[0].slot,b->transaction_id,b->versions[0].slot,a->result.page.id,a->result.slot,b->result.page.id,
        b->result.slot);
}

static void *run_a(void *context)
{
    worker_t *w = context;
    begin_write(w->m,0xa001,ROW_A,w->value,&w->txn);
    EXPECT(candidate_mvcc_apply(w->m,&w->txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(w->m,&w->txn,&w->info),CANDIDATE_WAL_OK);
    w->applied_at = next_order();
    step_set(w->step,1);
    step_wait(w->step,3);
    candidate_mvcc_value_t own = read_own(w->m,&w->txn,ROW_A);
    expect_bytes(&own,w->value);
    w->abort_at = next_order();
    EXPECT(candidate_mvcc_abort(w->m,&w->txn),CANDIDATE_WAL_OK);
    step_set(w->step,4);
    return NULL;
}

static void *run_b(void *context)
{
    worker_t *w = context;
    step_wait(w->step,1);
    begin_write(w->m,0xb001,ROW_B,w->value,&w->txn);
    EXPECT(candidate_mvcc_apply(w->m,&w->txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(w->m,&w->txn,&w->info),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(w->m,&w->txn,&w->response),CANDIDATE_WAL_OK);
    w->committed_at = next_order();
    step_set(w->step,2);
    return NULL;
}

static void check_overlap(mf_t *f, candidate_mvcc_t *m, worker_t *a, worker_t *b, const candidate_mvcc_reader_t *old)
{
    candidate_mvcc_txn_info_t live; EXPECT(candidate_mvcc_inspect(m,&a->txn,&live),CANDIDATE_WAL_OK);
    CHECK(live.state == CANDIDATE_MVCC_ACTIVE && live.is_applied && a->abort_at == 0 && a->applied_at < b->committed_at);
    CHECK(a->info.versions[0].page.id == b->info.versions[0].page.id);
    CHECK(a->info.versions[0].page.generation == b->info.versions[0].page.generation);
    CHECK(a->info.versions[0].slot != b->info.versions[0].slot);
    expect_response(&b->response,b->info.transaction_id,1);
    candidate_mvcc_value_t v = read_reader(m,old,ROW_A); expect_bytes(&v,f->initial[0]);
    v = read_reader(m,old,ROW_B); expect_bytes(&v,f->initial[1]);
    candidate_mvcc_reader_t fresh; EXPECT(candidate_mvcc_reader_open(m,&fresh),CANDIDATE_WAL_OK);
    v = read_reader(m,&fresh,ROW_B); expect_bytes(&v,b->value); CHECK(v.csn == b->response.csn);
    v = read_reader(m,&fresh,ROW_A); expect_bytes(&v,f->initial[0]);
    EXPECT(candidate_mvcc_reader_close(m,&fresh),CANDIDATE_WAL_OK);
    mf_chain_t chain; expect_chain(f,a->info.transaction_id,0,&chain); expect_chain(f,b->info.transaction_id,10,&chain);
    CHECK(chain.csn == b->response.csn && chain.terminal_end == b->response.terminal_end_lsn);
    print_txn("A-active",&live); print_txn("B-applied",&b->info);
    printf("ORDER a_applied=%" PRIu64 " b_committed=%" PRIu64 " a_abort_pending gate=%" PRIu64 "\n",a->applied_at,b->committed_at,
        candidate_mvcc_info(m).gate_entries);
}

static void expect_restart(mf_t *f, const worker_t *a, const worker_t *b, bool is_committed_a)
{
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_reader_t r; EXPECT(candidate_mvcc_reader_open(m,&r),CANDIDATE_WAL_OK);
    candidate_mvcc_value_t v = read_reader(m,&r,ROW_A); expect_bytes(&v,is_committed_a ? a->value : f->initial[0]);
    v = read_reader(m,&r,ROW_B); expect_bytes(&v,b->value);
    candidate_mvcc_answer_t q = query_write(m,0xb001,ROW_B,b->value);
    CHECK(q.lookup == CANDIDATE_CATALOG_MATCH && q.response.state == CANDIDATE_MVCC_COMMITTED);
    CHECK(q.response.csn == b->response.csn && q.response.size == b->response.size && memcmp(q.response.bytes,b->response.bytes,b->response.size) == 0);
    q = query_write(m,0xa001,ROW_A,a->value);
    CHECK(q.lookup == CANDIDATE_CATALOG_MATCH && q.response.state == (is_committed_a ? CANDIDATE_MVCC_COMMITTED : CANDIDATE_MVCC_ABORTED));
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_OK);
    candidate_mvcc_info_t info = candidate_mvcc_info(m);
    printf("REOPEN owner=%" PRIu64 " visible=%" PRIu64 " outcomes=%u a=%s\n",info.owner_epoch,info.visible_csn,info.outcomes,
        is_committed_a ? "committed" : "aborted");
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
}

static void case_overlap(void)
{
    mf_t f; mf_boot(&f,"overlap");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_reader_t old; EXPECT(candidate_mvcc_reader_open(m,&old),CANDIDATE_WAL_OK);
    step_t step = {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0};
    worker_t a = {.m = m,.step = &step}, b = {.m = m,.step = &step};
    mf_fill(0xa1,a.value); mf_fill(0xb1,b.value);
    pthread_t ta, tb;
    CHECK(pthread_create(&ta,NULL,run_a,&a) == 0 && pthread_create(&tb,NULL,run_b,&b) == 0);
    step_wait(&step,2);
    check_overlap(&f,m,&a,&b,&old);
    step_set(&step,3);
    CHECK(pthread_join(ta,NULL) == 0 && pthread_join(tb,NULL) == 0);
    CHECK(a.abort_at > b.committed_at);
    mf_chain_t chain; expect_chain(&f,a.info.transaction_id,11,&chain);
    candidate_mvcc_answer_t q = query_write(m,0xb001,ROW_B,b.value);
    CHECK(q.lookup == CANDIDATE_CATALOG_MATCH && memcmp(q.response.bytes,b.response.bytes,CANDIDATE_MVCC_RESPONSE_BYTES) == 0);
    EXPECT(candidate_mvcc_reader_close(m,&old),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    expect_restart(&f,&a,&b,false);
    expect_pages(&f,&a.info,&b.info,true);
    printf("SCOPE native persisted-store slice; not SQL/ACK27, not full25 SQL/GC, no throughput claim\n");
    mf_free(&f);
}

static void run_sequential(mf_t *f, candidate_mvcc_t *m, worker_t *a, worker_t *b, bool commit_a)
{
    mf_fill(0xa2,a->value); mf_fill(0xb2,b->value);
    begin_write(m,0xa001,ROW_A,a->value,&a->txn);
    EXPECT(candidate_mvcc_apply(m,&a->txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,&a->txn,&a->info),CANDIDATE_WAL_OK);
    begin_write(m,0xb001,ROW_B,b->value,&b->txn);
    EXPECT(candidate_mvcc_apply(m,&b->txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,&b->txn,&b->info),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(m,&b->txn,&b->response),CANDIDATE_WAL_OK);
    expect_response(&b->response,b->info.transaction_id,1);
    CHECK(a->info.versions[0].page.id == b->info.versions[0].page.id && f->is_seeded);
    if (!commit_a) { EXPECT(candidate_mvcc_abort(m,&a->txn),CANDIDATE_WAL_OK); return; }
    EXPECT(candidate_mvcc_commit(m,&a->txn,&a->response),CANDIDATE_WAL_OK);
    expect_response(&a->response,a->info.transaction_id,1);
    CHECK(a->response.csn > b->response.csn);
}

static void case_pair(const char *tag, bool commit_a)
{
    mf_t f; mf_boot(&f,tag);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    worker_t a = {.m = m}, b = {.m = m};
    run_sequential(&f,m,&a,&b,commit_a);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_chain_t chain; expect_chain(&f,a.info.transaction_id,commit_a ? 10 : 11,&chain);
    expect_restart(&f,&a,&b,commit_a);
    expect_restart(&f,&a,&b,commit_a);
    expect_pages(&f,&a.info,&b.info,true);
    mf_free(&f);
}

static void case_restart(void) { case_pair("restart",false); }

static void case_bothcommit(void) { case_pair("bothcommit",true); }

static void child_writer(mf_t *f, int fd)
{
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(f,&m),CANDIDATE_WAL_OK);
    worker_t a = {.m = m}, b = {.m = m};
    mf_fill(0xa2,a.value); mf_fill(0xb2,b.value);
    begin_write(m,0xa001,ROW_A,a.value,&a.txn);
    EXPECT(candidate_mvcc_apply(m,&a.txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,&a.txn,&a.info),CANDIDATE_WAL_OK);
    begin_write(m,0xb001,ROW_B,b.value,&b.txn);
    EXPECT(candidate_mvcc_apply(m,&b.txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,&b.txn,&b.info),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(m,&b.txn,&b.response),CANDIDATE_WAL_OK);
    worker_t report[2] = {a,b};
    CHECK(write(fd,report,sizeof(report)) == (ssize_t)sizeof(report));
    for (;;) pause();
}

static void case_kill(void)
{
    mf_t f; mf_boot(&f,"kill");
    int fds[2]; CHECK(pipe(fds) == 0);
    fflush(stdout);
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) { CHECK(close(fds[0]) == 0); child_writer(&f,fds[1]); _exit(1); }
    CHECK(close(fds[1]) == 0);
    worker_t report[2]; size_t got = 0;
    while (got < sizeof(report)) {
        ssize_t n = read(fds[0],(uint8_t *)report + got,sizeof(report) - got);
        CHECK(n > 0); got += (size_t)n;
    }
    CHECK(kill(pid,SIGKILL) == 0);
    int status = 0; CHECK(waitpid(pid,&status,0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(close(fds[0]) == 0);
    printf("KILLED child=%ld after B durable response a=%" PRIu64 " b=%" PRIu64 " csn=%" PRIu64 "\n",(long)pid,
        report[0].info.transaction_id,report[1].info.transaction_id,report[1].response.csn);
    mf_chain_t chain; expect_chain(&f,report[0].info.transaction_id,0,&chain);
    f.next_owner++;
    expect_restart(&f,&report[0],&report[1],false);
    expect_chain(&f,report[0].info.transaction_id,11,&chain);
    expect_pages(&f,&report[0].info,&report[1].info,true);
    mf_free(&f);
}

static void case_visibility(void)
{
    mf_t f; mf_boot(&f,"visibility");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_reader_t r0; EXPECT(candidate_mvcc_reader_open(m,&r0),CANDIDATE_WAL_OK);
    uint8_t one[MF_PAYLOAD], two[MF_PAYLOAD], three[MF_PAYLOAD]; mf_fill(1,one); mf_fill(2,two); mf_fill(3,three);
    candidate_mvcc_write_t writes[2] = {mf_write(ROW_NEW,one,MF_PAYLOAD),mf_write(ROW_A,two,MF_PAYLOAD)};
    candidate_mvcc_request_t request = mf_request(0x1001,writes,2);
    candidate_mvcc_txn_t t1; EXPECT(candidate_mvcc_begin(m,&request,&t1),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&t1),CANDIDATE_WAL_OK);
    candidate_mvcc_value_t v = read_own(m,&t1,ROW_NEW); expect_bytes(&v,one); CHECK(v.csn == 0);
    v = read_reader(m,&r0,ROW_NEW); CHECK(!v.is_found);
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t1,&response),CANDIDATE_WAL_OK);
    CHECK(response.affected_rows == 2);
    v = read_reader(m,&r0,ROW_NEW); CHECK(!v.is_found);
    v = read_reader(m,&r0,ROW_A); expect_bytes(&v,f.initial[0]);
    candidate_mvcc_reader_t r1; EXPECT(candidate_mvcc_reader_open(m,&r1),CANDIDATE_WAL_OK);
    v = read_reader(m,&r1,ROW_NEW); expect_bytes(&v,one); v = read_reader(m,&r1,ROW_A); expect_bytes(&v,two);
    candidate_mvcc_txn_t t2, t3;
    begin_write(m,0x1002,ROW_NEW,three,&t2);
    begin_write(m,0x1003,ROW_B,three,&t3);
    EXPECT(candidate_mvcc_apply(m,&t3),CANDIDATE_WAL_OK);
    v = read_own(m,&t2,ROW_B); expect_bytes(&v,f.initial[1]);
    v = read_own(m,&t2,ROW_A); expect_bytes(&v,two);
    EXPECT(candidate_mvcc_abort(m,&t3),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_abort(m,&t2),CANDIDATE_WAL_OK);
    v = read_reader(m,&r1,ROW_B); expect_bytes(&v,f.initial[1]);
    EXPECT(candidate_mvcc_reader_close(m,&r0),CANDIDATE_WAL_OK); EXPECT(candidate_mvcc_reader_close(m,&r1),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_reader_open(m,&r0),CANDIDATE_WAL_OK);
    v = read_reader(m,&r0,ROW_NEW); expect_bytes(&v,one); v = read_reader(m,&r0,ROW_B); expect_bytes(&v,f.initial[1]);
    EXPECT(candidate_mvcc_reader_close(m,&r0),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void expect_unchanged(candidate_mvcc_t *m, const candidate_mvcc_info_t *before)
{
    candidate_mvcc_info_t after = candidate_mvcc_info(m);
    CHECK(after.outcomes == before->outcomes && after.next_transaction_id == before->next_transaction_id);
    CHECK(after.active == before->active && after.fence == CANDIDATE_WAL_OK);
}

static void check_plans(candidate_mvcc_t *m, const uint8_t *bytes)
{
    candidate_mvcc_txn_t t; candidate_mvcc_info_t before = candidate_mvcc_info(m);
    candidate_mvcc_write_t writes[5] = {mf_write(ROW_A,bytes,MF_PAYLOAD),mf_write(ROW_B,bytes,MF_PAYLOAD),
        mf_write(ROW_NEW,bytes,MF_PAYLOAD),mf_write(ROW_A,bytes,MF_PAYLOAD),mf_write(ROW_A,bytes,MF_PAYLOAD)};
    candidate_mvcc_request_t r = mf_request(0x2101,writes,0); EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_UNSUPPORTED);
    r.count = 5; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_LIMIT);
    r.count = 4; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_INVALID);
    candidate_mvcc_write_t big = mf_write(ROW_A,bytes,CANDIDATE_MVCC_PAYLOAD_BYTES + 1);
    r = mf_request(0x2102,&big,1); EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_LIMIT);
    candidate_mvcc_write_t result_row = {{RESULT_TABLE,77},bytes,MF_PAYLOAD};
    r = mf_request(0x2103,&result_row,1); EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_INVALID);
    candidate_mvcc_write_t zero_row = {{1,0},bytes,MF_PAYLOAD};
    r = mf_request(0x2104,&zero_row,1); EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_INVALID);
    expect_unchanged(m,&before);
}

static void check_handles(candidate_mvcc_t *m, const uint8_t *bytes)
{
    candidate_mvcc_txn_t a; begin_write(m,0x2001,ROW_A,bytes,&a);
    candidate_mvcc_info_t before = candidate_mvcc_info(m);
    candidate_mvcc_txn_t other;
    candidate_mvcc_write_t w = mf_write(ROW_A,bytes,MF_PAYLOAD); candidate_mvcc_request_t r = mf_request(0x2002,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,&other),CANDIDATE_WAL_BUSY);
    w = mf_write(ROW_B,bytes,MF_PAYLOAD); r = mf_request(0x2001,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,&other),CANDIDATE_WAL_EXISTS);
    expect_unchanged(m,&before);
    candidate_mvcc_response_t response;
    EXPECT(candidate_mvcc_commit(m,&a,&response),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_mvcc_apply(m,&a),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&a),CANDIDATE_WAL_INVALID);
    candidate_mvcc_txn_t copy = a, foreign = a; foreign.owner_id++;
    EXPECT(candidate_mvcc_apply(m,&foreign),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_mvcc_commit(m,&a,&response),CANDIDATE_WAL_OK);
    CHECK(a.owner_id == 0 && a.generation == 0);
    EXPECT(candidate_mvcc_abort(m,&a),CANDIDATE_WAL_INVALID);
    EXPECT(candidate_mvcc_abort(m,&copy),CANDIDATE_WAL_STALE);
    candidate_mvcc_answer_t q = query_write(m,0x2001,ROW_A,bytes);
    CHECK(q.lookup == CANDIDATE_CATALOG_MATCH && memcmp(q.response.bytes,response.bytes,CANDIDATE_MVCC_RESPONSE_BYTES) == 0);
    q = query_write(m,0x2001,ROW_B,bytes); CHECK(q.lookup == CANDIDATE_CATALOG_CONTENT_MISMATCH);
    q = query_write(m,0x2999,ROW_A,bytes); CHECK(q.lookup == CANDIDATE_CATALOG_NOT_FOUND);
}

static void check_stale(candidate_mvcc_t *m, const uint8_t *bytes)
{
    candidate_mvcc_reader_t r; EXPECT(candidate_mvcc_reader_open(m,&r),CANDIDATE_WAL_OK);
    candidate_mvcc_txn_t c; begin_write(m,0x2201,ROW_B,bytes,&c);
    EXPECT(candidate_mvcc_apply(m,&c),CANDIDATE_WAL_OK);
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&c,&response),CANDIDATE_WAL_OK);
    candidate_mvcc_write_t w = mf_write(ROW_B,bytes,MF_PAYLOAD); candidate_mvcc_request_t req = mf_request(0x2202,&w,1);
    req.snapshot = &r;
    candidate_mvcc_info_t before = candidate_mvcc_info(m);
    candidate_mvcc_txn_t d; EXPECT(candidate_mvcc_begin(m,&req,&d),CANDIDATE_WAL_STALE);
    expect_unchanged(m,&before);
    w = mf_write(ROW_NEW,bytes,MF_PAYLOAD);
    EXPECT(candidate_mvcc_begin(m,&req,&d),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_mvcc_abort(m,&d),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_BUSY);
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_INVALID);
}

static void case_conflict(void)
{
    mf_t f; mf_boot(&f,"conflict");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t bytes[MF_PAYLOAD]; mf_fill(7,bytes);
    check_plans(m,bytes); check_handles(m,bytes); check_stale(m,bytes);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_limits(void)
{
    mf_t f; mf_boot(&f,"limits");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_reader_t readers[CANDIDATE_MVCC_READERS + 1];
    for (unsigned i = 0; i < CANDIDATE_MVCC_READERS; i++) EXPECT(candidate_mvcc_reader_open(m,&readers[i]),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_reader_open(m,&readers[CANDIDATE_MVCC_READERS]),CANDIDATE_WAL_LIMIT);
    unsigned committed = 0;
    uint8_t bytes[MF_PAYLOAD];
    candidate_mvcc_info_t start = candidate_mvcc_info(m);
    candidate_page_ref_t first_append = {0}, last_append = {0};
    while (committed < CANDIDATE_PAGE_MAX_SLOTS) {
        mf_fill((uint8_t)committed,bytes);
        candidate_mvcc_write_t w = mf_write(ROW_A,bytes,MF_PAYLOAD); candidate_mvcc_request_t r = mf_request(0x3000 + committed,&w,1);
        candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
        EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
        candidate_mvcc_txn_info_t info; EXPECT(candidate_mvcc_inspect(m,&t,&info),CANDIDATE_WAL_OK);
        if (committed == 0) first_append = info.versions[0].page;
        last_append = info.versions[0].page;
        candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
        committed++;
    }
    candidate_mvcc_info_t grown = candidate_mvcc_info(m);
    CHECK(grown.structure_groups > start.structure_groups && last_append.id > first_append.id);
    candidate_mvcc_value_t v = read_reader(m,&readers[0],ROW_A); expect_bytes(&v,f.initial[0]);
    candidate_mvcc_reader_t fresh; EXPECT(candidate_mvcc_reader_close(m,&readers[1]),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_reader_open(m,&fresh),CANDIDATE_WAL_OK);
    v = read_reader(m,&fresh,ROW_A); expect_bytes(&v,bytes); readers[1] = fresh;
    printf("CAPACITY versions_beyond_one_page committed=%u appends=%" PRIu64 "->%" PRIu64 " structure_groups=%" PRIu64 " readers=%u\n",
        committed,first_append.id,last_append.id,grown.structure_groups - start.structure_groups,candidate_mvcc_info(m).readers);
    for (unsigned i = 0; i < CANDIDATE_MVCC_READERS; i++) EXPECT(candidate_mvcc_reader_close(m,&readers[i]),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    f.wal_budget_bytes = CANDIDATE_WAL_SEGMENT_BYTES;
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    mf_fill(9,bytes);
    candidate_mvcc_write_t w = mf_write(ROW_B,bytes,MF_PAYLOAD); candidate_mvcc_request_t r = mf_request(0x3999,&w,1);
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_LIMIT);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_oom(void)
{
    mf_t f; mf_boot(&f,"oom");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t bytes[MF_PAYLOAD]; mf_fill(5,bytes);
    candidate_mvcc_txn_t t; begin_write(m,0x4001,ROW_A,bytes,&t);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    unsigned before = mf_faults.allocations;
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
    unsigned used = mf_faults.allocations - before;
    CHECK(used >= 1);
    begin_write(m,0x4002,ROW_B,bytes,&t);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    mf_arm_allocation(1);
    EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_NOMEM);
    mf_disarm();
    CHECK(t.generation != 0 && candidate_mvcc_info(m).fence == CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
    printf("OOM calibrated commit_allocations=%u refused_then_committed csn=%" PRIu64 "\n",used,response.csn);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void check_poisoned(candidate_mvcc_t *m, candidate_wal_status_t cause, const uint8_t *bytes)
{
    CHECK(candidate_mvcc_info(m).fence == cause);
    candidate_mvcc_txn_t t;
    candidate_mvcc_write_t w = mf_write(ROW_NEW,bytes,MF_PAYLOAD); candidate_mvcc_request_t r = mf_request(0x5999,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_POISONED);
    candidate_mvcc_reader_t reader; EXPECT(candidate_mvcc_reader_open(m,&reader),CANDIDATE_WAL_POISONED);
}

static void case_unknown(void)
{
    mf_t f; mf_boot(&f,"unknown");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t bytes[MF_PAYLOAD]; mf_fill(6,bytes);
    candidate_mvcc_txn_t t; begin_write(m,0x5001,ROW_A,bytes,&t);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    unsigned before = mf_faults.fsyncs;
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
    CHECK(mf_faults.fsyncs > before);
    begin_write(m,0x5002,ROW_B,bytes,&t);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    mf_arm_fsync(1);
    EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_IO);
    mf_disarm();
    CHECK(response.state == CANDIDATE_MVCC_UNKNOWN && t.generation == 0);
    check_poisoned(m,CANDIDATE_WAL_IO,bytes);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_answer_t q = query_write(m,0x5002,ROW_B,bytes);
    CHECK(q.lookup == CANDIDATE_CATALOG_MATCH);
    CHECK(q.response.state == CANDIDATE_MVCC_COMMITTED || q.response.state == CANDIDATE_MVCC_ABORTED);
    printf("UNKNOWN force-fault resolved_by_recovery state=%d id=%" PRIu64 "\n",(int)q.response.state,q.response.transaction_id);
    mf_arm_pwrite(1);
    candidate_mvcc_write_t w = mf_write(ROW_NEW,bytes,MF_PAYLOAD); candidate_mvcc_request_t r = mf_request(0x5003,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_IO);
    mf_disarm();
    check_poisoned(m,CANDIDATE_WAL_IO,bytes);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

typedef struct { const char *name; void (*run)(void); } selector_t;

static const selector_t SELECTORS[] = {
    {"overlap",case_overlap},{"restart",case_restart},{"bothcommit",case_bothcommit},{"kill",case_kill},
    {"visibility",case_visibility},{"conflict",case_conflict},{"limits",case_limits},{"oom",case_oom},{"unknown",case_unknown}};

int main(int argc, char **argv)
{
    alarm(600);
    setvbuf(stdout,NULL,_IOLBF,0);
    if (argc != 2) { fprintf(stderr,"usage: %s <selector>\n",argv[0]); return 2; }
    for (unsigned i = 0; i < sizeof(SELECTORS) / sizeof(SELECTORS[0]); i++) {
        if (strcmp(argv[1],SELECTORS[i].name) != 0) continue;
        SELECTORS[i].run();
        printf("PASS %s checks=%u\n",argv[1],checks);
        return 0;
    }
    fprintf(stderr,"unknown selector %s\n",argv[1]);
    return 2;
}
