#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_wal.h"
#include "storage/checksum.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)

static unsigned checks;
static char root[PATH_MAX];
static struct stat owned[2];
static unsigned owned_count, owned_syncs, other_syncs, directory_syncs;
static bool is_capturing, should_replace;
static char replace_path[PATH_MAX], held_path[PATH_MAX], prepared_path[PATH_MAX];

static int audited_sync(int fd)
{
    struct stat st;
    CHECK(fstat(fd, &st) == 0);
    if (is_capturing) {
        bool is_owned = false;
        for (unsigned i = 0; i < owned_count; i++)
            if (owned[i].st_dev == st.st_dev && owned[i].st_ino == st.st_ino) is_owned = true;
        if (S_ISDIR(st.st_mode)) directory_syncs++;
        else if (is_owned) owned_syncs++;
        else other_syncs++;
    }
    int status = fsync(fd);
    if (status == 0 && should_replace) {
        should_replace = false;
        CHECK(rename(replace_path, held_path) == 0);
        CHECK(rename(prepared_path, replace_path) == 0);
    }
    return status;
}

static const candidate_wal_io_t io = {pread, pwrite, audited_sync, ftruncate};

static candidate_wal_options_t options(void)
{
    candidate_wal_options_t o = {.owner_epoch = 7, .io = &io};
    for (unsigned i = 0; i < 16; i++) o.uuid[i] = (uint8_t)(i + 1);
    return o;
}

static unsigned descriptors(void)
{
    DIR *dir = opendir("/dev/fd"); CHECK(dir != NULL);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) count++;
    CHECK(closedir(dir) == 0);
    return count;
}

static void file_path(char out[PATH_MAX], const char *prefix, unsigned id)
{
    int n = snprintf(out, PATH_MAX, "%s.wal.%016" PRIx64, prefix, (uint64_t)id);
    CHECK(n > 0 && n < PATH_MAX);
}

static uint32_t file_crc(const char *name)
{
    int fd = open(name, O_RDONLY); CHECK(fd >= 0);
    uint8_t bytes[8192]; uint32_t crc = 0; ssize_t n;
    while ((n = read(fd, bytes, sizeof(bytes))) > 0) crc = crc32c(crc, bytes, (size_t)n);
    CHECK(n == 0); CHECK(close(fd) == 0);
    return crc;
}

static void copy_file(const char *from, const char *to)
{
    int in = open(from, O_RDONLY); CHECK(in >= 0);
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(out >= 0);
    uint8_t bytes[8192]; ssize_t n;
    while ((n = read(in, bytes, sizeof(bytes))) > 0) CHECK(write(out, bytes, (size_t)n) == n);
    CHECK(n == 0); CHECK(close(in) == 0); CHECK(close(out) == 0);
}

static uint64_t append(candidate_wal_t *w, size_t bytes)
{
    uint8_t *body = malloc(bytes); CHECK(body != NULL); memset(body, 0x5a, bytes);
    candidate_wal_record_t r = {.type = 2, .transaction_id = 9, .payload = body, .payload_bytes = bytes};
    uint64_t end = 0; OK(candidate_wal_append(w, &r, &end)); free(body);
    return end;
}

static void truncate_last(const char *name)
{
    int fd = open(name, O_RDWR); CHECK(fd >= 0); struct stat st;
    CHECK(fstat(fd, &st) == 0); CHECK(st.st_size > 64);
    CHECK(ftruncate(fd, st.st_size - 1) == 0); CHECK(close(fd) == 0);
}

static bool count_record(void *context, const candidate_wal_record_t *record)
{
    unsigned *count = context; (*count)++;
    CHECK(record->payload_bytes > 0 && record->payload[0] == 0x5a);
    return true;
}

static void scan(candidate_wal_t *w, unsigned expected)
{
    unsigned count = 0; candidate_wal_info_t info;
    OK(candidate_wal_scan(w, count_record, &count, &info));
    CHECK(count == expected && info.records == expected);
}

static void cwd_case(unsigned absolute, unsigned mode)
{
    unsigned before_fds = descriptors();
    char stem[128], prefix[PATH_MAX], name[PATH_MAX], decoy[PATH_MAX];
    snprintf(stem, sizeof(stem), "candidate-wal-files-%ld-cwd-%u-%u", (long)getpid(), absolute, mode);
    CHECK(snprintf(prefix, sizeof(prefix), "%s/build/%s", root, stem) < (int)sizeof(prefix));
    CHECK(chdir("build") == 0);
    const char *input = absolute ? prefix : stem;
    candidate_wal_options_t o = options(); candidate_wal_t *w = NULL;
    OK(candidate_wal_create(input, &o, &w));
    uint64_t end = 0; unsigned count = mode == 2 ? 4 : 1;
    for (unsigned i = 0; i < count; i++)
        end = append(w, mode == 2 ? CANDIDATE_WAL_RECORD_BYTES - 96 : 8);
    if (mode == 1 || mode == 3) {
        OK(candidate_wal_close(&w));
        if (mode == 3) { file_path(name, prefix, 0); truncate_last(name); }
        OK(candidate_wal_open(input, &o, &w));
    }
    owned_count = candidate_wal_info(w).segments;
    CHECK(owned_count == (mode == 2 ? 2u : 1u));
    for (unsigned i = 0; i < owned_count; i++) {
        file_path(name, prefix, i); CHECK(stat(name, &owned[i]) == 0);
    }
    CHECK(chdir("../build-regression") == 0);
    candidate_wal_options_t second = options(); second.uuid[0] = 99;
    candidate_wal_t *b = NULL; OK(candidate_wal_create(stem, &second, &b));
    OK(candidate_wal_close(&b)); file_path(decoy, stem, 0); uint32_t decoy_crc = file_crc(decoy);
    owned_syncs = other_syncs = directory_syncs = 0; is_capturing = true;
    if (mode == 3) {
        CHECK(candidate_wal_info(w).has_tail); scan(w, 0);
        OK(candidate_wal_repair_tail(w)); end = append(w, 8);
    }
    OK(candidate_wal_force(w, end));
    CHECK(candidate_wal_info(w).durable_end_lsn == end);
    is_capturing = false;
    CHECK(owned_syncs > 0 && other_syncs == 0);
    CHECK(directory_syncs == (mode == 1 || mode == 3 ? 1u : 0u));
    CHECK(file_crc(decoy) == decoy_crc);
    scan(w, count); scan(w, count);
    OK(candidate_wal_close(&w)); CHECK(chdir(root) == 0);
    OK(candidate_wal_open(prefix, &o, &w)); scan(w, count); OK(candidate_wal_close(&w));
    CHECK(descriptors() == before_fds);
    printf("CWD absolute=%u mode=%u records=%u owned-syncs=%u other-syncs=%u directory-syncs=%u\n",
           absolute, mode, count, owned_syncs, other_syncs, directory_syncs);
}

static void replacement_case(unsigned segment, unsigned operation)
{
    unsigned before_fds = descriptors();
    char prefix[PATH_MAX];
    CHECK(snprintf(prefix, sizeof(prefix), "build/candidate-wal-files-%ld-replace-%u-%u",
                   (long)getpid(), segment, operation) < (int)sizeof(prefix));
    candidate_wal_options_t o = options(); candidate_wal_t *w = NULL;
    OK(candidate_wal_create(prefix, &o, &w));
    unsigned records = segment == 0 ? 1 : 4;
    for (unsigned i = 0; i < records; i++) append(w, segment == 0 ? 8 : CANDIDATE_WAL_RECORD_BYTES - 96);
    file_path(replace_path, prefix, segment);
    if (operation == 3) {
        OK(candidate_wal_close(&w)); truncate_last(replace_path);
        OK(candidate_wal_open(prefix, &o, &w)); CHECK(candidate_wal_info(w).has_tail);
    }
    CHECK(snprintf(held_path, sizeof(held_path), "%s-held", replace_path) < (int)sizeof(held_path));
    CHECK(snprintf(prepared_path, sizeof(prepared_path), "%s-prepared", replace_path) < (int)sizeof(prepared_path));
    copy_file(replace_path, prepared_path);
    uint32_t crc = file_crc(replace_path);
    candidate_wal_info_t before = candidate_wal_info(w), output = {.records = UINT64_MAX};
    if (operation == 4) should_replace = true;
    else {
        CHECK(rename(replace_path, held_path) == 0);
        if (operation != 5) CHECK(rename(prepared_path, replace_path) == 0);
    }
    candidate_wal_status_t status;
    if (operation == 1) {
        const uint8_t body = 1;
        candidate_wal_record_t r = {.type = 2, .payload = &body, .payload_bytes = 1};
        uint64_t end = UINT64_MAX;
        status = candidate_wal_append(w, &r, &end); CHECK(end == UINT64_MAX);
    } else if (operation == 2) {
        status = candidate_wal_scan(w, NULL, NULL, &output); CHECK(output.records == UINT64_MAX);
    } else if (operation == 3) status = candidate_wal_repair_tail(w);
    else status = candidate_wal_force(w, before.valid_end_lsn);
    CHECK(status == CANDIDATE_WAL_IO);
    CHECK(!should_replace);
    candidate_wal_info_t after = candidate_wal_info(w);
    CHECK(after.durable_end_lsn == before.durable_end_lsn);
    CHECK(after.valid_end_lsn == before.valid_end_lsn && after.has_tail == before.has_tail);
    CHECK(candidate_wal_force(w, 0) == CANDIDATE_WAL_POISONED);
    CHECK(file_crc(held_path) == crc);
    if (operation == 5) { struct stat missing; CHECK(lstat(replace_path, &missing) != 0 && errno == ENOENT); }
    else CHECK(file_crc(replace_path) == crc);
    OK(candidate_wal_close(&w)); CHECK(descriptors() == before_fds);
    printf("REPLACED segment=%u operation=%u durable-unchanged=%llu\n", segment, operation,
           (unsigned long long)before.durable_end_lsn);
}

static void open_failure_releases_descriptors(void)
{
    unsigned before = descriptors();
    candidate_wal_options_t o = options(); candidate_wal_t *w = NULL;
    char missing[PATH_MAX];
    CHECK(snprintf(missing, sizeof(missing), "build/candidate-wal-files-%ld-missing/file",
                   (long)getpid()) < (int)sizeof(missing));
    for (unsigned i = 0; i < 8; i++) {
        CHECK(candidate_wal_create(missing, &o, &w) == CANDIDATE_WAL_IO); CHECK(w == NULL);
        CHECK(candidate_wal_open(missing, &o, &w) == CANDIDATE_WAL_IO); CHECK(w == NULL);
    }
    CHECK(descriptors() == before);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90);
    CHECK(getcwd(root, sizeof(root)) != NULL);
    for (unsigned absolute = 0; absolute < 2; absolute++)
        for (unsigned mode = 0; mode < 4; mode++) cwd_case(absolute, mode);
    for (unsigned segment = 0; segment < 2; segment++)
        for (unsigned operation = 0; operation < 6; operation++) replacement_case(segment, operation);
    open_failure_releases_descriptors();
    printf("PASS WAL owned files checks=%u; cwd-cases=8 replacement-cases=12; no transaction recovery claim\n", checks);
    return 0;
}
