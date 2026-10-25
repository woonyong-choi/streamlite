#define CANDIDATE_RECOVERY_TEST_MAIN recovery_snapshot_regression_main
#include "test_candidate_recovery.c"
#include <sys/file.h>
#include "../src/storage/candidate_wal_tail_internal.h"

typedef struct { fixture_t files; uint64_t cut, original_end; unsigned segments; } tail_fixture_t;
static ino_t wal_inodes[64];
static unsigned wal_count, truncates, directory_syncs, wal_syncs, wal_writes;
static unsigned fail_truncate, fail_directory, fail_barrier, stop_at, commit_reads, change_view;
static bool fail_reading;

static bool wal_file(int fd)
{
    struct stat st; CHECK(fstat(fd,&st) == 0);
    for (unsigned i = 0; i < wal_count; i++) if (st.st_ino == wal_inodes[i]) return true;
    return false;
}

static ssize_t repair_read(int fd, void *p, size_t size, off_t offset)
{
    if (fail_reading) { errno = EIO; return -1; }
    ssize_t n = pread(fd,p,size,offset);
    uint8_t *bytes = p;
    if (n >= 96 && size > 80 && memcmp(bytes,"SLWR",4) == 0 && read_le(bytes + 6,2) == 10) {
        commit_reads++;
        if (commit_reads == change_view) {
            candidate_wal_record_t r; OK(candidate_wal_decode(uuid,bytes,(size_t)n,&r));
            write_le(bytes + 80 + 32,2,8); size_t written;
            uint8_t encoded[1024]; OK(candidate_wal_encode(uuid,&r,encoded,sizeof(encoded),&written));
            CHECK(written == (size_t)n); memcpy(bytes,encoded,written);
        }
    }
    return n;
}

static ssize_t repair_write(int fd, const void *p, size_t size, off_t offset)
{
    ssize_t n = pwrite(fd,p,size,offset);
    if (wal_file(fd)) { wal_writes++; if (stop_at == 4) _exit(64); }
    return n;
}

static int repair_truncate(int fd, off_t offset)
{
    truncates++;
    if (fail_truncate == 1) { errno = EIO; return -1; }
    int result = ftruncate(fd,offset);
    if (stop_at == 1) _exit(61);
    if (fail_truncate == 2) { errno = EIO; return -1; }
    return result;
}

static int repair_sync(int fd)
{
    struct stat st; CHECK(fstat(fd,&st) == 0);
    if (S_ISDIR(st.st_mode)) {
        directory_syncs++;
        if (stop_at == 2) _exit(62);
        if (directory_syncs == fail_directory) { errno = EIO; return -1; }
        int result = fsync(fd); if (stop_at == 3) _exit(63); return result;
    }
    if (wal_file(fd)) {
        wal_syncs++;
        if (wal_syncs == fail_barrier) { errno = EIO; return -1; }
        int result = fsync(fd); if (stop_at == 5 && wal_syncs == 3) _exit(65); return result;
    }
    return fsync(fd);
}

static void copy_file(const char *source, const char *target)
{
    int a = open(source,O_RDONLY), b = open(target,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(a >= 0 && b >= 0);
    uint8_t bytes[8192]; ssize_t n;
    while ((n = read(a,bytes,sizeof(bytes))) > 0) CHECK(write(b,bytes,(size_t)n) == n);
    CHECK(n == 0); CHECK(fsync(b) == 0); CHECK(close(a) == 0); CHECK(close(b) == 0);
}

static void preserve(const tail_fixture_t *f, const char *tag)
{
    char path[320], target[360];
    for (unsigned i = 0; i < f->segments; i++) {
        CHECK(snprintf(path,sizeof(path),"%s.wal.%016x",f->files.wal,i) > 0);
        if (access(path,F_OK) != 0) { CHECK(errno == ENOENT); continue; }
        CHECK(snprintf(target,sizeof(target),"%s.%s-%u",f->files.wal,tag,i) > 0); copy_file(path,target);
    }
    if (strcmp(tag,"before") == 0) {
        CHECK(snprintf(target,sizeof(target),"%s.data-before",f->files.wal) > 0); copy_file(f->files.data,target);
        CHECK(snprintf(target,sizeof(target),"%s.control-before",f->files.wal) > 0); copy_file(f->files.control,target);
    }
}

static uint64_t planned(uint64_t at, size_t bytes)
{
    if (CANDIDATE_WAL_SEGMENT_BYTES - at % CANDIDATE_WAL_SEGMENT_BYTES < bytes)
        at = (at / CANDIDATE_WAL_SEGMENT_BYTES + 1) * CANDIDATE_WAL_SEGMENT_BYTES + 64;
    return at + bytes;
}

static candidate_wal_record_t emit_tail(chain_t *c, uint64_t txn, unsigned type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t r = {.type = (uint16_t)type,.transaction_id = txn,.owner_epoch = 7,
        .lsn = candidate_wal_info(c->wal).valid_end_lsn,.previous_lsn = c->last,.payload = body,.payload_bytes = size};
    r.lsn = planned(r.lsn,(size + 103) & ~(size_t)7) - ((size + 103) & ~(size_t)7);
    uint64_t end; OK(candidate_wal_append(c->wal,&r,&end)); CHECK(end == planned(r.lsn,(size + 103) & ~(size_t)7));
    if (c->records == 0) c->first = r.lsn;
    c->last = r.lsn; c->records++; c->crc = candidate_wal_witness_crc(c->crc,&r); return r;
}

static uint64_t physical(chain_t *c, uint64_t txn, unsigned gid, unsigned count, bool complete)
{
    uint64_t end = planned(candidate_wal_info(c->wal).valid_end_lsn,112);
    for (unsigned i = 0; i < count; i++) end = planned(end,8320);
    end = planned(end,120);
    uint8_t begin[16] = {0}; write_le(begin,gid,8); write_le(begin + 8,count,4); write_le(begin + 12,count,4);
    candidate_wal_record_t r = emit_tail(c,txn,3,begin,16); uint32_t crc = candidate_wal_witness_crc(0,&r);
    uint64_t cut = r.lsn;
    for (unsigned i = 0; i < (complete ? count : count - 1); i++) {
        uint8_t image[8224] = {0}; write_le(image,gid,8); write_le(image + 8,i + 1,8); write_le(image + 16,1,8);
        candidate_page_location_t location = {.ref = {i + 1,1},.lsn = end}; memcpy(location.uuid,uuid,16);
        candidate_page_version_t version = {.transaction_id = txn == 0 ? 11 : txn,.table_id = 1,.row_id = i + 1,.slot_generation = 1};
        OK(candidate_page_encode_versions(&location,&version,1,image + 32,8192));
        r = emit_tail(c,txn,4,image,sizeof(image)); crc = candidate_wal_witness_crc(crc,&r);
    }
    if (complete) {
        uint8_t finish[24] = {0}; write_le(finish,gid,8); write_le(finish + 8,count,4); write_le(finish + 12,count,4); write_le(finish + 16,crc,4);
        emit_tail(c,txn,8,finish,24); CHECK(candidate_wal_info(c->wal).valid_end_lsn == end);
    }
    return cut;
}

static unsigned fill_prefix(candidate_wal_t *wal, unsigned boundary)
{
    chain_t c = {.wal = wal}; uint8_t begin[16] = {0}; emit_tail(&c,10,2,begin,16);
    uint64_t ends[16]; unsigned gid = 2;
    uint64_t target = (uint64_t)boundary * CANDIDATE_WAL_SEGMENT_BYTES - 4 * 8320;
    while (candidate_wal_info(wal).valid_end_lsn + 8320 + 232 < target) {
        unsigned count = (unsigned)((target - candidate_wal_info(wal).valid_end_lsn - 232) / 8320);
        if (count > 64) count = 64;
        physical(&c,10,gid,count,true); ends[gid - 2] = candidate_wal_info(wal).valid_end_lsn; gid++;
    }
    uint8_t commit[408] = {0}; write_le(commit,c.first,8); write_le(commit + 8,c.last,8); write_le(commit + 16,c.records,8); write_le(commit + 24,c.crc,4);
    write_le(commit + 32,2,8); write_le(commit + 148,gid - 2,4);
    for (unsigned i = 0; i < gid - 2; i++) { write_le(commit + 152 + i * 16,i + 2,8); write_le(commit + 160 + i * 16,ends[i],8); }
    emit_tail(&c,10,10,commit,152 + (gid - 2) * 16); return gid;
}

static tail_fixture_t tail_fixture(unsigned cross, bool system, bool partial)
{
    tail_fixture_t f = {.files = fixture(0,1)}; candidate_recovery_options_t o = options(&f.files); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&o,&r)); OK(candidate_recovery_close(&r));
    candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL; OK(candidate_wal_open(f.files.wal,&wo,&wal));
    unsigned gid = cross ? fill_prefix(wal,cross) : 2;
    chain_t c = {.wal = wal}; uint8_t begin[16] = {0}; if (!system) emit_tail(&c,11,2,begin,16);
    f.cut = physical(&c,system ? 0 : 11,gid,cross ? 64 : 2,false);
    f.original_end = candidate_wal_info(wal).valid_end_lsn; f.segments = candidate_wal_info(wal).segments;
    OK(candidate_wal_force(wal,f.original_end)); OK(candidate_wal_close(&wal));
    wal_count = f.segments;
    for (unsigned i = 0; i < f.segments; i++) {
        char path[320]; CHECK(snprintf(path,sizeof(path),"%s.wal.%016x",f.files.wal,i) > 0); struct stat st; CHECK(stat(path,&st) == 0); wal_inodes[i] = st.st_ino;
        if (partial && i + 1 == f.segments) { int fd = open(path,O_RDWR); CHECK(fd >= 0); CHECK(ftruncate(fd,st.st_size - 17) == 0); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); }
    }
    preserve(&f,"before"); truncates = directory_syncs = wal_syncs = wal_writes = commit_reads = 0;
    fail_truncate = fail_directory = fail_barrier = stop_at = change_view = 0; fail_reading = false;
    printf("TAIL_FIXTURE %s cross=%u system=%u partial=%u cut=%llu end=%llu segments=%u\n",f.files.wal,cross,system,partial,
        (unsigned long long)f.cut,(unsigned long long)f.original_end,f.segments); return f;
}

static candidate_recovery_options_t repair_options(const tail_fixture_t *f)
{
    static const candidate_wal_io_t io = {repair_read,repair_write,repair_sync,repair_truncate};
    candidate_recovery_options_t o = options(&f->files); o.owner_epoch = 8; o.io = &io; return o;
}

static void receipt_zero(candidate_recovery_wal_info_t info)
{
    candidate_recovery_wal_info_t zero = {0}; CHECK(memcmp(&info,&zero,sizeof(info)) == 0);
}

static void normal_tails(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        tail_fixture_t f = tail_fixture(mode >= 3 ? mode - 2 : 0,mode == 1,mode == 2); candidate_recovery_options_t o = repair_options(&f);
        candidate_recovery_terminal_info_t strict; CHECK(candidate_recovery_resolve_terminals(&o,&strict) == CANDIDATE_WAL_STALE); CHECK(truncates == 0 && wal_writes == 0);
        candidate_recovery_wal_info_t info; OK(candidate_recovery_repair_wal(&o,&info));
        CHECK(info.discarded_group_lsn == f.cut && info.removed_segments == (mode >= 3 ? 1u : 0u));
        CHECK(info.terminals.appended_aborts == (mode == 1 ? 0u : 1u)); CHECK(info.terminals.existing_commits == (mode >= 3 ? 2u : 1u));
        preserve(&f,"first"); unsigned old_writes = wal_writes; OK(candidate_recovery_repair_wal(&o,&info));
        CHECK(info.discarded_group_lsn == 0 && info.terminals.appended_aborts == 0 && old_writes == wal_writes);
        printf("TAIL_OK %s mode=%u writes=%u syncs=%u\n",f.files.wal,mode,wal_writes,wal_syncs);
    }
}

static void fault_tails(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        tail_fixture_t f = tail_fixture(mode == 5,false,false); candidate_recovery_options_t o = repair_options(&f);
        if (mode < 2) fail_truncate = mode + 1;
        else if (mode < 5) fail_barrier = mode - 1;
        else fail_directory = 1;
        candidate_recovery_wal_info_t info; CHECK(candidate_recovery_repair_wal(&o,&info) == CANDIDATE_WAL_IO); receipt_zero(info);
        preserve(&f,"failed"); fail_truncate = fail_barrier = fail_directory = 0;
        OK(candidate_recovery_repair_wal(&o,&info)); CHECK(info.terminals.existing_commits == (mode == 5 ? 2u : 1u));
        printf("TAIL_IO %s mode=%u\n",f.files.wal,mode);
    }
}

static void process_tails(void)
{
    for (unsigned mode = 1; mode <= 5; mode++) {
        tail_fixture_t f = tail_fixture(mode == 2 || mode == 3,false,false); candidate_recovery_options_t o = repair_options(&f);
        pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) { alarm(30); stop_at = mode; candidate_recovery_wal_info_t info; candidate_recovery_repair_wal(&o,&info); _exit(99); }
        int status; CHECK(waitpid(child,&status,0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == (int)(60 + mode));
        preserve(&f,"interrupted"); candidate_recovery_wal_info_t info; OK(candidate_recovery_repair_wal(&o,&info));
        unsigned before = wal_writes; OK(candidate_recovery_repair_wal(&o,&info)); CHECK(info.terminals.appended_aborts == 0 && before == wal_writes);
        printf("TAIL_PROCESS %s mode=%u\n",f.files.wal,mode);
    }
}

static void guarded_tails(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        tail_fixture_t f = tail_fixture(false,false,false); candidate_recovery_options_t o = repair_options(&f); candidate_recovery_wal_info_t info;
        int fd = -1;
        if (mode == 0) o.owner_epoch = 7;
        if (mode == 1) { fd = open(f.files.data,O_RDWR); CHECK(fd >= 0); CHECK(flock(fd,LOCK_EX | LOCK_NB) == 0); }
        if (mode == 2) fail_reading = true;
        if (mode == 3) change_view = 6;
        if (mode == 4) {
            candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL;
            OK(candidate_wal_open(f.files.wal,&wo,&wal)); chain_t c = {.wal = wal}; uint8_t begin[16] = {0}; emit_tail(&c,12,2,begin,16);
            uint8_t commit[152] = {0}; write_le(commit,c.first,8); write_le(commit + 8,c.last,8);
            write_le(commit + 16,c.records,8); write_le(commit + 24,c.crc,4); write_le(commit + 32,2,8);
            emit_tail(&c,12,10,commit,sizeof(commit)); OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn)); OK(candidate_wal_close(&wal));
            preserve(&f,"guard-input");
        }
        candidate_wal_status_t status = candidate_recovery_repair_wal(&o,&info);
        CHECK(status == (mode == 0 ? CANDIDATE_WAL_STALE : mode == 1 ? CANDIDATE_WAL_BUSY : mode == 2 ? CANDIDATE_WAL_IO : CANDIDATE_WAL_CORRUPT));
        receipt_zero(info); CHECK(truncates == 0 && wal_writes == 0); if (fd >= 0) CHECK(close(fd) == 0);
        printf("TAIL_GUARD %s mode=%u commits_read=%u\n",f.files.wal,mode,commit_reads);
    }
}

static void memory_tails(void)
{
    tail_fixture_t f = tail_fixture(false,false,false); candidate_recovery_options_t o = repair_options(&f);
    memory_t count = {0}; candidate_wal_memory_t memory = {&count,allocate,release}; o.memory = &memory;
    candidate_recovery_wal_info_t info; OK(candidate_recovery_repair_wal(&o,&info)); CHECK(count.live == 0);
    unsigned calls = count.calls; size_t peak = count.peak;
    for (unsigned at = 1; at <= calls; at++) {
        tail_fixture_t failed = tail_fixture(false,false,false); o = repair_options(&failed); o.memory = &memory;
        count = (memory_t){.fail_at = at}; CHECK(candidate_recovery_repair_wal(&o,&info) == CANDIDATE_WAL_NOMEM); receipt_zero(info); CHECK(count.live == 0);
        preserve(&failed,"oom"); count = (memory_t){0}; OK(candidate_recovery_repair_wal(&o,&info)); CHECK(count.live == 0);
    }
    tail_fixture_t mutated = tail_fixture(false,false,false); o = repair_options(&mutated); o.memory = &memory;
    count = (memory_t){.mutate = &o,.mutate_memory = &memory}; OK(candidate_recovery_repair_wal(&o,&info)); CHECK(count.live == 0 && o.owner_epoch == 0);
    printf("TAIL_MEMORY calls=%u peak=%zu; owned requests exclude stack/allocator/RSS\n",calls,peak);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90);
    normal_tails(); fault_tails(); process_tails(); guarded_tails(); memory_tails();
    printf("PASS durable WAL tail checks=%u; no data/control rewrite or DB activation\n",checks); return 0;
}
