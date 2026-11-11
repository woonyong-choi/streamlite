#include "test_candidate_buffer_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_checkpoint.h"
#include "storage/candidate_wal_witness.h"
#include "../src/storage/candidate_outcome_internal.h"
#include "../src/storage/candidate_recovery_internal.h"
#include "../src/storage/candidate_redo_internal.h"

#define TRACE_HEADER_BYTES 64u
#define WAL_INTENT_BYTES 112u
#define CONTROL_INTENT_BYTES 192u
#define CONTROL_TORN_BYTES 100u

bf_t *bf_env;

static const char *KIND_NAMES[EV_KINDS] = {"none","wal-write","wal-sync","data-write","data-sync","control-write","control-sync","open",
    "directory-sync","done","fault"};

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

typedef struct { uint64_t seq, inode; bf_kind_t kind; bf_fault_t mode; } io_attempt_t;

static void trace(bf_t *b, bf_event_t e, const void *side, size_t side_bytes, uint32_t aux)
{
    if (b->trace_fd < 0) return;
    int saved = errno;
    uint8_t header[TRACE_HEADER_BYTES] = {'T','2','4','E'};
    write_le(header + 4,e.kind,4); write_le(header + 8,++b->trace_seq,8); write_le(header + 16,e.inode,8);
    write_le(header + 24,e.offset,8); write_le(header + 32,e.size,8); write_le(header + 40,e.id,8); write_le(header + 48,(uint64_t)getpid(),8);
    write_le(header + 56,side_bytes,4); write_le(header + 60,aux,4);
    CHECK(write(b->trace_fd,header,sizeof(header)) == (ssize_t)sizeof(header));
    if (side_bytes > 0) CHECK(write(b->trace_fd,side,side_bytes) == (ssize_t)side_bytes);
    errno = saved;
}

static io_attempt_t record(bf_event_t e, const void *side, size_t side_bytes)
{
    io_attempt_t a = {0,e.inode,e.kind,FAULT_ERROR};
    bf_t *b = bf_env;
    if (b == NULL) return a;
    if (e.kind == EV_WAL_SYNC) b->wal_syncs++;
    if (e.kind == EV_WAL_WRITE || e.kind == EV_DATA_WRITE || e.kind == EV_CONTROL_WRITE) b->changes++;
    trace(b,e,side,side_bytes,0);
    a.seq = b->trace_seq;
    if (!b->is_tracing) return a;
    b->counts[e.kind]++;
    if (b->event_count < BF_EVENTS) b->events[b->event_count++] = e;
    return a;
}

static void finished(const io_attempt_t *a, uint64_t bytes, int result)
{
    bf_t *b = bf_env;
    if (b != NULL && a->seq != 0) trace(b,(bf_event_t){EV_DONE,a->seq,bytes,a->kind,a->inode},NULL,0,(uint32_t)result);
}

static bool triggered(io_attempt_t *a, const uint8_t *p, size_t n)
{
    bf_t *b = bf_env;
    if (b == NULL || !b->is_tracing || b->trigger.kind != a->kind || b->trigger.has_fired) return false;
    bool is_frame = a->kind == EV_WAL_WRITE && n >= 96 && memcmp(p,"SLWR",4) == 0;
    unsigned type = is_frame ? (unsigned)read_le(p + 6,2) : 0, chunk = is_frame ? (unsigned)read_le(p + 86,2) : 0;
    if (b->trigger.type != 0) {
        if (type != b->trigger.type || (b->trigger.chunk != 0 && chunk != b->trigger.chunk)) return false;
        if (++b->trigger.matched != b->trigger.at) return false;
    } else if (b->counts[a->kind] != b->trigger.at) return false;
    b->trigger.has_fired = true; a->mode = b->trigger.mode;
    uint8_t attempt[8]; write_le(attempt,a->seq,8);
    trace(b,(bf_event_t){EV_FAULT,b->trigger.at,b->trigger.mode,a->kind,a->inode},attempt,sizeof(attempt),type);
    printf("FAULT-HIT kind=%s at=%u mode=%u type=%u chunk=%u inode=%llu pid=%ld\n",KIND_NAMES[a->kind],b->trigger.at,(unsigned)a->mode,
        type,chunk,(unsigned long long)a->inode,(long)getpid());
    fflush(stdout);
    return true;
}

static bf_kind_t kind_of(int fd, bool is_write)
{
    struct stat st; CHECK(fstat(fd,&st) == 0);
    if (!is_write && S_ISDIR(st.st_mode)) return EV_DIRECTORY_SYNC;
    if (bf_env != NULL && st.st_ino == bf_env->control_inode) return is_write ? EV_CONTROL_WRITE : EV_CONTROL_SYNC;
    return is_write ? EV_WAL_WRITE : EV_WAL_SYNC;
}

static bool is_viewed(const bf_t *b, int fd)
{
    return b != NULL && b->view.mode != VIEW_OFF && b->wal_syncs >= b->view.from_sync && inode_of(fd) == b->view.inode;
}

static void install_swap(bf_t *b, const char *phase)
{
    b->swap.is_armed = false; b->swap.on_verify = false; b->swap.phase = phase; b->swap.verifies = b->verifies;
    bf_view_altered(b,b->swap.lsn,b->swap.offset,true);
    b->view.mode = VIEW_ALTERED; b->view.from_sync = 0; b->view.reads = 0;
    printf("SWAP phase=%s lsn=%llu offset=%zu verifies=%u wal_end=%llu view_bytes=%zu\n",phase,(unsigned long long)b->swap.lsn,
        b->swap.offset,b->verifies,(unsigned long long)candidate_wal_info(b->wal).valid_end_lsn,b->view.size);
    fflush(stdout);
}

void bf_swap_at_scan(bf_t *b, uint64_t lsn, size_t offset, unsigned after_syncs, unsigned at_scan)
{
    b->swap = (bf_swap_t){.lsn = lsn,.offset = offset,.from_sync = b->wal_syncs + after_syncs,.at_scan = at_scan,.is_armed = true};
}

void bf_swap_on_verify(bf_t *b, uint64_t lsn, size_t offset)
{
    b->swap = (bf_swap_t){.lsn = lsn,.offset = offset,.on_verify = true};
}

static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset)
{
    bf_t *b = bf_env;
    bool is_scan = b != NULL && b->swap.is_armed && offset == 0 && b->wal_syncs >= b->swap.from_sync && inode_of(fd) == b->view.inode;
    if (is_scan && ++b->swap.scans == b->swap.at_scan) install_swap(b,"proof");
    if (!is_viewed(b,fd)) return pread(fd,p,n,offset);
    b->view.reads++;
    if (b->view.mode == VIEW_ERROR) { errno = EIO; return -1; }
    if (b->view.mode == VIEW_EOF || (uint64_t)offset >= b->view.size) return 0;
    size_t left = b->view.size - (size_t)offset, copied = n < left ? n : left;
    memcpy(p,b->view.bytes + offset,copied);
    return (ssize_t)copied;
}

static bool alter_payload(bf_mutate_t *m, candidate_wal_record_t *r, uint8_t *payload)
{
    bool is_target = !m->has_fired && ((m->mode == MUTATE_IMAGE && r->type == 4) || (m->mode == MUTATE_CHUNK && r->type == 13) ||
        (m->mode == MUTATE_END && r->type == 14));
    if (is_target && r->type == 4) { payload[m->offset] ^= 0x5a; bf_set_lsn(payload + 32,bf_lsn(payload + 32)); }
    if (is_target && r->type == 13) {
        CHECK(read_le(payload + 6,2) == 1 && read_le(payload + 20,4) >= 2 && read_le(payload + 48,8) < read_le(payload + 72,8));
        write_le(payload + 48,read_le(payload + 72,8),8);
    }
    if (is_target && r->type == 14) payload[32] ^= 1;
    if (is_target) m->has_fired = true;
    if (r->type == 3) { m->is_tracking = true; m->group_crc = candidate_wal_witness_crc(0,r); return is_target; }
    if (r->type == 12) { memset(m->digest,0,32); m->is_tracking = true; }
    if (!m->is_tracking) return is_target;
    if (r->type == 4 || r->type == 5) m->group_crc = candidate_wal_witness_crc(m->group_crc,r);
    if (r->type == 8) { write_le(payload + 16,m->group_crc,4); m->is_tracking = false; return m->has_fired; }
    if (r->type == 12 || r->type == 13) OK(candidate_checkpoint_digest(m->digest,payload,r->payload_bytes,m->digest));
    if (r->type == 14 && m->mode == MUTATE_CHUNK) { memcpy(payload + 32,m->digest,32); m->is_tracking = false; return m->has_fired; }
    if (r->type == 14) m->is_tracking = false;
    return is_target;
}

static ssize_t mutated_write(bf_t *b, int fd, const void *p, size_t n, off_t offset)
{
    static uint8_t payload[CANDIDATE_WAL_RECORD_BYTES], frame[CANDIDATE_WAL_RECORD_BYTES];
    candidate_wal_record_t r;
    if (n > sizeof(frame) || candidate_wal_decode(fx_uuid,p,n,&r) != CANDIDATE_WAL_OK) return pwrite(fd,p,n,offset);
    memcpy(payload,r.payload,r.payload_bytes); r.payload = payload;
    if (!alter_payload(&b->mutate,&r,payload)) return pwrite(fd,p,n,offset);
    size_t written; OK(candidate_wal_encode(fx_uuid,&r,frame,sizeof(frame),&written)); CHECK(written == n);
    printf("MUTATED type=%u lsn=%llu\n",r.type,(unsigned long long)r.lsn);
    return pwrite(fd,frame,n,offset);
}

static ssize_t fault_write(int fd, const void *p, size_t n, off_t offset, const io_attempt_t *a, int error)
{
    if (a->mode == FAULT_CRASH_BEFORE) _exit(77);
    size_t torn = a->kind == EV_CONTROL_WRITE ? CONTROL_TORN_BYTES : n / 2;
    size_t bytes = a->mode == FAULT_CRASH_AFTER ? n : a->mode == FAULT_TORN ? torn : a->mode == FAULT_SHORT ? n / 2 : 0;
    if (bytes > 0) CHECK(pwrite(fd,p,bytes,offset) == (ssize_t)bytes);
    bool is_crash = a->mode == FAULT_CRASH_AFTER || a->mode == FAULT_TORN;
    finished(a,bytes,is_crash ? 0 : error);
    if (is_crash) _exit(a->mode == FAULT_TORN ? 79 : 78);
    errno = error;
    return -1;
}

static int fault_sync(int fd, const io_attempt_t *a)
{
    if (a->mode == FAULT_CRASH_BEFORE) _exit(77);
    if (a->mode == FAULT_CRASH_AFTER) { CHECK(fsync(fd) == 0); finished(a,0,0); _exit(78); }
    finished(a,0,EIO);
    errno = EIO;
    return -1;
}

static int error_of(int saved) { return saved != 0 ? saved : EIO; }

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    bf_kind_t kind = kind_of(fd,true);
    size_t intent = kind == EV_CONTROL_WRITE ? CONTROL_INTENT_BYTES : WAL_INTENT_BYTES;
    io_attempt_t a = record((bf_event_t){kind,(uint64_t)offset,n,0,(uint64_t)inode_of(fd)},p,n < intent ? n : intent);
    if (triggered(&a,p,n)) return fault_write(fd,p,n,offset,&a,ENOSPC);
    bool is_mutating = kind == EV_WAL_WRITE && bf_env != NULL && bf_env->mutate.mode != MUTATE_OFF;
    ssize_t written = is_mutating ? mutated_write(bf_env,fd,p,n,offset) : pwrite(fd,p,n,offset);
    int saved = errno;
    finished(&a,written < 0 ? 0 : (uint64_t)written,written < 0 ? error_of(saved) : 0);
    errno = saved;
    return written;
}

static void inject_append(bf_t *b, int fd)
{
    b->is_injecting = false; b->changes++;
    candidate_wal_info_t info = candidate_wal_info(b->wal);
    uint8_t body[16] = {0}, frame[256]; size_t written;
    candidate_wal_record_t r = {.type = 2,.lsn = info.valid_end_lsn,.transaction_id = b->next_transaction++,
        .owner_epoch = b->owner,.payload = body,.payload_bytes = sizeof(body)};
    OK(candidate_wal_encode(fx_uuid,&r,frame,sizeof(frame),&written));
    CHECK(pwrite(fd,frame,written,(off_t)(info.valid_end_lsn % CANDIDATE_WAL_SEGMENT_BYTES)) == (ssize_t)written);
    printf("INJECTED lsn=%llu bytes=%zu\n",(unsigned long long)info.valid_end_lsn,written);
}

static int hook_fsync(int fd)
{
    bf_kind_t kind = kind_of(fd,false);
    if (kind == EV_WAL_SYNC && bf_env != NULL && bf_env->is_injecting) inject_append(bf_env,fd);
    io_attempt_t a = record((bf_event_t){kind,0,0,0,(uint64_t)inode_of(fd)},NULL,0);
    if (triggered(&a,NULL,0)) return fault_sync(fd,&a);
    int result = fsync(fd), saved = errno;
    finished(&a,0,result == 0 ? 0 : error_of(saved));
    errno = saved;
    return result;
}

static const candidate_wal_io_t HOOKS = {hook_pread,hook_pwrite,hook_fsync,ftruncate};

static candidate_wal_status_t data_read(void *context, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES], bool *present)
{
    bf_t *b = context;
    memset(out,0,CANDIDATE_PAGE_BYTES);
    ssize_t n = pread(b->data_fd,out,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES));
    if (n < 0) return CANDIDATE_WAL_IO;
    *present = n != 0;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t data_write(void *context, uint64_t id, const uint8_t page[CANDIDATE_PAGE_BYTES])
{
    bf_t *b = context;
    off_t offset = (off_t)((id - 1) * CANDIDATE_PAGE_BYTES);
    io_attempt_t a = record((bf_event_t){EV_DATA_WRITE,(uint64_t)offset,CANDIDATE_PAGE_BYTES,id,(uint64_t)b->data_inode},page,
        CANDIDATE_PAGE_BYTES);
    unsigned index = b->data_writes++;
    if (index < BF_WRITES) { memcpy(b->written[index],page,CANDIDATE_PAGE_BYTES); b->written_ids[index] = id; }
    if (triggered(&a,page,CANDIDATE_PAGE_BYTES)) {
        fault_write(b->data_fd,page,CANDIDATE_PAGE_BYTES,offset,&a,EIO);
        return CANDIDATE_WAL_IO;
    }
    if (b->on_write != NULL) b->on_write(b,id,index);
    ssize_t written = pwrite(b->data_fd,page,CANDIDATE_PAGE_BYTES,offset);
    int saved = errno;
    finished(&a,written < 0 ? 0 : (uint64_t)written,written == CANDIDATE_PAGE_BYTES ? 0 : error_of(saved));
    return written == CANDIDATE_PAGE_BYTES ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static candidate_wal_status_t data_sync(void *context)
{
    bf_t *b = context;
    io_attempt_t a = record((bf_event_t){EV_DATA_SYNC,0,0,0,(uint64_t)b->data_inode},NULL,0);
    if (triggered(&a,NULL,0)) {
        fault_sync(b->data_fd,&a);
        return CANDIDATE_WAL_IO;
    }
    int result = fsync(b->data_fd), saved = errno;
    finished(&a,0,result == 0 ? 0 : error_of(saved));
    return result == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static void *counted_allocate(void *context, size_t size)
{
    bf_memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    m->sizes[m->size_count++ % BF_SIZES] = size;
    void *p = malloc(size);
    if (p != NULL) m->live += size;
    return p;
}

static void counted_release(void *context, void *p, size_t size)
{
    bf_memory_t *m = context;
    if (p == NULL) return;
    m->releases++;
    if (m->on_release != NULL) m->on_release(bf_env,size);
    m->live -= size;
    if (!m->is_quarantine) { free(p); return; }
    CHECK(m->held_count < BF_HELD);
    m->held[m->held_count] = p; m->held_sizes[m->held_count++] = size;
}

void bf_release_held(bf_t *b)
{
    for (unsigned i = 0; i < b->memory.held_count; i++) free(b->memory.held[i]);
    b->memory.held_count = 0; b->memory.is_quarantine = false;
}

void bf_mutate(bf_t *b, bf_mutate_mode_t mode, size_t offset)
{
    b->mutate = (bf_mutate_t){.mode = mode,.offset = offset};
}

static void *plain_allocate(void *context, size_t size) { (void)context; return malloc(size); }
static void plain_release(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }
static const candidate_wal_memory_t PLAIN = {NULL,plain_allocate,plain_release};

candidate_wal_status_t bf_prove(bf_t *b, const candidate_control_state_t *anchor, const char *label)
{
    candidate_redo_options_t redo = {.checkpoint = anchor,.target = {b,data_read,data_write,data_sync},.memory = &PLAIN};
    memcpy(redo.uuid,fx_uuid,16);
    candidate_redo_preview_t *preview = NULL;
    candidate_wal_status_t preview_status = candidate_redo_preview_open(b->wal,&redo,&preview);
    candidate_catalog_load_t load = {.root = anchor->outcome_root,.load = candidate_redo_preview_load,.context = preview,
        .memory = &PLAIN};
    memcpy(load.uuid,fx_uuid,16);
    candidate_recovery_info_t info = {.anchor = *anchor};
    candidate_wal_status_t state = CANDIDATE_WAL_STOPPED, outcome = CANDIDATE_WAL_STOPPED;
    if (preview_status == CANDIDATE_WAL_OK) state = candidate_recovery_validate_preview(b->wal,anchor,&load,&info);
    if (state == CANDIDATE_WAL_OK) {
        candidate_outcome_witness_options_t outcomes = {.catalog = load,.checkpoint = anchor};
        outcome = candidate_outcome_preview(b->wal,&outcomes,b->owner);
    }
    candidate_redo_preview_close(&preview);
    printf("PROVE %s checkpoint=%llu begin=%llu preview=%s state=%s outcome=%s live=%u versions=%llu\n",label,
        (unsigned long long)anchor->checkpoint_id,(unsigned long long)anchor->checkpoint_begin,bf_status(preview_status),
        bf_status(state),bf_status(outcome),info.live_pages,(unsigned long long)info.versions);
    return preview_status != CANDIDATE_WAL_OK ? preview_status : state != CANDIDATE_WAL_OK ? state : outcome;
}

static candidate_wal_status_t verify_witness(bf_t *b, const candidate_control_state_t *anchor)
{
    candidate_witness_options_t options = {.enable_system_groups = true}; memcpy(options.uuid,fx_uuid,16);
    candidate_wal_witness_t *witness = NULL;
    candidate_wal_status_t status = candidate_wal_witness_analyze_checkpoint(b->wal,&options,anchor,&witness);
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_system_info_t system = candidate_wal_witness_system_info(witness);
        if (!system.has_reservation || !system.has_epoch) status = CANDIDATE_WAL_CORRUPT;
    }
    candidate_wal_witness_close(&witness);
    printf("VERIFY weak checkpoint=%llu status=%s\n",(unsigned long long)anchor->checkpoint_id,bf_status(status));
    return status;
}

static candidate_wal_status_t verify_checkpoint(void *context, const candidate_control_state_t *anchor)
{
    bf_t *b = context;
    b->verifies++;
    if (b->swap.on_verify) install_swap(b,"control");
    return b->is_weak_verifier ? verify_witness(b,anchor) : bf_prove(b,anchor,"control");
}

static void boot_scenario(bf_t *b, const char *tag, unsigned requests)
{
    static req_t req[16];
    CHECK(requests >= 1 && requests <= 16);
    for (unsigned i = 0; i < requests; i++) req[i] = (req_t){REQ_COMMIT,STORE_NONE,1,2600,false,true};
    char name[128]; CHECK(snprintf(name,sizeof(name),"task24-%s-%ld",tag,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = requests,.req = req};
    b->built = scen_build(&s); b->fx = b->built.fx;
    scen_finish(&b->built);
}

void bf_boot(bf_t *b, const char *tag, unsigned requests)
{
    memset(b,0,sizeof(*b)); b->data_fd = -1; b->trace_fd = -1; bf_env = b;
    b->written = malloc(BF_WRITES * CANDIDATE_PAGE_BYTES); CHECK(b->written != NULL);
    CHECK(snprintf(b->tag,sizeof(b->tag),"%s",tag) > 0);
    boot_scenario(b,tag,requests);
    OK(bf_recover(b,8));
    char path[300]; CHECK(snprintf(path,sizeof(path),"%s.trace",b->fx->wal) > 0);
    b->trace_fd = open(path,O_CREAT | O_EXCL | O_WRONLY | O_APPEND,0600); CHECK(b->trace_fd >= 0);
    printf("FIXTURE %s prefix=%s trace=%s\n",tag,b->fx->wal,path);
    b->owner = 8; b->fx->owner = 8;
    b->next_transaction = b->built.spec.requests + 8;
    bf_open_handles(b);
    OK(bf_open_pool(b));
    for (uint64_t id = 1; id <= candidate_buffer_info(b->pool).anchor.page_highwater && b->page_count < BF_PAGES; id++) {
        uint8_t page[CANDIDATE_PAGE_BYTES]; bool present = false;
        bf_read_data(b,id,page,&present);
        if (present && page[22] == 1) b->pages[b->page_count++] = id;
    }
    CHECK(b->page_count >= 2);
}

void bf_open_handles(bf_t *b)
{
    b->memory = (bf_memory_t){0};
    b->allocator = (candidate_wal_memory_t){&b->memory,counted_allocate,counted_release};
    struct stat st; CHECK(stat(b->fx->control,&st) == 0); b->control_inode = st.st_ino;
    char segment[300]; CHECK(snprintf(segment,sizeof(segment),"%s.wal.0000000000000000",b->fx->wal) > 0);
    CHECK(stat(segment,&st) == 0); b->view.inode = st.st_ino;
    candidate_wal_options_t wo = {.owner_epoch = b->owner,.io = &HOOKS}; memcpy(wo.uuid,fx_uuid,16);
    OK(candidate_wal_open(b->fx->wal,&wo,&b->wal));
    b->fx->w = b->wal;
    b->data_fd = open(b->fx->data,O_RDWR); CHECK(b->data_fd >= 0); b->data_inode = inode_of(b->data_fd);
    candidate_control_options_t co = {.backend = CANDIDATE_CONTROL_WAL,.io = &HOOKS,.verify = verify_checkpoint,.context = b};
    memcpy(co.uuid,fx_uuid,16);
    OK(candidate_control_open(b->fx->control,&co,&b->control));
    candidate_wal_info_t info = candidate_wal_info(b->wal);
    char directory[300]; CHECK(snprintf(directory,sizeof(directory),"%s",b->fx->wal) > 0);
    char *slash = strrchr(directory,'/'); if (slash != NULL) *slash = 0; else CHECK(snprintf(directory,sizeof(directory),".") > 0);
    CHECK(stat(directory,&st) == 0);
    uint8_t inodes[24];
    write_le(inodes,(uint64_t)b->control_inode,8); write_le(inodes + 8,(uint64_t)b->data_inode,8); write_le(inodes + 16,(uint64_t)st.st_ino,8);
    trace(b,(bf_event_t){EV_OPEN,info.durable_end_lsn,info.valid_end_lsn,b->owner,(uint64_t)b->view.inode},inodes,sizeof(inodes),0);
}

candidate_buffer_options_t bf_options(bf_t *b)
{
    candidate_buffer_options_t o = {.owner_epoch = b->owner,.wal = b->wal,.control = b->control,
        .target = {b,data_read,data_write,data_sync},.memory = &b->allocator,.frames = 8,.leases = 4};
    memcpy(o.uuid,fx_uuid,16);
    return o;
}

candidate_wal_status_t bf_open_pool(bf_t *b)
{
    candidate_buffer_options_t o = bf_options(b);
    return candidate_buffer_open(&o,&b->pool);
}

void bf_close(bf_t *b)
{
    if (b->pool != NULL) OK(candidate_buffer_close(&b->pool));
    if (b->control != NULL) OK(candidate_control_close(&b->control));
    if (b->wal != NULL) CHECK(candidate_wal_close(&b->wal) == CANDIDATE_WAL_OK);
    b->fx->w = NULL;
    if (b->data_fd >= 0) { CHECK(close(b->data_fd) == 0); b->data_fd = -1; }
}

void bf_free(bf_t *b)
{
    bf_close(b);
    if (b->trace_fd >= 0) { CHECK(fsync(b->trace_fd) == 0); CHECK(close(b->trace_fd) == 0); b->trace_fd = -1; }
    scen_free(&b->built);
    free(b->written); free(b->view.bytes);
    bf_env = NULL;
}

static void snapshot_set(bf_t *b, const char *label, const char *phase);

candidate_wal_status_t bf_recover(bf_t *b, uint64_t owner)
{
    bool is_traced = b->trace_fd >= 0;
    unsigned n = is_traced ? ++b->recoveries : 0;
    char label[32]; CHECK(snprintf(label,sizeof(label),"recover%u-pre",n) > 0);
    if (is_traced) snapshot_set(b,label,"pre");
    candidate_recovery_options_t o = fx_options(b->fx,owner);
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    if (r != NULL) CHECK(candidate_recovery_close(&r) == CANDIDATE_WAL_OK);
    b->changes++;
    printf("RECOVER owner=%llu status=%s\n",(unsigned long long)owner,bf_status(status));
    b->is_post = is_traced && status == CANDIDATE_WAL_OK; b->post_changes = b->changes;
    CHECK(snprintf(label,sizeof(label),"recover%u-post",n) > 0);
    if (b->is_post) snapshot_set(b,label,"post");
    return status;
}

void bf_reattach(bf_t *b, uint64_t owner)
{
    b->owner = owner; b->fx->owner = owner;
    bf_open_handles(b);
    OK(bf_open_pool(b));
}

void bf_restart(bf_t *b, uint64_t owner)
{
    bf_close(b);
    OK(bf_recover(b,owner));
    bf_reattach(b,owner);
}

void bf_trace(bf_t *b, bool on)
{
    b->is_tracing = on;
    if (on) { b->event_count = 0; memset(b->counts,0,sizeof(b->counts)); }
}

void bf_dump(const bf_t *b, const char *label)
{
    for (unsigned i = 0; i < b->event_count; i++)
        printf("EVENT %s %u %s offset=%llu size=%llu page=%llu\n",label,i,KIND_NAMES[b->events[i].kind],
            (unsigned long long)b->events[i].offset,(unsigned long long)b->events[i].size,(unsigned long long)b->events[i].id);
    fflush(stdout);
}

unsigned bf_find(const bf_t *b, unsigned from, bf_kind_t kind)
{
    for (unsigned i = from; i < b->event_count; i++) if (b->events[i].kind == kind) return i;
    return UINT32_MAX;
}

void bf_arm(bf_t *b, bf_kind_t kind, unsigned at, bf_fault_t mode)
{
    b->trigger = (bf_trigger_t){.kind = kind,.at = at,.mode = mode};
}

void bf_arm_record(bf_t *b, uint16_t type, uint16_t chunk, unsigned at, bf_fault_t mode)
{
    b->trigger = (bf_trigger_t){.kind = EV_WAL_WRITE,.at = at,.mode = mode,.type = type,.chunk = chunk};
}

static void load_segment(bf_t *b)
{
    char segment[300]; CHECK(snprintf(segment,sizeof(segment),"%s.wal.0000000000000000",b->fx->wal) > 0);
    CHECK(candidate_wal_info(b->wal).valid_end_lsn < CANDIDATE_WAL_SEGMENT_BYTES);
    b->view.size = (size_t)fx_file_bytes(segment);
    free(b->view.bytes); b->view.bytes = malloc(b->view.size); CHECK(b->view.bytes != NULL);
    int fd = open(segment,O_RDONLY); CHECK(fd >= 0);
    CHECK(pread(fd,b->view.bytes,b->view.size,0) == (ssize_t)b->view.size); CHECK(close(fd) == 0);
}

static void reseal(uint8_t *payload, const candidate_wal_record_t *r, uint32_t *group, uint64_t *transactions, uint32_t *chains)
{
    if (r->type == 8) write_le(payload + 16,*group,4);
    unsigned t = 0;
    while (r->transaction_id != 0 && t < 64 && transactions[t] != 0 && transactions[t] != r->transaction_id) t++;
    CHECK(t < 64);
    if (r->transaction_id != 0 && (r->type == 10 || r->type == 11)) write_le(payload + 24,chains[t],4);
    if (r->type == 3) *group = candidate_wal_witness_crc(0,r);
    else if (r->type != 8) *group = candidate_wal_witness_crc(*group,r);
    if (r->transaction_id != 0) { transactions[t] = r->transaction_id; chains[t] = candidate_wal_witness_crc(chains[t],r); }
}

void bf_view_altered(bf_t *b, uint64_t lsn, size_t offset, bool keep_checksums)
{
    load_segment(b);
    static uint8_t payload[CANDIDATE_WAL_RECORD_BYTES];
    uint64_t transactions[64] = {0}; uint32_t chains[64] = {0}, group = 0; bool found = false;
    for (size_t at = 64; at < b->view.size && b->view.bytes[at] != 0;) {
        size_t total = (size_t)read_le(b->view.bytes + at + 8,4);
        candidate_wal_record_t r;
        OK(candidate_wal_decode(fx_uuid,b->view.bytes + at,total,&r));
        if (r.lsn == lsn && !keep_checksums) { b->view.bytes[at + 80 + offset] ^= 0x5a; found = true; break; }
        memcpy(payload,r.payload,r.payload_bytes); r.payload = payload;
        if (r.lsn == lsn) {
            payload[offset] ^= 0x5a; found = true;
            if (r.type == 4) bf_set_lsn(payload + 32,bf_lsn(payload + 32));
        }
        reseal(payload,&r,&group,transactions,chains);
        size_t written;
        OK(candidate_wal_encode(fx_uuid,&r,b->view.bytes + at,total,&written)); CHECK(written == total);
        at += total;
    }
    CHECK(found);
    printf("VIEW altered lsn=%llu offset=%zu valid=%d\n",(unsigned long long)lsn,offset,keep_checksums);
}

void bf_view_set(bf_t *b, bf_view_mode_t mode, unsigned after_syncs)
{
    if (mode == VIEW_ERROR || mode == VIEW_EOF) load_segment(b);
    b->view.mode = mode; b->view.from_sync = b->wal_syncs + after_syncs; b->view.reads = 0;
}

void bf_snapshot(bf_t *b, const char *label)
{
    char target[320]; CHECK(snprintf(target,sizeof(target),"%s-%s.pages",b->fx->wal,label) > 0);
    fx_copy_file(b->fx->data,target);
    printf("SNAPSHOT %s path=%s\n",label,target);
}

static void snapshot_file(const char *source, const char *target)
{
    uint8_t digest[32]; char hex[65];
    fx_copy_file(source,target); fx_sha256_file(target,digest);
    for (unsigned i = 0; i < 32; i++) CHECK(snprintf(hex + 2 * i,3,"%02x",digest[i]) == 2);
    printf("RAW %s sha256=%s bytes=%llu\n",target,hex,(unsigned long long)fx_file_bytes(target));
}

void bf_snapshot_all(bf_t *b, const char *label)
{
    snapshot_set(b,label,b->is_post && b->changes == b->post_changes ? "post" : "live");
}

static void snapshot_set(bf_t *b, const char *label, const char *phase)
{
    char source[320], target[400];
    if (b->trace_fd >= 0) CHECK(fsync(b->trace_fd) == 0);
    for (unsigned id = 0;; id++) {
        CHECK(snprintf(source,sizeof(source),"%s.wal.%016x",b->fx->wal,id) > 0);
        if (access(source,F_OK) != 0) { CHECK(id > 0); break; }
        CHECK(snprintf(target,sizeof(target),"%s-raw-%s.wal.%016x",b->fx->wal,label,id) > 0);
        snapshot_file(source,target);
    }
    const char *suffixes[3] = {"pages","control","trace"};
    const char *sources[3] = {b->fx->data,b->fx->control,NULL};
    CHECK(snprintf(source,sizeof(source),"%s.trace",b->fx->wal) > 0); sources[2] = source;
    for (unsigned i = 0; i < 3; i++) {
        CHECK(snprintf(target,sizeof(target),"%s-raw-%s.%s",b->fx->wal,label,suffixes[i]) > 0);
        snapshot_file(sources[i],target);
    }
    printf("SNAPSHOT-SET %s prefix=%s-raw-%s phase=%s\n",label,b->fx->wal,label,phase);
    b->has_snapshot = true; b->snapshot_changes = b->changes;
    fflush(stdout);
}

candidate_wal_status_t bf_recover_view(bf_t *b, const char *label, uint64_t owner, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    CHECK(b->view.bytes != NULL && b->view.size > 0);
    char prefix[320], wal[340], control[340], data[340];
    CHECK(snprintf(prefix,sizeof(prefix),"%s-%s",b->fx->wal,label) > 0);
    CHECK(snprintf(wal,sizeof(wal),"%s.wal.0000000000000000",prefix) > 0);
    CHECK(snprintf(control,sizeof(control),"%s.control",prefix) > 0 && snprintf(data,sizeof(data),"%s.pages",prefix) > 0);
    int fd = open(wal,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(fd >= 0);
    CHECK(write(fd,b->view.bytes,b->view.size) == (ssize_t)b->view.size && fsync(fd) == 0 && close(fd) == 0);
    fx_copy_file(b->fx->control,control); fx_copy_file(b->fx->data,data);
    uint8_t digest[32]; fx_sha256_file(wal,digest);
    printf("ALTFILE %s sha256=",wal);
    for (unsigned i = 0; i < 32; i++) printf("%02x",digest[i]);
    printf(" bytes=%zu\n",b->view.size);
    candidate_recovery_options_t o = {.owner_epoch = owner,.wal_prefix = prefix,.control_path = control,.data_path = data};
    memcpy(o.uuid,fx_uuid,16);
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    if (r != NULL) CHECK(candidate_recovery_close(&r) == CANDIDATE_WAL_OK);
    fd = open(data,O_RDONLY); CHECK(fd >= 0);
    memset(out,0,CANDIDATE_PAGE_BYTES);
    CHECK(pread(fd,out,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)) == CANDIDATE_PAGE_BYTES && close(fd) == 0);
    printf("ALTERNATIVE %s prefix=%s recovery=%s page=%llu\n",label,prefix,bf_status(status),(unsigned long long)id);
    return status;
}

const char *bf_status(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

void bf_expect_status(candidate_wal_status_t actual, candidate_wal_status_t expected, const char *function, int line,
    const char *call)
{
    checks++;
    if (actual == expected) return;
    fprintf(stderr,"%s:%d: %s returned %s, expected %s\n",function,line,call,bf_status(actual),bf_status(expected));
    exit(1);
}

void bf_report(const bf_t *b, const char *label, candidate_wal_status_t status)
{
    printf("CASE %s status=%s prefix=%s\n",label,bf_status(status),b->fx->wal);
    fflush(stdout);
}
