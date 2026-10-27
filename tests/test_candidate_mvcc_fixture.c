#include "test_candidate_mvcc_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "storage/checksum.h"

mf_faults_t mf_faults;

static unsigned serial;
static mf_fsync_hook_t fsync_hook;
static void *fsync_context;
static mf_pwrite_hook_t pwrite_hook;
static void *pwrite_context;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static bool is_armed(unsigned *counter, unsigned fail_at)
{
    unsigned count = __atomic_add_fetch(counter,1,__ATOMIC_RELAXED);
    return fail_at != 0 && count == fail_at;
}

static ssize_t hook_pread(int fd, void *bytes, size_t size, off_t offset) { return pread(fd,bytes,size,offset); }

static ssize_t hook_pwrite(int fd, const void *bytes, size_t size, off_t offset)
{
    if (is_armed(&mf_faults.pwrites,mf_faults.fail_pwrite_at)) { errno = EIO; return -1; }
    ssize_t written = pwrite(fd,bytes,size,offset);
    mf_pwrite_hook_t hook = pwrite_hook;
    if (written == (ssize_t)size && hook != NULL && hook(pwrite_context,bytes,size)) pwrite_hook = NULL;
    return written;
}

static int hook_fsync(int fd)
{
    if (is_armed(&mf_faults.fsyncs,mf_faults.fail_fsync_at)) { errno = EIO; return -1; }
    mf_fsync_hook_t hook = fsync_hook;
    if (hook != NULL && hook(fsync_context)) fsync_hook = NULL;
    return fsync(fd);
}

static int hook_ftruncate(int fd, off_t size) { return ftruncate(fd,size); }

static const candidate_wal_io_t HOOKS = {hook_pread,hook_pwrite,hook_fsync,hook_ftruncate};

static void *hook_allocate(void *context, size_t size)
{
    (void)context;
    if (is_armed(&mf_faults.allocations,mf_faults.fail_allocation_at)) return NULL;
    return malloc(size);
}

static void hook_release(void *context, void *bytes, size_t size) { (void)context; (void)size; free(bytes); }

static const candidate_wal_memory_t MEMORY = {NULL,hook_allocate,hook_release};

void mf_arm_fsync(unsigned after) { mf_faults.fail_fsync_at = __atomic_load_n(&mf_faults.fsyncs,__ATOMIC_RELAXED) + after; }
void mf_arm_pwrite(unsigned after) { mf_faults.fail_pwrite_at = __atomic_load_n(&mf_faults.pwrites,__ATOMIC_RELAXED) + after; }
void mf_arm_allocation(unsigned after)
{
    mf_faults.fail_allocation_at = __atomic_load_n(&mf_faults.allocations,__ATOMIC_RELAXED) + after;
}
void mf_disarm(void) { mf_faults.fail_fsync_at = 0; mf_faults.fail_pwrite_at = 0; mf_faults.fail_allocation_at = 0; }
void mf_arm_fsync_hook(mf_fsync_hook_t hook, void *context) { fsync_context = context; fsync_hook = hook; }
void mf_arm_pwrite_hook(mf_pwrite_hook_t hook, void *context) { pwrite_context = context; pwrite_hook = hook; }

void mf_read_page(const mf_t *f, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    int fd = open(f->built.fx->data,O_RDONLY); CHECK(fd >= 0);
    memset(out,0,CANDIDATE_PAGE_BYTES);
    ssize_t n = pread(fd,out,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES));
    CHECK(close(fd) == 0);
    CHECK(n == 0 || n == CANDIDATE_PAGE_BYTES);
}

static void find_pages(mf_t *f)
{
    unsigned found = 0;
    for (uint64_t id = 1; id <= f->built.anchor.page_highwater && found < 2; id++) {
        uint8_t page[CANDIDATE_PAGE_BYTES]; mf_read_page(f,id,page);
        if (read_le(page + 22,2) != 1) continue;
        candidate_page_ref_t ref = {id,read_le(page + 32,8)};
        if (found++ == 0) f->row_page = ref; else f->result_page = ref;
    }
    CHECK(found == 2);
}

static void boot(mf_t *f, const char *prefix, const char *tag, void (*before_checkpoint)(void *, void *), void *context,
    uint64_t txn_highwater)
{
    static const req_t req[5] = {{REQ_COMMIT,STORE_NONE,1,3000,false,true},{REQ_COMMIT,STORE_NONE,1,3000,false,true},
        {REQ_COMMIT,STORE_NONE,1,MF_PAYLOAD,false,true},{REQ_COMMIT,STORE_NONE,1,MF_PAYLOAD,false,true},
        {REQ_COMMIT,STORE_NONE,1,2000,false,true}};
    memset(f,0,sizeof(*f));
    CHECK(snprintf(f->name,sizeof(f->name),"%s-%s-%ld-%u",prefix,tag,(long)getpid(),++serial) > 0);
    scen_t s = {.name = f->name,.requests = 5,.req = req,.before_checkpoint = before_checkpoint,.context = context,
        .txn_highwater = txn_highwater};
    f->built = scen_build(&s); scen_finish(&f->built);
    find_pages(f);
    for (unsigned i = 0; i < MF_ROWS; i++) f->rows[i] = (candidate_mvcc_row_t){mf_key(i),f->row_page};
    fx_payload(3,MF_PAYLOAD,f->initial[0]); fx_payload(4,MF_PAYLOAD,f->initial[1]);
    f->next_owner = 8;
    printf("FIXTURE %s prefix=%s row_page=%" PRIu64 "/%" PRIu64 " result_page=%" PRIu64 "/%" PRIu64 "\n",tag,f->built.fx->wal,
        f->row_page.id,f->row_page.generation,f->result_page.id,f->result_page.generation);
}

void mf_boot(mf_t *f, const char *tag) { boot(f,"task25a",tag,NULL,NULL,0); }

void mf_boot_boundary(mf_t *f, const char *tag, void (*before_checkpoint)(void *, void *), void *context)
{
    boot(f,"task25a-boundary",tag,before_checkpoint,context,0);
}

void mf_boot_highwater(mf_t *f, const char *tag, uint64_t txn_highwater)
{
    boot(f,"task25a-boundary",tag,NULL,NULL,txn_highwater);
}

void mf_free(mf_t *f) { scen_free(&f->built); }

candidate_wal_status_t mf_open(mf_t *f, candidate_mvcc_t **out)
{
    candidate_mvcc_options_t o = {.recovery = fx_options(f->built.fx,f->next_owner++),.rows = f->rows,.row_count = MF_ROWS,
        .result_page = f->result_page,.result_table_id = 900,.wal_budget_bytes = f->wal_budget_bytes};
    o.recovery.io = &HOOKS; o.recovery.memory = &MEMORY;
    candidate_wal_status_t status = candidate_mvcc_open(&o,out);
    printf("OPEN owner=%" PRIu64 " status=%s\n",o.recovery.owner_epoch,mf_status(status));
    return status;
}

candidate_mvcc_key_t mf_key(unsigned row)
{
    static const uint64_t ids[MF_ROWS] = {3,4,50};
    return (candidate_mvcc_key_t){1,ids[row]};
}

candidate_mvcc_write_t mf_write(unsigned row, const uint8_t *bytes, uint32_t size)
{
    return (candidate_mvcc_write_t){mf_key(row),bytes,size};
}

candidate_mvcc_request_t mf_request(uint64_t nonce, const candidate_mvcc_write_t *writes, uint32_t count)
{
    candidate_mvcc_request_t r = {.writes = writes,.count = count};
    for (unsigned i = 0; i < 8; i++) r.nonce[i] = (uint8_t)(nonce >> (8 * i));
    r.nonce[15] = 0x25;
    return r;
}

void mf_fill(uint8_t seed, uint8_t out[MF_PAYLOAD])
{
    for (unsigned i = 0; i < MF_PAYLOAD; i++) out[i] = (uint8_t)(seed * 17 + i);
}

static uint8_t *read_segment(const mf_t *f, size_t *size)
{
    char path[320]; CHECK(snprintf(path,sizeof(path),"%s.wal.%016x",f->built.fx->wal,0u) > 0);
    *size = (size_t)fx_file_bytes(path);
    uint8_t *bytes = malloc(*size + 1); CHECK(bytes != NULL);
    int fd = open(path,O_RDONLY); CHECK(fd >= 0);
    CHECK(pread(fd,bytes,*size,0) == (ssize_t)*size); CHECK(close(fd) == 0);
    char next[320]; CHECK(snprintf(next,sizeof(next),"%s.wal.%016x",f->built.fx->wal,1u) > 0);
    CHECK(access(next,F_OK) != 0);
    return bytes;
}

static void observe_record(const candidate_wal_record_t *r, uint64_t end, uint64_t transaction_id, mf_chain_t *chain, mf_wal_t *wal)
{
    wal->records++; wal->end_lsn = end;
    if (r->transaction_id == 0 && r->type == 16) { wal->reserves++; wal->highwater = read_le(r->payload + 16,8); }
    if (r->type == 10) wal->commits++;
    if (r->type == 11) wal->aborts++;
    if (r->transaction_id != transaction_id || chain->count == MF_RECORDS) return;
    chain->types[chain->count] = r->type; chain->lsns[chain->count++] = r->lsn;
    if (r->type == 10) chain->csn = read_le(r->payload + 32,8);
    if (r->type == 10 || r->type == 11) chain->terminal_end = end;
}

void mf_scan_wal(const mf_t *f, uint64_t transaction_id, mf_chain_t *chain, mf_wal_t *wal)
{
    *chain = (mf_chain_t){0}; *wal = (mf_wal_t){0};
    size_t size; uint8_t *bytes = read_segment(f,&size);
    for (size_t at = 64; at + 96 <= size;) {
        size_t total = (size_t)read_le(bytes + at + 8,4);
        candidate_wal_record_t r;
        if (total < 96 || total > size - at || candidate_wal_decode(fx_uuid,bytes + at,total,&r) != CANDIDATE_WAL_OK) break;
        CHECK(r.lsn == at);
        observe_record(&r,at + total,transaction_id,chain,wal);
        at += total;
    }
    free(bytes);
}

unsigned mf_scan_records(const mf_t *f, uint64_t from_lsn, mf_record_t *out, unsigned capacity)
{
    unsigned count = 0;
    size_t size; uint8_t *bytes = read_segment(f,&size);
    for (size_t at = 64; at + 96 <= size;) {
        size_t total = (size_t)read_le(bytes + at + 8,4);
        candidate_wal_record_t r;
        if (total < 96 || total > size - at || candidate_wal_decode(fx_uuid,bytes + at,total,&r) != CANDIDATE_WAL_OK) break;
        CHECK(r.lsn == at);
        if (r.lsn >= from_lsn) {
            CHECK(count < capacity);
            mf_record_t *o = &out[count++];
            *o = (mf_record_t){.type = r.type,.lsn = r.lsn,.end = at + total,.transaction_id = r.transaction_id};
            for (unsigned i = 0; i < 3 && (size_t)(i + 1) * 8 <= r.payload_bytes; i++) o->words[i] = read_le(r.payload + i * 8,8);
        }
        at += total;
    }
    free(bytes);
    return count;
}

static void decode_slot(const uint8_t *v, uint32_t generation, mf_slot_t *out)
{
    *out = (mf_slot_t){.transaction_id = read_le(v,8),.frozen_csn = read_le(v + 8,8),
        .previous = {{read_le(v + 16,8),read_le(v + 24,8)},(uint32_t)read_le(v + 32,4),(uint32_t)read_le(v + 36,4)},
        .table_id = read_le(v + 40,8),.row_id = read_le(v + 48,8),.size = read_le(v + 56,8),.generation = generation,.bytes = v + 104};
}

unsigned mf_slots(const uint8_t page[CANDIDATE_PAGE_BYTES], mf_slot_t *out, unsigned capacity)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,page,52); crc = crc32c(crc,zero,4); crc = crc32c(crc,page + 56,CANDIDATE_PAGE_BYTES - 56);
    CHECK(memcmp(page + 16,"SLPG",4) == 0 && read_le(page + 22,2) == 1 && crc == read_le(page + 52,4));
    unsigned count = (unsigned)read_le(page + 64,4);
    CHECK(count <= capacity);
    size_t at = 72 + (size_t)count * 8;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *slot = page + 72 + (size_t)i * 8;
        CHECK(read_le(slot,2) == at);
        decode_slot(page + at,(uint32_t)read_le(slot + 4,4),&out[i]);
        CHECK(read_le(page + at + 72,4) == 1 && read_le(page + at + 80,8) == out[i].size);
        at += (size_t)read_le(slot + 2,2);
    }
    CHECK(at == 64 + read_le(page + 48,4));
    return count;
}

const mf_slot_t *mf_find_slot(const mf_slot_t *slots, unsigned count, uint64_t transaction_id, uint64_t table_id)
{
    for (unsigned i = 0; i < count; i++)
        if (slots[i].transaction_id == transaction_id && slots[i].table_id == table_id) return &slots[i];
    return NULL;
}

const char *mf_status(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

void mf_expect(candidate_wal_status_t actual, candidate_wal_status_t expected, const char *function, int line, const char *call)
{
    __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);
    if (actual == expected) return;
    fprintf(stderr,"%s:%d: %s returned %s, expected %s\n",function,line,call,mf_status(actual),mf_status(expected));
    exit(1);
}
