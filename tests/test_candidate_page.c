#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_page.h"
#include "storage/checksum.h"
#include "storage/record.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)

static unsigned checks;
static const uint8_t uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0; for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static void resign(uint8_t *p)
{
    memset(p + 52, 0, 4); write_le(p + 52, crc32c(0, p, 8192), 4);
}

static candidate_page_location_t location(uint64_t id)
{
    candidate_page_location_t l = {.ref = {id, 1}, .lsn = 1024}; memcpy(l.uuid, uuid, 16); return l;
}

static candidate_page_version_t version(void)
{
    return (candidate_page_version_t){.transaction_id = 9, .table_id = 1, .row_id = 42,
                                     .slot_generation = 7};
}

static void print_digest(const uint8_t digest[32])
{
    for (unsigned i = 0; i < 32; i++) printf("%02x", digest[i]);
}

static void digest_vectors(void)
{
    const char *messages[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (unsigned i = 0; i < 3; i++) {
        uint8_t digest[32]; char hex[65]; OK(candidate_digest_sha256((const uint8_t *)messages[i], strlen(messages[i]), digest));
        for (unsigned j = 0; j < 32; j++) snprintf(hex + j * 2, 3, "%02x", digest[j]);
        CHECK(strcmp(hex, expected[i]) == 0);
    }
    uint8_t *bytes = malloc(CANDIDATE_RESULT_MAX_BYTES); CHECK(bytes != NULL);
    for (unsigned i = 0; i < CANDIDATE_RESULT_MAX_BYTES; i++) bytes[i] = (uint8_t)(i % 251);
    for (unsigned i = 0; i < 135; i++) {
        size_t size = i < 131 ? i : (i == 131 ? 65535 : (i == 132 ? 65536 : (i == 133 ? 65537 : 1048576)));
        uint8_t digest[32]; OK(candidate_digest_sha256(bytes, size, digest));
        printf("SHA256 size=%zu digest=", size); print_digest(digest); printf("\n");
    }
    uint8_t out[32]; memset(out, 0xcc, sizeof(out));
    CHECK(candidate_digest_sha256(NULL, 1, out) == CANDIDATE_WAL_INVALID);
    if (SIZE_MAX > UINT64_MAX / 8) CHECK(candidate_digest_sha256(bytes, SIZE_MAX, out) == CANDIDATE_WAL_LIMIT);
    for (unsigned i = 0; i < 32; i++) CHECK(out[i] == 0xcc);
    OK(candidate_digest_sha256(NULL, 0, out)); free(bytes);
}

static void write_fixture(const char *suffix, const uint8_t *bytes, size_t size)
{
    char name[PATH_MAX]; CHECK(snprintf(name, sizeof(name), "build/candidate-page-%ld-%s", (long)getpid(), suffix) < (int)sizeof(name));
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(fd >= 0);
    CHECK(write(fd, bytes, size) == (ssize_t)size); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
    printf("FIXTURE %s %s bytes=%zu\n", suffix, name, size);
}

static void codec_guards(void)
{
    candidate_page_location_t l = location(1);
    const uint8_t body[] = {0,255,1,128,0};
    candidate_page_version_t values[3] = {version(),version(),version()};
    values[0].payload_bytes = sizeof(body); values[0].inline_bytes = body;
    values[1].is_tombstone = true; values[1].slot_generation = 8;
    values[1].previous = (candidate_version_ref_t){{1,1},0,7};
    values[2].payload_bytes = 9000; values[2].overflow = (candidate_page_ref_t){2,1};
    uint8_t golden[8192]; OK(candidate_page_encode_versions(&l, values, 3, golden, sizeof(golden)));
    candidate_page_info_t info; OK(candidate_page_decode(uuid, golden, sizeof(golden), &info));
    CHECK(info.slots == 3 && info.type == 1 && info.location.lsn == 1024);
    for (unsigned i = 0; i < 3; i++) {
        candidate_page_version_t v; candidate_version_ref_t ref = {{1,1},i,values[i].slot_generation};
        OK(candidate_page_get_version(uuid, golden, sizeof(golden), ref, &v));
        CHECK(v.payload_bytes == values[i].payload_bytes && v.is_tombstone == values[i].is_tombstone);
    }
    for (size_t size = 0; size < 8192; size++) {
        info.body_bytes = UINT32_MAX;
        CHECK(candidate_page_decode(uuid, golden, size, &info) == CANDIDATE_WAL_CORRUPT);
        CHECK(info.body_bytes == UINT32_MAX);
    }
    uint8_t page[8192];
    for (unsigned i = 0; i < 8192; i++) {
        memcpy(page, golden, sizeof(page)); page[i] ^= 1;
        CHECK(candidate_page_decode(uuid, page, sizeof(page), &info) != CANDIDATE_WAL_OK);
    }
    unsigned first = (unsigned)read_le(golden + 72, 2), second = (unsigned)read_le(golden + 80, 2);
    struct { unsigned offset, width; uint64_t value; } mutations[] = {
        {16,4,0},{20,2,2},{22,2,3},{24,8,0},{32,8,0},{40,8,1},{48,4,8129},{56,8,1},
        {64,4,102},{68,4,1},{72,2,0},{74,2,65535},{76,4,0},{first,8,0},
        {first+40,8,0},{first+48,8,0},{first+56,8,UINT64_MAX},{first+64,4,2},
        {first+68,4,1},{first+72,4,3},{first+76,4,1},{first+80,8,UINT64_MAX},
        {first+88,8,1},{first+36,4,1},{8191,1,1},{80,2,first},{84,4,0},{second+56,8,1}
    };
    for (unsigned i = 0; i < sizeof(mutations)/sizeof(mutations[0]); i++) {
        memcpy(page, golden, sizeof(page)); write_le(page + mutations[i].offset, mutations[i].value, mutations[i].width); resign(page);
        CHECK(candidate_page_decode(uuid, page, sizeof(page), &info) != CANDIDATE_WAL_OK);
    }
    memcpy(page, golden, sizeof(page));
    write_le(page + first + 16, 1, 8); write_le(page + first + 24, 1, 8); write_le(page + first + 36, 7, 4); resign(page);
    CHECK(candidate_page_decode(uuid, page, sizeof(page), &info) == CANDIDATE_WAL_CORRUPT);
    uint32_t seed = 23;
    for (unsigned i = 0; i < 1024; i++) {
        seed = seed * 1664525u + 1013904223u;
        memcpy(page, golden, sizeof(page)); page[seed % 8192] ^= (uint8_t)(1u << (seed % 8)); resign(page);
        if (candidate_page_decode(uuid, page, sizeof(page), &info) != CANDIDATE_WAL_OK) continue;
        candidate_page_version_t decoded[3];
        for (unsigned j = 0; j < 3; j++) {
            unsigned generation = (unsigned)read_le(page + 76 + j * 8, 4);
            OK(candidate_page_get_version(uuid, page, sizeof(page), (candidate_version_ref_t){info.location.ref,j,generation}, &decoded[j]));
        }
        uint8_t roundtrip[8192]; OK(candidate_page_encode_versions(&info.location, decoded, 3, roundtrip, sizeof(roundtrip)));
        CHECK(memcmp(roundtrip, page, sizeof(page)) == 0);
    }
    write_fixture("golden", golden, sizeof(golden));
    printf("CODEC truncations=8192 byte-corruptions=8192 signed-invalid=%zu randomized=1024\n",
           sizeof(mutations)/sizeof(mutations[0])+1);
}

static void encode_limits(void)
{
    uint8_t page[8192], data[8009]; memset(page, 0xcc, sizeof(page)); memset(data, 9, sizeof(data));
    candidate_page_location_t l = location(1); candidate_page_version_t v = version();
    v.inline_bytes = data; v.payload_bytes = 8009;
    CHECK(candidate_page_encode_versions(&l, &v, 1, page, sizeof(page)) == CANDIDATE_WAL_LIMIT);
    for (unsigned i = 0; i < sizeof(page); i++) CHECK(page[i] == 0xcc);
    v.payload_bytes = 8008; OK(candidate_page_encode_versions(&l, &v, 1, page, sizeof(page)));
    candidate_page_info_t info; OK(candidate_page_decode(uuid, page, sizeof(page), &info)); CHECK(info.body_bytes == 8128);
    v = version(); v.is_tombstone = true; candidate_page_version_t tombstones[101];
    for (unsigned i = 0; i < 101; i++) tombstones[i] = v;
    OK(candidate_page_encode_versions(&l, tombstones, 101, page, sizeof(page)));
    OK(candidate_page_decode(uuid, page, sizeof(page), &info)); CHECK(info.slots == 101);
    CHECK(candidate_page_encode_versions(&l, tombstones, 102, page, sizeof(page)) == CANDIDATE_WAL_LIMIT);
    OK(candidate_page_encode_versions(&l, NULL, 0, page, sizeof(page)));
    OK(candidate_page_decode(uuid, page, sizeof(page), &info)); CHECK(info.slots == 0);
    v = version(); v.previous = (candidate_version_ref_t){{1,1},0,7};
    CHECK(candidate_page_encode_versions(&l, &v, 1, page, sizeof(page)) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_page_encode_overflow(&l, (candidate_page_ref_t){2,1}, data, 1, page, sizeof(page)) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_page_encode_overflow(&l, (candidate_page_ref_t){0,1}, data, 1, page, sizeof(page)) == CANDIDATE_WAL_INVALID);
}

typedef struct {
    uint8_t *pages; unsigned count, calls, fail_call;
    unsigned allocations, fail_allocation; size_t live;
} fixture_t;

static candidate_wal_status_t load(void *context, candidate_page_ref_t ref, uint8_t page[8192])
{
    fixture_t *f = context;
    if (++f->calls == f->fail_call) return CANDIDATE_WAL_IO;
    if (ref.id == 0 || ref.id > f->count) return CANDIDATE_WAL_IO;
    memcpy(page, f->pages + (ref.id - 1) * 8192, 8192); return CANDIDATE_WAL_OK;
}

static void *allocate(void *context, size_t size)
{
    fixture_t *f = context;
    if (++f->allocations == f->fail_allocation) return NULL;
    void *p = malloc(size); if (p != NULL) f->live += size; return p;
}

static void release(void *context, void *p, size_t size)
{
    fixture_t *f = context; CHECK(f->live >= size); f->live -= size; free(p);
}

static candidate_result_read_t prepare(fixture_t *f, const uint8_t *body, size_t size)
{
    unsigned overflows = size > 8008 ? (unsigned)((size + 8103) / 8104) : 0;
    f->count = 1 + overflows; f->pages = calloc(f->count, 8192); CHECK(f->pages != NULL);
    candidate_page_version_t v = version(); v.payload_bytes = size;
    if (overflows > 0) v.overflow = (candidate_page_ref_t){2,1}; else v.inline_bytes = body;
    candidate_page_location_t l = location(1); OK(candidate_page_encode_versions(&l, &v, 1, f->pages, 8192));
    size_t at = 0;
    for (unsigned i = 0; i < overflows; i++) {
        size_t bytes = size - at < 8104 ? size - at : 8104;
        candidate_page_ref_t next = {i + 1 == overflows ? 0 : i + 3, i + 1 == overflows ? 0 : 1};
        l = location(i + 2); OK(candidate_page_encode_overflow(&l, next, body + at, bytes, f->pages + (i + 1) * 8192, 8192)); at += bytes;
    }
    candidate_result_read_t r = {.ref = {{1,1},0,7}, .transaction_id = 9, .result_bytes = size, .load = load, .context = f};
    memcpy(r.uuid, uuid, 16); OK(candidate_digest_sha256(body, size, r.digest)); return r;
}

static void rejected(candidate_result_read_t *r, candidate_wal_status_t expected)
{
    fixture_t *f = r->context; f->calls = f->allocations = 0;
    candidate_page_result_t *result = NULL;
    CHECK(candidate_page_read_result(r, &result) == expected); CHECK(result == NULL && f->live == 0);
}

static void result_reader(void)
{
    uint8_t *body = malloc(CANDIDATE_RESULT_MAX_BYTES); CHECK(body != NULL);
    for (unsigned i = 0; i < CANDIDATE_RESULT_MAX_BYTES; i++) body[i] = (uint8_t)(i % 251);
    const size_t sizes[] = {0,1,8008,8009,9000,24312,1048576};
    for (unsigned i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        fixture_t f = {0}; candidate_result_read_t r = prepare(&f, body, sizes[i]);
        candidate_wal_memory_t memory = {&f, allocate, release}; r.memory = &memory;
        candidate_page_result_t *result = NULL; OK(candidate_page_read_result(&r, &result));
        CHECK(candidate_page_result_size(result) == sizes[i]);
        if (sizes[i] > 0) CHECK(memcmp(candidate_page_result_bytes(result), body, sizes[i]) == 0);
        CHECK(f.live > 0); candidate_page_result_close(&result); candidate_page_result_close(&result); CHECK(f.live == 0);
        for (unsigned call = 1; call <= f.count; call++) { f.fail_call = call; rejected(&r, CANDIDATE_WAL_IO); }
        f.fail_call = 0;
        for (unsigned call = 1; call <= (sizes[i] == 0 ? 1u : 2u); call++) { f.fail_allocation = call; rejected(&r, CANDIDATE_WAL_NOMEM); }
        f.fail_allocation = 0; r.digest[0] ^= 1; rejected(&r, CANDIDATE_WAL_CORRUPT); r.digest[0] ^= 1;
        r.transaction_id = 10; rejected(&r, CANDIDATE_WAL_CORRUPT); r.transaction_id = 9;
        r.ref.slot_generation++; rejected(&r, CANDIDATE_WAL_CORRUPT); r.ref.slot_generation--;
        r.ref.page.generation++; rejected(&r, CANDIDATE_WAL_CORRUPT); r.ref.page.generation--;
        r.ref.slot = UINT32_MAX; rejected(&r, CANDIDATE_WAL_CORRUPT); r.ref.slot = 0;
        if (sizes[i] == 24312) {
            uint8_t *second = f.pages + 2 * 8192;
            write_le(second + 64, 2, 8); resign(second); rejected(&r, CANDIDATE_WAL_CORRUPT);
        }
        if (sizes[i] == 9000) {
            write_fixture("overflow", f.pages, f.count * 8192);
            uint8_t *overflow = f.pages + 8192;
            overflow[88] ^= 1; resign(overflow); rejected(&r, CANDIDATE_WAL_CORRUPT);
            overflow[88] ^= 1; write_le(overflow + 64, 0, 8); write_le(overflow + 72, 0, 8); resign(overflow);
            rejected(&r, CANDIDATE_WAL_CORRUPT);
        }
        r.result_bytes = 1048577; rejected(&r, CANDIDATE_WAL_LIMIT); CHECK(f.calls == 0);
        free(f.pages);
    }
    free(body);
}

static void invalid_reader_inputs(void)
{
    const uint8_t body = 1;
    fixture_t f = {0}; candidate_result_read_t r = prepare(&f, &body, 1), saved = r;
    for (unsigned i = 0; i < 6; i++) {
        r = saved;
        if (i == 0) r.ref.page.id = 0;
        if (i == 1) r.ref.page.generation = 0;
        if (i == 2) r.ref.slot_generation = 0;
        if (i == 3) memset(r.uuid, 0, 16);
        if (i == 4) r.transaction_id = 0;
        if (i == 5) r.load = NULL;
        rejected(&r, CANDIDATE_WAL_INVALID); CHECK(f.calls == 0);
    }
    r = saved; candidate_wal_memory_t invalid = {&f, allocate, NULL}; r.memory = &invalid;
    rejected(&r, CANDIDATE_WAL_INVALID); CHECK(f.calls == 0);
    candidate_page_result_t *out = NULL;
    CHECK(candidate_page_read_result(NULL, &out) == CANDIDATE_WAL_INVALID && out == NULL);
    CHECK(candidate_page_read_result(&saved, NULL) == CANDIDATE_WAL_INVALID); free(f.pages);
}

static void record_roundtrip(void)
{
    const uint8_t text[] = {0xed,0x95,0x9c,0xea,0xb8,0x80,0,0xeb,0x81,0x9d};
    uint8_t blob[9000]; for (unsigned i = 0; i < sizeof(blob); i++) blob[i] = (uint8_t)i;
    record_value_t input[] = {{.type = RECORD_NULL},{.type = RECORD_INTEGER,.integer = -5},
        {.type = RECORD_TEXT,.bytes = text,.size_bytes = sizeof(text)},
        {.type = RECORD_BLOB,.bytes = blob,.size_bytes = sizeof(blob)}};
    record_buffer_t encoded = {0}; CHECK(record_encode(input, 4, &encoded) == RECORD_OK);
    fixture_t f = {0}; candidate_result_read_t r = prepare(&f, encoded.bytes, encoded.size_bytes);
    candidate_page_result_t *result = NULL; OK(candidate_page_read_result(&r, &result));
    record_buffer_t restored = {(uint8_t *)candidate_page_result_bytes(result), candidate_page_result_size(result)};
    record_value_t decoded[4]; size_t count = 0; CHECK(record_decode(&restored, decoded, 4, &count) == RECORD_OK); CHECK(count == 4);
    CHECK(decoded[0].type == RECORD_NULL && decoded[1].integer == -5);
    CHECK(decoded[2].size_bytes == sizeof(text) && memcmp(decoded[2].bytes, text, sizeof(text)) == 0);
    CHECK(decoded[3].size_bytes == sizeof(blob) && memcmp(decoded[3].bytes, blob, sizeof(blob)) == 0);
    candidate_page_result_close(&result); free(f.pages); free(encoded.bytes);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90);
    digest_vectors(); codec_guards(); encode_limits(); result_reader(); invalid_reader_inputs(); record_roundtrip();
    printf("PASS candidate pages checks=%u; body integrity only, no APPLIED/MVCC/recovery activation\n", checks);
    return 0;
}
