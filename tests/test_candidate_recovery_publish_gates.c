#include "test_candidate_recovery_publish_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_wal_witness.h"
#include "../src/storage/candidate_recovery_internal.h"

unsigned checks;

enum { K_WAL, K_DATA, K_CONTROL, K_KINDS };
typedef struct {
    ino_t data, control;
    unsigned writes[K_KINDS], syncs[K_KINDS], truncates, allocs;
    int fail_kind, fail_sync_kind, stop_kind, stop_sync_kind;
    unsigned fail_write_at, fail_sync_at, stop_write_at, stop_sync_at, fail_alloc_at, fail_read_at, data_reads;
    unsigned mutations, fail_mutation_at;
    bool torn, stop_after_sync, short_io, interrupt_io, interrupted;
    size_t live, peak;
} faults_t;
static faults_t io;
typedef struct { uint8_t kind, sync; uint16_t type; unsigned count; } step_t;
static struct { bool on; unsigned count; step_t steps[16384]; } trace;

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }
static int kind_of(int fd) { ino_t i = inode_of(fd); return i == io.data ? K_DATA : i == io.control ? K_CONTROL : K_WAL; }

static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset)
{
    if (io.interrupt_io && !io.interrupted) { io.interrupted = true; errno = EINTR; return -1; }
    io.interrupted = false;
    if (kind_of(fd) == K_DATA && io.fail_read_at != 0 && ++io.data_reads == io.fail_read_at) { errno = EIO; return -1; }
    return pread(fd,p,io.short_io && n > 19 ? 19 : n,offset);
}

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    if (io.interrupt_io && !io.interrupted) { io.interrupted = true; errno = EINTR; return -1; }
    io.interrupted = false;
    if (io.fail_mutation_at != 0 && ++io.mutations == io.fail_mutation_at) { errno = EIO; return -1; }
    int k = kind_of(fd); unsigned count = ++io.writes[k];
    if (trace.on && trace.count < 16384) {
        const uint8_t *b = p; uint16_t type = n >= 8 && memcmp(b,"SLWR",4) == 0 ? (uint16_t)(b[6] | b[7] << 8) : 0;
        trace.steps[trace.count++] = (step_t){(uint8_t)k,0,type,count};
    }
    if (k == io.fail_kind && count == io.fail_write_at) {
        if (io.torn && n > 17) CHECK(pwrite(fd,p,17,offset) == 17);
        errno = ENOSPC; return -1;
    }
    if (k == io.stop_kind && count == io.stop_write_at) { if (n > 17) CHECK(pwrite(fd,p,17,offset) == 17); _exit(78); }
    return pwrite(fd,p,io.short_io && n > 31 ? 31 : n,offset);
}

static int hook_fsync(int fd)
{
    int k = kind_of(fd); unsigned count = ++io.syncs[k];
    if (trace.on && trace.count < 16384) trace.steps[trace.count++] = (step_t){(uint8_t)k,1,0,count};
    if (k == io.fail_sync_kind && count == io.fail_sync_at) { errno = EIO; return -1; }
    if (k == io.stop_sync_kind && count == io.stop_sync_at && !io.stop_after_sync) _exit(79);
    int value = fsync(fd);
    if (k == io.stop_sync_kind && count == io.stop_sync_at && io.stop_after_sync) _exit(80);
    return value;
}

static int hook_truncate(int fd, off_t size)
{
    if (io.fail_mutation_at != 0 && ++io.mutations == io.fail_mutation_at) { errno = EIO; return -1; }
    io.truncates++; return ftruncate(fd,size);
}
static const candidate_wal_io_t hooks = {hook_pread,hook_pwrite,hook_fsync,hook_truncate};

static void *allocate(void *context, size_t size)
{
    (void)context;
    if (io.fail_alloc_at != 0 && ++io.allocs == io.fail_alloc_at) return NULL;
    if (io.fail_alloc_at == 0) io.allocs++;
    void *p = malloc(size); if (p != NULL) { io.live += size; if (io.live > io.peak) io.peak = io.live; }
    return p;
}
static void release(void *context, void *p, size_t size) { (void)context; CHECK(io.live >= size); io.live -= size; free(p); }
static const candidate_wal_memory_t memory = {NULL,allocate,release};

static void io_reset(const fx_t *fx)
{
    memset(&io,0,sizeof(io)); io.fail_kind = io.fail_sync_kind = io.stop_kind = io.stop_sync_kind = -1;
    struct stat st; CHECK(stat(fx->data,&st) == 0); io.data = st.st_ino;
    CHECK(stat(fx->control,&st) == 0); io.control = st.st_ino;
}

static unsigned io_writes(void) { return io.writes[K_WAL] + io.writes[K_DATA] + io.writes[K_CONTROL] + io.truncates; }

static candidate_wal_status_t recover_with(const fx_t *fx, uint64_t owner, candidate_recovery_t **out)
{
    candidate_recovery_options_t o = fx_options(fx,owner); o.io = &hooks; o.memory = &memory;
    return candidate_recovery_recover(&o,out);
}

static candidate_wal_status_t recover(const fx_t *fx, uint64_t owner, candidate_recovery_t **out)
{
    io_reset(fx); return recover_with(fx,owner,out);
}

static const char *name_of(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

typedef struct { uint8_t control[32], data[32], wal[64][32]; unsigned segments; } snapshot_t;
static snapshot_t take(const fx_t *fx)
{
    snapshot_t s = {0}; fx_sha256_file(fx->control,s.control); fx_sha256_file(fx->data,s.data);
    for (unsigned id = 0; id < 64; id++) {
        char name[300]; CHECK(snprintf(name,sizeof(name),"%s.wal.%016" PRIx64,fx->wal,(uint64_t)id) > 0);
        if (access(name,F_OK) != 0) break;
        fx_sha256_file(name,s.wal[id]); s.segments++;
    }
    return s;
}
static bool same(const snapshot_t *a, const snapshot_t *b) { return memcmp(a,b,sizeof(*a)) == 0; }

static void receipt(const char *tag, candidate_recovery_t *r)
{
    candidate_recovery_publication_t p = candidate_recovery_publication(r); candidate_recovery_info_t i = candidate_recovery_info(r);
    printf("RECEIPT %s owner=%" PRIu64 " checkpoint=%" PRIu64 " outcomes=%u nodes=%u new=%u reused=%u alloc=%u reserved=%" PRIu64
        " reuse_ids=%" PRIu64 " groups=%u events=%u wal_bytes=%" PRIu64 " protected=%" PRIu64 " maintenance=%" PRIu64
        " data_bytes=%" PRIu64 " hwm=%" PRIu64 "->%" PRIu64 " aborts=%u discarded=%" PRIu64 " tail=%" PRIu64 " resident=%zu\n",
        tag,i.anchor.owner_epoch,i.anchor.checkpoint_id,p.outcomes,p.nodes,p.new_nodes,p.reused_nodes,p.allocation_pages,p.reserved_pages,
        p.reused_reserved_pages,p.system_groups,p.events,p.wal_bytes_appended,p.protected_pages,p.maintenance_pages,p.data_file_bytes,
        p.page_highwater_before,p.page_highwater_after,p.repair.terminals.appended_aborts,p.repair.discarded_group_lsn,
        p.repair.terminals.repaired_tail_bytes,i.resident_bytes);
    candidate_recovery_extent_t e = candidate_recovery_publication_extent(r); struct stat st; CHECK(fstat(r->fd,&st) == 0);
    printf("EXTENT %s data_bytes=%" PRIu64 " maintenance=%" PRIu64 " allocated=%" PRIu64 " carrier=%" PRIu64 " protected=%" PRIu64 "\n",tag,
        e.data_file_bytes,e.maintenance_pages,e.allocated_pages,e.carrier_pages,e.protected_pages);
    fflush(stdout);
    CHECK(e.data_file_bytes == (uint64_t)st.st_size && e.data_file_bytes % CANDIDATE_PAGE_BYTES == 0 && e.carrier_pages <= 2);
    CHECK(e.protected_pages == p.protected_pages && e.allocated_pages == p.reserved_pages + p.reused_reserved_pages);
    CHECK(e.allocated_pages == (uint64_t)p.allocation_pages + e.carrier_pages);
}

static void check_answers(candidate_recovery_t *r, const built_t *b, uint64_t floor)
{
    for (unsigned i = 0; i < b->spec.requests; i++) {
        candidate_catalog_query_t q = scen_query(b,i); candidate_recovery_answer_t a;
        OK(candidate_recovery_query(r,&q,&a));
        const req_t *req = &b->spec.req[i];
        if (req->epoch < floor) { CHECK(a.lookup == CANDIDATE_CATALOG_OUTSIDE_RETENTION && a.body == NULL); continue; }
        CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.outcome.transaction_id == b->txn[i].id);
        if (req->state == REQ_COMMIT) {
            CHECK(a.outcome.state == CANDIDATE_OUTCOME_COMMITTED && a.outcome.csn == b->txn[i].csn);
            CHECK(a.outcome.terminal_end_lsn == b->txn[i].terminal_end && a.outcome.result_bytes == req->bytes);
            CHECK(candidate_page_result_size(a.body) == req->bytes);
            if (req->bytes > 0) {
                uint8_t *expected = malloc(req->bytes); CHECK(expected != NULL); fx_payload(i + 1,req->bytes,expected);
                CHECK(memcmp(candidate_page_result_bytes(a.body),expected,req->bytes) == 0); free(expected);
            }
        } else {
            CHECK(a.outcome.state == CANDIDATE_OUTCOME_ABORTED && a.outcome.csn == 0);
            if (req->state == REQ_ABORT) CHECK(a.outcome.terminal_end_lsn == b->txn[i].terminal_end);
            bool retained = req->state == REQ_ABORT && req->retain && req->store == STORE_FINAL;
            CHECK(retained ? a.outcome.result_bytes == req->bytes : (a.outcome.result.page.id == 0 && a.body == NULL));
        }
        candidate_page_result_close(&a.body);
    }
}

static void check_misses(candidate_recovery_t *r, const built_t *b)
{
    candidate_catalog_query_t q = scen_query(b,0); candidate_recovery_answer_t a;
    q.nonce[15] ^= 0xff; OK(candidate_recovery_query(r,&q,&a)); CHECK(a.lookup == CANDIDATE_CATALOG_NOT_FOUND && a.body == NULL);
    q = scen_query(b,0); q.operation_digest[0] ^= 1; OK(candidate_recovery_query(r,&q,&a));
    CHECK(a.lookup == CANDIDATE_CATALOG_CONTENT_MISMATCH && a.body == NULL);
    q = scen_query(b,0); q.epoch = 0; OK(candidate_recovery_query(r,&q,&a)); CHECK(a.lookup == CANDIDATE_CATALOG_INVALID_ID);
}

static candidate_wal_status_t accept(void *context, const candidate_control_state_t *anchor)
{
    (void)context; (void)anchor; return CANDIDATE_WAL_OK;
}

static void check_slots(const fx_t *fx, const candidate_control_state_t *original, uint64_t owner)
{
    candidate_control_options_t o = {.backend = CANDIDATE_CONTROL_WAL,.verify = accept}; memcpy(o.uuid,fx_uuid,16);
    candidate_control_t *control = NULL; OK(candidate_control_open(fx->control,&o,&control));
    candidate_control_info_t info = candidate_control_info(control);
    CHECK(info.valid_mask == 3u && info.selected >= 0);
    const candidate_control_state_t *old = NULL, *now = &info.slots[info.selected];
    for (unsigned i = 0; i < 2; i++) if (info.slots[i].checkpoint_id == original->checkpoint_id) old = &info.slots[i];
    CHECK(old != NULL && old != now);
    CHECK(old->owner_epoch == original->owner_epoch && old->checkpoint_begin == original->checkpoint_begin);
    CHECK(old->checkpoint_end == original->checkpoint_end && old->outcome_root.id == original->outcome_root.id);
    CHECK(now->owner_epoch == owner && now->outcome_root.id == original->outcome_root.id && now->allocation_root.id == original->allocation_root.id);
    uint64_t floor = old->redo_begin < now->redo_begin ? old->redo_begin : now->redo_begin;
    CHECK(info.redo_floor == floor);
    OK(candidate_control_close(&control));
}

static void settle(const built_t *b, uint64_t owner, uint64_t floor)
{
    candidate_recovery_t *r = NULL; snapshot_t before = take(b->fx);
    CHECK(recover(b->fx,owner,&r) == CANDIDATE_WAL_OK); CHECK(io_writes() == 0);
    snapshot_t after = take(b->fx); CHECK(same(&before,&after));
    check_answers(r,b,floor); OK(candidate_recovery_close(&r));
    candidate_recovery_options_t o = fx_options(b->fx,owner); OK(candidate_recovery_open(&o,&r));
    check_answers(r,b,floor); OK(candidate_recovery_close(&r));
}

static void keep_before(const built_t *b, fx_t *before)
{
    fx_copy_set(b->fx,"before",before);
}

static void announce(const char *tag, const built_t *b, const fx_t *before, candidate_recovery_t *r, uint64_t floor)
{
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    printf("FIXTURE tag=%s wal=%s control=%s data=%s before=%s protected=%" PRIu64 " floor=%" PRIu64 " owner=%" PRIu64 "\n",tag,
        b->fx->wal,b->fx->control,b->fx->data,before->wal,p.protected_pages,floor,candidate_recovery_info(r).anchor.owner_epoch);
    fflush(stdout);
}

static void finish_case(const char *tag, built_t *b, uint64_t owner, uint64_t floor)
{
    fx_t before; keep_before(b,&before);
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(b->fx,owner,&r);
    if (status == CANDIDATE_WAL_OK) announce(tag,b,&before,r,floor);
    printf("CASE %s status=%s writes=%u wal=%u data=%u control=%u truncates=%u peak=%zu\n",tag,name_of(status),io_writes(),
        io.writes[K_WAL],io.writes[K_DATA],io.writes[K_CONTROL],io.truncates,io.peak);
    CHECK(status == CANDIDATE_WAL_OK && r != NULL); CHECK(io.live > 0);
    receipt(tag,r); check_answers(r,b,floor); check_misses(r,b);
    OK(candidate_recovery_close(&r)); CHECK(io.live == 0);
    check_slots(b->fx,&b->anchor,owner);
    settle(b,owner,floor);
}

/* 미완료 거래·불완전 물리 group·transport tail을 checkpoint 뒤에 둔다. */
static void add_tail(built_t *b, uint64_t txn)
{
    fx_t *fx = b->fx; fx_txn_t t = {0}; fx_begin(fx,&t,txn);
    static fx_image_t image; uint64_t ballast = b->alloc_count;
    uint64_t end = fx_group_end(fx,1,0);
    candidate_catalog_node_t node = {.count = 1};
    node.entries[0] = (candidate_outcome_t){.epoch = b->anchor.retention_floor,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,.admitting_owner_epoch = 7};
    node.entries[0].nonce[3] = 0x77;
    candidate_page_location_t l = fx_location(ballast,1,end);
    OK(candidate_catalog_encode_node(&l,&node,image.page,CANDIDATE_PAGE_BYTES));
    image.id = ballast; image.generation = 1; image.previous = fx->pages[ballast].lsn;
    fx_group(fx,&t,&image,1,NULL,0,false,false);
    fx_tail_garbage(fx,56);
}

static const req_t mixed[] = {
    {REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_COMMIT,STORE_ADMITTED,1,9000,false,false},
    {REQ_COMMIT,STORE_NONE,1,70000,false,false},{REQ_COMMIT,STORE_FINAL,1,0,false,true},
    {REQ_ABORT,STORE_FINAL,1,0,false,false},{REQ_ABORT,STORE_FINAL,1,30,true,false},
    {REQ_ABORT,STORE_ADMITTED,1,20,false,false},{REQ_UNFINISHED,STORE_ADMITTED,1,50,false,false},
    {REQ_UNFINISHED,STORE_NONE,1,0,false,false},{REQ_COMMIT,STORE_ADMITTED,1,8008,false,false}};

static built_t build_mixed(const char *tag, bool tail, unsigned ballast)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-%s-%ld",tag,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = sizeof(mixed) / sizeof(mixed[0]),.req = mixed,.ballast = ballast == 0 ? 1 : ballast};
    built_t b = scen_build(&s);
    if (tail) add_tail(&b,b.spec.requests + 7);
    scen_finish(&b);
    return b;
}

static void case_basic(void)
{
    built_t b = build_mixed("basic",true,1);
    finish_case("basic",&b,8,1);
    fx_t kept, work; fx_copy_set(b.fx,"owner9-before",&kept); fx_copy_set(b.fx,"owner9",&work);
    built_t nine = b; nine.fx = &work;
    candidate_recovery_t *r = NULL; CHECK(recover(&work,9,&r) == CANDIDATE_WAL_OK);
    candidate_recovery_publication_t p = candidate_recovery_publication(r); receipt("basic-owner9",r);
    announce("basic-owner9",&nine,&kept,r,1);
    CHECK(p.new_nodes == 0 && p.allocation_pages == 1 && p.reserved_pages == 1 && p.system_groups == 2);
    check_answers(r,&nine,1); OK(candidate_recovery_close(&r));
    settle(&nine,9,1);
    scen_free(&b);
}

static void case_selection(void)
{
    /* 높은 slot의 visible 상한 위반은 CORRUPT fallback이며 tail·불완전 group이 있어도 선택은 쓰기를 하지 않는다. */
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-selection-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = sizeof(mixed) / sizeof(mixed[0]),.req = mixed,.ballast = 2,.extra_empty_roots = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx;
    uint64_t extra = b.alloc_count - 2;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 5);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0);
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = b.anchor.transaction_highwater,
        .page_highwater = b.anchor.page_highwater,.visible_csn = fx->highest_csn + 2,.retained_bytes = CANDIDATE_PAGE_BYTES};
    candidate_page_location_t l = fx_location(extra,1,end); OK(candidate_catalog_encode_root(&l,&root,image.page,CANDIDATE_PAGE_BYTES));
    image.id = extra; image.generation = 1; image.previous = fx->pages[extra].lsn;
    fx_group(fx,&t,&image,1,NULL,0,true,true); fx_commit(fx,&t);
    candidate_control_state_t anchors[2] = {b.anchor,b.anchor};
    anchors[1].checkpoint_id = 2; anchors[1].outcome_root = (candidate_page_ref_t){extra,1}; anchors[1].visible_csn = fx->highest_csn;
    const fx_txn_t *active[2] = {&b.txn[7],&b.txn[8]};
    fx_checkpoint(fx,&anchors[1],active,2);
    add_tail(&b,b.spec.requests + 9);
    fx_control(fx,anchors,2); fx_close(fx);
    snapshot_t before = take(fx); candidate_recovery_t *r = NULL;
    io_reset(fx); io.fail_mutation_at = 1;
    candidate_wal_status_t status = recover_with(fx,8,&r);
    snapshot_t after = take(fx);
    printf("CASE selection-first-write status=%s unchanged=%d\n",name_of(status),same(&before,&after));
    CHECK(status == CANDIDATE_WAL_IO && r == NULL && same(&before,&after));
    io_reset(fx); io.fail_read_at = 3; status = recover_with(fx,8,&r); after = take(fx);
    printf("CASE selection-read-io status=%s writes=%u unchanged=%d\n",name_of(status),io_writes(),same(&before,&after));
    CHECK(status == CANDIDATE_WAL_IO && r == NULL && io_writes() == 0 && same(&before,&after));
    io_reset(fx); io.fail_alloc_at = 2; status = recover_with(fx,8,&r); after = take(fx);
    CHECK(status == CANDIDATE_WAL_NOMEM && r == NULL && io_writes() == 0 && same(&before,&after) && io.live == 0);
    fx_t kept; keep_before(&b,&kept);
    status = recover(fx,8,&r);
    printf("CASE selection status=%s writes=%u\n",name_of(status),io_writes());
    CHECK(status == CANDIDATE_WAL_OK); receipt("selection",r); announce("selection",&b,&kept,r,1);
    candidate_recovery_info_t info = candidate_recovery_info(r); candidate_recovery_publication_t p = candidate_recovery_publication(r);
    CHECK(info.anchor.checkpoint_id == 3 && info.anchor.outcome_root.id == b.root_id);
    CHECK(p.repair.discarded_group_lsn != 0 && p.repair.terminals.appended_aborts == 3 && p.repair.terminals.repaired_tail_bytes == 0);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r));
    check_slots(fx,&b.anchor,8);
    settle(&b,8,1);
    scen_free(&b);
}

static void case_empty(void)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-empty-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name}; built_t b = scen_build(&s); scen_finish(&b);
    fx_t kept; keep_before(&b,&kept);
    candidate_recovery_t *r = NULL; CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK); receipt("empty",r); announce("empty",&b,&kept,r,1);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    CHECK(p.outcomes == 0 && p.nodes == 0 && p.new_nodes == 0 && p.allocation_pages == 1 && p.reserved_pages == 1);
    OK(candidate_recovery_close(&r)); check_slots(b.fx,&b.anchor,8); settle(&b,8,1); scen_free(&b);
}

static void case_resolved(void)
{
    req_t req[4] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_ABORT,STORE_FINAL,1,0,false,false},
        {REQ_COMMIT,STORE_FINAL,1,0,false,true},{REQ_ABORT,STORE_FINAL,1,24,true,false}};
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-resolved-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 4,.req = req}; built_t b = scen_build(&s); scen_finish(&b);
    fx_t kept; keep_before(&b,&kept);
    candidate_recovery_t *r = NULL; CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK); receipt("resolved",r); announce("resolved",&b,&kept,r,1);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    CHECK(p.outcomes == 4 && p.nodes == 1 && p.new_nodes == 0 && p.reused_nodes == 1 && p.allocation_pages == 1);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); check_slots(b.fx,&b.anchor,8); settle(&b,8,1); scen_free(&b);
}

static void case_boundary(unsigned count, bool new_node)
{
    req_t req[1] = {{REQ_COMMIT,new_node ? STORE_NONE : STORE_FINAL,1,40,false,false}};
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-boundary-%u-%d-%ld",count,new_node,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 1,.req = req,.pad_alloc_to = count}; built_t b = scen_build(&s); scen_finish(&b);
    CHECK(b.alloc_count == count);
    fx_t kept; keep_before(&b,&kept);
    candidate_recovery_t *r = NULL; CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK);
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"boundary-%u-%d",count,new_node) > 0); receipt(tag,r); announce(tag,&b,&kept,r,1);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    uint32_t expected = new_node ? 1 + (count % CANDIDATE_ALLOCATION_NODE_ENTRIES == 0 ? 1 : 0) : 1;
    CHECK(p.allocation_pages == expected && p.reserved_pages == expected && p.new_nodes == (new_node ? 1u : 0u));
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); check_slots(b.fx,&b.anchor,8); settle(&b,8,1); scen_free(&b);
}

static void case_sparse(void)
{
    req_t req[2] = {{REQ_COMMIT,STORE_NONE,1,40,false,false},{REQ_COMMIT,STORE_ADMITTED,1,80,false,false}};
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-sparse-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 2,.req = req,.page_highwater = 50000}; built_t b = scen_build(&s); scen_finish(&b);
    uint64_t before = fx_file_bytes(b.fx->data);
    fx_t kept; keep_before(&b,&kept);
    candidate_recovery_t *r = NULL; CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK); receipt("sparse",r); announce("sparse",&b,&kept,r,1);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    CHECK(p.page_highwater_before == 50000 && p.page_highwater_after == 50000 + p.reserved_pages && p.data_file_bytes == before);
    uint64_t after = fx_file_bytes(b.fx->data);
    printf("CASE sparse data_bytes=%" PRIu64 "->%" PRIu64 "\n",before,after);
    CHECK(after == (50000 + p.reserved_pages) * CANDIDATE_PAGE_BYTES);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); check_slots(b.fx,&b.anchor,8); settle(&b,8,1); scen_free(&b);
}

static void case_outcomes(bool partial)
{
    req_t *req = calloc(4096,sizeof(*req)); CHECK(req != NULL);
    for (unsigned i = 0; i < 4096; i++) {
        req[i] = (req_t){REQ_COMMIT,STORE_NONE,1 + i / 1024,8 + i % 33,false,false};
        if (i % 97 == 5) req[i].state = REQ_ABORT;
        if (partial && i < 2300 && i != 4095) req[i].store = STORE_FINAL;
    }
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-outcomes-%d-%ld",partial,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 4096,.req = req,.root_epoch = 4,.root_floor = 1,.txn_highwater = 8192};
    built_t b = scen_build(&s); scen_finish(&b);
    fx_t kept; keep_before(&b,&kept);
    candidate_recovery_t *r = NULL; struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    candidate_wal_status_t status = recover(b.fx,8,&r); clock_gettime(CLOCK_MONOTONIC,&t1);
    printf("CASE outcomes-%d status=%s seconds=%.3f peak=%zu\n",partial,name_of(status),
        (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9,io.peak);
    CHECK(status == CANDIDATE_WAL_OK); receipt(partial ? "outcomes-partial" : "outcomes",r);
    announce(partial ? "outcomes-partial" : "outcomes",&b,&kept,r,1);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    CHECK(p.outcomes == 4096 && p.nodes == 90);
    if (!partial) CHECK(p.new_nodes == 90 && p.system_groups == 3);
    else CHECK(p.reused_nodes == 2300 / 46 && p.new_nodes == 90 - 2300 / 46);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); check_slots(b.fx,&b.anchor,8); settle(&b,8,1);
    scen_free(&b); free(req);
}

typedef struct { unsigned groups, frees, images; uint64_t room; } filler_t;

/* ABORT와 fixture checkpoint(BEGIN/END)를 놓은 뒤의 WAL 끝을 같은 배치 규칙으로 계산한다. */
static uint64_t end_after(uint64_t position)
{
    uint64_t start; position = fx_place(position,32,&start);
    position = fx_place(position,160,&start); return fx_place(position,64,&start);
}

static unsigned next_count(const fx_t *fx, const filler_t *f, unsigned g)
{
    if (f->room == 0) return g < f->groups ? (f->images == 0 ? 1 : f->images) : 0;
    for (unsigned k = 64; k > 0; k--) {
        uint64_t end = end_after(fx_group_end(fx,k,0));
        if (end <= CANDIDATE_WAL_MAX_BYTES && CANDIDATE_WAL_MAX_BYTES - end >= f->room) return k;
    }
    return 0;
}

static void fill_groups(void *built, void *context)
{
    built_t *b = built; const filler_t *f = context; fx_t *fx = b->fx;
    fx_txn_t t = {0}; fx_begin(fx,&t,b->spec.requests + 2);
    static fx_image_t images[64];
    uint64_t first = b->alloc_count - b->spec.ballast + 1;
    for (unsigned g = 0;; g++) {
        unsigned count = next_count(fx,f,g);
        if (count == 0) break;
        uint64_t end = fx_group_end(fx,count,0);
        for (unsigned i = 0; i < count; i++) {
            uint64_t id = first + (i % b->spec.ballast);
            candidate_catalog_node_t node = {.count = 1};
            node.entries[0] = (candidate_outcome_t){.epoch = 1,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,.admitting_owner_epoch = 7};
            uint64_t unique = ((uint64_t)g << 20) ^ id; memcpy(node.entries[0].nonce,&unique,8);
            candidate_page_location_t l = fx_location(id,1,end); OK(candidate_catalog_encode_node(&l,&node,images[i].page,CANDIDATE_PAGE_BYTES));
            images[i].id = id; images[i].generation = 1; images[i].previous = fx->pages[id].lsn;
        }
        fx_group(fx,&t,images,count,NULL,0,true,true);
    }
    fx_abort(fx,&t);
    /* FREE: 62개씩 allocation root와 해당 node image를 같은 system group에 둔다. */
    uint64_t highwater = b->spec.page_highwater != 0 ? b->spec.page_highwater : b->alloc_count;
    candidate_page_ref_t nodes[CANDIDATE_ALLOCATION_NODES];
    uint32_t node_total = (fx->alloc_count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    for (uint32_t i = 0; i < node_total; i++) nodes[i] = (candidate_page_ref_t){2 + i,1};
    for (unsigned done = 0; done < f->frees;) {
        unsigned count = f->frees - done < 62 ? f->frees - done : 62;
        uint32_t touched[2] = {UINT32_MAX,UINT32_MAX}; unsigned n = 1;
        for (unsigned i = 0; i < count; i++) {
            candidate_allocation_entry_t *e = fx_alloc_find(fx,first + done + i); CHECK(e != NULL);
            uint32_t ordinal = (uint32_t)((e - fx->alloc) / CANDIDATE_ALLOCATION_NODE_ENTRIES);
            if (touched[0] == UINT32_MAX) { touched[0] = ordinal; n++; }
            else if (touched[0] != ordinal && touched[1] == UINT32_MAX) { touched[1] = ordinal; n++; }
        }
        uint64_t end = fx_group_end(fx,n,count);
        fx_event_t events[62];
        for (unsigned i = 0; i < count; i++) {
            uint64_t id = first + done + i; candidate_allocation_entry_t *e = fx_alloc_find(fx,id);
            e->state = CANDIDATE_ALLOCATION_FREE; e->release_horizon = 1; e->transition_lsn = end;
            events[i] = (fx_event_t){7,id,1,1,1};
        }
        static fx_image_t imgs[3]; n = 0;
        imgs[n].id = 1; imgs[n].generation = 1; imgs[n].previous = fx->pages[1].lsn;
        fx_encode_alloc_root(fx,1,end,7,highwater,1,nodes,imgs[n].page); n++;
        for (unsigned k = 0; k < 2; k++) {
            if (touched[k] == UINT32_MAX) continue;
            uint64_t id = nodes[touched[k]].id;
            imgs[n].id = id; imgs[n].generation = 1; imgs[n].previous = fx->pages[id].lsn;
            fx_encode_alloc_node(fx,touched[k],id,end,imgs[n].page); n++;
        }
        fx_group(fx,NULL,imgs,n,events,count,true,true);
        done += count;
    }
}

static void case_caps(const char *kind, unsigned amount, bool expect_limit)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    filler_t f = {0}; unsigned ballast = 1;
    if (strcmp(kind,"groups") == 0) { f.groups = amount; ballast = 1; }
    if (strcmp(kind,"events") == 0) { f.frees = amount; ballast = amount; }
    if (strcmp(kind,"wal") == 0) { f.room = amount; ballast = 64; }
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-caps-%s-%u-%ld",kind,amount,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = ballast,.before_checkpoint = fill_groups,.context = &f,.txn_highwater = 256};
    s.gc_horizon = f.frees > 0 ? 1 : 0;
    built_t b = scen_build(&s); scen_finish(&b);
    fx_t kept; if (!expect_limit) keep_before(&b,&kept);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(b.fx,8,&r);
    snapshot_t after = take(b.fx);
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"caps-%s-%u",kind,amount) > 0);
    if (!expect_limit && status == CANDIDATE_WAL_OK) announce(tag,&b,&kept,r,1);
    printf("CASE %s status=%s writes=%u wal=%u data=%u control=%u unchanged=%d\n",tag,name_of(status),io_writes(),io.writes[K_WAL],
        io.writes[K_DATA],io.writes[K_CONTROL],same(&before,&after));
    if (expect_limit) { CHECK(status == CANDIDATE_WAL_LIMIT && r == NULL && io_writes() == 0 && same(&before,&after)); }
    else { CHECK(status == CANDIDATE_WAL_OK); receipt(tag,r); OK(candidate_recovery_close(&r)); settle(&b,8,1); }
    scen_free(&b);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    const char *c = argv[1];
    if (strcmp(c,"basic") == 0) case_basic();
    else if (strcmp(c,"selection") == 0) case_selection();
    else if (strcmp(c,"empty") == 0) case_empty();
    else if (strcmp(c,"resolved") == 0) case_resolved();
    else if (strcmp(c,"sparse") == 0) case_sparse();
    else if (strcmp(c,"boundary") == 0) {
        unsigned counts[] = {200,201,202,203,403,404,405};
        for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) { case_boundary(counts[i],false); case_boundary(counts[i],true); }
    } else if (strcmp(c,"outcomes") == 0) case_outcomes(false);
    else if (strcmp(c,"outcomes-partial") == 0) case_outcomes(true);
    else if (strcmp(c,"caps") == 0) { CHECK(argc == 5); case_caps(argv[2],(unsigned)strtoul(argv[3],NULL,10),strcmp(argv[4],"limit") == 0); }
    else CHECK(false);
    printf("PASS %s checks=%u\n",c,checks);
    return 0;
}
