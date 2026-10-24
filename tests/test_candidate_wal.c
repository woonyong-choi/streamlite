#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "storage/candidate_wal.h"
#include "storage/checksum.h"

static unsigned checks;
static unsigned serial;
static const char *base;
static unsigned first_cut;
static unsigned last_cut = 360;
static const uint8_t small[] = {0, 1, 2, 255, 128, 'a', 'b', 0, 'z'};
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)

typedef struct {
    size_t read_max;
    size_t write_max;
    unsigned read_calls;
    unsigned write_calls;
    unsigned fail_write;
    bool has_interrupts;
    bool has_permanent_interrupts;
    bool has_zero_read;
    bool has_zero_write;
    bool has_truncate_failure;
    int force_action;
} hooks_t;
static hooks_t hooks;

static ssize_t injected_read(int fd, void *p, size_t n, off_t at)
{
    if (hooks.has_zero_read) return 0;
    if (hooks.has_permanent_interrupts) { hooks.read_calls++; errno = EINTR; return -1; }
    if (hooks.has_interrupts && ++hooks.read_calls % 2 == 1) { errno = EINTR; return -1; }
    if (hooks.read_max > 0 && n > hooks.read_max) n = hooks.read_max;
    return pread(fd, p, n, at);
}

static ssize_t injected_write(int fd, const void *p, size_t n, off_t at)
{
    if (hooks.has_zero_write) return 0;
    if (hooks.has_permanent_interrupts) { hooks.write_calls++; errno = EINTR; return -1; }
    hooks.write_calls++;
    if (hooks.write_calls == hooks.fail_write) { errno = ENOSPC; return -1; }
    if (hooks.has_interrupts && hooks.write_calls % 2 == 1) { errno = EINTR; return -1; }
    if (hooks.write_max > 0 && n > hooks.write_max) n = hooks.write_max;
    return pwrite(fd, p, n, at);
}

static int injected_sync(int fd)
{
    if (hooks.force_action == 1) { errno = EIO; return -1; }
    if (hooks.force_action == 3) _exit(83);
    int result = fsync(fd);
    if (hooks.force_action == 4) _exit(84);
    if (hooks.force_action == 2) { errno = EIO; return -1; }
    return result;
}

static int injected_truncate(int fd, off_t size)
{
    if (hooks.has_truncate_failure) { errno = ENOSPC; return -1; }
    return ftruncate(fd, size);
}

static const candidate_wal_io_t io = {injected_read, injected_write, injected_sync, injected_truncate};

static candidate_wal_options_t options(void)
{
    candidate_wal_options_t o = {.owner_epoch = 7};
    for (unsigned i = 0; i < 16; i++) o.uuid[i] = (uint8_t)i + 1;
    return o;
}

static void prefix(char out[PATH_MAX], const char *name)
{
    int n = snprintf(out, PATH_MAX, "%s-%s-%u", base, name, ++serial);
    CHECK(n > 0 && n < PATH_MAX - 22);
}

static void path(char out[PATH_MAX], const char *p, unsigned id)
{
    int n = snprintf(out, PATH_MAX, "%s.wal.%016" PRIx64, p, (uint64_t)id);
    CHECK(n > 0 && n < PATH_MAX);
}

static uint64_t file_size(const char *p, unsigned id)
{
    char f[PATH_MAX]; path(f, p, id);
    struct stat st; CHECK(stat(f, &st) == 0);
    return (uint64_t)st.st_size;
}

static uint32_t file_crc(const char *p, unsigned id)
{
    char f[PATH_MAX]; path(f, p, id);
    int fd = open(f, O_RDONLY); CHECK(fd >= 0);
    uint8_t bytes[8192]; uint32_t crc = 0; ssize_t n;
    while ((n = read(fd, bytes, sizeof(bytes))) > 0) crc = crc32c(crc, bytes, (size_t)n);
    CHECK(n == 0); CHECK(close(fd) == 0); return crc;
}

static void copy_segment(const char *from, const char *to, unsigned id)
{
    char a[PATH_MAX], b[PATH_MAX]; path(a, from, id); path(b, to, id);
    int in = open(a, O_RDONLY); CHECK(in >= 0);
    int out = open(b, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(out >= 0);
    uint8_t bytes[8192]; ssize_t n;
    while ((n = read(in, bytes, sizeof(bytes))) > 0) CHECK(write(out, bytes, (size_t)n) == n);
    CHECK(n == 0); CHECK(close(in) == 0); CHECK(close(out) == 0);
}

static void truncate_segment(const char *p, unsigned id, uint64_t size)
{
    char f[PATH_MAX]; path(f, p, id);
    int fd = open(f, O_RDWR); CHECK(fd >= 0);
    CHECK(ftruncate(fd, (off_t)size) == 0); CHECK(close(fd) == 0);
}

static void poke(const char *p, uint64_t at, uint8_t value)
{
    char f[PATH_MAX]; path(f, p, 0);
    int fd = open(f, O_RDWR); CHECK(fd >= 0);
    CHECK(pwrite(fd, &value, 1, (off_t)at) == 1); CHECK(close(fd) == 0);
}

static uint64_t append(candidate_wal_t *w, unsigned type, const void *bytes, size_t size)
{
    candidate_wal_record_t r = {.type = (uint16_t)type, .transaction_id = 42,
                                .payload = bytes, .payload_bytes = size};
    uint64_t end = 0; OK(candidate_wal_append(w, &r, &end)); return end;
}

typedef struct { unsigned count; uint64_t bytes; uint32_t crc; } seen_t;
static bool visit(void *p, const candidate_wal_record_t *r)
{
    seen_t *seen = p;
    seen->count++;
    seen->bytes += r->payload_bytes;
    seen->crc = crc32c(seen->crc, r->payload, r->payload_bytes);
    return true;
}

static seen_t scan(candidate_wal_t *w, unsigned expected)
{
    seen_t seen = {0}; candidate_wal_info_t info;
    OK(candidate_wal_scan(w, visit, &seen, &info));
    CHECK(seen.count == expected); CHECK(info.records == expected); return seen;
}

static void golden_and_lifetime(char golden[PATH_MAX])
{
    prefix(golden, "golden"); candidate_wal_options_t o = options(); candidate_wal_t *w = NULL;
    OK(candidate_wal_create(golden, &o, &w));
    CHECK(candidate_wal_info(w).durable_end_lsn == 64);
    CHECK(append(w, 2, small, sizeof(small)) == 176);
    CHECK(candidate_wal_info(w).durable_end_lsn == 64);
    uint8_t body[257]; for (unsigned i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i % 251);
    candidate_wal_record_t r = {.type = 5, .transaction_id = 43, .previous_lsn = 64,
                                .payload = body, .payload_bytes = sizeof(body)};
    uint64_t end; OK(candidate_wal_append(w, &r, &end)); CHECK(end == 536);
    OK(candidate_wal_force(w, end)); CHECK(candidate_wal_info(w).durable_end_lsn == end);
    CHECK(candidate_wal_force(w, end + 8) == CANDIDATE_WAL_INVALID);
    candidate_wal_t *other = NULL;
    CHECK(candidate_wal_open(golden, &o, &other) == CANDIDATE_WAL_BUSY); CHECK(other == NULL);
    CHECK(candidate_wal_create(golden, &o, &other) == CANDIDATE_WAL_EXISTS);
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) {
        if (candidate_wal_force(w, end) != CANDIDATE_WAL_FORKED) _exit(2);
        if (candidate_wal_close(&w) != CANDIDATE_WAL_OK) _exit(3);
        _exit(0);
    }
    int status; CHECK(waitpid(pid, &status, 0) == pid); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    seen_t seen = scan(w, 2);
    CHECK(seen.bytes == sizeof(small) + sizeof(body));
    CHECK(seen.crc == crc32c(crc32c(0, small, sizeof(small)), body, sizeof(body)));
    OK(candidate_wal_close(&w)); CHECK(w == NULL); OK(candidate_wal_close(&w));
    OK(candidate_wal_open(golden, &o, &w)); CHECK(candidate_wal_info(w).durable_end_lsn == 0);
    scan(w, 2); OK(candidate_wal_force(w, end)); OK(candidate_wal_close(&w));
    printf("FIXTURE golden %s\n", golden);
}

static void all_frame_truncations(const char *golden)
{
    candidate_wal_options_t o = options();
    for (unsigned cut = first_cut; cut < last_cut; cut++) {
        char raw[PATH_MAX], work[PATH_MAX]; prefix(raw, "tail-raw"); prefix(work, "tail-repair");
        copy_segment(golden, raw, 0); truncate_segment(raw, 0, 176 + cut);
        uint32_t before = file_crc(raw, 0); copy_segment(raw, work, 0);
        candidate_wal_t *w = NULL; OK(candidate_wal_open(work, &o, &w));
        candidate_wal_info_t info = candidate_wal_info(w); CHECK(info.records == 1);
        CHECK(info.has_tail == (cut != 0)); CHECK(file_size(work, 0) == 176 + cut);
        if (cut != 0) {
            candidate_wal_record_t r = {.type = 2}; uint64_t end;
            CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_TAIL);
            CHECK(candidate_wal_force(w, 176) == CANDIDATE_WAL_TAIL);
        }
        OK(candidate_wal_repair_tail(w)); CHECK(file_size(work, 0) == 176);
        uint64_t end = append(w, 2, "new", 3); OK(candidate_wal_force(w, end));
        scan(w, 2); OK(candidate_wal_close(&w));
        CHECK(file_crc(raw, 0) == before);
    }
    printf("TRUNCATIONS first=%u count=%u raw-preserved=%u\n", first_cut, last_cut - first_cut, last_cut - first_cut);
}

static void corrupt_frames_are_not_tail(const char *golden)
{
    const unsigned offsets[] = {0, 4, 6, 8, 12, 16, 32, 40, 56, 64, 68, 72, 80, 89, 96, 100, 104};
    candidate_wal_options_t o = options();
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        char p[PATH_MAX]; prefix(p, "corrupt"); copy_segment(golden, p, 0);
        poke(p, 64 + offsets[i], 0xee); uint32_t before = file_crc(p, 0);
        candidate_wal_t *w = NULL; candidate_wal_status_t status = candidate_wal_open(p, &o, &w);
        CHECK(status == CANDIDATE_WAL_CORRUPT || status == CANDIDATE_WAL_UNSUPPORTED);
        CHECK(w == NULL); CHECK(file_crc(p, 0) == before);
    }
    char p[PATH_MAX]; prefix(p, "length-hides-suffix"); copy_segment(golden, p, 0);
    for (unsigned i = 0; i < 4; i++) poke(p, 72 + i, (uint8_t)(1048576u >> (i * 8)));
    for (unsigned i = 0; i < 4; i++) poke(p, 76 + i, (uint8_t)(1048480u >> (i * 8)));
    candidate_wal_t *w = NULL; CHECK(candidate_wal_open(p, &o, &w) == CANDIDATE_WAL_CORRUPT);
    prefix(p, "foreign"); copy_segment(golden, p, 0); o.uuid[0] = 99;
    uint32_t before = file_crc(p, 0); CHECK(candidate_wal_open(p, &o, &w) == CANDIDATE_WAL_UNSUPPORTED);
    CHECK(file_crc(p, 0) == before);
}

static void resign_first_frame(const char *p)
{
    char f[PATH_MAX]; path(f, p, 0); int fd = open(f, O_RDWR); CHECK(fd >= 0);
    uint8_t bytes[112]; CHECK(pread(fd, bytes, sizeof(bytes), 64) == sizeof(bytes));
    memset(bytes + 68, 0, 4); uint32_t crc = crc32c(0, bytes, sizeof(bytes));
    for (unsigned i = 0; i < 4; i++) bytes[68 + i] = (uint8_t)(crc >> (8 * i));
    CHECK(pwrite(fd, bytes, sizeof(bytes), 64) == sizeof(bytes)); CHECK(close(fd) == 0);
}

static void signed_field_and_order_guards(const char *golden)
{
    const unsigned offsets[] = {4, 6, 12, 16, 32, 40, 64, 72, 89, 96, 100, 104};
    candidate_wal_options_t o = options();
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        char p[PATH_MAX]; prefix(p, "signed-corrupt"); copy_segment(golden, p, 0);
        poke(p, 64 + offsets[i], 0xee); resign_first_frame(p);
        candidate_wal_t *w = NULL; candidate_wal_status_t status = candidate_wal_open(p, &o, &w);
        CHECK(status == CANDIDATE_WAL_CORRUPT || status == CANDIDATE_WAL_UNSUPPORTED);
        CHECK(w == NULL);
    }
    char p[PATH_MAX], f[PATH_MAX]; prefix(p, "reordered"); copy_segment(golden, p, 0); path(f, p, 0);
    int fd = open(f, O_RDWR); CHECK(fd >= 0); uint8_t first[112], second[360];
    CHECK(pread(fd, first, sizeof(first), 64) == sizeof(first));
    CHECK(pread(fd, second, sizeof(second), 176) == sizeof(second));
    CHECK(pwrite(fd, second, sizeof(second), 64) == sizeof(second));
    CHECK(pwrite(fd, first, sizeof(first), 424) == sizeof(first)); CHECK(close(fd) == 0);
    candidate_wal_t *w = NULL; CHECK(candidate_wal_open(p, &o, &w) == CANDIDATE_WAL_CORRUPT);
    prefix(p, "owner-regression"); copy_segment(golden, p, 0);
    poke(p, 64 + 56, 8); resign_first_frame(p); o.owner_epoch = 8;
    CHECK(candidate_wal_open(p, &o, &w) == CANDIDATE_WAL_CORRUPT);
    o.owner_epoch = 7;
    uint8_t bytes[112]; size_t written = 0;
    candidate_wal_record_t r = {.type = 2, .lsn = UINT64_MAX - 103, .owner_epoch = 7,
                               .payload = small, .payload_bytes = 1};
    CHECK(candidate_wal_encode(o.uuid, &r, bytes, sizeof(bytes), &written) == CANDIDATE_WAL_LIMIT);
    r.lsn = 64; OK(candidate_wal_encode(o.uuid, &r, bytes, sizeof(bytes), &written));
    r.payload_bytes = SIZE_MAX;
    CHECK(candidate_wal_encode(o.uuid, &r, bytes, sizeof(bytes), &written) == CANDIDATE_WAL_LIMIT);
}

static void transport_errors(const char *golden)
{
    candidate_wal_options_t o = options(); o.io = &io;
    char p[PATH_MAX]; prefix(p, "short-eintr"); memset(&hooks, 0, sizeof(hooks));
    hooks.read_max = 7; hooks.write_max = 11; hooks.has_interrupts = true;
    candidate_wal_t *w = NULL; OK(candidate_wal_create(p, &o, &w));
    uint64_t end = append(w, 2, small, sizeof(small)); OK(candidate_wal_force(w, end));
    OK(candidate_wal_close(&w)); OK(candidate_wal_open(p, &o, &w)); scan(w, 1); OK(candidate_wal_close(&w));
    memset(&hooks, 0, sizeof(hooks)); prefix(p, "partial-write"); copy_segment(golden, p, 0);
    OK(candidate_wal_open(p, &o, &w)); hooks.write_max = 13; hooks.fail_write = 4;
    candidate_wal_record_t r = {.type = 2, .payload = small, .payload_bytes = sizeof(small)};
    CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_IO);
    CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_POISONED); OK(candidate_wal_close(&w));
    memset(&hooks, 0, sizeof(hooks)); OK(candidate_wal_open(p, &o, &w)); CHECK(candidate_wal_info(w).has_tail);
    hooks.has_truncate_failure = true; CHECK(candidate_wal_repair_tail(w) == CANDIDATE_WAL_IO);
    CHECK(candidate_wal_force(w, 536) == CANDIDATE_WAL_POISONED); OK(candidate_wal_close(&w));
    memset(&hooks, 0, sizeof(hooks)); hooks.has_zero_read = true;
    CHECK(candidate_wal_open(golden, &o, &w) == CANDIDATE_WAL_IO); CHECK(w == NULL);
    memset(&hooks, 0, sizeof(hooks)); hooks.has_permanent_interrupts = true;
    CHECK(candidate_wal_open(golden, &o, &w) == CANDIDATE_WAL_IO); CHECK(hooks.read_calls == 33);
    memset(&hooks, 0, sizeof(hooks)); prefix(p, "zero-write"); OK(candidate_wal_create(p, &o, &w));
    hooks.has_zero_write = true; CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_IO);
    CHECK(file_size(p, 0) == 64); OK(candidate_wal_close(&w));
    for (int action = 1; action <= 2; action++) {
        memset(&hooks, 0, sizeof(hooks)); prefix(p, "force-failure");
        OK(candidate_wal_create(p, &o, &w)); end = append(w, 2, small, sizeof(small));
        hooks.force_action = action; CHECK(candidate_wal_force(w, end) == CANDIDATE_WAL_IO);
        CHECK(candidate_wal_info(w).durable_end_lsn == 64);
        CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_POISONED);
        OK(candidate_wal_close(&w)); memset(&hooks, 0, sizeof(hooks));
        OK(candidate_wal_open(p, &o, &w)); scan(w, 1); OK(candidate_wal_close(&w));
    }
    memset(&hooks, 0, sizeof(hooks));
}

static bool stop_visitor(void *context, const candidate_wal_record_t *record)
{
    candidate_wal_t *w = context; uint64_t end;
    CHECK(candidate_wal_append(w, record, &end) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_wal_force(w, record->lsn) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_wal_close(&w) == CANDIDATE_WAL_INVALID);
    return false;
}

static void scan_stop_and_error_fence(const char *golden)
{
    char p[PATH_MAX]; prefix(p, "scan-fence"); copy_segment(golden, p, 0);
    candidate_wal_options_t o = options(); candidate_wal_t *w = NULL; candidate_wal_info_t info;
    OK(candidate_wal_open(p, &o, &w));
    CHECK(candidate_wal_scan(w, stop_visitor, w, &info) == CANDIDATE_WAL_STOPPED);
    scan(w, 2); poke(p, 64 + 80, 99);
    CHECK(candidate_wal_scan(w, NULL, NULL, &info) == CANDIDATE_WAL_CORRUPT);
    CHECK(candidate_wal_force(w, 536) == CANDIDATE_WAL_POISONED); OK(candidate_wal_close(&w));
}

static void segment_boundaries_and_gaps(void)
{
    uint8_t *body = malloc(CANDIDATE_WAL_RECORD_BYTES); CHECK(body != NULL);
    memset(body, 0xa7, CANDIDATE_WAL_RECORD_BYTES);
    char p[PATH_MAX]; prefix(p, "exact-segment"); candidate_wal_options_t o = options();
    candidate_wal_t *w = NULL; OK(candidate_wal_create(p, &o, &w));
    for (unsigned i = 0; i < 3; i++) append(w, 4, body, CANDIDATE_WAL_RECORD_BYTES - 96);
    uint64_t end = append(w, 4, body, CANDIDATE_WAL_RECORD_BYTES - 160);
    CHECK(end == CANDIDATE_WAL_SEGMENT_BYTES);
    end = append(w, 2, small, sizeof(small)); CHECK(end == CANDIDATE_WAL_SEGMENT_BYTES + 176);
    OK(candidate_wal_force(w, end)); OK(candidate_wal_close(&w));
    OK(candidate_wal_open(p, &o, &w)); scan(w, 5); OK(candidate_wal_close(&w));
    prefix(p, "rotation"); OK(candidate_wal_create(p, &o, &w));
    for (unsigned i = 0; i < 7; i++) end = append(w, 4, body, CANDIDATE_WAL_RECORD_BYTES - 96);
    OK(candidate_wal_force(w, end)); CHECK(candidate_wal_info(w).segments == 3);
    OK(candidate_wal_close(&w)); OK(candidate_wal_open(p, &o, &w)); scan(w, 7); OK(candidate_wal_close(&w));
    char gap[PATH_MAX]; prefix(gap, "gap"); copy_segment(p, gap, 0); copy_segment(p, gap, 2);
    CHECK(candidate_wal_open(gap, &o, &w) == CANDIDATE_WAL_CORRUPT);
    char header[PATH_MAX]; prefix(header, "header-tail");
    copy_segment(p, header, 0); copy_segment(p, header, 1); copy_segment(p, header, 2);
    truncate_segment(header, 2, 31); uint32_t raw_crc = file_crc(header, 2);
    char repair[PATH_MAX]; prefix(repair, "header-repair");
    for (unsigned i = 0; i < 3; i++) copy_segment(header, repair, i);
    OK(candidate_wal_open(repair, &o, &w)); CHECK(candidate_wal_info(w).has_tail);
    OK(candidate_wal_repair_tail(w)); end = append(w, 2, small, sizeof(small));
    OK(candidate_wal_force(w, end)); scan(w, 7); OK(candidate_wal_close(&w)); CHECK(file_crc(header, 2) == raw_crc);
    o.owner_epoch = 8; OK(candidate_wal_open(p, &o, &w));
    end = append(w, 2, small, sizeof(small)); OK(candidate_wal_force(w, end)); OK(candidate_wal_close(&w));
    o.owner_epoch = 7; CHECK(candidate_wal_open(p, &o, &w) == CANDIDATE_WAL_STALE);
    prefix(p, "quota"); o.max_bytes = CANDIDATE_WAL_SEGMENT_BYTES;
    OK(candidate_wal_create(p, &o, &w));
    for (unsigned i = 0; i < 3; i++) append(w, 4, body, CANDIDATE_WAL_RECORD_BYTES - 96);
    candidate_wal_record_t r = {.type = 4, .payload = body, .payload_bytes = CANDIDATE_WAL_RECORD_BYTES - 96};
    uint64_t before = file_size(p, 0); CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_LIMIT);
    CHECK(file_size(p, 0) == before); end = append(w, 2, small, sizeof(small));
    OK(candidate_wal_force(w, end)); OK(candidate_wal_close(&w)); free(body);
    printf("FIXTURE rotations %s\n", repair);
}

typedef struct { unsigned calls; unsigned fail_call; size_t live_bytes; } memory_t;
static void *allocate(void *context, size_t size)
{
    memory_t *m = context;
    if (++m->calls == m->fail_call) return NULL;
    void *p = malloc(size); if (p != NULL) m->live_bytes += size; return p;
}
static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live_bytes >= size); m->live_bytes -= size; free(p);
}

static void allocation_and_input_limits(const char *golden)
{
    memory_t invalid_memory = {0}; candidate_wal_memory_t allocator = {&invalid_memory, allocate, release};
    candidate_wal_options_t invalid = options(); invalid.memory = &allocator; invalid.max_bytes = 1;
    candidate_wal_t *rejected = NULL;
    CHECK(candidate_wal_open(golden, &invalid, &rejected) == CANDIDATE_WAL_INVALID);
    candidate_wal_io_t invalid_io = io; invalid_io.pread = NULL; invalid.max_bytes = 0; invalid.io = &invalid_io;
    CHECK(candidate_wal_open(golden, &invalid, &rejected) == CANDIDATE_WAL_INVALID);
    CHECK(invalid_memory.calls == 0 && invalid_memory.live_bytes == 0 && rejected == NULL);
    for (unsigned i = 1; i <= 3; i++) {
        memory_t m = {.fail_call = i}; candidate_wal_memory_t a = {&m, allocate, release};
        candidate_wal_options_t o = options(); o.memory = &a;
        candidate_wal_t *w = NULL; candidate_wal_status_t status = candidate_wal_open(golden, &o, &w);
        if (i <= 2) { CHECK(status == CANDIDATE_WAL_NOMEM); CHECK(w == NULL); }
        else { OK(status); CHECK(candidate_wal_info(w).resident_bytes == m.live_bytes); OK(candidate_wal_close(&w)); }
        CHECK(m.live_bytes == 0);
    }
    candidate_wal_options_t o = options(); candidate_wal_t *w = NULL; char p[PATH_MAX]; prefix(p, "invalid");
    OK(candidate_wal_create(p, &o, &w)); candidate_wal_record_t r = {.type = 17}; uint64_t end;
    CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_UNSUPPORTED);
    r.type = 2; r.payload_bytes = SIZE_MAX;
    CHECK(candidate_wal_append(w, &r, &end) == CANDIDATE_WAL_LIMIT);
    CHECK(file_size(p, 0) == 64); OK(candidate_wal_close(&w));
    char link_prefix[PATH_MAX], target[PATH_MAX], link_name[PATH_MAX]; prefix(link_prefix, "symlink");
    path(target, golden, 0); path(link_name, link_prefix, 0);
    char absolute[PATH_MAX]; CHECK(realpath(target, absolute) != NULL); CHECK(symlink(absolute, link_name) == 0);
    uint32_t before = file_crc(golden, 0); CHECK(candidate_wal_open(link_prefix, &o, &w) == CANDIDATE_WAL_IO);
    CHECK(file_crc(golden, 0) == before);
}

static void crash_barriers(void)
{
    for (int action = 3; action <= 5; action++) {
        char p[PATH_MAX]; prefix(p, "crash"); int ack[2]; CHECK(pipe(ack) == 0);
        pid_t pid = fork(); CHECK(pid >= 0);
        if (pid == 0) {
            close(ack[0]); memset(&hooks, 0, sizeof(hooks));
            candidate_wal_options_t o = options(); o.io = &io; candidate_wal_t *w = NULL;
            if (candidate_wal_create(p, &o, &w) != CANDIDATE_WAL_OK) _exit(2);
            uint64_t end = append(w, 2, small, sizeof(small));
            hooks.force_action = action == 5 ? 0 : action;
            if (candidate_wal_force(w, end) != CANDIDATE_WAL_OK) _exit(3);
            if (write(ack[1], "A", 1) != 1) _exit(4);
            for (;;) pause();
        }
        CHECK(close(ack[1]) == 0); char byte = 0; ssize_t n = read(ack[0], &byte, 1);
        CHECK(close(ack[0]) == 0);
        if (action == 5) { CHECK(n == 1 && byte == 'A'); CHECK(kill(pid, SIGKILL) == 0); }
        else CHECK(n == 0);
        int status; CHECK(waitpid(pid, &status, 0) == pid);
        if (action == 5) CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
        else CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 80 + action);
        candidate_wal_options_t o = options(); candidate_wal_t *w = NULL;
        OK(candidate_wal_open(p, &o, &w)); seen_t seen = scan(w, 1);
        CHECK(seen.crc == crc32c(0, small, sizeof(small))); OK(candidate_wal_close(&w));
    }
    printf("PROCESS before-force=83 after-force=84 acknowledged=SIGKILL\n");
}

int main(int argc, char **argv)
{
    char automatic[128]; snprintf(automatic, sizeof(automatic), "build/candidate-wal-%ld", (long)getpid());
    base = argc > 1 && argv[1][0] != 0 ? argv[1] : automatic;
    if (argc > 2) {
        char *end = NULL; errno = 0;
        unsigned long batch = strtoul(argv[2], &end, 10);
        if (errno != 0 || *end != 0 || batch >= 12 || argv[2][0] == 0) return 2;
        first_cut = (unsigned)batch * 30;
        last_cut = first_cut + 30;
    }
    if (argc > 3) return 2;
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90);
    char golden[PATH_MAX]; golden_and_lifetime(golden);
    all_frame_truncations(golden);
    if (first_cut == 0) {
        corrupt_frames_are_not_tail(golden);
        signed_field_and_order_guards(golden);
        scan_stop_and_error_fence(golden);
        transport_errors(golden);
        segment_boundaries_and_gaps();
        allocation_and_input_limits(golden);
        crash_barriers();
    }
    printf("PASS candidate WAL transport checks=%u; transaction outcomes not implemented\n", checks);
    return 0;
}
