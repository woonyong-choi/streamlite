#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_wal_witness.h"
#include "storage/checksum.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
static unsigned checks, serial;
static const char *base;
static candidate_wal_options_t options;
static candidate_witness_options_t witness_options;

typedef struct { uint64_t id, first, last, count; uint32_t crc; } transaction_t;
typedef struct { uint64_t starts[32], ends[32]; unsigned count; } positions_t;

static uint64_t get(const uint8_t *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void put(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static void prefix(char out[PATH_MAX], const char *name)
{
    int n = snprintf(out, PATH_MAX, "%s-%s-%u", base, name, ++serial);
    CHECK(n > 0 && n < PATH_MAX - 22);
}

static void filename(char out[PATH_MAX], const char *p)
{
    int n = snprintf(out, PATH_MAX, "%s.wal.0000000000000000", p);
    CHECK(n > 0 && n < PATH_MAX);
}

static uint64_t append(candidate_wal_t *wal, transaction_t *t, unsigned type,
    const uint8_t *body, size_t bytes, positions_t *positions)
{
    uint64_t start = candidate_wal_info(wal).valid_end_lsn, end = 0;
    candidate_wal_record_t r = {.type = (uint16_t)type, .lsn = start,
        .transaction_id = t->id, .previous_lsn = t->last, .owner_epoch = options.owner_epoch,
        .payload = body, .payload_bytes = bytes};
    OK(candidate_wal_append(wal, &r, &end));
    if (positions != NULL) {
        CHECK(positions->count < 32);
        positions->starts[positions->count] = start;
        positions->ends[positions->count++] = end;
    }
    if (t->count == 0) t->first = start;
    t->last = start; t->count++;
    t->crc = candidate_wal_witness_crc(t->crc, &r);
    return end;
}

static void make_begin(uint8_t out[16], uint64_t snapshot)
{
    memset(out, 0, 16); put(out + 8, snapshot, 8);
}

static void make_terminal(uint8_t out[168], const transaction_t *t)
{
    memset(out, 0, 168); put(out, t->first, 8); put(out + 8, t->last, 8);
    put(out + 16, t->count, 8); put(out + 24, t->crc, 4);
}

static void sign_page(uint8_t page[8192])
{
    memset(page + 52, 0, 4); put(page + 52, crc32c(0, page, 8192), 4);
}

static uint64_t group(candidate_wal_t *wal, transaction_t *t, uint64_t id,
    unsigned type, positions_t *positions)
{
    uint8_t begin[16] = {0}; put(begin, id, 8); put(begin + 8, 1, 4); put(begin + 12, 1, 4);
    uint64_t first = candidate_wal_info(wal).valid_end_lsn, previous = t->last;
    append(wal, t, 3, begin, sizeof(begin), positions);
    candidate_wal_record_t b = {.type=3, .lsn=first, .transaction_id=t->id,
        .previous_lsn=previous, .owner_epoch=7,
        .payload=begin, .payload_bytes=16};
    uint32_t crc = candidate_wal_witness_crc(0, &b);
    uint8_t body[8224] = {0}; put(body, id, 8); put(body + 8, 1, 8); put(body + 16, 1, 8);
    size_t bytes = type == 4 ? sizeof(body) : 49;
    uint64_t at = candidate_wal_info(wal).valid_end_lsn;
    uint64_t expected_end = at + ((96 + bytes + 7) / 8 * 8) + 120;
    if (type == 4) {
        uint8_t *page = body + 32; memcpy(page, options.uuid, 16); memcpy(page + 16, "SLPG", 4);
        put(page + 20, 1, 2); put(page + 22, 1, 2); put(page + 24, 1, 8); put(page + 32, 1, 8);
        put(page + 40, expected_end, 8); put(page + 48, 1, 4); page[64] = 91; sign_page(page);
    } else {
        put(body + 32, 1, 4); put(body + 40, 64, 4); put(body + 44, 1, 4); body[48] = 92;
    }
    candidate_wal_record_t r = {.type=(uint16_t)type, .lsn=at, .transaction_id=t->id,
        .previous_lsn=t->last, .owner_epoch=7, .payload=body, .payload_bytes=bytes};
    crc = candidate_wal_witness_crc(crc, &r);
    append(wal, t, type, body, bytes, positions);
    uint8_t end[24] = {0}; put(end, id, 8); put(end + 8, 1, 4); put(end + 12, 1, 4); put(end + 16, crc, 4);
    uint64_t actual_end = append(wal, t, 8, end, sizeof(end), positions);
    CHECK(actual_end == expected_end);
    return actual_end;
}

static void golden(char out[PATH_MAX], positions_t *positions)
{
    prefix(out, "golden"); candidate_wal_t *wal = NULL; OK(candidate_wal_create(out, &options, &wal));
    transaction_t a = {.id=11}, b = {.id=22}, c = {.id=33}, d = {.id=44};
    uint8_t begin[16]; make_begin(begin, 0); append(wal, &a, 2, begin, 16, positions);
    uint8_t admit[64] = {0}; put(admit, 1, 8); admit[8] = 33;
    memset(admit + 24, 0xa1, 32); put(admit + 56, 65536, 8);
    append(wal, &b, 1, admit, 64, positions); append(wal, &b, 2, begin, 16, positions);
    uint64_t group_end = group(wal, &b, 1, 4, positions);
    uint8_t chunk[112] = {0}; put(chunk, 1, 8); chunk[8] = 33;
    put(chunk + 28, 1, 4); put(chunk + 32, 3, 8); memset(chunk + 40, 0xb2, 32);
    put(chunk + 72, 1, 8); put(chunk + 80, 1, 8); put(chunk + 92, 1, 4); put(chunk + 104, 3, 4);
    append(wal, &b, 9, chunk, sizeof(chunk), positions);
    uint8_t terminal[168]; make_terminal(terminal, &b); put(terminal + 32, 1, 8);
    put(terminal + 40, 1, 8); terminal[48] = 33; memset(terminal + 64, 0xa1, 32);
    memset(terminal + 96, 0xb2, 32); put(terminal + 128, 3, 8); put(terminal + 136, 1, 4);
    put(terminal + 148, 1, 4); put(terminal + 152, 1, 8); put(terminal + 160, group_end, 8);
    append(wal, &b, 10, terminal, sizeof(terminal), positions);
    group(wal, &a, 2, 5, positions); make_terminal(terminal, &a);
    append(wal, &a, 11, terminal, 32, positions);
    make_begin(begin, 1); append(wal, &c, 2, begin, 16, positions);
    make_terminal(terminal, &c); put(terminal + 32, 3, 8); append(wal, &c, 10, terminal, 152, positions);
    make_begin(begin, 3); append(wal, &d, 2, begin, 16, positions);
    uint8_t start[16] = {0}; put(start, 3, 8); put(start + 8, 1, 4); put(start + 12, 1, 4);
    append(wal, &d, 3, start, sizeof(start), positions);
    uint8_t delta[49] = {0}; put(delta, 3, 8); put(delta + 8, 1, 8); put(delta + 16, 1, 8);
    put(delta + 32, 1, 4); put(delta + 40, 64, 4); put(delta + 44, 1, 4); delta[48] = 93;
    uint64_t end = append(wal, &d, 5, delta, sizeof(delta), positions);
    OK(candidate_wal_force(wal, end)); OK(candidate_wal_close(&wal));
    printf("FIXTURE golden %s records=%u\n", out, positions->count);
}

static uint32_t file_crc(const char *p)
{
    char name[PATH_MAX]; filename(name, p); int fd = open(name, O_RDONLY); CHECK(fd >= 0);
    uint8_t bytes[8192]; ssize_t n; uint32_t crc = 0;
    while ((n = read(fd, bytes, sizeof(bytes))) > 0) crc = crc32c(crc, bytes, (size_t)n);
    CHECK(n == 0); CHECK(close(fd) == 0); return crc;
}

static void copy_prefix(const char *source, const char *target, uint64_t bytes)
{
    char a[PATH_MAX], b[PATH_MAX]; filename(a, source); filename(b, target);
    int in = open(a, O_RDONLY), out = open(b, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(in >= 0 && out >= 0); uint8_t buffer[8192]; uint64_t at = 0;
    while (at < bytes) {
        size_t n = bytes - at < sizeof(buffer) ? (size_t)(bytes - at) : sizeof(buffer);
        CHECK(read(in, buffer, n) == (ssize_t)n); CHECK(write(out, buffer, n) == (ssize_t)n); at += n;
    }
    CHECK(close(in) == 0); CHECK(close(out) == 0);
}

static candidate_wal_status_t analyze(const char *p, candidate_wal_witness_t **out)
{
    uint32_t before = file_crc(p); candidate_wal_t *wal = NULL;
    candidate_wal_status_t status = candidate_wal_open(p, &options, &wal);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_witness_analyze(wal, &witness_options, out);
    else CHECK(wal == NULL && *out == NULL);
    OK(candidate_wal_close(&wal)); CHECK(file_crc(p) == before); return status;
}

static void valid_and_truncated(const char *p, const positions_t *positions)
{
    candidate_wal_witness_t *w = NULL; OK(analyze(p, &w));
    candidate_witness_info_t info = candidate_wal_witness_info(w);
    CHECK(info.transactions == 4 && info.commits == 2 && info.aborts == 1 && info.unfinished == 1);
    CHECK(info.highest_csn == 3 && info.complete_groups == 2);
    CHECK(info.incomplete_group_lsn == positions->starts[15]);
    const candidate_witness_transaction_t *b = candidate_wal_witness_find(w, 22);
    CHECK(b != NULL && b->state == CANDIDATE_WITNESS_COMMIT && b->csn == 1);
    CHECK(b->result_bytes == 3 && b->result_chunks == 1 && b->groups == 1);
    CHECK(candidate_wal_witness_find(w, 11)->state == CANDIDATE_WITNESS_ABORT);
    CHECK(candidate_wal_witness_find(w, 44)->state == CANDIDATE_WITNESS_UNFINISHED);
    CHECK(candidate_wal_witness_find(w, 999) == NULL);
    candidate_wal_witness_close(&w); CHECK(w == NULL); candidate_wal_witness_close(&w);
    for (unsigned i = 0; i <= positions->count; i++) {
        char cut[PATH_MAX]; prefix(cut, "cut"); copy_prefix(p, cut, i == 0 ? 64 : positions->ends[i - 1]);
        OK(analyze(cut, &w)); info = candidate_wal_witness_info(w);
        CHECK(info.commits == (unsigned)(i >= 8) + (unsigned)(i >= 14));
        CHECK(info.aborts == (unsigned)(i >= 12)); CHECK(info.records == i);
        candidate_wal_witness_close(&w);
    }
    char tail[PATH_MAX]; prefix(tail, "partial-commit"); copy_prefix(p, tail, positions->ends[7] - 1);
    OK(analyze(tail, &w)); info = candidate_wal_witness_info(w);
    CHECK(info.commits == 0 && info.transport.has_tail); candidate_wal_witness_close(&w);
    printf("BOUNDARIES count=%u partial-commit=unconfirmed\n", positions->count + 1);
}

typedef struct { unsigned record, offset, size; uint64_t value; bool is_header, should_sign_page; candidate_wal_status_t expected; } mutation_t;

static void mutate(const char *p, uint64_t at, const mutation_t *m)
{
    char name[PATH_MAX]; filename(name, p); int fd = open(name, O_RDWR); CHECK(fd >= 0);
    uint8_t frame[9000], encoded[9000]; CHECK(pread(fd, frame, 80, (off_t)at) == 80);
    size_t size = (size_t)get(frame + 8, 4); CHECK(size <= sizeof(frame));
    CHECK(pread(fd, frame, size, (off_t)at) == (ssize_t)size);
    candidate_wal_record_t r; OK(candidate_wal_decode(options.uuid, frame, size, &r));
    if (m->is_header) {
        if (m->offset == 32) r.previous_lsn = m->value;
        else if (m->offset == 56) r.owner_epoch = m->value;
        else r.transaction_id = m->value;
    } else {
        CHECK(m->offset + m->size <= r.payload_bytes); put(frame + 80 + m->offset, m->value, m->size);
        if (m->should_sign_page) sign_page(frame + 80 + 32);
    }
    size_t written; OK(candidate_wal_encode(options.uuid, &r, encoded, sizeof(encoded), &written));
    CHECK(written == size); CHECK(pwrite(fd, encoded, size, (off_t)at) == (ssize_t)size); CHECK(close(fd) == 0);
}

static void signed_corruption(const char *p, const positions_t *positions)
{
    const mutation_t cases[] = {
        {7,0,8,64,0,0,CANDIDATE_WAL_CORRUPT}, {7,8,8,64,0,0,CANDIDATE_WAL_CORRUPT},
        {7,16,8,99,0,0,CANDIDATE_WAL_CORRUPT}, {7,24,4,99,0,0,CANDIDATE_WAL_CORRUPT},
        {7,32,8,0,0,0,CANDIDATE_WAL_CORRUPT}, {7,40,8,2,0,0,CANDIDATE_WAL_CORRUPT},
        {7,48,1,99,0,0,CANDIDATE_WAL_CORRUPT}, {7,64,1,99,0,0,CANDIDATE_WAL_CORRUPT},
        {7,96,1,99,0,0,CANDIDATE_WAL_CORRUPT}, {7,128,8,4,0,0,CANDIDATE_WAL_CORRUPT},
        {7,136,4,0,0,0,CANDIDATE_WAL_CORRUPT}, {7,140,1,1,0,0,CANDIDATE_WAL_CORRUPT},
        {7,148,4,0,0,0,CANDIDATE_WAL_CORRUPT}, {7,152,8,99,0,0,CANDIDATE_WAL_CORRUPT},
        {7,160,8,99,0,0,CANDIDATE_WAL_CORRUPT}, {13,32,8,1,0,0,CANDIDATE_WAL_CORRUPT},
        {3,8,4,2,0,0,CANDIDATE_WAL_CORRUPT}, {3,8,8,(UINT64_C(65)<<32)|65,0,0,CANDIDATE_WAL_LIMIT},
        {5,16,4,0,0,0,CANDIDATE_WAL_CORRUPT}, {5,8,4,2,0,0,CANDIDATE_WAL_CORRUPT},
        {4,32,1,99,0,1,CANDIDATE_WAL_UNSUPPORTED}, {4,72,8,1,0,1,CANDIDATE_WAL_CORRUPT},
        {4,56,8,2,0,1,CANDIDATE_WAL_CORRUPT}, {4,96,1,2,0,0,CANDIDATE_WAL_CORRUPT},
        {6,24,4,1,0,0,CANDIDATE_WAL_CORRUPT}, {6,32,8,65537,0,0,CANDIDATE_WAL_CORRUPT},
        {6,80,8,0,0,0,CANDIDATE_WAL_CORRUPT}, {6,104,4,2,0,0,CANDIDATE_WAL_CORRUPT},
        {9,40,4,8192,0,0,CANDIDATE_WAL_CORRUPT}, {9,44,4,9999,0,0,CANDIDATE_WAL_CORRUPT},
        {7,32,8,64,1,0,CANDIDATE_WAL_CORRUPT}, {7,56,8,8,1,0,CANDIDATE_WAL_CORRUPT},
        {12,24,8,11,1,0,CANDIDATE_WAL_CORRUPT}, {2,8,8,10,0,0,CANDIDATE_WAL_CORRUPT},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char target[PATH_MAX]; prefix(target, "signed-corrupt"); copy_prefix(p, target, positions->ends[16]);
        mutate(target, positions->starts[cases[i].record], &cases[i]);
        candidate_wal_witness_t *w = NULL; candidate_wal_status_t actual = analyze(target, &w);
        if (actual != cases[i].expected) fprintf(stderr, "mutation=%u actual=%d expected=%d\n", i, actual, cases[i].expected);
        CHECK(actual == cases[i].expected); CHECK(w == NULL);
    }
    printf("SIGNED_CORRUPTION count=%zu\n", sizeof(cases) / sizeof(cases[0]));
}

typedef struct { unsigned allocations; size_t live; bool should_fail; } memory_t;
static void *allocate(void *context, size_t size)
{
    memory_t *m = context; m->allocations++; if (m->should_fail) return NULL;
    void *p = malloc(size); if (p != NULL) m->live += size; return p;
}
static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live >= size); m->live -= size; free(p);
}

static void limits_and_unsupported(const char *p)
{
    candidate_wal_t *wal = NULL; OK(candidate_wal_open(p, &options, &wal));
    memory_t memory = {.should_fail=true}; candidate_wal_memory_t allocator = {&memory, allocate, release};
    candidate_witness_options_t o = witness_options; o.memory = &allocator;
    candidate_wal_witness_t *w = NULL;
    CHECK(candidate_wal_witness_analyze(wal, &o, &w) == CANDIDATE_WAL_NOMEM);
    CHECK(w == NULL && memory.live == 0); memory.should_fail = false;
    OK(candidate_wal_witness_analyze(wal, &o, &w)); CHECK(memory.live == candidate_wal_witness_info(w).resident_bytes);
    candidate_wal_witness_close(&w); CHECK(memory.live == 0); OK(candidate_wal_close(&wal));
    char target[PATH_MAX]; prefix(target, "active-limit"); OK(candidate_wal_create(target, &options, &wal));
    for (unsigned i = 1; i <= 65; i++) {
        transaction_t t = {.id=i}; uint8_t begin[16] = {0}; append(wal, &t, 2, begin, 16, NULL);
    }
    OK(candidate_wal_close(&wal)); CHECK(analyze(target, &w) == CANDIDATE_WAL_LIMIT); CHECK(w == NULL);
    for (unsigned type = 12; type <= 16; type++) {
        prefix(target, "unsupported"); OK(candidate_wal_create(target, &options, &wal));
        transaction_t t = {.id=1}; uint8_t body[16] = {0}; append(wal, &t, type, body, 16, NULL);
        OK(candidate_wal_close(&wal)); CHECK(analyze(target, &w) == CANDIDATE_WAL_UNSUPPORTED); CHECK(w == NULL);
    }
    prefix(target, "transaction-limit"); OK(candidate_wal_create(target, &options, &wal));
    for (unsigned i = 1; i <= CANDIDATE_WITNESS_TRANSACTIONS; i++) {
        transaction_t t = {.id=i}; uint8_t begin[16] = {0}, terminal[168];
        append(wal, &t, 2, begin, 16, NULL); make_terminal(terminal, &t);
        append(wal, &t, 11, terminal, 32, NULL);
    }
    transaction_t t = {.id=CANDIDATE_WITNESS_TRANSACTIONS + 1}; uint8_t begin[16] = {0};
    append(wal, &t, 2, begin, 16, NULL); OK(candidate_wal_close(&wal));
    CHECK(analyze(target, &w) == CANDIDATE_WAL_LIMIT); CHECK(w == NULL);
}

static bool has_read_failure;
static uint64_t read_failure_at;
static ssize_t injected_read(int fd, void *p, size_t size, off_t at)
{
    if (has_read_failure && (uint64_t)at >= read_failure_at) { errno = EIO; return -1; }
    return pread(fd, p, size, at);
}

static void scan_error_has_no_partial_result(const char *p, uint64_t at)
{
    candidate_wal_io_t io = {injected_read, pwrite, fsync, ftruncate};
    candidate_wal_options_t o = options; o.io = &io; candidate_wal_t *wal = NULL;
    OK(candidate_wal_open(p, &o, &wal)); has_read_failure = true; read_failure_at = at;
    candidate_wal_witness_t *w = NULL;
    CHECK(candidate_wal_witness_analyze(wal, &witness_options, &w) == CANDIDATE_WAL_IO);
    CHECK(w == NULL); CHECK(candidate_wal_force(wal, 64) == CANDIDATE_WAL_POISONED);
    has_read_failure = false; OK(candidate_wal_close(&wal));
}

static void empty_and_multiple_result_chunks(void)
{
    const uint8_t empty_digest[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    for (unsigned scenario = 0; scenario < 2; scenario++) {
        char p[PATH_MAX]; prefix(p, "result-chunks"); candidate_wal_t *wal = NULL;
        OK(candidate_wal_create(p, &options, &wal)); transaction_t t = {.id=1};
        uint8_t admission[64] = {0}; put(admission, 1, 8); admission[8] = scenario == 0 ? 0 : 55;
        memset(admission + 24, 0xa1, 32); put(admission + 56, 65537, 8);
        append(wal, &t, 1, admission, sizeof(admission), NULL);
        uint8_t begin[16] = {0}; append(wal, &t, 2, begin, 16, NULL);
        if (scenario == 1) {
            for (unsigned i = 0; i < 2; i++) {
                uint8_t chunk[112] = {0}; put(chunk, 1, 8); chunk[8] = 55;
                put(chunk + 24, i, 4); put(chunk + 28, 2, 4); put(chunk + 32, 65537, 8);
                memset(chunk + 40, 0xb2, 32); put(chunk + 72, 1, 8); put(chunk + 80, 1, 8);
                put(chunk + 92, 1, 4); put(chunk + 96, i * 65536, 8); put(chunk + 104, i == 0 ? 65536 : 1, 4);
                append(wal, &t, 9, chunk, sizeof(chunk), NULL);
            }
        }
        uint8_t terminal[168]; make_terminal(terminal, &t); put(terminal + 32, 1, 8);
        put(terminal + 40, 1, 8); terminal[48] = scenario == 0 ? 0 : 55; memset(terminal + 64, 0xa1, 32);
        if (scenario == 0) memcpy(terminal + 96, empty_digest, 32);
        else { memset(terminal + 96, 0xb2, 32); put(terminal + 128, 65537, 8); put(terminal + 136, 2, 4); }
        uint64_t end = append(wal, &t, 10, terminal, 152, NULL); OK(candidate_wal_force(wal, end));
        OK(candidate_wal_close(&wal)); candidate_wal_witness_t *w = NULL; OK(analyze(p, &w));
        const candidate_witness_transaction_t *value = candidate_wal_witness_find(w, 1);
        CHECK(value->state == CANDIDATE_WITNESS_COMMIT);
        CHECK(value->result_bytes == (scenario == 0 ? 0 : 65537));
        CHECK(value->result_chunks == (scenario == 0 ? 0 : 2)); candidate_wal_witness_close(&w);
    }
}

static void duplicate_and_interleaved_records(const char *source, const positions_t *positions)
{
    for (unsigned scenario = 0; scenario < 7; scenario++) {
        char p[PATH_MAX]; prefix(p, "protocol");
        uint64_t size = scenario == 4 ? positions->ends[13] : positions->ends[16];
        copy_prefix(source, p, size); candidate_wal_t *wal = NULL;
        OK(candidate_wal_open(p, &options, &wal));
        uint8_t body[168] = {0}; transaction_t t = {.id=55};
        candidate_wal_status_t expected = CANDIDATE_WAL_CORRUPT;
        if (scenario == 0) {
            t.id = 22; t.last = positions->starts[7]; append(wal, &t, 10, body, 152, NULL);
        } else if (scenario == 1) {
            t.id = 11; t.last = positions->starts[11]; append(wal, &t, 2, body, 16, NULL);
        } else if (scenario == 2) {
            t.id = 44; t.last = positions->starts[16]; append(wal, &t, 2, body, 16, NULL);
        } else if (scenario == 3) {
            append(wal, &t, 2, body, 16, NULL);
        } else if (scenario == 4) {
            append(wal, &t, 2, body, 16, NULL); append(wal, &t, 8, body, 24, NULL);
        } else if (scenario == 5) {
            t.id = 44; t.last = positions->starts[16]; put(body, 3, 8); put(body + 8, 1, 8);
            put(body + 16, 1, 8); append(wal, &t, 6, body, 48, NULL); expected = CANDIDATE_WAL_UNSUPPORTED;
        } else {
            OK(candidate_wal_close(&wal));
            char duplicate[PATH_MAX]; prefix(duplicate, "duplicate-id");
            copy_prefix(source, duplicate, positions->ends[13]); strcpy(p, duplicate);
            OK(candidate_wal_open(p, &options, &wal)); put(body, 1, 8); body[8] = 33;
            memset(body + 24, 99, 32); append(wal, &t, 1, body, 64, NULL);
        }
        OK(candidate_wal_close(&wal)); candidate_wal_witness_t *w = NULL;
        CHECK(analyze(p, &w) == expected); CHECK(w == NULL);
    }
}

int main(int argc, char **argv)
{
    if (argc > 2) return 2;
    char automatic[128]; snprintf(automatic, sizeof(automatic), "build/candidate-witness-%ld", (long)getpid());
    alarm(90); base = argc == 2 ? argv[1] : automatic;
    setvbuf(stdout, NULL, _IONBF, 0); options.owner_epoch = 7;
    for (unsigned i = 0; i < 16; i++) options.uuid[i] = witness_options.uuid[i] = (uint8_t)i + 1;
    char p[PATH_MAX]; positions_t positions = {0}; golden(p, &positions);
    valid_and_truncated(p, &positions); signed_corruption(p, &positions); limits_and_unsupported(p);
    scan_error_has_no_partial_result(p, positions.starts[14]);
    empty_and_multiple_result_chunks();
    duplicate_and_interleaved_records(p, &positions);
    printf("PASS WAL witness checks=%u; result bodies, redo, APPLIED and recovery activation not implemented\n", checks);
    return 0;
}
