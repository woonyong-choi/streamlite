#include "test_candidate_recovery_publish_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "../src/storage/candidate_wal_internal.h"

unsigned checks;

typedef struct {
    ino_t data, control;
    unsigned wal_writes, data_writes, control_writes, truncates, syncs;
    ino_t spoof_inode;
    uint64_t spoof_offset, spoof_last;
    const uint8_t *spoof_frame;
    size_t spoof_bytes;
    bool spoof_armed;
    unsigned spoofed, data_syncs, crash_after_data_sync;
} io_state_t;
static io_state_t io;

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }

static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset)
{
    if (io.spoof_frame != NULL && inode_of(fd) == io.spoof_inode) {
        uint64_t at = (uint64_t)offset;
        if (at == 0) io.spoof_last = 0;
        bool is_back = at < io.spoof_last;
        if (at > io.spoof_last) io.spoof_last = at;
        if (at == io.spoof_offset && n == 80 && is_back) io.spoof_armed = true;
        if (io.spoof_armed && at == io.spoof_offset && n <= io.spoof_bytes) {
            memcpy(p,io.spoof_frame,n); if (n == io.spoof_bytes) { io.spoof_armed = false; io.spoofed++; }
            return (ssize_t)n;
        }
    }
    return pread(fd,p,n,offset);
}

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    ino_t inode = inode_of(fd);
    if (inode == io.data) io.data_writes++; else if (inode == io.control) io.control_writes++; else io.wal_writes++;
    return pwrite(fd,p,n,offset);
}

static int hook_fsync(int fd)
{
    io.syncs++; int value = fsync(fd);
    if (inode_of(fd) == io.data && ++io.data_syncs == io.crash_after_data_sync) _exit(77);
    return value;
}
static int hook_truncate(int fd, off_t size) { io.truncates++; return ftruncate(fd,size); }
static const candidate_wal_io_t hooks = {hook_pread,hook_pwrite,hook_fsync,hook_truncate};

static void io_reset(const fx_t *fx)
{
    memset(&io,0,sizeof(io));
    struct stat st; CHECK(stat(fx->data,&st) == 0); io.data = st.st_ino;
    CHECK(stat(fx->control,&st) == 0); io.control = st.st_ino;
}

static unsigned io_writes(void) { return io.wal_writes + io.data_writes + io.control_writes + io.truncates; }

typedef struct { uint8_t control[32], data[32], wal[64][32]; uint64_t wal_bytes[64]; unsigned segments; } snapshot_t;

static snapshot_t take(const fx_t *fx)
{
    snapshot_t s = {0}; fx_sha256_file(fx->control,s.control); fx_sha256_file(fx->data,s.data);
    for (unsigned id = 0; id < 64; id++) {
        char name[300]; CHECK(snprintf(name,sizeof(name),"%s.wal.%016" PRIx64,fx->wal,(uint64_t)id) > 0);
        if (access(name,F_OK) != 0) break;
        fx_sha256_file(name,s.wal[id]); s.wal_bytes[id] = fx_file_bytes(name); s.segments++;
    }
    return s;
}

static bool same(const snapshot_t *a, const snapshot_t *b) { return memcmp(a,b,sizeof(*a)) == 0; }

static candidate_wal_status_t recover(const fx_t *fx, uint64_t owner, candidate_recovery_t **out)
{
    io_reset(fx);
    candidate_recovery_options_t o = fx_options(fx,owner); o.io = &hooks;
    return candidate_recovery_recover(&o,out);
}

static const char *name_of(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

static void report(const char *tag, candidate_wal_status_t s, const snapshot_t *before, const fx_t *fx)
{
    snapshot_t after = take(fx);
    printf("CASE %s status=%s writes=%u wal=%u data=%u control=%u truncates=%u unchanged=%d\n",tag,name_of(s),io_writes(),
        io.wal_writes,io.data_writes,io.control_writes,io.truncates,same(before,&after));
    fflush(stdout);
}

static fx_txn_t later_root(built_t *b, uint64_t root_page, uint64_t node_page, const candidate_outcome_t *entries, uint32_t count,
    uint64_t visible)
{
    fx_t *fx = b->fx; fx_txn_t t = {0}; fx_begin(fx,&t,b->spec.requests + 2);
    static fx_image_t images[2]; uint64_t end = fx_group_end(fx,count == 0 ? 1 : 2,0);
    candidate_catalog_root_t root = {.owner_epoch = fx->owner,.current_epoch = b->anchor.request_epoch,.retention_floor = b->anchor.retention_floor,
        .transaction_highwater = b->anchor.transaction_highwater,.page_highwater = b->anchor.page_highwater,.visible_csn = visible,
        .retained_bytes = 2 * CANDIDATE_PAGE_BYTES,.count = count,.pages = {{node_page,1}}};
    for (uint32_t i = 0; i < count; i++) root.retained_bytes += entries[i].result_bytes;
    candidate_page_location_t l = fx_location(root_page,1,end);
    OK(candidate_catalog_encode_root(&l,&root,images[0].page,CANDIDATE_PAGE_BYTES));
    images[0].id = root_page; images[0].generation = 1; images[0].previous = fx->pages[root_page].lsn;
    if (count > 0) {
        candidate_catalog_node_t node = {.count = count}; memcpy(node.entries,entries,count * sizeof(entries[0]));
        l = fx_location(node_page,1,end); OK(candidate_catalog_encode_node(&l,&node,images[1].page,CANDIDATE_PAGE_BYTES));
        images[1].id = node_page; images[1].generation = 1; images[1].previous = fx->pages[node_page].lsn;
    }
    fx_group(fx,&t,images,count == 0 ? 1 : 2,NULL,0,true,true);
    fx_commit(fx,&t);
    return t;
}

static uint64_t plan_id(const built_t *b, unsigned kind_index_from_end)
{
    return b->alloc_count - kind_index_from_end;
}

/* 높은 slot의 저장 terminal과 WAL 미완료 거래는 STALE로 멈추며 낮은 slot fallback·쓰기가 없다. */
static void case_f1(bool abort)
{
    req_t req[2] = {{REQ_COMMIT,STORE_ADMITTED,1,40,false,false},{REQ_UNFINISHED,STORE_NONE,1,50,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-f1-%s-%ld",abort ? "abort" : "commit",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 2,.req = req,.extra_empty_roots = 1,.ballast = 1};
    built_t b = scen_build(&s);
    /* 둘째 root에 거래2의 terminal을 넣는다: 요청·owner·limit·결과 조각은 WAL과 같지만 WAL terminal은 없다. */
    const fx_txn_t *u = &b.txn[1];
    candidate_outcome_t e = {.epoch = 1,.transaction_id = u->id,.state = abort ? CANDIDATE_OUTCOME_ABORTED : CANDIDATE_OUTCOME_COMMITTED,
        .csn = abort ? 0 : 1,.terminal_end_lsn = b.anchor.checkpoint_end,.result = u->result,.result_bytes = u->result_bytes,
        .result_chunks = u->chunks,.declared_result_limit = u->limit,.admitting_owner_epoch = 7};
    memcpy(e.nonce,u->nonce,16); memcpy(e.operation_digest,u->operation,32); memcpy(e.result_digest,u->result_digest,32);
    uint64_t extra_root = plan_id(&b,1), ballast = plan_id(&b,0);
    later_root(&b,extra_root,ballast,&e,1,b.fx->highest_csn);
    candidate_control_state_t anchors[2] = {b.anchor,b.anchor};
    anchors[1].checkpoint_id = 2; anchors[1].outcome_root = (candidate_page_ref_t){extra_root,1};
    const fx_txn_t *active[1] = {u};
    fx_checkpoint(b.fx,&anchors[1],active,1);
    fx_control(b.fx,anchors,2); fx_close(b.fx);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(b.fx,8,&r);
    report(abort ? "f1-abort" : "f1-commit",status,&before,b.fx);
    CHECK(status == CANDIDATE_WAL_STALE && r == NULL);
    CHECK(io_writes() == 0); snapshot_t after = take(b.fx); CHECK(same(&before,&after));
    scen_free(&b);
}

/* 관측 commit 없이 visible1인 root는 선택 단계에서 CORRUPT다. */
static void case_f2(bool fallback)
{
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-f2-%s-%ld",fallback ? "fallback" : "single",(long)getpid()) > 0);
    scen_t s = {.name = name,.system_bootstrap = true,.stored_visible_ahead = !fallback,.ballast = fallback ? 1 : 0};
    built_t b = scen_build(&s);
    fx_t *fx = b.fx; candidate_control_state_t anchors[2] = {b.anchor,b.anchor}; unsigned count = 1;
    if (fallback) {
        uint64_t bad = plan_id(&b,0), ph = b.anchor.page_highwater + 1;
        static fx_image_t images[2]; fx_event_t reserve = {16,b.anchor.transaction_highwater,b.anchor.transaction_highwater,
            b.anchor.page_highwater,ph};
        uint64_t end = fx_group_end(fx,2,1);
        candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = b.anchor.transaction_highwater,
            .page_highwater = ph,.visible_csn = 1,.retained_bytes = CANDIDATE_PAGE_BYTES};
        candidate_page_location_t l = fx_location(bad,1,end);
        OK(candidate_catalog_encode_root(&l,&root,images[0].page,CANDIDATE_PAGE_BYTES));
        images[0].id = bad; images[0].generation = 1; images[0].previous = fx->pages[bad].lsn;
        candidate_page_ref_t nodes[1] = {{2,1}};
        images[1].id = 1; images[1].generation = 1; images[1].previous = fx->pages[1].lsn;
        fx_encode_alloc_root(fx,1,end,7,ph,0,nodes,images[1].page);
        fx_group(fx,NULL,images,2,&reserve,1,true,true);
        anchors[1].checkpoint_id = 2; anchors[1].page_highwater = ph; anchors[1].outcome_root = (candidate_page_ref_t){bad,1};
        fx_checkpoint(fx,&anchors[1],NULL,0); count = 2;
    }
    fx_txn_t t = {0}; fx_begin(fx,&t,1);
    fx_control(fx,anchors,count); fx_close(fx);
    snapshot_t before = take(fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(fx,8,&r);
    report(fallback ? "f2-fallback" : "f2-single",status,&before,fx);
    if (!fallback) {
        CHECK(status == CANDIDATE_WAL_CORRUPT && r == NULL); CHECK(io_writes() == 0);
        snapshot_t after = take(fx); CHECK(same(&before,&after));
    } else {
        CHECK(status == CANDIDATE_WAL_OK && r != NULL);
        candidate_recovery_info_t info = candidate_recovery_info(r);
        CHECK(info.anchor.outcome_root.id == b.root_id && info.anchor.owner_epoch == 8 && info.anchor.checkpoint_id == 3);
        candidate_recovery_publication_t p = candidate_recovery_publication(r);
        CHECK(p.repair.terminals.appended_aborts == 1 && p.has_publication);
        OK(candidate_recovery_close(&r));
        candidate_recovery_options_t o = fx_options(fx,8); OK(candidate_recovery_open(&o,&r));
        CHECK(candidate_recovery_info(r).anchor.owner_epoch == 8); OK(candidate_recovery_close(&r));
    }
    scen_free(&b);
}

/* 낮은 ID와 매우 큰 HWM에서 새 ID가 data offset을 넘으면 metadata 효과 전에 LIMIT다. */
static void case_f3(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-f3-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 1,.req = req,.page_highwater = UINT64_MAX - 128};
    built_t b = scen_build(&s); scen_finish(&b);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(b.fx,8,&r);
    report("f3-highwater",status,&before,b.fx);
    candidate_wal_status_t again = recover(b.fx,9,&r);
    printf("CASE f3-reopen status=%s\n",name_of(again)); fflush(stdout);
    CHECK(status == CANDIDATE_WAL_LIMIT && r == NULL);
    CHECK(again == CANDIDATE_WAL_LIMIT); snapshot_t after = take(b.fx); CHECK(same(&before,&after));
    candidate_recovery_options_t o = fx_options(b.fx,7); CHECK(candidate_recovery_open(&o,&r) == CANDIDATE_WAL_STALE && r == NULL);
    scen_free(&b);
}

/* 결과 VersionRef.previous와 그 overflow page까지 보호 물리 page로 센다. */
static void case_f4(unsigned ballast, bool expect_limit)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-f4-%u-%ld",ballast,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 1,.req = req,.chain_b = 1,.ballast = ballast};
    built_t b = scen_build(&s); scen_finish(&b);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(b.fx,8,&r);
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"f4-quota-%u",ballast) > 0);
    report(tag,status,&before,b.fx);
    if (status == CANDIDATE_WAL_OK) printf("CASE %s protected=%" PRIu64 "\n",tag,candidate_recovery_publication(r).protected_pages);
    if (expect_limit) {
        CHECK(status == CANDIDATE_WAL_LIMIT && r == NULL); CHECK(io.wal_writes == 0 && io.control_writes == 0);
    } else {
        CHECK(status == CANDIDATE_WAL_OK);
        CHECK(candidate_recovery_publication(r).protected_pages == (uint64_t)ballast + 6);
        OK(candidate_recovery_close(&r));
    }
    scen_free(&b);
}

static void case_f4_shared(void)
{
    req_t req[2] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_COMMIT,STORE_FINAL,1,48,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-f4-shared-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 2,.req = req,.chain_b = 2,.ballast = 3};
    built_t b = scen_build(&s); scen_finish(&b);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(b.fx,8,&r);
    report("f4-shared",status,&before,b.fx);
    CHECK(status == CANDIDATE_WAL_OK);
    uint64_t protected_pages = candidate_recovery_publication(r).protected_pages;
    printf("CASE f4-shared protected=%" PRIu64 "\n",protected_pages);
    /* R+N+ballast3 + 결과 page1 + 이전 page1 + 이전 slot별 overflow2 */
    CHECK(protected_pages == 2 + 3 + 1 + 1 + 2);
    OK(candidate_recovery_close(&r)); scen_free(&b);
}

static void check_answers(candidate_recovery_t *r, const built_t *b, uint64_t floor)
{
    for (unsigned i = 0; i < b->spec.requests; i++) {
        candidate_catalog_query_t q = scen_query(b,i); candidate_recovery_answer_t a;
        OK(candidate_recovery_query(r,&q,&a));
        const req_t *req = &b->spec.req[i];
        if (req->epoch < floor) { CHECK(a.lookup == CANDIDATE_CATALOG_OUTSIDE_RETENTION); continue; }
        CHECK(a.lookup == CANDIDATE_CATALOG_MATCH);
        if (req->state == REQ_COMMIT) {
            CHECK(a.outcome.state == CANDIDATE_OUTCOME_COMMITTED && a.outcome.csn == b->txn[i].csn);
            CHECK(a.outcome.result_bytes == req->bytes && candidate_page_result_size(a.body) == req->bytes);
            if (req->bytes > 0) {
                uint8_t *expected = malloc(req->bytes); CHECK(expected != NULL); fx_payload(i + 1,req->bytes,expected);
                CHECK(memcmp(candidate_page_result_bytes(a.body),expected,req->bytes) == 0); free(expected);
            }
        } else CHECK(a.outcome.state == CANDIDATE_OUTCOME_ABORTED);
        candidate_page_result_close(&a.body);
    }
}

/* 범위 공백: 선택한 옛 root(3/1)와 다른 root의 REQUEST_EPOCH(4/2). 다른 LIVE root 또는 임시 carrier로 예약한다. */
static void case_stale_root(bool freed, bool interrupt)
{
    req_t req[6] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_ABORT,STORE_FINAL,1,0,false,false},
        {REQ_COMMIT,STORE_FINAL,2,64,false,false},{REQ_ABORT,STORE_FINAL,2,24,true,false},
        {REQ_COMMIT,STORE_FINAL,3,0,false,true},{REQ_COMMIT,STORE_ADMITTED,3,72,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-stale-%s-%s-%ld",freed ? "alloc" : "carrier",interrupt ? "retry" : "once",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 6,.req = req,.root_epoch = 3,.root_floor = 1,.epoch_to = 4,.floor_to = 2,
        .free_epoch_root = freed,.invalid_extra_root = freed};
    built_t b = scen_build(&s); scen_finish(&b);
    snapshot_t before = take(b.fx); candidate_recovery_t *r = NULL;
    uint32_t alloc_before = b.alloc_count;
    if (interrupt) {
        pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            /* redo sync 다음의 reserve group data sync가 끝난 직후 process를 멈춘다. */
            io_reset(b.fx); io.crash_after_data_sync = 2; candidate_recovery_options_t o = fx_options(b.fx,8); o.io = &hooks;
            candidate_recovery_t *child_recovery = NULL; (void)candidate_recovery_recover(&o,&child_recovery); _exit(0);
        }
        int wstatus; CHECK(waitpid(child,&wstatus,0) == child);
        printf("CASE stale interrupted exit=%d\n",WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
        CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 77);
    }
    candidate_wal_status_t status = recover(b.fx,8,&r);
    report(freed ? "stale-root-alloc" : "stale-root-carrier",status,&before,b.fx);
    CHECK(status == CANDIDATE_WAL_OK && r != NULL);
    candidate_recovery_info_t info = candidate_recovery_info(r); candidate_recovery_publication_t p = candidate_recovery_publication(r);
    printf("CASE stale publication reserved=%" PRIu64 " reused=%" PRIu64 " new_nodes=%u alloc_pages=%u groups=%u\n",
        p.reserved_pages,p.reused_reserved_pages,p.new_nodes,p.allocation_pages,p.system_groups);
    CHECK(info.anchor.outcome_root.id == b.root_id && info.anchor.request_epoch == 4 && info.anchor.retention_floor == 2);
    if (interrupt) CHECK(p.reserved_pages == 0 && p.reused_reserved_pages > 0);
    check_answers(r,&b,2);
    OK(candidate_recovery_close(&r));
    (void)alloc_before;
    candidate_recovery_options_t o = fx_options(b.fx,8); OK(candidate_recovery_open(&o,&r)); check_answers(r,&b,2); OK(candidate_recovery_close(&r));
    snapshot_t settled = take(b.fx); CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK); CHECK(io_writes() == 0);
    snapshot_t again = take(b.fx); CHECK(same(&settled,&again)); OK(candidate_recovery_close(&r));
    scen_free(&b);
}

/* 순차 scan은 원 frame, read-at만 같은 LSN·CRC가 유효한 대체 frame을 받는다. */
static void case_source_view(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_ADMITTED,1,40,false,false}};
    char name[64]; CHECK(snprintf(name,sizeof(name),"t23pub-source-%ld",(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 1,.req = req,.extra_empty_roots = 1,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx;
    const fx_txn_t *c = &b.txn[0];
    candidate_outcome_t bad = {.epoch = 1,.transaction_id = c->id,.state = CANDIDATE_OUTCOME_COMMITTED,.csn = c->csn + 1,
        .terminal_end_lsn = c->terminal_end,.result = c->result,.result_bytes = c->result_bytes,.result_chunks = c->chunks,
        .declared_result_limit = c->limit,.admitting_owner_epoch = 7};
    memcpy(bad.nonce,c->nonce,16); memcpy(bad.operation_digest,c->operation,32); memcpy(bad.result_digest,c->result_digest,32);
    uint64_t extra_root = plan_id(&b,1), node = plan_id(&b,0);
    /* 둘째 root의 원 node는 잘못된 CSN이며 data에 쓰지 않아 preview가 WAL frame을 random read해야 한다. */
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    static fx_image_t images[2]; uint64_t end = fx_group_end(fx,2,0);
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = b.anchor.transaction_highwater,
        .page_highwater = b.anchor.page_highwater,.visible_csn = fx->highest_csn + 1,.retained_bytes = 2 * CANDIDATE_PAGE_BYTES + c->result_bytes,
        .count = 1,.pages = {{node,1}}};
    candidate_page_location_t l = fx_location(extra_root,1,end);
    OK(candidate_catalog_encode_root(&l,&root,images[0].page,CANDIDATE_PAGE_BYTES));
    images[0].id = extra_root; images[0].generation = 1; images[0].previous = fx->pages[extra_root].lsn;
    candidate_catalog_node_t n = {.count = 1,.entries = {bad}}; l = fx_location(node,1,end);
    OK(candidate_catalog_encode_node(&l,&n,images[1].page,CANDIDATE_PAGE_BYTES));
    images[1].id = node; images[1].generation = 1; images[1].previous = fx->pages[node].lsn;
    uint64_t begin_record, root_record, node_record, at = fx_place(fx_position(fx),16,&begin_record);
    at = fx_place(at,8224,&root_record); at = fx_place(at,8224,&node_record); (void)at;
    fx_group(fx,&t,images,2,NULL,0,true,false);
    fx_commit(fx,&t);
    /* 대체 frame: 같은 record header 필드와 CSN만 고친 node image를 encode해 같은 길이·유효 CRC를 만든다. */
    candidate_catalog_node_t good = n; good.entries[0].csn = c->csn;
    static uint8_t payload[8224], spoof[8320]; size_t written;
    memset(payload,0,sizeof(payload)); uint64_t group_id = fx->group_id;
    for (unsigned i = 0; i < 8; i++) { payload[i] = (uint8_t)(group_id >> (8 * i)); payload[8 + i] = (uint8_t)(node >> (8 * i)); }
    payload[16] = 1; for (unsigned i = 0; i < 8; i++) payload[24 + i] = (uint8_t)(images[1].previous >> (8 * i));
    l = fx_location(node,1,end); OK(candidate_catalog_encode_node(&l,&good,payload + 32,8192));
    candidate_wal_record_t record = {.type = 4,.lsn = node_record,.transaction_id = t.id,.previous_lsn = node_record - 8320,
        .owner_epoch = 7,.payload = payload,.payload_bytes = sizeof(payload)};
    record.previous_lsn = root_record;
    OK(candidate_wal_encode(fx_uuid,&record,spoof,sizeof(spoof),&written)); CHECK(written == sizeof(spoof));
    candidate_control_state_t anchors[2] = {b.anchor,b.anchor};
    anchors[1].checkpoint_id = 2; anchors[1].outcome_root = (candidate_page_ref_t){extra_root,1}; anchors[1].visible_csn = fx->highest_csn;
    fx_checkpoint(fx,&anchors[1],NULL,0);
    fx_control(fx,anchors,2); fx_close(fx);
    snapshot_t before = take(fx);
    char segment[300]; CHECK(snprintf(segment,sizeof(segment),"%s.wal.%016" PRIx64,fx->wal,node_record / CANDIDATE_WAL_SEGMENT_BYTES) > 0);
    struct stat st; CHECK(stat(segment,&st) == 0);
    candidate_recovery_t *r = NULL;
    io_reset(fx); io.spoof_inode = st.st_ino; io.spoof_offset = node_record % CANDIDATE_WAL_SEGMENT_BYTES;
    io.spoof_frame = spoof; io.spoof_bytes = sizeof(spoof);
    candidate_recovery_options_t o = fx_options(fx,8); o.io = &hooks;
    candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    unsigned spoofed = io.spoofed;
    report("source-view",status,&before,fx);
    printf("CASE source-view spoofed_reads=%u\n",spoofed);
    CHECK(spoofed > 0);
    CHECK(status == CANDIDATE_WAL_CORRUPT && r == NULL && io_writes() == 0);
    snapshot_t after = take(fx); CHECK(same(&before,&after));
    status = recover(fx,8,&r);
    report("source-view-clean",status,&before,fx);
    CHECK(status == CANDIDATE_WAL_OK && r != NULL);
    CHECK(candidate_recovery_info(r).anchor.outcome_root.id == b.root_id);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r));
    scen_free(&b);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *c = argv[1];
    if (strcmp(c,"f1-commit") == 0) case_f1(false);
    else if (strcmp(c,"f1-abort") == 0) case_f1(true);
    else if (strcmp(c,"f2-single") == 0) case_f2(false);
    else if (strcmp(c,"f2-fallback") == 0) case_f2(true);
    else if (strcmp(c,"f3-highwater") == 0) case_f3();
    else if (strcmp(c,"f4-quota-pass") == 0) case_f4(8186,false);
    else if (strcmp(c,"f4-quota-limit") == 0) case_f4(8188,true);
    else if (strcmp(c,"f4-shared") == 0) case_f4_shared();
    else if (strcmp(c,"stale-root-carrier") == 0) case_stale_root(false,false);
    else if (strcmp(c,"stale-root-alloc") == 0) case_stale_root(true,false);
    else if (strcmp(c,"stale-root-carrier-retry") == 0) case_stale_root(false,true);
    else if (strcmp(c,"stale-root-alloc-retry") == 0) case_stale_root(true,true);
    else if (strcmp(c,"source-view") == 0) case_source_view();
    else CHECK(false);
    printf("PASS %s checks=%u\n",c,checks);
    return 0;
}
