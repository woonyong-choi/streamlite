#include "test_candidate_mvcc_store.h"

#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/storage/candidate_mvcc_internal.h"

#define BULK_TABLE 7u
#define FILL_NONCE 0x9000u
#define FILL_ROWS 166u
#define SPLIT_ROW 165u
#define B_NONCE 0xb25bu
#define A_NONCE 0xa25au
#define SCAN_RECORDS 4096u
#define THREAD_STACK (8u * 1024u * 1024u)

unsigned checks;

typedef struct {
    candidate_mvcc_t *m;
    candidate_mvcc_txn_t a_txn;
    candidate_mvcc_txn_info_t a_info, b_info;
    candidate_mvcc_response_t b_response;
    uint64_t armed_lsn, right_leaf, new_root;
    uint8_t a_bytes[MF_PAYLOAD];
} split_run_t;

static mf_record_t records[SCAN_RECORDS];
static candidate_store_node_t disk_node, disk_child;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static candidate_mvcc_key_t bulk_key(uint64_t row) { return (candidate_mvcc_key_t){BULK_TABLE,row + 1}; }

static void bulk_bytes(uint64_t row, uint8_t out[MF_PAYLOAD])
{
    for (unsigned i = 0; i < MF_PAYLOAD; i++) out[i] = (uint8_t)(row * 31 + i);
}

static candidate_mvcc_request_t bulk_request(uint64_t nonce, uint64_t first, unsigned count, candidate_mvcc_write_t *w,
    uint8_t bytes[][MF_PAYLOAD])
{
    for (unsigned i = 0; i < count; i++) {
        bulk_bytes(first + i,bytes[i]);
        w[i] = (candidate_mvcc_write_t){bulk_key(first + i),bytes[i],MF_PAYLOAD};
    }
    return mf_request(nonce,w,count);
}

static void insert_rows(candidate_mvcc_t *m, uint64_t nonce, uint64_t first, unsigned count, candidate_mvcc_txn_info_t *info,
    candidate_mvcc_response_t *out)
{
    uint8_t bytes[CANDIDATE_MVCC_WRITES][MF_PAYLOAD]; candidate_mvcc_write_t w[CANDIDATE_MVCC_WRITES];
    CHECK(count >= 1 && count <= CANDIDATE_MVCC_WRITES);
    candidate_mvcc_request_t r = bulk_request(nonce,first,count,w,bytes);
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,&t,info),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(m,&t,out),CANDIDATE_WAL_OK);
    CHECK(out->state == CANDIDATE_MVCC_COMMITTED && out->affected_rows == count && t.generation == 0);
}

static void fill(candidate_mvcc_t *m, uint64_t first, uint64_t rows)
{
    for (uint64_t r = first; r < first + rows; r += CANDIDATE_MVCC_WRITES) {
        uint64_t left = first + rows - r;
        candidate_mvcc_txn_info_t info; candidate_mvcc_response_t response;
        insert_rows(m,FILL_NONCE + r,r,left < CANDIDATE_MVCC_WRITES ? (unsigned)left : CANDIDATE_MVCC_WRITES,&info,&response);
    }
}

static candidate_mvcc_value_t read_key(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader, candidate_mvcc_key_t key)
{
    candidate_mvcc_value_t v; EXPECT(candidate_mvcc_reader_read(m,reader,key,&v),CANDIDATE_WAL_OK); return v;
}

static void expect_bulk(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader, uint64_t row, bool is_present)
{
    candidate_mvcc_value_t v = read_key(m,reader,bulk_key(row));
    uint8_t bytes[MF_PAYLOAD]; bulk_bytes(row,bytes);
    if (!is_present) { CHECK(!v.is_found); return; }
    CHECK(v.is_found && v.size == MF_PAYLOAD && memcmp(v.bytes,bytes,MF_PAYLOAD) == 0);
}

static void expect_rows(candidate_mvcc_t *m, uint64_t first, uint64_t rows, bool is_present)
{
    candidate_mvcc_reader_t r; EXPECT(candidate_mvcc_reader_open(m,&r),CANDIDATE_WAL_OK);
    for (uint64_t row = first; row < first + rows; row++) expect_bulk(m,&r,row,is_present);
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_OK);
}

static candidate_mvcc_store_info_t store_of(candidate_mvcc_t *m)
{
    candidate_mvcc_store_info_t s; EXPECT(candidate_mvcc_store_info(m,&s),CANDIDATE_WAL_OK); return s;
}

static uint64_t wal_end(const mf_t *f)
{
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(f,0,&chain,&wal); return wal.end_lsn;
}

static unsigned split_allocations(const mf_t *f, uint64_t from, unsigned want, uint64_t *ids)
{
    unsigned count = mf_scan_records(f,from,records,SCAN_RECORDS), found = 0, group = 0;
    uint64_t pending[CANDIDATE_BUFFER_GROUP_PAGES];
    bool has_store = false;
    for (unsigned i = 0; i < count; i++) {
        const mf_record_t *r = &records[i];
        if (r->transaction_id != 0) continue;
        if (r->type == 3) { group = 0; has_store = false; }
        if (r->type == 4 && r->page_type == 3) has_store = true;
        if (r->type == 6 && group < CANDIDATE_BUFFER_GROUP_PAGES) pending[group++] = r->words[1];
        if (r->type != 8 || !has_store || group != want || found != 0) continue;
        memcpy(ids,pending,(size_t)group * sizeof(ids[0])); found = group;
    }
    return found;
}

static bool is_page_absent(const mf_t *f, uint64_t id)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_read_page(f,id,page);
    return memcmp(page + 16,"SLPG",4) != 0;
}

static void decode_disk(const mf_t *f, uint64_t id, candidate_store_node_t *out)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_read_page(f,id,page);
    EXPECT(candidate_store_decode(fx_uuid,page,sizeof(page),out),CANDIDATE_WAL_OK);
    CHECK(read_le(page + 24,8) == id);
}

static candidate_recovery_info_t recover_store(mf_t *f)
{
    candidate_recovery_options_t o = fx_options(f->built.fx,f->next_owner++);
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    printf("RECOVER owner=%" PRIu64 " status=%s\n",o.owner_epoch,mf_status(status));
    EXPECT(status,CANDIDATE_WAL_OK);
    candidate_recovery_info_t info = candidate_recovery_info(r);
    EXPECT(candidate_recovery_close(&r),CANDIDATE_WAL_OK);
    return info;
}

static uint32_t check_disk_tree(const mf_t *f, const candidate_mvcc_store_info_t *s)
{
    decode_disk(f,s->descriptor.root.id,&disk_node);
    CHECK(disk_node.kind == CANDIDATE_STORE_BRANCH && disk_node.level == 1 && disk_node.count == s->root_count);
    uint32_t entries = 0;
    candidate_store_key_t last = {0,0};
    for (uint32_t i = 0; i < disk_node.count; i++) {
        decode_disk(f,disk_node.children[i].child.id,&disk_child);
        CHECK(disk_child.kind == CANDIDATE_STORE_LEAF && disk_child.count > 0);
        CHECK(candidate_store_compare(disk_child.entries[0].key,disk_node.children[i].low) >= 0);
        CHECK(candidate_store_compare(disk_child.entries[0].key,last) > 0);
        bool has_next = i + 1 < disk_node.count;
        if (has_next) CHECK(candidate_store_compare(disk_child.entries[disk_child.count - 1].key,disk_node.children[i + 1].low) < 0);
        last = disk_child.entries[disk_child.count - 1].key; entries += disk_child.count;
    }
    return entries;
}

static void case_split_growth(void)
{
    mf_t f; mf_boot(&f,"split-growth");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_store_info_t before = store_of(m);
    CHECK(before.descriptor.height == 1 && before.root_level == 0 && before.root_count == 2);
    candidate_mvcc_reader_t old; EXPECT(candidate_mvcc_reader_open(m,&old),CANDIDATE_WAL_OK);
    mf_chain_t chain; mf_wal_t wal0, wal1; mf_scan_wal(&f,0,&chain,&wal0);
    uint64_t rows = CANDIDATE_STORE_LEAF_ENTRIES + 12;
    fill(m,0,rows);
    candidate_mvcc_store_info_t after = store_of(m);
    candidate_page_ref_t store = candidate_mvcc_info(m).store;
    mf_scan_wal(&f,0,&chain,&wal1);
    printf("GROWTH rows=%" PRIu64 " height=%u->%u root=%" PRIu64 "->%" PRIu64 " root_count=%u structures=%u->%u allocations=%u->%u\n",
        rows,before.descriptor.height,after.descriptor.height,before.descriptor.root.id,after.descriptor.root.id,after.root_count,
        wal0.structures,wal1.structures,wal0.allocations,wal1.allocations);
    CHECK(after.descriptor.height == 2 && after.root_level == 1 && after.root_count >= 2);
    CHECK(after.descriptor.root.id != before.descriptor.root.id && after.descriptor_lsn > before.descriptor_lsn);
    CHECK(wal1.structures > wal0.structures && wal1.allocations >= wal0.allocations + 2);
    for (uint64_t row = 0; row < rows; row++) expect_bulk(m,&old,row,false);
    candidate_mvcc_value_t v = read_key(m,&old,mf_key(0)); mf_expect_initial(&f,&v,0);
    expect_rows(m,0,rows,true);
    EXPECT(candidate_mvcc_reader_close(m,&old),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_store_info_t reopened = store_of(m);
    CHECK(candidate_mvcc_info(m).store.id == store.id && reopened.descriptor.root.id == after.descriptor.root.id);
    CHECK(reopened.descriptor.height == 2 && reopened.root_count == after.root_count);
    expect_rows(m,0,rows,true);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    CHECK(check_disk_tree(&f,&after) == rows + 2);
    candidate_recovery_info_t recovered = recover_store(&f);
    CHECK(recovered.store_root.id == store.id && recovered.anchor.catalog_root.id == store.id);
    CHECK(recovered.store_pages == 2 + after.root_count);
    printf("GROWTH-REOPEN store=%" PRIu64 " pages=%u entries=%" PRIu64 "\n",store.id,recovered.store_pages,rows + 2);
    mf_free(&f);
}

static void start_active(split_run_t *s, const mf_t *f)
{
    fill(s->m,0,FILL_ROWS);
    mf_fill(0xa5,s->a_bytes);
    candidate_mvcc_write_t w = {bulk_key(SPLIT_ROW),s->a_bytes,MF_PAYLOAD};
    candidate_mvcc_request_t r = mf_request(A_NONCE,&w,1);
    EXPECT(candidate_mvcc_begin(s->m,&r,&s->a_txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(s->m,&s->a_txn),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(s->m,&s->a_txn,&s->a_info),CANDIDATE_WAL_OK);
    CHECK(s->a_info.state == CANDIDATE_MVCC_ACTIVE && s->a_info.is_applied);
    CHECK(store_of(s->m).descriptor.height == 1);
    s->armed_lsn = wal_end(f);
}

static void insert_b(split_run_t *s)
{
    insert_rows(s->m,B_NONCE,FILL_ROWS,CANDIDATE_MVCC_WRITES,&s->b_info,&s->b_response);
}

static void *run_b(void *context)
{
    insert_b(context);
    return NULL;
}

static void run_split_thread(split_run_t *s)
{
    pthread_attr_t attr; CHECK(pthread_attr_init(&attr) == 0);
    CHECK(pthread_attr_setstacksize(&attr,THREAD_STACK) == 0);
    pthread_t thread; CHECK(pthread_create(&thread,&attr,run_b,s) == 0);
    CHECK(pthread_join(thread,NULL) == 0);
    CHECK(pthread_attr_destroy(&attr) == 0);
}

static void find_split(const mf_t *f, split_run_t *s)
{
    uint64_t ids[2] = {0};
    CHECK(split_allocations(f,s->armed_lsn,2,ids) == 2);
    s->right_leaf = ids[0]; s->new_root = ids[1];
    printf("SPLIT armed=%" PRIu64 " right_leaf=%" PRIu64 " new_root=%" PRIu64 "\n",s->armed_lsn,s->right_leaf,s->new_root);
}

static void expect_b_response(candidate_mvcc_t *m, const split_run_t *s)
{
    uint8_t bytes[CANDIDATE_MVCC_WRITES][MF_PAYLOAD]; candidate_mvcc_write_t w[CANDIDATE_MVCC_WRITES];
    candidate_mvcc_request_t r = bulk_request(B_NONCE,FILL_ROWS,CANDIDATE_MVCC_WRITES,w,bytes);
    candidate_mvcc_answer_t a; EXPECT(candidate_mvcc_query(m,&r,&a),CANDIDATE_WAL_OK);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.state == CANDIDATE_MVCC_COMMITTED);
    CHECK(a.response.csn == s->b_response.csn && a.response.size == s->b_response.size);
    CHECK(memcmp(a.response.bytes,s->b_response.bytes,s->b_response.size) == 0 && a.response.affected_rows == CANDIDATE_MVCC_WRITES);
    candidate_mvcc_write_t aw = {bulk_key(SPLIT_ROW),s->a_bytes,MF_PAYLOAD};
    candidate_mvcc_request_t ar = mf_request(A_NONCE,&aw,1);
    EXPECT(candidate_mvcc_query(m,&ar,&a),CANDIDATE_WAL_OK);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.state == CANDIDATE_MVCC_ABORTED);
    CHECK(a.response.transaction_id == s->a_info.transaction_id);
}

static void expect_after_abort(candidate_mvcc_t *m)
{
    expect_rows(m,0,FILL_ROWS,true);
    expect_rows(m,FILL_ROWS,CANDIDATE_MVCC_WRITES,true);
    CHECK(store_of(m).descriptor.height == 2);
}

static void expect_moved_head(const mf_t *f, const split_run_t *s)
{
    decode_disk(f,s->right_leaf,&disk_node);
    uint32_t at;
    CHECK(disk_node.kind == CANDIDATE_STORE_LEAF);
    CHECK(mvcc_leaf_find(&disk_node,mvcc_store_key(bulk_key(SPLIT_ROW)),&at));
    const candidate_store_entry_t *e = &disk_node.entries[at];
    CHECK(e->head_transaction == s->a_info.transaction_id && e->head.page.id == s->a_info.versions[0].page.id);
    CHECK(e->head.slot == s->a_info.versions[0].slot);
    CHECK(mvcc_leaf_find(&disk_node,mvcc_store_key(bulk_key(FILL_ROWS)),&at));
    CHECK(disk_node.entries[at].head_transaction == s->b_info.transaction_id);
    uint8_t page[CANDIDATE_PAGE_BYTES], original[MF_PAYLOAD]; mf_slot_t a, prior;
    mf_read_version(f,s->a_info.versions[0],page,&a);
    CHECK(a.transaction_id == s->a_info.transaction_id && memcmp(a.bytes,s->a_bytes,MF_PAYLOAD) == 0);
    mf_read_version(f,a.previous,page,&prior);
    bulk_bytes(SPLIT_ROW,original);
    CHECK(prior.row_id == bulk_key(SPLIT_ROW).row_id && memcmp(prior.bytes,original,MF_PAYLOAD) == 0);
}

static void check_no_steal(const mf_t *f, split_run_t *s, bool is_active)
{
    candidate_buffer_flush_t flush;
    EXPECT(candidate_buffer_flush(s->m->pool,CANDIDATE_BUFFER_GROUP_PAGES,&flush),CANDIDATE_WAL_OK);
    printf("FLUSH active=%d written=%u unresolved=%u pinned=%u busy=%u right_leaf_on_disk=%d\n",is_active,flush.written,
        flush.unresolved,flush.pinned,flush.busy,!is_page_absent(f,s->right_leaf));
    if (is_active) { CHECK(flush.unresolved >= 2 && is_page_absent(f,s->right_leaf)); return; }
    CHECK(flush.unresolved == 0 && flush.written >= 2 && !is_page_absent(f,s->right_leaf));
}

static void case_split_active(void)
{
    mf_t f; mf_boot(&f,"split-active");
    split_run_t s = {0};
    EXPECT(mf_open(&f,&s.m),CANDIDATE_WAL_OK);
    start_active(&s,&f);
    candidate_mvcc_reader_t old; EXPECT(candidate_mvcc_reader_open(s.m,&old),CANDIDATE_WAL_OK);
    run_split_thread(&s);
    candidate_mvcc_txn_info_t live; EXPECT(candidate_mvcc_inspect(s.m,&s.a_txn,&live),CANDIDATE_WAL_OK);
    CHECK(live.state == CANDIDATE_MVCC_ACTIVE && live.is_applied && s.b_response.csn != 0);
    CHECK(store_of(s.m).descriptor.height == 2);
    find_split(&f,&s);
    check_no_steal(&f,&s,true);
    candidate_mvcc_value_t v; EXPECT(candidate_mvcc_read(s.m,&s.a_txn,bulk_key(SPLIT_ROW),&v),CANDIDATE_WAL_OK);
    CHECK(v.is_found && memcmp(v.bytes,s.a_bytes,MF_PAYLOAD) == 0 && v.transaction_id == s.a_info.transaction_id);
    expect_bulk(s.m,&old,SPLIT_ROW,true); expect_bulk(s.m,&old,FILL_ROWS,false);
    expect_rows(s.m,SPLIT_ROW,1,true); expect_rows(s.m,FILL_ROWS,CANDIDATE_MVCC_WRITES,true);
    EXPECT(candidate_mvcc_abort(s.m,&s.a_txn),CANDIDATE_WAL_OK);
    check_no_steal(&f,&s,false);
    expect_after_abort(s.m);
    expect_bulk(s.m,&old,FILL_ROWS,false);
    EXPECT(candidate_mvcc_reader_close(s.m,&old),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&s.m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&s.m),CANDIDATE_WAL_OK);
    expect_after_abort(s.m); expect_b_response(s.m,&s);
    EXPECT(candidate_mvcc_close(&s.m),CANDIDATE_WAL_OK);
    expect_moved_head(&f,&s);
    mf_free(&f);
}

static void child_split(mf_t *f, int fd)
{
    split_run_t s = {0};
    EXPECT(mf_open(f,&s.m),CANDIDATE_WAL_OK);
    start_active(&s,f);
    insert_b(&s);
    CHECK(store_of(s.m).descriptor.height == 2);
    s.m = NULL;
    CHECK(write(fd,&s,sizeof(s)) == (ssize_t)sizeof(s));
    for (;;) pause();
}

static void case_split_kill(void)
{
    mf_t f; mf_boot(&f,"split-kill");
    int fds[2]; CHECK(pipe(fds) == 0);
    fflush(stdout);
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) { CHECK(close(fds[0]) == 0); child_split(&f,fds[1]); _exit(1); }
    CHECK(close(fds[1]) == 0);
    split_run_t s; size_t got = 0;
    while (got < sizeof(s)) {
        ssize_t n = read(fds[0],(uint8_t *)&s + got,sizeof(s) - got);
        CHECK(n > 0); got += (size_t)n;
    }
    CHECK(kill(pid,SIGKILL) == 0);
    int status = 0; CHECK(waitpid(pid,&status,0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(close(fds[0]) == 0);
    printf("KILLED child=%ld after B durable split a=%" PRIu64 " b=%" PRIu64 " csn=%" PRIu64 "\n",(long)pid,s.a_info.transaction_id,
        s.b_info.transaction_id,s.b_response.csn);
    find_split(&f,&s);
    f.next_owner++;
    EXPECT(mf_open(&f,&s.m),CANDIDATE_WAL_OK);
    expect_after_abort(s.m); expect_b_response(s.m,&s);
    EXPECT(candidate_mvcc_close(&s.m),CANDIDATE_WAL_OK);
    expect_moved_head(&f,&s);
    mf_free(&f);
}

static void case_split_abort(void)
{
    mf_t f; mf_boot(&f,"split-abort");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    fill(m,0,FILL_ROWS - 1);
    candidate_mvcc_reader_t old; EXPECT(candidate_mvcc_reader_open(m,&old),CANDIDATE_WAL_OK);
    uint8_t bytes[CANDIDATE_MVCC_WRITES][MF_PAYLOAD]; candidate_mvcc_write_t w[CANDIDATE_MVCC_WRITES];
    candidate_mvcc_request_t r = bulk_request(0xab07,FILL_ROWS,CANDIDATE_MVCC_WRITES,w,bytes);
    r.snapshot = &old;
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
    uint64_t armed = wal_end(&f);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    uint64_t ids[2] = {0};
    CHECK(split_allocations(&f,armed,2,ids) == 2 && store_of(m).descriptor.height == 2);
    for (unsigned i = 0; i < CANDIDATE_MVCC_WRITES; i++) {
        candidate_mvcc_value_t v; EXPECT(candidate_mvcc_read(m,&t,bulk_key(FILL_ROWS + i),&v),CANDIDATE_WAL_OK);
        CHECK(v.is_found && memcmp(v.bytes,bytes[i],MF_PAYLOAD) == 0 && v.csn == 0);
        expect_bulk(m,&old,FILL_ROWS + i,false);
    }
    expect_bulk(m,&old,0,true);
    EXPECT(candidate_mvcc_abort(m,&t),CANDIDATE_WAL_OK);
    expect_rows(m,FILL_ROWS,CANDIDATE_MVCC_WRITES,false);
    expect_rows(m,0,FILL_ROWS - 1,true);
    EXPECT(candidate_mvcc_reader_close(m,&old),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    CHECK(store_of(m).descriptor.height == 2 && store_of(m).descriptor.root.id == ids[1]);
    expect_rows(m,FILL_ROWS,CANDIDATE_MVCC_WRITES,false);
    expect_rows(m,0,FILL_ROWS - 1,true);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    printf("SPLIT-ABORT structure kept root=%" PRIu64 " right_leaf=%" PRIu64 " after logical abort\n",ids[1],ids[0]);
    mf_free(&f);
}

static bool is_store_publication(void *context, const void *bytes, size_t size)
{
    (void)context;
    const uint8_t *p = bytes;
    return size == CANDIDATE_CONTROL_BYTES && memcmp(p,"SLCT",4) == 0 && read_le(p + 152,8) != 0;
}

static void case_bootstrap_adopt(void)
{
    mf_t f; mf_boot_highwater(&f,"bootstrap-adopt",0);
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(&f,0,&chain,&wal);
    CHECK(wal.structures == 0);
    mf_arm_pwrite_fault(is_store_publication,NULL);
    candidate_mvcc_t *m = NULL;
    candidate_wal_status_t status = mf_open(&f,&m);
    mf_disarm();
    CHECK(status != CANDIDATE_WAL_OK && m == NULL);
    uint64_t ids[3] = {0};
    CHECK(split_allocations(&f,0,3,ids) == 3);
    mf_scan_wal(&f,0,&chain,&wal);
    CHECK(wal.structures == 1 && wal.allocations == 3);
    candidate_recovery_info_t pending = recover_store(&f);
    printf("BOOTSTRAP-PENDING open=%s descriptor=%" PRIu64 " pending=%" PRIu64 " published=%" PRIu64 "\n",mf_status(status),ids[0],
        pending.store_root.id,pending.anchor.catalog_root.id);
    CHECK(pending.anchor.catalog_root.id == 0 && pending.store_root.id == ids[0] && pending.store_pages == 2);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    CHECK(candidate_mvcc_info(m).store.id == ids[0]);
    candidate_mvcc_txn_info_t info; candidate_mvcc_response_t response;
    insert_rows(m,0xad09,0,2,&info,&response);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_scan_wal(&f,0,&chain,&wal);
    CHECK(wal.structures == 1);
    candidate_recovery_info_t published = recover_store(&f);
    CHECK(published.anchor.catalog_root.id == ids[0] && published.store_root.id == ids[0]);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    expect_rows(m,0,2,true);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void tamper_control(const mf_t *f, const uint8_t *original, candidate_page_ref_t root)
{
    uint8_t bytes[CANDIDATE_CONTROL_FILE_BYTES];
    memcpy(bytes,original,sizeof(bytes));
    for (unsigned slot = 0; slot < 2; slot++) {
        candidate_control_state_t state;
        uint8_t *p = bytes + slot * CANDIDATE_CONTROL_BYTES;
        if (candidate_control_decode(fx_uuid,p,CANDIDATE_CONTROL_BYTES,&state) != CANDIDATE_WAL_OK) continue;
        state.catalog_root = root;
        OK(candidate_control_encode(&state,p,CANDIDATE_CONTROL_BYTES));
    }
    int fd = open(f->built.fx->control,O_WRONLY); CHECK(fd >= 0);
    CHECK(pwrite(fd,bytes,sizeof(bytes),0) == (ssize_t)sizeof(bytes) && fsync(fd) == 0 && close(fd) == 0);
}

static candidate_wal_status_t open_recovery(mf_t *f)
{
    candidate_recovery_options_t o = fx_options(f->built.fx,f->next_owner);
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_open(&o,&r);
    if (status == CANDIDATE_WAL_OK) EXPECT(candidate_recovery_close(&r),CANDIDATE_WAL_OK);
    CHECK(status != CANDIDATE_WAL_OK || r == NULL);
    return status;
}

static void case_store_control(void)
{
    mf_t f; mf_boot(&f,"store-control");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_store_info_t s = store_of(m);
    candidate_page_ref_t store = candidate_mvcc_info(m).store;
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    uint8_t original[CANDIDATE_CONTROL_FILE_BYTES];
    int fd = open(f.built.fx->control,O_RDONLY); CHECK(fd >= 0);
    CHECK(pread(fd,original,sizeof(original),0) == (ssize_t)sizeof(original) && close(fd) == 0);
    const candidate_page_ref_t variants[3] = {s.descriptor.root,{store.id,store.generation + 1},s.descriptor.append};
    const char *labels[3] = {"leaf-as-descriptor","descriptor-generation","append-as-descriptor"};
    for (unsigned i = 0; i < 3; i++) {
        tamper_control(&f,original,variants[i]);
        candidate_wal_status_t status = open_recovery(&f);
        printf("CONTROL %s root=%" PRIu64 "/%" PRIu64 " open=%s\n",labels[i],variants[i].id,variants[i].generation,mf_status(status));
        EXPECT(status,CANDIDATE_WAL_CORRUPT);
    }
    fd = open(f.built.fx->control,O_WRONLY); CHECK(fd >= 0);
    CHECK(pwrite(fd,original,sizeof(original),0) == (ssize_t)sizeof(original) && fsync(fd) == 0 && close(fd) == 0);
    EXPECT(open_recovery(&f),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    CHECK(candidate_mvcc_info(m).store.id == store.id);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

typedef struct {
    uint16_t type;
    unsigned allocations;
    bool has_end;
} frame_fault_t;

static bool decode_frame(const void *bytes, size_t size, candidate_wal_record_t *out)
{
    const uint8_t *p = bytes;
    if (size < 96 || read_le(p + 8,4) != size) return false;
    return candidate_wal_decode(fx_uuid,p,size,out) == CANDIDATE_WAL_OK;
}

static void count_split_frame(frame_fault_t *h, const candidate_wal_record_t *r)
{
    if (r->type == 3) h->allocations = 0;
    if (r->type == 6) h->allocations++;
}

static bool is_structure_frame(void *context, const void *bytes, size_t size)
{
    frame_fault_t *h = context; candidate_wal_record_t r;
    if (!decode_frame(bytes,size,&r) || r.transaction_id != 0) return false;
    count_split_frame(h,&r);
    return r.type == h->type && h->allocations >= 2;
}

static bool observe_structure_end(void *context, const void *bytes, size_t size)
{
    frame_fault_t *h = context; candidate_wal_record_t r;
    if (!decode_frame(bytes,size,&r) || r.transaction_id != 0) return false;
    count_split_frame(h,&r);
    h->has_end = r.type == 8 && h->allocations >= 2;
    return h->has_end;
}

static bool is_structure_force(void *context) { return ((frame_fault_t *)context)->has_end; }

static void arm_fault(frame_fault_t *h)
{
    if (h->type != 0) { mf_arm_pwrite_fault(is_structure_frame,h); return; }
    mf_arm_pwrite_hook(observe_structure_end,h); mf_arm_fsync_fault(is_structure_force,h);
}

static void case_split_fault(const char *tag, uint16_t type, uint32_t expected_height)
{
    mf_t f; mf_boot(&f,tag);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    fill(m,0,FILL_ROWS - 1);
    uint8_t bytes[CANDIDATE_MVCC_WRITES][MF_PAYLOAD]; candidate_mvcc_write_t w[CANDIDATE_MVCC_WRITES];
    candidate_mvcc_request_t r = bulk_request(0xfa17,FILL_ROWS,CANDIDATE_MVCC_WRITES,w,bytes);
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
    frame_fault_t h = {.type = type};
    arm_fault(&h);
    candidate_wal_status_t status = candidate_mvcc_apply(m,&t);
    mf_disarm(); mf_arm_pwrite_hook(NULL,NULL);
    candidate_mvcc_info_t info = candidate_mvcc_info(m);
    printf("SPLIT-FAULT %s status=%s fence=%s pool=%d allocations=%u end=%d\n",tag,mf_status(status),mf_status(info.fence),
        candidate_buffer_info(m->pool).is_fenced,h.allocations,h.has_end);
    EXPECT(status,CANDIDATE_WAL_IO);
    CHECK(info.fence == CANDIDATE_WAL_IO && info.active == 0 && candidate_buffer_info(m->pool).is_fenced);
    candidate_mvcc_reader_t reader; EXPECT(candidate_mvcc_reader_open(m,&reader),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_answer_t a; EXPECT(candidate_mvcc_query(m,&r,&a),CANDIDATE_WAL_OK);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.state == CANDIDATE_MVCC_ABORTED);
    CHECK(store_of(m).descriptor.height == expected_height);
    expect_rows(m,FILL_ROWS,CANDIDATE_MVCC_WRITES,false);
    expect_rows(m,0,FILL_ROWS - 1,true);
    fill(m,FILL_ROWS,CANDIDATE_MVCC_WRITES);
    CHECK(store_of(m).descriptor.height == 2);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_alloc_fault(void) { case_split_fault("split-alloc-fault",6,1); }
static void case_end_fault(void) { case_split_fault("split-end-fault",8,1); }
static void case_force_fault(void) { case_split_fault("split-force-fault",0,2); }

static void expect_untouched(const mf_t *f, candidate_mvcc_t *m, uint64_t end, uint64_t groups)
{
    CHECK(wal_end(f) == end && candidate_mvcc_info(m).structure_groups == groups);
    CHECK(candidate_mvcc_info(m).fence == CANDIDATE_WAL_OK && !candidate_buffer_info(m->pool).is_fenced);
}

static void case_split_nomem(void)
{
    mf_t f; mf_boot(&f,"split-nomem");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    fill(m,0,FILL_ROWS - 1);
    uint8_t bytes[CANDIDATE_MVCC_WRITES][MF_PAYLOAD]; candidate_mvcc_write_t w[CANDIDATE_MVCC_WRITES];
    candidate_mvcc_request_t r = bulk_request(0x7e11,FILL_ROWS,CANDIDATE_MVCC_WRITES,w,bytes);
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
    uint64_t end = wal_end(&f), groups = candidate_mvcc_info(m).structure_groups;
    mf_arm_allocation(1);
    candidate_wal_status_t status = candidate_mvcc_apply(m,&t);
    mf_disarm();
    printf("SPLIT-NOMEM status=%s end=%" PRIu64 " groups=%" PRIu64 "\n",mf_status(status),end,groups);
    EXPECT(status,CANDIDATE_WAL_NOMEM);
    expect_untouched(&f,m,end,groups);
    candidate_mvcc_txn_info_t info; EXPECT(candidate_mvcc_inspect(m,&t,&info),CANDIDATE_WAL_OK);
    CHECK(info.state == CANDIDATE_MVCC_ACTIVE && !info.is_applied && store_of(m).descriptor.height == 1);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    CHECK(store_of(m).descriptor.height == 2 && candidate_mvcc_info(m).structure_groups > groups);
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
    expect_rows(m,FILL_ROWS,CANDIDATE_MVCC_WRITES,true);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    f.wal_budget_bytes = CANDIDATE_WAL_SEGMENT_BYTES;
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    end = wal_end(&f); groups = candidate_mvcc_info(m).structure_groups;
    r = bulk_request(0x7e12,FILL_ROWS + CANDIDATE_MVCC_WRITES,CANDIDATE_MVCC_WRITES,w,bytes);
    EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_LIMIT);
    CHECK(t.generation == 0 && candidate_mvcc_info(m).active == 0);
    expect_untouched(&f,m,end,groups);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void expect_refused_structure(const mf_t *f, candidate_mvcc_t *m, const char *label, const candidate_buffer_page_t *pages,
    uint32_t count, uint32_t allocated, candidate_wal_status_t expected)
{
    uint64_t end = wal_end(f);
    candidate_buffer_structure_t s = {pages,count,allocated};
    candidate_buffer_admission_t receipt;
    candidate_wal_status_t status = candidate_buffer_write_structure(m->pool,&s,&receipt);
    printf("STRUCTURE %s status=%s group=%" PRIu64 "\n",label,mf_status(status),receipt.group_id);
    EXPECT(status,expected);
    CHECK(receipt.group_id == 0 && wal_end(f) == end && !candidate_buffer_info(m->pool).is_fenced);
}

static void encode_leaf(uint64_t id, uint64_t first_row, bool is_sorted, uint8_t page[CANDIDATE_PAGE_BYTES])
{
    memset(&disk_node,0,sizeof(disk_node));
    disk_node.kind = CANDIDATE_STORE_LEAF; disk_node.count = 2;
    disk_node.entries[0].key = (candidate_store_key_t){BULK_TABLE,first_row};
    disk_node.entries[1].key = (candidate_store_key_t){BULK_TABLE,first_row + 1};
    candidate_page_location_t location = {.ref = {id,1}};
    memcpy(location.uuid,fx_uuid,16);
    OK(candidate_store_encode(&location,&disk_node,page,CANDIDATE_PAGE_BYTES));
    if (is_sorted) return;
    page[64 + 16 + 8] = (uint8_t)(first_row + 1); page[64 + 16 + 48 + 8] = (uint8_t)first_row;
}

static void case_structure_refusal(void)
{
    mf_t f; mf_boot(&f,"structure-refusal");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint64_t next = candidate_buffer_info(m->pool).page_highwater + 1;
    static uint8_t pages[2][CANDIDATE_PAGE_BYTES];
    encode_leaf(next,500,false,pages[0]);
    candidate_buffer_page_t unsorted = {{next,1},pages[0]};
    expect_refused_structure(&f,m,"unsorted-leaf-body",&unsorted,1,1,CANDIDATE_WAL_INVALID);
    encode_leaf(next + 1,500,true,pages[1]);
    candidate_buffer_page_t skipped = {{next + 1,1},pages[1]};
    expect_refused_structure(&f,m,"noncontiguous-id",&skipped,1,1,CANDIDATE_WAL_INVALID);
    expect_refused_structure(&f,m,"no-allocation",&skipped,1,0,CANDIDATE_WAL_INVALID);
    candidate_buffer_info_t pool = candidate_buffer_info(m->pool);
    uint8_t root[CANDIDATE_PAGE_BYTES]; mf_read_page(&f,pool.anchor.outcome_root.id,root);
    candidate_buffer_page_t metadata[2] = {{pool.anchor.outcome_root,root},{{next,1},pages[1]}};
    encode_leaf(next,500,true,pages[1]);
    expect_refused_structure(&f,m,"metadata-page",metadata,2,1,CANDIDATE_WAL_INVALID);
    candidate_buffer_page_t orphan = {{next,1},pages[1]};
    candidate_buffer_structure_t s = {&orphan,1,1};
    candidate_buffer_admission_t receipt;
    candidate_wal_status_t status = candidate_buffer_write_structure(m->pool,&s,&receipt);
    printf("STRUCTURE orphan-leaf status=%s fenced=%d\n",mf_status(status),candidate_buffer_info(m->pool).is_fenced);
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(candidate_buffer_info(m->pool).is_fenced && receipt.group_id == 0);
    printf("STRUCTURE orphan-leaf close=%s\n",mf_status(candidate_mvcc_close(&m)));
    status = mf_open(&f,&m);
    printf("STRUCTURE orphan-leaf reopen=%s\n",mf_status(status));
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(m == NULL);
    mf_free(&f);
}

typedef struct { const char *name; void (*run)(void); } selector_t;

static const selector_t SELECTORS[] = {
    {"codec",case_store_codec},{"graph",case_store_graph},{"split-growth",case_split_growth},{"split-active",case_split_active},
    {"split-kill",case_split_kill},{"split-abort",case_split_abort},{"bootstrap-adopt",case_bootstrap_adopt},
    {"store-control",case_store_control},{"split-alloc-fault",case_alloc_fault},{"split-end-fault",case_end_fault},
    {"split-force-fault",case_force_fault},{"split-nomem",case_split_nomem},{"structure-refusal",case_structure_refusal}};

int main(int argc, char **argv)
{
    alarm(900);
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
