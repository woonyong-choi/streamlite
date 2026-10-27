#define main original_result_test_main
#include "test_candidate_result_wal.c"
#undef main

#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_checkpoint.h"
#include "storage/candidate_recovery.h"

typedef struct { char wal[256], control[280], data[280]; candidate_control_state_t anchor; } fixture_t;
typedef struct {
    unsigned calls, fail_at;
    size_t live, peak;
    candidate_recovery_options_t *mutate;
    candidate_wal_memory_t *mutate_memory;
    candidate_recovery_t **reenter;
} memory_t;
static unsigned serial, writes, syncs, fail_write, crash_write, crash_sync, fail_read, fail_sync;
static ino_t data_inode;
static bool short_io, interrupt_io;

static bool is_data(int fd) { struct stat st; return fstat(fd,&st) == 0 && st.st_ino == data_inode; }
static ssize_t fault_pread(int fd, void *p, size_t n, off_t offset)
{
    if (is_data(fd) && fail_read != 0 && --fail_read == 0) { errno = EIO; return -1; }
    if (interrupt_io) { interrupt_io = false; errno = EINTR; return -1; }
    return pread(fd,p,short_io && n > 19 ? 19 : n,offset);
}
static ssize_t fault_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    if (is_data(fd)) {
        writes++;
        if (writes == fail_write) { CHECK(pwrite(fd,p,17,offset) == 17); errno = ENOSPC; return -1; }
        if (writes == crash_write) { CHECK(pwrite(fd,p,17,offset) == 17); _exit(41); }
    }
    return pwrite(fd,p,short_io && n > 31 ? 31 : n,offset);
}
static int fault_sync(int fd)
{
    if (is_data(fd)) {
        syncs++; if (syncs == fail_sync) { errno = EIO; return -1; }
        if (crash_sync == 1) _exit(42);
        int value = fsync(fd); if (crash_sync == 2) _exit(43); return value;
    }
    return fsync(fd);
}
static void *allocate(void *context, size_t size)
{
    memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    if (m->mutate != NULL) { memset(m->mutate,0,sizeof(*m->mutate)); m->mutate = NULL; }
    if (m->mutate_memory != NULL) { memset(m->mutate_memory,0,sizeof(*m->mutate_memory)); m->mutate_memory = NULL; }
    if (m->reenter != NULL) {
        CHECK(candidate_recovery_close(m->reenter) == CANDIDATE_WAL_INVALID); CHECK(*m->reenter != NULL); m->reenter = NULL;
    }
    void *p = malloc(size); if (p != NULL) { m->live += size; if (m->live > m->peak) m->peak = m->live; } return p;
}
static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live >= size); m->live -= size; free(p);
}

static candidate_wal_status_t setup_verify(void *context, const candidate_control_state_t *anchor)
{
    (void)context; (void)anchor; return CANDIDATE_WAL_OK;
}

static void checkpoint(candidate_wal_t *wal, fixture_t *f, const chain_t *active)
{
    f->anchor.checkpoint_begin = candidate_wal_info(wal).valid_end_lsn;
    f->anchor.checkpoint_end = f->anchor.checkpoint_begin + (active == NULL ? 416 : 600);
    candidate_checkpoint_summary_t summary = {.anchor = f->anchor,.transaction_count = active == NULL ? 0 : 1};
    uint8_t body[160], digest[32] = {0}; size_t size; uint64_t end, previous = 0;
    OK(candidate_checkpoint_encode_begin(&summary,body,sizeof(body),&size));
    candidate_wal_record_t record = {.type = 12,.payload = body,.payload_bytes = size};
    previous = candidate_wal_info(wal).valid_end_lsn; OK(candidate_wal_append(wal,&record,&end));
    OK(candidate_checkpoint_digest(digest,body,size,digest));
    if (active != NULL) {
        candidate_checkpoint_chunk_t chunk = {.checkpoint_id = 1,.kind = 2,.count = 1};
        chunk.entries.transactions[0] = (candidate_checkpoint_transaction_t){9,active->first,active->last,0,0,7,CANDIDATE_CHECKPOINT_ACTIVE};
        OK(candidate_checkpoint_encode_chunk(&chunk,body,sizeof(body),&size));
        record = (candidate_wal_record_t){.type = 13,.previous_lsn = previous,.payload = body,.payload_bytes = size};
        previous = candidate_wal_info(wal).valid_end_lsn; OK(candidate_wal_append(wal,&record,&end));
        OK(candidate_checkpoint_digest(digest,body,size,digest));
    }
    candidate_checkpoint_end_t last = {.checkpoint_id = 1,.begin_lsn = f->anchor.checkpoint_begin,.chunks = active == NULL ? 0 : 1}; memcpy(last.digest,digest,32);
    OK(candidate_checkpoint_encode_end(&last,body,sizeof(body),&size));
    record = (candidate_wal_record_t){.type = 14,.previous_lsn = previous,.payload = body,.payload_bytes = size};
    OK(candidate_wal_append(wal,&record,&end)); CHECK(end == f->anchor.checkpoint_end);
}

static void encode_image(unsigned mode, unsigned id, const uint8_t *payload,
    size_t payload_bytes, uint64_t group_end, uint64_t terminal_end, uint8_t out[8192])
{
    unsigned overflow = payload_bytes > 1 ? (unsigned)((payload_bytes + 8103) / 8104) : 0;
    unsigned outcome = 2 + overflow, allocation = outcome + 2, total = allocation + 1;
    candidate_page_location_t location = {.ref = {id,1},.lsn = group_end}; memcpy(location.uuid,uuid,16);
    if (id == 1) {
        candidate_page_version_t versions[2] = {{.transaction_id = 9,.table_id = 1,.row_id = 42,.payload_bytes = payload_bytes,
            .slot_generation = 7,.inline_bytes = overflow == 0 ? payload : NULL,.overflow = {overflow == 0 ? 0 : 2,overflow == 0 ? 0 : 1}}};
        if (mode == 1) versions[0].previous = (candidate_version_ref_t){{100,1},0,7};
        if (mode == 8) versions[0].frozen_csn = 2;
        if (mode == 2) {
            versions[1] = versions[0]; versions[0].previous = (candidate_version_ref_t){{1,1},1,7};
            versions[1].previous = (candidate_version_ref_t){{1,1},0,7};
        }
        OK(candidate_page_encode_versions(&location,versions,mode == 2 ? 2 : 1,out,8192));
    } else if (id < outcome) {
        size_t at = (size_t)(id - 2) * 8104, size = payload_bytes - at < 8104 ? payload_bytes - at : 8104;
        candidate_page_ref_t next = {id + 1 == outcome ? 0 : id + 1,id + 1 == outcome ? 0 : 1};
        OK(candidate_page_encode_overflow(&location,next,payload + at,size,out,8192));
    } else if (id == outcome) {
        candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = 16,
            .page_highwater = total,.visible_csn = mode == 9 ? 0 : 1,.retained_bytes = (uint64_t)total * 8192 + payload_bytes,
            .count = 1,.pages = {{outcome + 1,1}}};
        OK(candidate_catalog_encode_root(&location,&root,out,8192));
    } else if (id == outcome + 1) {
        candidate_catalog_node_t node = {.count = 1};
        node.entries[0] = (candidate_outcome_t){.epoch = 1,.transaction_id = 9,
            .state = mode == 9 ? CANDIDATE_OUTCOME_ADMITTED : CANDIDATE_OUTCOME_COMMITTED,.csn = mode == 9 ? 0 : 1,
            .terminal_end_lsn = mode == 9 ? 0 : terminal_end,.result = mode == 9 ? (candidate_version_ref_t){0} : (candidate_version_ref_t){{1,1},0,7},
            .result_bytes = mode == 9 ? 0 : payload_bytes,.result_chunks = mode == 9 ? 0 : (uint32_t)((payload_bytes + 65535) / 65536),
            .declared_result_limit = payload_bytes,.admitting_owner_epoch = 7};
        memset(node.entries[0].operation_digest,0x5c,32);
        if (mode != 9) OK(candidate_digest_sha256(payload,payload_bytes,node.entries[0].result_digest));
        OK(candidate_catalog_encode_node(&location,&node,out,8192));
    } else if (id == allocation) {
        candidate_allocation_root_t root = {.owner_epoch = 7,.page_highwater = total,.gc_horizon = mode == 3 ? 1 : 0,
            .count = mode == 5 ? total - 1 : total,.nodes = {{allocation + 1,1}}};
        OK(candidate_allocation_encode_root(&location,&root,out,8192));
    } else {
        candidate_allocation_node_t node = {.count = mode == 5 ? total - 1 : total};
        for (unsigned i = 0; i < node.count; i++) node.entries[i] = (candidate_allocation_entry_t){{i + 1 + (mode == 5),1},CANDIDATE_ALLOCATION_LIVE,0,0};
        if (mode == 3) { node.entries[0].state = CANDIDATE_ALLOCATION_FREE; node.entries[0].release_horizon = 1; }
        if (mode == 4) node.entries[0].ref.generation = 2;
        OK(candidate_allocation_encode_node(&location,&node,out,8192));
    }
}

static fixture_t fixture(unsigned mode, size_t payload_bytes)
{
    fixture_t f = {0}; unsigned overflow = payload_bytes > 1 ? (unsigned)((payload_bytes + 8103) / 8104) : 0;
    unsigned outcome = 2 + overflow, allocation = outcome + 2, total = allocation + 1, groups = (total + 63) / 64;
    CHECK(snprintf(f.wal,sizeof(f.wal),"build/candidate-recovery-%ld-%u-%u",(long)getpid(),mode,++serial) > 0);
    CHECK(snprintf(f.control,sizeof(f.control),"%s.control",f.wal) > 0); CHECK(snprintf(f.data,sizeof(f.data),"%s.pages",f.wal) > 0);
    candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL;
    OK(candidate_wal_create(f.wal,&wo,&wal)); chain_t chain = {.wal = wal};
    uint8_t admit[64] = {0}; write_le(admit,1,8); memset(admit + 24,0x5c,32); write_le(admit + 56,payload_bytes,8);
    emit(&chain,1,admit,64); uint8_t begin[16] = {0}; emit(&chain,2,begin,16);
    uint8_t *payload = malloc(payload_bytes + 1); CHECK(payload != NULL);
    for (size_t i = 0; i < payload_bytes; i++) payload[i] = (uint8_t)(i % 251);
    uint64_t ends[4], next = candidate_wal_info(wal).valid_end_lsn;
    for (unsigned i = 0; i < groups; i++) { unsigned count = total - i * 64 < 64 ? total - i * 64 : 64; ends[i] = next + 112 + count * 8320 + 120; next = ends[i]; }
    unsigned chunks = (unsigned)((payload_bytes + 65535) / 65536); uint64_t terminal_end = next + chunks * 208 + 248 + groups * 16;
    unsigned id = 1;
    for (unsigned g = 0; g < groups; g++) {
        unsigned count = total - g * 64 < 64 ? total - g * 64 : 64;
        uint8_t start[16] = {0}; write_le(start,g + 1,8); write_le(start + 8,count,4); write_le(start + 12,count,4);
        candidate_wal_record_t r = emit(&chain,3,start,16); uint32_t crc = candidate_wal_witness_crc(0,&r);
        for (unsigned j = 0; j < count; j++,id++) {
            uint8_t image[8224] = {0}; write_le(image,g + 1,8); write_le(image + 8,id,8); write_le(image + 16,1,8);
            encode_image(mode,id,payload,payload_bytes,ends[g],terminal_end,image + 32);
            r = emit(&chain,4,image,sizeof(image)); crc = candidate_wal_witness_crc(crc,&r);
        }
        uint8_t finish[24] = {0}; write_le(finish,g + 1,8); write_le(finish + 8,count,4); write_le(finish + 12,count,4); write_le(finish + 16,crc,4);
        emit(&chain,8,finish,24); CHECK(candidate_wal_info(wal).valid_end_lsn == ends[g]);
    }
    uint8_t digest[32]; OK(candidate_digest_sha256(payload,payload_bytes,digest));
    if (mode != 9) {
        for (unsigned i = 0; i < chunks; i++) {
            uint8_t chunk[112] = {0}; write_le(chunk,1,8); write_le(chunk + 24,i,4); write_le(chunk + 28,chunks,4);
            write_le(chunk + 32,payload_bytes,8); memcpy(chunk + 40,digest,32); write_le(chunk + 72,1,8); write_le(chunk + 80,1,8); write_le(chunk + 92,7,4);
            write_le(chunk + 96,(uint64_t)i * 65536,8); write_le(chunk + 104,payload_bytes - i * 65536 < 65536 ? payload_bytes - i * 65536 : 65536,4);
            emit(&chain,9,chunk,112);
        }
        uint8_t commit[216] = {0}; write_le(commit,chain.first,8); write_le(commit + 8,chain.last,8); write_le(commit + 16,chain.records,8); write_le(commit + 24,chain.crc,4);
        write_le(commit + 32,1,8); write_le(commit + 40,1,8); memset(commit + 64,0x5c,32); memcpy(commit + 96,digest,32);
        write_le(commit + 128,payload_bytes,8); write_le(commit + 136,chunks,4); write_le(commit + 148,groups,4);
        for (unsigned i = 0; i < groups; i++) { write_le(commit + 152 + i * 16,i + 1,8); write_le(commit + 160 + i * 16,ends[i],8); }
        emit(&chain,10,commit,152 + groups * 16); CHECK(candidate_wal_info(wal).valid_end_lsn == terminal_end);
    }
    f.anchor = (candidate_control_state_t){.backend = 1,.generation = 1,.owner_epoch = 7,.checkpoint_id = 1,.redo_begin = 64,
        .transaction_highwater = 16,.page_highwater = total,.request_epoch = 1,.retention_floor = 1,.visible_csn = mode == 9 ? 0 : 1,
        .outcome_root = {mode == 6 ? 1 : outcome,1},.allocation_root = {allocation,1},.gc_horizon = mode == 3 ? 1 : 0};
    memcpy(f.anchor.uuid,uuid,16); if (mode == 7) f.anchor.catalog_root = (candidate_page_ref_t){1,1};
    checkpoint(wal,&f,mode == 9 ? &chain : NULL);
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn)); OK(candidate_wal_close(&wal)); free(payload);
    candidate_control_options_t co = {.backend = 1,.verify = setup_verify}; memcpy(co.uuid,uuid,16); candidate_control_t *control = NULL;
    OK(candidate_control_create(f.control,&co,&control)); OK(candidate_control_publish(control,&f.anchor)); OK(candidate_control_close(&control));
    int fd = open(f.data,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(fd >= 0); struct stat st; CHECK(fstat(fd,&st) == 0); data_inode = st.st_ino;
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); int directory = open("build",O_RDONLY | O_DIRECTORY); CHECK(directory >= 0); CHECK(fsync(directory) == 0); CHECK(close(directory) == 0);
    writes = syncs = fail_write = crash_write = crash_sync = fail_read = fail_sync = 0; short_io = interrupt_io = false;
    printf("FIXTURE %s mode=%u payload=%zu pages=%u begin=%llu end=%llu\n",f.wal,mode,payload_bytes,total,
        (unsigned long long)f.anchor.checkpoint_begin,(unsigned long long)f.anchor.checkpoint_end);
    return f;
}

static candidate_recovery_options_t options(const fixture_t *f)
{
    static const candidate_wal_io_t io = {fault_pread,fault_pwrite,fault_sync,ftruncate};
    candidate_recovery_options_t request = {.owner_epoch = 7,.wal_prefix = f->wal,.control_path = f->control,.data_path = f->data,.io = &io};
    memcpy(request.uuid,uuid,16); return request;
}

static candidate_catalog_query_t query(void)
{
    candidate_catalog_query_t q = {.epoch = 1}; memcpy(q.uuid,uuid,16); memset(q.operation_digest,0x5c,32); return q;
}

static void check_result(candidate_recovery_t *r, size_t bytes)
{
    candidate_catalog_query_t q = query(); candidate_recovery_answer_t answer;
    OK(candidate_recovery_query(r,&q,&answer)); CHECK(answer.lookup == CANDIDATE_CATALOG_MATCH);
    CHECK(answer.outcome.state == CANDIDATE_OUTCOME_COMMITTED && answer.outcome.csn == 1);
    CHECK(candidate_page_result_size(answer.body) == bytes);
    for (size_t i = 0; i < bytes; i++) CHECK(candidate_page_result_bytes(answer.body)[i] == (uint8_t)(i % 251));
    candidate_page_result_close(&answer.body);
    q.operation_digest[0] ^= 1; OK(candidate_recovery_query(r,&q,&answer)); CHECK(answer.lookup == CANDIDATE_CATALOG_CONTENT_MISMATCH && answer.body == NULL);
    q.nonce[0] = 1; OK(candidate_recovery_query(r,&q,&answer)); CHECK(answer.lookup == CANDIDATE_CATALOG_NOT_FOUND && answer.body == NULL);
}

static void normal_and_rejections(void)
{
    for (unsigned mode = 0; mode <= 9; mode++) {
        fixture_t f = fixture(mode,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = (void *)1;
        candidate_wal_status_t status = candidate_recovery_open(&request,&r);
        candidate_wal_status_t expected = mode == 0 ? CANDIDATE_WAL_OK : mode == 9 ? CANDIDATE_WAL_STALE : CANDIDATE_WAL_CORRUPT;
        printf("STATUS mode=%u actual=%u expected=%u\n",mode,status,expected); CHECK(status == expected);
        if (status == CANDIDATE_WAL_OK) {
            check_result(r,1); candidate_recovery_info_t info = candidate_recovery_info(r);
            CHECK(info.live_pages == 5 && info.free_pages == 0 && info.versions == 1 && info.redo.pages_written == 5);
            OK(candidate_recovery_close(&r)); OK(candidate_recovery_open(&request,&r)); check_result(r,1);
            CHECK(candidate_recovery_info(r).redo.pages_written == 0); OK(candidate_recovery_close(&r));
        } else CHECK(r == NULL);
    }
}

static void memory_failures(void)
{
    fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f);
    memory_t count = {0}; candidate_wal_memory_t memory = {&count,allocate,release}; request.memory = &memory;
    candidate_recovery_t *r = NULL; OK(candidate_recovery_open(&request,&r));
    unsigned allocations = count.calls; size_t peak = count.peak;
    CHECK(count.live == candidate_recovery_info(r).resident_bytes);
    candidate_recovery_t *other = (void *)1; CHECK(candidate_recovery_open(&request,&other) == CANDIDATE_WAL_BUSY && other == NULL);
    candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = (void *)1;
    CHECK(candidate_wal_open(f.wal,&wo,&wal) == CANDIDATE_WAL_BUSY && wal == NULL);
    candidate_control_options_t co = {.backend = 1,.verify = setup_verify}; memcpy(co.uuid,uuid,16); candidate_control_t *control = (void *)1;
    CHECK(candidate_control_open(f.control,&co,&control) == CANDIDATE_WAL_BUSY && control == NULL);
    candidate_catalog_query_t q = query(); candidate_recovery_answer_t answer; OK(candidate_recovery_query(r,&q,&answer));
    OK(candidate_recovery_close(&r)); CHECK(candidate_page_result_size(answer.body) == 1 && candidate_page_result_bytes(answer.body)[0] == 0);
    candidate_page_result_close(&answer.body); CHECK(count.live == 0);
    for (unsigned at = 1; at <= allocations; at++) {
        count = (memory_t){.fail_at = at}; r = (void *)1;
        CHECK(candidate_recovery_open(&request,&r) == CANDIDATE_WAL_NOMEM); CHECK(r == NULL && count.live == 0);
    }
    count = (memory_t){.mutate = &request,.mutate_memory = &memory};
    OK(candidate_recovery_open(&request,&r)); OK(candidate_recovery_close(&r)); CHECK(count.live == 0);
    printf("MEMORY calls=%u peak=%zu; includes WAL/control/graph/redo/outcome owner requests, excludes stack/allocator/RSS\n",allocations,peak);
}

static void io_and_processes(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = (void *)1;
        if (mode == 0) fail_write = 2;
        if (mode == 1) fail_read = 1;
        if (mode == 2) { short_io = true; interrupt_io = true; }
        if (mode == 3) fail_sync = 1;
        candidate_wal_status_t status = candidate_recovery_open(&request,&r);
        CHECK(status == (mode == 2 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO));
        if (mode != 2) {
            CHECK(r == NULL); fail_write = fail_read = fail_sync = 0;
            OK(candidate_recovery_open(&request,&r));
        }
        check_result(r,1); OK(candidate_recovery_close(&r));
        printf("IO_RESTART %s mode=%u\n",f.wal,mode);
    }
    for (unsigned mode = 0; mode < 3; mode++) {
        fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f);
        pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            alarm(10); if (mode == 0) crash_write = 2; else crash_sync = mode;
            candidate_recovery_t *r = NULL; candidate_recovery_open(&request,&r); _exit(99);
        }
        int status; CHECK(waitpid(child,&status,0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == (int)(41 + mode));
        candidate_recovery_t *r = NULL; OK(candidate_recovery_open(&request,&r)); check_result(r,1); OK(candidate_recovery_close(&r));
        printf("PROCESS_RESTART %s mode=%u\n",f.wal,mode);
    }
    fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&request,&r)); pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        alarm(10); candidate_recovery_answer_t answer; candidate_catalog_query_t q = query();
        CHECK(candidate_recovery_query(r,&q,&answer) == CANDIDATE_WAL_FORKED);
        OK(candidate_recovery_close(&r));
        CHECK(candidate_recovery_open(&request,&r) == CANDIDATE_WAL_BUSY && r == NULL); _exit(0);
    }
    int status; CHECK(waitpid(child,&status,0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    check_result(r,1); OK(candidate_recovery_close(&r));
}

typedef struct { candidate_wal_t *wal; uint64_t previous; uint32_t crc; } system_chain_t;

static void append_system(system_chain_t *chain, unsigned type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t record = {.type = type,.owner_epoch = 7,.previous_lsn = chain->previous,
        .lsn = candidate_wal_info(chain->wal).valid_end_lsn,.payload = body,.payload_bytes = size};
    uint64_t end; OK(candidate_wal_append(chain->wal,&record,&end));
    CHECK(end == record.lsn + ((96 + size + 7) & ~(size_t)7));
    chain->previous = record.lsn; chain->crc = candidate_wal_witness_crc(chain->crc,&record);
}

static uint64_t append_lifetime(candidate_wal_t *wal, unsigned phase, uint64_t previous)
{
    unsigned pages = phase == 0 ? 4 : phase == 1 ? 2 : 3, events = phase == 0 ? 2 : 1;
    uint64_t end = candidate_wal_info(wal).valid_end_lsn + 112 + pages * 8320 + events * 136 + 120;
    uint64_t group = phase + 2, generation = phase == 2 ? 2 : 1;
    uint8_t images[4][8192]; unsigned ids[4] = {4,5,6,2};
    candidate_page_location_t location = {.ref = {4,1},.lsn = end}; memcpy(location.uuid,uuid,16);
    candidate_allocation_root_t root = {.owner_epoch = 7,.page_highwater = 6,.gc_horizon = phase == 0 ? 0 : 1,
        .count = 6,.nodes = {{5,1}}};
    OK(candidate_allocation_encode_root(&location,&root,images[0],8192));
    candidate_allocation_node_t node = {.count = 6};
    for (unsigned i = 0; i < 6; i++) node.entries[i] = (candidate_allocation_entry_t){{i + 1,1},CANDIDATE_ALLOCATION_LIVE,0,0};
    node.entries[5] = (candidate_allocation_entry_t){{6,generation},phase == 1 ? CANDIDATE_ALLOCATION_FREE : CANDIDATE_ALLOCATION_LIVE,phase == 1 ? 1 : 0,end};
    location.ref.id = 5; OK(candidate_allocation_encode_node(&location,&node,images[1],8192));
    uint8_t value = 0x51; location.ref = (candidate_page_ref_t){6,generation};
    OK(candidate_page_encode_overflow(&location,(candidate_page_ref_t){0},&value,1,images[2],8192));
    location.ref = (candidate_page_ref_t){2,1};
    candidate_catalog_root_t catalog = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = 16,
        .page_highwater = 6,.visible_csn = 1,.retained_bytes = 6 * 8192 + 1,.count = 1,.pages = {{3,1}}};
    OK(candidate_catalog_encode_root(&location,&catalog,images[3],8192));
    system_chain_t chain = {.wal = wal}; uint8_t begin[16] = {0};
    write_le(begin,group,8); write_le(begin + 8,pages,4); write_le(begin + 12,pages + events,4); append_system(&chain,3,begin,16);
    for (unsigned i = 0; i < pages; i++) {
        uint8_t body[8224] = {0}; write_le(body,group,8); write_le(body + 8,ids[i],8); write_le(body + 16,ids[i] == 6 ? generation : 1,8);
        write_le(body + 24,ids[i] == 6 ? 0 : previous,8); memcpy(body + 32,images[i],8192); append_system(&chain,4,body,sizeof(body));
    }
    if (phase == 0) {
        uint8_t reserve[40] = {0}; write_le(reserve,group,8); write_le(reserve + 8,16,8); write_le(reserve + 16,16,8);
        write_le(reserve + 24,5,8); write_le(reserve + 32,6,8); append_system(&chain,16,reserve,40);
    }
    uint8_t event[40] = {0}; write_le(event,group,8); write_le(event + 8,6,8);
    write_le(event + 16,phase == 0 ? 0 : 1,8); write_le(event + 24,generation,8); write_le(event + 32,phase == 1 ? 1 : 0,8);
    append_system(&chain,phase == 1 ? 7 : 6,event,40);
    uint8_t finish[24] = {0}; write_le(finish,group,8); write_le(finish + 8,pages,4); write_le(finish + 12,pages + events,4);
    write_le(finish + 16,chain.crc,4); append_system(&chain,8,finish,24); CHECK(candidate_wal_info(wal).valid_end_lsn == end); return end;
}

static void lifetime_recovery(void)
{
    for (unsigned reuse = 0; reuse < 2; reuse++) {
        fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
        OK(candidate_recovery_open(&request,&r)); OK(candidate_recovery_close(&r));
        candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL;
        OK(candidate_wal_open(f.wal,&wo,&wal)); uint64_t previous = 42168;
        for (unsigned phase = 0; phase < 2 + reuse; phase++) previous = append_lifetime(wal,phase,previous);
        OK(candidate_wal_force(wal,previous)); OK(candidate_wal_close(&wal));
        OK(candidate_recovery_open(&request,&r)); check_result(r,1);
        candidate_recovery_info_t info = candidate_recovery_info(r); CHECK(info.live_pages == 5 + reuse && info.free_pages == 1 - reuse);
        OK(candidate_recovery_close(&r)); OK(candidate_recovery_open(&request,&r)); CHECK(candidate_recovery_info(r).redo.pages_written == 0);
        OK(candidate_recovery_close(&r)); printf("LIFETIME %s reuse=%u\n",f.wal,reuse);
    }
}

static void query_failures(void)
{
    fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f);
    memory_t count = {0}; candidate_wal_memory_t memory = {&count,allocate,release}; request.memory = &memory;
    candidate_recovery_t *r = NULL; OK(candidate_recovery_open(&request,&r)); size_t resident = count.live;
    candidate_catalog_query_t q = query(); candidate_recovery_answer_t answer;
    count.reenter = &r; OK(candidate_recovery_query(r,&q,&answer)); CHECK(count.reenter == NULL);
    candidate_page_result_close(&answer.body);
    count.fail_at = count.calls + 1;
    CHECK(candidate_recovery_query(r,&q,&answer) == CANDIDATE_WAL_NOMEM);
    CHECK(answer.lookup == CANDIDATE_CATALOG_UNAVAILABLE && answer.body == NULL && count.live == resident);
    count.fail_at = 0; check_result(r,1);
    char held[300]; CHECK(snprintf(held,sizeof(held),"%s.held",f.data) > 0); CHECK(rename(f.data,held) == 0);
    int fd = open(f.data,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(fd >= 0); CHECK(close(fd) == 0);
    CHECK(candidate_recovery_query(r,&q,&answer) == CANDIDATE_WAL_IO && answer.body == NULL);
    CHECK(candidate_recovery_query(r,&q,&answer) == CANDIDATE_WAL_POISONED && answer.body == NULL);
    OK(candidate_recovery_close(&r)); CHECK(count.live == 0); printf("REPLACED %s held=%s\n",f.wal,held);
}

static void unfinished_and_tail(void)
{
    for (unsigned tail = 0; tail < 2; tail++) {
        fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
        OK(candidate_recovery_open(&request,&r)); OK(candidate_recovery_close(&r)); writes = 0;
        candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL;
        OK(candidate_wal_open(f.wal,&wo,&wal)); uint64_t end = candidate_wal_info(wal).valid_end_lsn;
        if (tail == 0) {
            system_chain_t chain = {.wal = wal}; uint8_t begin[16] = {0}; write_le(begin,2,8); write_le(begin + 8,1,4); write_le(begin + 12,1,4);
            append_system(&chain,3,begin,16);
            uint8_t image[8224] = {0}; write_le(image,2,8); write_le(image + 8,6,8); write_le(image + 16,1,8);
            candidate_page_location_t location = {.ref = {6,1},.lsn = end + 112 + 8320 + 120}; memcpy(location.uuid,uuid,16); uint8_t value = 0x51;
            OK(candidate_page_encode_overflow(&location,(candidate_page_ref_t){0},&value,1,image + 32,8192)); append_system(&chain,4,image,sizeof(image));
            end = candidate_wal_info(wal).valid_end_lsn; OK(candidate_wal_force(wal,end));
        }
        OK(candidate_wal_close(&wal));
        if (tail != 0) {
            char path[300]; CHECK(snprintf(path,sizeof(path),"%s.wal.0000000000000000",f.wal) > 0);
            int fd = open(path,O_RDWR); CHECK(fd >= 0); CHECK(pwrite(fd,"S",1,(off_t)end) == 1); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
        }
        CHECK(candidate_recovery_open(&request,&r) == (tail == 0 ? CANDIDATE_WAL_STALE : CANDIDATE_WAL_TAIL)); CHECK(r == NULL && writes == 0);
        struct stat st; CHECK(stat(f.data,&st) == 0 && st.st_size == 5 * 8192);
        printf("INCOMPLETE %s tail=%u writes=%u\n",f.wal,tail,writes);
    }
}

static void control_fallback(void)
{
    for (unsigned dangling = 0; dangling < 2; dangling++) {
        fixture_t f = fixture(0,1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
        OK(candidate_recovery_open(&request,&r)); OK(candidate_recovery_close(&r)); writes = 0;
        candidate_control_options_t co = {.backend = 1,.verify = setup_verify}; memcpy(co.uuid,uuid,16); candidate_control_t *control = NULL;
        OK(candidate_control_open(f.control,&co,&control)); candidate_control_state_t next = f.anchor; next.checkpoint_id = 2;
        if (dangling != 0) next.catalog_root = (candidate_page_ref_t){1,1};
        OK(candidate_control_publish(control,&next)); OK(candidate_control_close(&control));
        OK(candidate_recovery_open(&request,&r));
        candidate_recovery_info_t info = candidate_recovery_info(r);
        CHECK(info.anchor.checkpoint_id == 1 && info.anchor.catalog_root.id == 0 && info.store_root.id == 0);
        check_result(r,1); OK(candidate_recovery_close(&r));
        CHECK(writes == 0); printf("FALLBACK %s dangling_catalog_root=%u selected=%llu writes=%u\n",f.wal,dangling,
            (unsigned long long)info.anchor.checkpoint_id,writes);
    }
}

#ifndef CANDIDATE_RECOVERY_TEST_MAIN
#define CANDIDATE_RECOVERY_TEST_MAIN main
#endif
int CANDIDATE_RECOVERY_TEST_MAIN(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90); normal_and_rejections();
    fixture_t f = fixture(0,1048576); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&request,&r)); check_result(r,1048576); CHECK(candidate_recovery_info(r).live_pages == 135); OK(candidate_recovery_close(&r));
    memory_failures(); io_and_processes(); lifetime_recovery(); query_failures(); unfinished_and_tail(); control_fallback();
    printf("PASS exclusive recovery checks=%u; resolved snapshots only, no product activation\n",checks); return 0;
}
