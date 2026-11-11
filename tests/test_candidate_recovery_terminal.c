#define CANDIDATE_RECOVERY_TEST_MAIN recovery_snapshot_regression_main
#include "test_candidate_recovery.c"
#include "storage/checksum.h"

static ino_t wal_inode;
static unsigned wal_writes, wal_syncs, stop_write, stop_sync, fail_wal_write, fail_wal_sync;
static bool should_fail_truncate;

static bool is_wal(int fd) { struct stat st; return fstat(fd,&st) == 0 && st.st_ino == wal_inode; }
static ssize_t terminal_write(int fd, const void *bytes, size_t size, off_t offset)
{
    if (is_wal(fd)) {
        wal_writes++;
        if (wal_writes == fail_wal_write) { CHECK(pwrite(fd,bytes,17,offset) == 17); errno = ENOSPC; return -1; }
        if (wal_writes == stop_write) { CHECK(pwrite(fd,bytes,size,offset) == (ssize_t)size); _exit(51); }
    }
    return pwrite(fd,bytes,size,offset);
}
static int terminal_sync(int fd)
{
    if (is_wal(fd)) {
        wal_syncs++; if (wal_syncs == fail_wal_sync) { errno = EIO; return -1; }
        if (stop_sync != 0 && wal_syncs == 2) {
            if (stop_sync == 1) _exit(52);
            CHECK(fsync(fd) == 0); _exit(53);
        }
    }
    return fsync(fd);
}
static int terminal_truncate(int fd, off_t offset)
{
    if (is_wal(fd) && should_fail_truncate) { errno = EIO; return -1; }
    return ftruncate(fd,offset);
}

static void wal_path(const fixture_t *f, char path[300])
{
    CHECK(snprintf(path,300,"%s.wal.0000000000000000",f->wal) > 0);
}

static void archive_path(const char *path, const char *suffix, char target[340])
{
    const char *wal = strstr(path,".wal.");
    int size = wal == NULL ? snprintf(target,340,"%s.%s",path,suffix) :
        snprintf(target,340,"%.*s.%s-wal",(int)(wal - path),path,suffix);
    CHECK(size > 0 && size < 340);
}

static void preserve_file(const char *path, const char *suffix)
{
    char target[340]; archive_path(path,suffix,target);
    int source = open(path,O_RDONLY), dest = open(target,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(source >= 0 && dest >= 0);
    uint8_t bytes[8192]; ssize_t size;
    while ((size = read(source,bytes,sizeof(bytes))) > 0) CHECK(write(dest,bytes,(size_t)size) == size);
    CHECK(size == 0); CHECK(fsync(dest) == 0); CHECK(close(source) == 0); CHECK(close(dest) == 0);
}

static void unchanged_file(const char *path)
{
    char before[340]; archive_path(path,"before",before);
    int a = open(path,O_RDONLY), b = open(before,O_RDONLY); CHECK(a >= 0 && b >= 0);
    uint8_t x[8192], y[8192]; ssize_t size;
    do { size = read(a,x,sizeof(x)); CHECK(size >= 0); CHECK(read(b,y,sizeof(y)) == size); CHECK(memcmp(x,y,(size_t)size) == 0); } while (size > 0);
    CHECK(close(a) == 0); CHECK(close(b) == 0);
}

static fixture_t terminal_fixture(unsigned mode)
{
    fixture_t f = fixture(mode == 1 ? 0 : 9,mode == 5 ? 65537 : 1); candidate_recovery_options_t request = options(&f); candidate_recovery_t *r = NULL;
    CHECK(candidate_recovery_open(&request,&r) == (mode == 1 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE)); OK(candidate_recovery_close(&r));
    candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16); candidate_wal_t *wal = NULL;
    OK(candidate_wal_open(f.wal,&wo,&wal));
    if (mode == 1 || mode == 2) {
        uint8_t admit[64] = {0}; write_le(admit,1,8); admit[8] = 1; memset(admit + 24,0x6c,32); write_le(admit + 56,1,8);
        candidate_wal_record_t record = {.type = 1,.transaction_id = 10,.payload = admit,.payload_bytes = 64}; uint64_t end;
        OK(candidate_wal_append(wal,&record,&end));
    }
    if (mode == 4) {
        uint8_t begin[16] = {0}; write_le(begin,2,8); write_le(begin + 8,1,4); write_le(begin + 12,1,4);
        candidate_wal_record_t record = {.type = 3,.transaction_id = 0,.payload = begin,.payload_bytes = 16}; uint64_t end;
        OK(candidate_wal_append(wal,&record,&end));
    }
    if (mode == 5) {
        uint8_t payload[65537], chunk[112] = {0}; for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i % 251);
        write_le(chunk,1,8); write_le(chunk + 28,2,4); write_le(chunk + 32,sizeof(payload),8);
        OK(candidate_digest_sha256(payload,sizeof(payload),chunk + 40)); write_le(chunk + 72,1,8); write_le(chunk + 80,1,8);
        write_le(chunk + 92,7,4); write_le(chunk + 104,65536,4);
        candidate_wal_record_t record = {.type = 9,.transaction_id = 9,.previous_lsn = f.anchor.checkpoint_begin - 120,.payload = chunk,.payload_bytes = sizeof(chunk)};
        uint64_t end; OK(candidate_wal_append(wal,&record,&end));
    }
    uint64_t end = candidate_wal_info(wal).valid_end_lsn;
    OK(candidate_wal_force(wal,end)); OK(candidate_wal_close(&wal));
    char path[300]; wal_path(&f,path);
    if (mode == 3) {
        int fd = open(path,O_RDWR); CHECK(fd >= 0); CHECK(pwrite(fd,"S",1,(off_t)end) == 1); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
    }
    struct stat st; CHECK(stat(path,&st) == 0); wal_inode = st.st_ino;
    preserve_file(path,"before"); preserve_file(f.data,"before"); preserve_file(f.control,"before");
    wal_writes = wal_syncs = stop_write = stop_sync = fail_wal_write = fail_wal_sync = 0;
    should_fail_truncate = false;
    printf("TERMINAL %s mode=%u\n",f.wal,mode); return f;
}

static candidate_recovery_options_t terminal_options(const fixture_t *f)
{
    static const candidate_wal_io_t io = {pread,terminal_write,terminal_sync,terminal_truncate};
    candidate_recovery_options_t request = options(f); request.owner_epoch = 8; request.io = &io; return request;
}

static void assert_receipt(candidate_recovery_terminal_info_t info, unsigned aborted, unsigned commits)
{
    CHECK(info.checkpoint_id == 1 && info.appended_aborts == aborted && info.existing_commits == commits);
    CHECK(info.after_end_lsn >= info.before_end_lsn);
}

static void terminal_cases(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        fixture_t f = terminal_fixture(mode); candidate_recovery_options_t request = terminal_options(&f); candidate_recovery_terminal_info_t info;
        candidate_wal_status_t status = candidate_recovery_resolve_terminals(&request,&info);
        printf("TERMINAL_STATUS %s mode=%u status=%u\n",f.wal,mode,status);
        CHECK(status == (mode == 4 ? CANDIDATE_WAL_STALE : CANDIDATE_WAL_OK));
        if (mode != 4) {
            assert_receipt(info,mode == 2 ? 2 : 1,mode == 1 ? 1 : 0); CHECK(info.repaired_tail_bytes == (mode == 3 ? 1 : 0));
            char path[300]; wal_path(&f,path); preserve_file(path,"first");
            OK(candidate_recovery_resolve_terminals(&request,&info)); assert_receipt(info,0,mode == 1 ? 1 : 0);
            CHECK(info.before_end_lsn == info.after_end_lsn && info.existing_aborts == (mode == 2 ? 2u : 1u));
            candidate_recovery_t *r = (void *)1; CHECK(candidate_recovery_open(&request,&r) == CANDIDATE_WAL_STALE && r == NULL);
        } else {
            candidate_recovery_terminal_info_t zero = {0}; CHECK(memcmp(&info,&zero,sizeof(info)) == 0);
            char path[300]; wal_path(&f,path); unchanged_file(path);
        }
        unchanged_file(f.data); unchanged_file(f.control);
    }
    fixture_t f = terminal_fixture(0); candidate_recovery_options_t request = terminal_options(&f); request.owner_epoch = 7;
    candidate_recovery_terminal_info_t info; CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_STALE);
    char path[300]; wal_path(&f,path); unchanged_file(path); unchanged_file(f.data); unchanged_file(f.control);
    printf("TERMINAL_STALE_OWNER %s\n",f.wal);
}

static void terminal_io_and_death(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        fixture_t f = terminal_fixture(2); candidate_recovery_options_t request = terminal_options(&f); candidate_recovery_terminal_info_t info;
        if (mode == 0) fail_wal_write = 2; else fail_wal_sync = mode;
        CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_IO);
        candidate_recovery_terminal_info_t zero = {0}; CHECK(memcmp(&info,&zero,sizeof(info)) == 0);
        char path[300]; wal_path(&f,path); preserve_file(path,"failed"); fail_wal_write = fail_wal_sync = 0;
        OK(candidate_recovery_resolve_terminals(&request,&info)); assert_receipt(info,mode == 0 ? 1 : mode == 1 ? 2 : 0,0);
        unchanged_file(f.data); unchanged_file(f.control); printf("TERMINAL_IO %s mode=%u\n",f.wal,mode);
    }
    for (unsigned mode = 0; mode < 3; mode++) {
        fixture_t f = terminal_fixture(2); candidate_recovery_options_t request = terminal_options(&f); pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            alarm(10); if (mode == 0) stop_write = 1; else stop_sync = mode;
            candidate_recovery_terminal_info_t info; candidate_recovery_resolve_terminals(&request,&info); _exit(99);
        }
        int status; CHECK(waitpid(child,&status,0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == (int)(51 + mode));
        char path[300]; wal_path(&f,path); preserve_file(path,"interrupted"); candidate_recovery_terminal_info_t info;
        OK(candidate_recovery_resolve_terminals(&request,&info)); assert_receipt(info,mode == 0 ? 1 : 0,0);
        unchanged_file(f.data); unchanged_file(f.control); printf("TERMINAL_PROCESS %s mode=%u\n",f.wal,mode);
    }
}

static void terminal_memory(void)
{
    fixture_t f = terminal_fixture(0); candidate_recovery_options_t request = terminal_options(&f);
    memory_t count = {0}; candidate_wal_memory_t memory = {&count,allocate,release}; request.memory = &memory;
    candidate_recovery_terminal_info_t info; OK(candidate_recovery_resolve_terminals(&request,&info)); CHECK(count.live == 0);
    unsigned calls = count.calls; size_t peak = count.peak;
    for (unsigned at = 1; at <= calls; at++) {
        fixture_t failed = terminal_fixture(0); request = terminal_options(&failed); request.memory = &memory;
        count = (memory_t){.fail_at = at}; CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_NOMEM); CHECK(count.live == 0);
        candidate_recovery_terminal_info_t zero = {0}; CHECK(memcmp(&info,&zero,sizeof(info)) == 0);
        char path[300]; wal_path(&failed,path); preserve_file(path,"oom"); count = (memory_t){0};
        OK(candidate_recovery_resolve_terminals(&request,&info)); CHECK(count.live == 0); unchanged_file(failed.data); unchanged_file(failed.control);
    }
    printf("TERMINAL_MEMORY calls=%u peak=%zu; owned requests exclude stack/allocator/RSS\n",calls,peak);
    fixture_t mutated = terminal_fixture(0); request = terminal_options(&mutated); request.memory = &memory;
    memory = (candidate_wal_memory_t){&count,allocate,release}; count = (memory_t){.mutate = &request,.mutate_memory = &memory};
    OK(candidate_recovery_resolve_terminals(&request,&info)); CHECK(count.live == 0 && request.owner_epoch == 0);
    assert_receipt(info,1,0); unchanged_file(mutated.data); unchanged_file(mutated.control); printf("TERMINAL_INPUT_COPY %s\n",mutated.wal);
}

static void terminal_guards(void)
{
    fixture_t f = fixture(0,1); candidate_recovery_options_t request = terminal_options(&f); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&request,&r)); candidate_recovery_terminal_info_t info;
    CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_BUSY); OK(candidate_recovery_close(&r));
    char path[300]; wal_path(&f,path); preserve_file(path,"before"); preserve_file(f.data,"before"); preserve_file(f.control,"before");
    OK(candidate_recovery_resolve_terminals(&request,&info)); assert_receipt(info,0,1);
    unchanged_file(path); unchanged_file(f.data); unchanged_file(f.control);
    OK(candidate_recovery_open(&request,&r)); check_result(r,1); OK(candidate_recovery_close(&r));
    printf("TERMINAL_NOOP %s\n",f.wal);

    f = terminal_fixture(3); request = terminal_options(&f); should_fail_truncate = true;
    CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_IO); wal_path(&f,path); unchanged_file(path);
    should_fail_truncate = false; OK(candidate_recovery_resolve_terminals(&request,&info)); assert_receipt(info,1,0);
    unchanged_file(f.data); unchanged_file(f.control); printf("TERMINAL_TRUNCATE %s\n",f.wal);

    f = terminal_fixture(0); request = terminal_options(&f); wal_path(&f,path);
    int fd = open(path,O_RDWR); CHECK(fd >= 0); uint8_t frame[160]; off_t offset = (off_t)f.anchor.checkpoint_end - 160;
    CHECK(pread(fd,frame,160,offset) == 160); frame[112] ^= 1; memset(frame + 68,0,4); write_le(frame + 68,crc32c(0,frame,160),4);
    CHECK(pwrite(fd,frame,160,offset) == 160); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); preserve_file(path,"tampered");
    CHECK(candidate_recovery_resolve_terminals(&request,&info) == CANDIDATE_WAL_CORRUPT && wal_writes == 0);
    fd = open(path,O_RDONLY); CHECK(fd >= 0); uint8_t actual[160]; CHECK(pread(fd,actual,160,offset) == 160); CHECK(memcmp(actual,frame,160) == 0); CHECK(close(fd) == 0);
    unchanged_file(f.data); unchanged_file(f.control); printf("TERMINAL_CORRUPT %s\n",f.wal);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90); terminal_cases(); terminal_io_and_death(); terminal_memory(); terminal_guards();
    printf("PASS durable terminal checks=%u; no outcome rewrite or DB activation\n",checks); return 0;
}
