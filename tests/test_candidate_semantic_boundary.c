#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "storage/candidate_semantic_seed.h"
#include "../src/storage/candidate_wal_internal.h"

#define CHECK(c) do { boundary_checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
#define SENTINEL ((candidate_semantic_seed_t *)(void *)&sentinel)
#define OWNER_EPOCH 7u
#define BEGIN_LSN 64u
#define BEGIN_END 176u
#define CHILD_WAIT_STEPS 200u
#define CHILD_WAIT_NS 25000000L

typedef struct {
    candidate_wal_t *wal;
    char path[256];
} wal_case_t;
typedef struct {
    candidate_wal_status_t status;
    candidate_semantic_seed_t *out;
    unsigned reads;
} attempt_t;
typedef struct {
    candidate_wal_t *wal;
    const candidate_semantic_seed_t *seed;
    candidate_wal_status_t legacy;
    attempt_t attempt;
    unsigned visits;
} reentry_t;

static unsigned boundary_checks;
static char sentinel;
static const uint8_t uuid_a[16] = {0x24,0xa1,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static const uint8_t uuid_b[16] = {0x24,0xa1,3,4,5,6,7,8,9,10,11,12,13,14,15,0x61};
static struct { unsigned reads, fsync_failures; bool should_fail_fsync; } io_state;

static ssize_t counted_pread(int fd, void *out, size_t size, off_t offset)
{
    io_state.reads++;
    return pread(fd, out, size, offset);
}

static int injected_fsync(int fd)
{
    if (!io_state.should_fail_fsync) return fsync(fd);
    io_state.fsync_failures++;
    errno = EIO;
    return -1;
}

static const candidate_wal_io_t boundary_io = {counted_pread, pwrite, injected_fsync, ftruncate};

static const char *status_name(candidate_wal_status_t status)
{
    static const char *names[] = {"OK", "INVALID", "NOMEM", "LIMIT", "IO", "CORRUPT", "UNSUPPORTED", "EXISTS",
        "BUSY", "FORKED", "POISONED", "TAIL", "STOPPED", "STALE"};
    return (size_t)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

static const char *describe_out(const candidate_semantic_seed_t *out)
{
    if (out == NULL) return "NULL";
    return out == SENTINEL ? "UNWRITTEN" : "SEED";
}

static candidate_wal_options_t wal_options(const uint8_t uuid[16])
{
    candidate_wal_options_t o = {.owner_epoch = OWNER_EPOCH, .io = &boundary_io};
    memcpy(o.uuid, uuid, 16);
    return o;
}

static candidate_witness_options_t witness_options(const uint8_t uuid[16])
{
    candidate_witness_options_t o = {.enable_system_groups = false};
    memcpy(o.uuid, uuid, 16);
    return o;
}

static void segment_path(const wal_case_t *c, char *out, size_t size)
{
    int n = snprintf(out, size, "%s.wal.0000000000000000", c->path);
    CHECK(n > 0 && (size_t)n < size);
}

static void reserve_prefix(wal_case_t *c, const char *label)
{
    static unsigned serial;
    memset(c, 0, sizeof(*c));
    int n = snprintf(c->path, sizeof(c->path), "build/task24a1-boundary-%ld-%s-%u", (long)getpid(), label, ++serial);
    CHECK(n > 0 && n < (int)sizeof(c->path) - 22);
    char segment[sizeof(c->path) + 32];
    segment_path(c, segment, sizeof(segment));
    struct stat st;
    errno = 0;
    CHECK(lstat(segment, &st) != 0 && errno == ENOENT);
}

static void create_case(wal_case_t *c, const char *label, bool has_begin)
{
    reserve_prefix(c, label);
    candidate_wal_options_t o = wal_options(uuid_a);
    OK(candidate_wal_create(c->path, &o, &c->wal));
    if (!has_begin) return;
    uint8_t body[16] = {0};
    candidate_wal_record_t r = {.type = 2, .lsn = BEGIN_LSN, .transaction_id = 1, .owner_epoch = OWNER_EPOCH,
        .payload = body, .payload_bytes = sizeof(body)};
    uint64_t end = 0;
    OK(candidate_wal_append(c->wal, &r, &end));
    CHECK(end == BEGIN_END);
}

static void create_forced_case(wal_case_t *c, const char *label)
{
    create_case(c, label, true);
    OK(candidate_wal_force(c->wal, BEGIN_END));
}

static attempt_t capture_with(candidate_wal_t *wal, const uint8_t uuid[16])
{
    candidate_witness_options_t o = witness_options(uuid);
    attempt_t a = {.out = SENTINEL};
    io_state.reads = 0;
    a.status = candidate_semantic_seed_capture(wal, &o, &a.out);
    a.reads = io_state.reads;
    return a;
}

static attempt_t continue_empty(candidate_wal_t *wal, const candidate_semantic_seed_t *seed)
{
    candidate_witness_options_t o = witness_options(uuid_a);
    attempt_t a = {.out = SENTINEL};
    io_state.reads = 0;
    a.status = candidate_semantic_seed_continue(wal, &o, seed, &a.out);
    a.reads = io_state.reads;
    return a;
}

static bool release_out(attempt_t *a)
{
    bool is_null = a->out == NULL;
    if (a->out != SENTINEL) candidate_semantic_seed_close(&a->out);
    a->out = NULL;
    return is_null;
}

static candidate_semantic_seed_t *capture_cut(candidate_wal_t *wal)
{
    attempt_t a = capture_with(wal, uuid_a);
    CHECK(a.status == CANDIDATE_WAL_OK && a.out != NULL && a.out != SENTINEL);
    candidate_wal_info_t transport = candidate_wal_info(wal);
    candidate_semantic_seed_info_t info = candidate_semantic_seed_info(a.out);
    CHECK(info.cut.end_lsn == transport.valid_end_lsn && info.cut.records == transport.records);
    CHECK(memcmp(info.cut.uuid, uuid_a, 16) == 0);
    return a.out;
}

static void probe_read_counter(const char *label, candidate_wal_t *wal)
{
    candidate_wal_record_t r;
    io_state.reads = 0;
    OK(candidate_wal_read_at(wal, BEGIN_LSN, &r));
    printf("READ_PROBE %s lsn=%u reads=%u\n", label, BEGIN_LSN, io_state.reads);
    CHECK(io_state.reads == 2 && r.type == 2 && r.transaction_id == 1);
}

static candidate_wal_status_t analyze_legacy(candidate_wal_t *wal, const uint8_t uuid[16])
{
    candidate_witness_options_t o = witness_options(uuid);
    candidate_wal_witness_t *w = NULL;
    candidate_wal_status_t status = candidate_wal_witness_analyze(wal, &o, &w);
    bool is_consistent = (w != NULL) == (status == CANDIDATE_WAL_OK);
    candidate_wal_witness_close(&w);
    CHECK(is_consistent);
    return status;
}

static bool settle_continue_refusal(const char *label, candidate_wal_status_t expected, attempt_t *a)
{
    printf("DEFECT %s continue expected=%s out=NULL reads=0 actual=%s out=%s reads=%u\n", label,
        status_name(expected), status_name(a->status), describe_out(a->out), a->reads);
    bool is_null = release_out(a);
    return a->status == expected && is_null && a->reads == 0;
}

static bool settle_capture_refusal(const char *label, attempt_t *a)
{
    bool is_tagged_b = false;
    if (a->out != NULL && a->out != SENTINEL) {
        candidate_semantic_seed_info_t info = candidate_semantic_seed_info(a->out);
        is_tagged_b = memcmp(info.cut.uuid, uuid_b, 16) == 0;
    }
    printf("DEFECT %s capture uuid=B expected=UNSUPPORTED out=NULL actual=%s out=%s cut_uuid=%s\n", label,
        status_name(a->status), describe_out(a->out), is_tagged_b ? "B" : "-");
    bool is_null = release_out(a);
    return a->status == CANDIDATE_WAL_UNSUPPORTED && is_null;
}

static void expect_empty_refusal(wal_case_t *c, candidate_semantic_seed_t *seed, const char *label,
    candidate_wal_status_t expected)
{
    candidate_wal_status_t legacy = analyze_legacy(c->wal, uuid_a);
    attempt_t a = continue_empty(c->wal, seed);

    printf("LEGACY %s analyze expected=%s actual=%s\n", label, status_name(expected), status_name(legacy));
    bool is_refused = settle_continue_refusal(label, expected, &a);
    candidate_semantic_seed_close(&seed);
    candidate_wal_status_t closed = candidate_wal_close(&c->wal);
    CHECK(legacy == expected);
    CHECK(is_refused);
    OK(closed);
}

static bool is_same_cut(const candidate_semantic_cut_t *a, const candidate_semantic_cut_t *b)
{
    bool is_same = memcmp(a->uuid, b->uuid, 16) == 0 && a->end_lsn == b->end_lsn;
    is_same = is_same && a->records == b->records && a->highest_owner_epoch == b->highest_owner_epoch;
    return is_same && a->segments == b->segments;
}

static void empty_continue_on_healthy_handle_returns_same_cut(void)
{
    wal_case_t c;
    create_forced_case(&c, "healthy");
    candidate_semantic_seed_t *seed = capture_cut(c.wal);
    probe_read_counter("healthy", c.wal);

    attempt_t a = continue_empty(c.wal, seed);
    candidate_wal_status_t legacy = analyze_legacy(c.wal, uuid_a);

    bool is_written = a.out != NULL && a.out != SENTINEL;
    candidate_semantic_seed_info_t before = candidate_semantic_seed_info(seed);
    candidate_semantic_seed_info_t after = candidate_semantic_seed_info(is_written ? a.out : NULL);
    bool is_same = is_written && is_same_cut(&before.cut, &after.cut) && before.admissions == after.admissions;
    printf("CONTROL healthy continue expected=OK out=SEED reads=0 actual=%s out=%s reads=%u same_cut=%d legacy=%s\n",
        status_name(a.status), describe_out(a.out), a.reads, (int)is_same, status_name(legacy));
    release_out(&a);
    candidate_semantic_seed_close(&seed);
    candidate_wal_status_t closed = candidate_wal_close(&c.wal);
    CHECK(a.status == CANDIDATE_WAL_OK && is_same && a.reads == 0);
    CHECK(legacy == CANDIDATE_WAL_OK);
    OK(closed);
}

static void empty_continue_after_fsync_failure_returns_poisoned(void)
{
    wal_case_t c;
    create_case(&c, "poison-empty", true);
    candidate_semantic_seed_t *seed = capture_cut(c.wal);
    probe_read_counter("poison-empty", c.wal);
    io_state.fsync_failures = 0;
    io_state.should_fail_fsync = true;
    candidate_wal_status_t forced = candidate_wal_force(c.wal, BEGIN_END);
    io_state.should_fail_fsync = false;
    candidate_wal_info_t info = candidate_wal_info(c.wal);
    printf("SETUP poison-empty force expected=IO actual=%s fsync_failures=%u cached_end=%llu durable=%llu\n",
        status_name(forced), io_state.fsync_failures, (unsigned long long)info.valid_end_lsn,
        (unsigned long long)info.durable_end_lsn);
    CHECK(forced == CANDIDATE_WAL_IO && io_state.fsync_failures == 1 && info.valid_end_lsn == BEGIN_END);

    expect_empty_refusal(&c, seed, "poison-empty", CANDIDATE_WAL_POISONED);
}

static void corrupt_begin_payload(const wal_case_t *c)
{
    char segment[sizeof(c->path) + 32];
    segment_path(c, segment, sizeof(segment));
    int fd = open(segment, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0);
    off_t offset = (off_t)(BEGIN_LSN + CANDIDATE_WAL_RECORD_HEADER_BYTES);
    uint8_t byte = 0;
    CHECK(pread(fd, &byte, 1, offset) == 1);
    byte ^= 0xff;
    CHECK(pwrite(fd, &byte, 1, offset) == 1);
    CHECK(close(fd) == 0);
}

static void empty_continue_after_malformed_pinned_read_returns_corrupt(void)
{
    wal_case_t c;
    create_forced_case(&c, "rejected-empty");
    OK(candidate_wal_pin(c.wal));
    candidate_semantic_seed_t *seed = capture_cut(c.wal);
    probe_read_counter("rejected-empty", c.wal);
    corrupt_begin_payload(&c);
    candidate_wal_record_t r;
    candidate_wal_status_t first = candidate_wal_read_at(c.wal, BEGIN_LSN, &r);
    candidate_wal_status_t repeated = candidate_wal_read_at(c.wal, BEGIN_LSN, &r);
    printf("SETUP rejected-empty malformed read expected=CORRUPT actual=%s repeat expected=CORRUPT actual=%s\n",
        status_name(first), status_name(repeated));
    CHECK(first == CANDIDATE_WAL_CORRUPT && repeated == CANDIDATE_WAL_CORRUPT);

    expect_empty_refusal(&c, seed, "rejected-empty", CANDIDATE_WAL_CORRUPT);
}

static int check_forked_child(candidate_wal_t *wal, const candidate_semantic_seed_t *seed)
{
    candidate_wal_status_t legacy = analyze_legacy(wal, uuid_a);
    attempt_t a = continue_empty(wal, seed);
    printf("LEGACY fork-empty child analyze expected=FORKED actual=%s\n", status_name(legacy));
    bool is_refused = settle_continue_refusal("fork-empty child", CANDIDATE_WAL_FORKED, &a);
    return legacy == CANDIDATE_WAL_FORKED && is_refused ? 0 : 1;
}

static int wait_child(pid_t child)
{
    const struct timespec step = {0, CHILD_WAIT_NS};
    int status = 0;
    for (unsigned i = 0; i < CHILD_WAIT_STEPS; i++) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        CHECK(done == 0);
        nanosleep(&step, NULL);
    }
    kill(child, SIGKILL);
    CHECK(waitpid(child, &status, 0) == child);
    return -1;
}

static void empty_continue_in_forked_child_returns_forked(void)
{
    wal_case_t c;
    create_forced_case(&c, "fork-empty");
    candidate_semantic_seed_t *seed = capture_cut(c.wal);
    probe_read_counter("fork-empty", c.wal);

    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) _exit(check_forked_child(c.wal, seed));
    int code = wait_child(child);
    attempt_t a = continue_empty(c.wal, seed);

    printf("FORK fork-empty child_exit expected=0 actual=%d parent continue expected=OK actual=%s out=%s reads=%u\n",
        code, status_name(a.status), describe_out(a.out), a.reads);
    bool is_parent_ok = a.status == CANDIDATE_WAL_OK && a.out != NULL && a.out != SENTINEL && a.reads == 0;
    release_out(&a);
    candidate_semantic_seed_close(&seed);
    candidate_wal_status_t closed = candidate_wal_close(&c.wal);
    CHECK(code == 0);
    CHECK(is_parent_ok);
    OK(closed);
}

static bool visit_reentrant(void *context, const candidate_wal_record_t *record)
{
    reentry_t *r = context;
    (void)record;
    if (r->visits++ > 0) return true;
    r->legacy = analyze_legacy(r->wal, uuid_a);
    r->attempt = continue_empty(r->wal, r->seed);
    return true;
}

static void empty_continue_inside_scan_returns_invalid(void)
{
    wal_case_t c;
    create_forced_case(&c, "reentrant-empty");
    candidate_semantic_seed_t *seed = capture_cut(c.wal);
    probe_read_counter("reentrant-empty", c.wal);

    reentry_t r = {.wal = c.wal, .seed = seed, .attempt = {.out = SENTINEL}};
    candidate_wal_info_t info;
    candidate_wal_status_t scanned = candidate_wal_scan(c.wal, visit_reentrant, &r, &info);
    attempt_t after = continue_empty(c.wal, seed);

    printf("SETUP reentrant-empty scan expected=OK actual=%s visits=%u\n", status_name(scanned), r.visits);
    printf("LEGACY reentrant-empty analyze expected=INVALID actual=%s\n", status_name(r.legacy));
    bool is_refused = settle_continue_refusal("reentrant-empty", CANDIDATE_WAL_INVALID, &r.attempt);
    printf("CONTROL reentrant-empty continue after scan expected=OK actual=%s out=%s reads=%u\n",
        status_name(after.status), describe_out(after.out), after.reads);
    bool is_after_ok = after.status == CANDIDATE_WAL_OK && after.out != NULL && after.out != SENTINEL;
    release_out(&after);
    candidate_semantic_seed_close(&seed);
    candidate_wal_status_t closed = candidate_wal_close(&c.wal);
    CHECK(scanned == CANDIDATE_WAL_OK && r.visits == 1);
    CHECK(r.legacy == CANDIDATE_WAL_INVALID);
    CHECK(is_refused);
    CHECK(is_after_ok && after.reads == 0);
    OK(closed);
}

static void foreign_uuid_capture_returns_unsupported(const char *label, bool has_begin)
{
    wal_case_t c;
    create_case(&c, label, has_begin);
    OK(candidate_wal_force(c.wal, candidate_wal_info(c.wal).valid_end_lsn));
    OK(candidate_wal_close(&c.wal));
    candidate_wal_options_t foreign = wal_options(uuid_b), own = wal_options(uuid_a);
    candidate_wal_t *other = NULL;
    candidate_wal_status_t opened = candidate_wal_open(c.path, &foreign, &other);
    printf("AUTHORITY %s prefix_uuid=A open uuid=B expected=UNSUPPORTED actual=%s handle=%s\n", label,
        status_name(opened), other == NULL ? "NULL" : "WAL");
    CHECK(opened == CANDIDATE_WAL_UNSUPPORTED && other == NULL);
    OK(candidate_wal_open(c.path, &own, &c.wal));

    candidate_wal_status_t legacy = analyze_legacy(c.wal, uuid_b);
    candidate_semantic_seed_t *control = capture_cut(c.wal);
    attempt_t a = capture_with(c.wal, uuid_b);

    printf("LEGACY %s analyze uuid=B expected=OK actual=%s; CONTROL capture uuid=A records=%llu\n", label,
        status_name(legacy), (unsigned long long)candidate_semantic_seed_info(control).cut.records);
    bool is_refused = settle_capture_refusal(label, &a);
    candidate_semantic_seed_close(&control);
    candidate_wal_status_t closed = candidate_wal_close(&c.wal);
    CHECK(legacy == CANDIDATE_WAL_OK);
    CHECK(is_refused);
    OK(closed);
}

static void capture_of_empty_wal_with_foreign_uuid_returns_unsupported(void)
{
    foreign_uuid_capture_returns_unsupported("uuid-empty", false);
}

static void capture_of_transaction_wal_with_foreign_uuid_returns_unsupported(void)
{
    foreign_uuid_capture_returns_unsupported("uuid-transaction", true);
}

static void run_all(void)
{
    empty_continue_on_healthy_handle_returns_same_cut();
    empty_continue_after_fsync_failure_returns_poisoned();
    empty_continue_after_malformed_pinned_read_returns_corrupt();
    empty_continue_in_forked_child_returns_forked();
    empty_continue_inside_scan_returns_invalid();
    capture_of_empty_wal_with_foreign_uuid_returns_unsupported();
    capture_of_transaction_wal_with_foreign_uuid_returns_unsupported();
}

/* 인자 없이는 all, 사례 이름 하나면 그 사례만 실행하며 단언 실패는 1, 잘못된 인자는 2로 끝난다. */
int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"all", run_all}, {"healthy", empty_continue_on_healthy_handle_returns_same_cut},
        {"poison-empty", empty_continue_after_fsync_failure_returns_poisoned},
        {"rejected-empty", empty_continue_after_malformed_pinned_read_returns_corrupt},
        {"fork-empty", empty_continue_in_forked_child_returns_forked},
        {"reentrant-empty", empty_continue_inside_scan_returns_invalid},
        {"uuid-empty", capture_of_empty_wal_with_foreign_uuid_returns_unsupported},
        {"uuid-transaction", capture_of_transaction_wal_with_foreign_uuid_returns_unsupported},
    };
    const char *selected = argc == 2 ? argv[1] : "all";
    void (*run)(void) = NULL;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        if (strcmp(cases[i].name, selected) == 0) run = cases[i].run;
    if (argc > 2 || run == NULL) {
        fprintf(stderr, "usage: %s [all|healthy|poison-empty|rejected-empty|fork-empty|reentrant-empty|"
            "uuid-empty|uuid-transaction]\n", argv[0]);
        return 2;
    }
    alarm(120);
    setvbuf(stdout, NULL, _IONBF, 0);
    run();
    printf("PASS semantic boundary case=%s checks=%u\n", selected, boundary_checks);
    return 0;
}
