#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_catalog.h"
#include "storage/candidate_digest.h"
#include "storage/checksum.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
static unsigned checks;
static const uint8_t uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
typedef struct { uint8_t pages[91][8192]; unsigned calls, fail_at; int fd; } fixture_t;
typedef struct { size_t live, peak; unsigned calls; bool fail; } memory_t;

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static void resign(uint8_t *p)
{
    memset(p + 52, 0, 4); write_le(p + 52, crc32c(0, p, 8192), 4);
}

static candidate_page_location_t location(unsigned id)
{
    candidate_page_location_t l = {.ref = {id,1}, .lsn = 1024}; memcpy(l.uuid, uuid, 16); return l;
}

static candidate_outcome_t outcome(unsigned i)
{
    candidate_outcome_t e = {.epoch = 2 + i / 1024, .transaction_id = i + 1,
        .state = (candidate_outcome_state_t)(1 + i % 3), .admitting_owner_epoch = 7,
        .declared_result_limit = 1048576};
    write_le(e.nonce, i % 1024, 8); memset(e.operation_digest, 0x5c, 32);
    if (e.state != CANDIDATE_OUTCOME_ADMITTED) {
        e.terminal_end_lsn = 2048; OK(candidate_digest_sha256(NULL, 0, e.result_digest));
        if (e.state == CANDIDATE_OUTCOME_COMMITTED) { e.csn = 4096 - i; e.result = (candidate_version_ref_t){{10000,1},0,7}; }
    }
    return e;
}

static candidate_catalog_root_t root(unsigned count)
{
    candidate_catalog_root_t r = {.owner_epoch = 7, .current_epoch = 5, .retention_floor = 2,
        .transaction_highwater = 4096, .page_highwater = 10000, .visible_csn = 4096,
        .retained_bytes = 91u * 8192u, .count = count};
    for (unsigned i = 0; i < 90; i++) r.pages[i] = (candidate_page_ref_t){i + 2,1};
    return r;
}

static void fill(fixture_t *f, unsigned count)
{
    candidate_catalog_root_t r = root(count); candidate_page_location_t l = location(1);
    OK(candidate_catalog_encode_root(&l, &r, f->pages[0], 8192));
    for (unsigned at = 0, i = 0; at < count; i++) {
        candidate_catalog_node_t node = {.ordinal = i, .count = count - at < 46 ? count - at : 46};
        for (unsigned j = 0; j < node.count; j++, at++) node.entries[j] = outcome(at);
        l = location(i + 2); OK(candidate_catalog_encode_node(&l, &node, f->pages[i + 1], 8192));
    }
    f->calls = 0; f->fail_at = 0; f->fd = -1;
}

static candidate_wal_status_t load(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    fixture_t *f = context;
    if (++f->calls == f->fail_at || ref.id == 0 || ref.id > 91) return CANDIDATE_WAL_IO;
    if (f->fd >= 0) return pread(f->fd, bytes, 8192, (off_t)((ref.id - 1) * 8192)) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
    memcpy(bytes, f->pages[ref.id - 1], 8192); return CANDIDATE_WAL_OK;
}

static void *allocate(void *context, size_t size)
{
    memory_t *m = context; m->calls++;
    if (m->fail) return NULL;
    void *p = malloc(size); if (p != NULL) { m->live += size; if (m->peak < m->live) m->peak = m->live; } return p;
}

static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live >= size); m->live -= size; free(p);
}

static candidate_catalog_load_t request(fixture_t *f, const candidate_wal_memory_t *memory)
{
    candidate_catalog_load_t r = {.root = {1,1}, .load = load, .context = f, .memory = memory}; memcpy(r.uuid, uuid, 16); return r;
}

static void codec(fixture_t *f)
{
    fill(f, 4096); uint8_t bad[8192], encoded[8192];
    candidate_catalog_root_t r; candidate_catalog_node_t n;
    for (unsigned kind = 0; kind < 2; kind++) {
        for (unsigned size = 0; size < 8192; size++) {
            candidate_wal_status_t s = kind == 0 ? candidate_catalog_decode_root(uuid, f->pages[kind], size, &r)
                : candidate_catalog_decode_node(uuid, f->pages[kind], size, &n); CHECK(s != CANDIDATE_WAL_OK);
        }
        for (unsigned byte = 0; byte < 8192; byte++) {
            memcpy(bad, f->pages[kind], 8192); bad[byte] ^= 1;
            candidate_wal_status_t s = kind == 0 ? candidate_catalog_decode_root(uuid, bad, 8192, &r)
                : candidate_catalog_decode_node(uuid, bad, 8192, &n); CHECK(s != CANDIDATE_WAL_OK);
        }
    }
    OK(candidate_catalog_decode_root(uuid, f->pages[0], 8192, &r));
    candidate_page_location_t l = location(1); OK(candidate_catalog_encode_root(&l, &r, encoded, 8192)); CHECK(memcmp(encoded, f->pages[0], 8192) == 0);
    for (unsigned i = 1; i < 91; i++) {
        OK(candidate_catalog_decode_node(uuid, f->pages[i], 8192, &n)); l = location(i + 1);
        OK(candidate_catalog_encode_node(&l, &n, encoded, 8192)); CHECK(memcmp(encoded, f->pages[i], 8192) == 0);
    }
    const unsigned root_offsets[] = {64,68,70,72,80,88,96,104,120,128,132,136,144};
    for (unsigned i = 0; i < sizeof(root_offsets) / sizeof(root_offsets[0]); i++) {
        memcpy(bad, f->pages[0], 8192); write_le(bad + root_offsets[i], 0, 8);
        if (root_offsets[i] == 132) bad[132] = 1;
        resign(bad); CHECK(candidate_catalog_decode_root(uuid, bad, 8192, &r) != CANDIDATE_WAL_OK);
    }
    const unsigned node_offsets[] = {68,70,76,80,96,152,160,164,168,176,184,192,200,208,216,248,252,256,264};
    for (unsigned i = 0; i < sizeof(node_offsets) / sizeof(node_offsets[0]); i++) {
        memcpy(bad, f->pages[1], 8192);
        if (node_offsets[i] == 96 || node_offsets[i] == 152 || node_offsets[i] == 264) write_le(bad + node_offsets[i], 0, 8);
        else bad[node_offsets[i]] ^= 0x80;
        resign(bad); CHECK(candidate_catalog_decode_node(uuid, bad, 8192, &n) != CANDIDATE_WAL_OK);
    }
    l = location(1); r = root(4097); memset(encoded, 0xaa, 8192);
    CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID); CHECK(encoded[0] == 0xaa);
    r = root(4096); r.pages[1] = r.pages[0]; CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID);
    r = root(4096); r.retention_floor = 1; CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID);
    r = root(4096); r.retained_bytes = 67108865; CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID);
    n = (candidate_catalog_node_t){.count = 1}; n.entries[0] = outcome(1); n.entries[0].result_bytes = 1048577;
    CHECK(candidate_catalog_encode_node(&l, &n, encoded, 8192) == CANDIDATE_WAL_INVALID);
    r = root(4096); r.pages[0] = l.ref; CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID);
    r = root(4096); r.pages[1] = (candidate_page_ref_t){2,2}; CHECK(candidate_catalog_encode_root(&l, &r, encoded, 8192) == CANDIDATE_WAL_INVALID);
    n.entries[0] = outcome(2); n.entries[0].result_bytes = 1; n.entries[0].result_chunks = 1;
    n.entries[0].result = (candidate_version_ref_t){{10000,1},0,7};
    OK(candidate_digest_sha256((const uint8_t *)"x", 1, n.entries[0].result_digest));
    OK(candidate_catalog_encode_node(&l, &n, encoded, 8192));
    candidate_catalog_node_t decoded; OK(candidate_catalog_decode_node(uuid, encoded, 8192, &decoded));
    CHECK(decoded.entries[0].state == CANDIDATE_OUTCOME_ABORTED && decoded.entries[0].result_bytes == 1);
    candidate_outcome_t base = n.entries[0];
    for (unsigned change = 0; change < 12; change++) {
        n.entries[0] = base;
        if (change == 0) n.entries[0].result = (candidate_version_ref_t){0};
        if (change == 1) n.entries[0].result.slot_generation = 0;
        if (change == 2) n.entries[0].result.slot = 101;
        if (change == 3) n.entries[0].csn = 1;
        if (change == 4) n.entries[0].terminal_end_lsn = 2049;
        if (change == 5) n.entries[0].terminal_end_lsn = 0;
        if (change == 6) n.entries[0].declared_result_limit = 0;
        if (change == 7) n.entries[0].result_chunks = 2;
        if (change == 8) n.entries[0].result.page.generation = 0;
        if (change == 9) n.entries[0].admitting_owner_epoch = 0;
        if (change == 10) n.entries[0].transaction_id = 0;
        if (change == 11) n.entries[0].epoch = 0;
        CHECK(candidate_catalog_encode_node(&l, &n, encoded, 8192) == CANDIDATE_WAL_INVALID);
    }
}

static void reject_load(fixture_t *f, const candidate_catalog_load_t *r, memory_t *m, candidate_wal_status_t expected)
{
    f->calls = 0; candidate_catalog_t *c = (candidate_catalog_t *)1;
    CHECK(candidate_catalog_load(r, &c) == expected); CHECK(c == NULL && m->live == 0);
}

static void catalog(fixture_t *f)
{
    fill(f, 4096); memory_t m = {0}; candidate_wal_memory_t memory = {&m, allocate, release};
    candidate_catalog_load_t r = request(f, &memory); candidate_catalog_t *c = NULL; OK(candidate_catalog_load(&r, &c));
    CHECK(f->calls == 91 && m.calls == 1 && m.live == candidate_catalog_resident_bytes(c)); CHECK(m.peak < 1048576);
    CHECK(candidate_catalog_root(c)->count == 4096);
    candidate_catalog_query_t q = {.epoch = 2}; memcpy(q.uuid, uuid, 16); memset(q.operation_digest, 0x5c, 32);
    const candidate_outcome_t *e = NULL;
    for (unsigned i = 0; i < 4096; i++) {
        q.epoch = 2 + i / 1024; write_le(q.nonce, i % 1024, 8);
        CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_MATCH);
        CHECK(e->transaction_id == i + 1 && e->state == (candidate_outcome_state_t)(1 + i % 3));
    }
    q.operation_digest[0] ^= 1; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_CONTENT_MISMATCH && e == NULL);
    q.operation_digest[0] ^= 1; q.nonce[15] = 1; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_NOT_FOUND && e == NULL);
    q.epoch = 1; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_OUTSIDE_RETENTION);
    q.epoch = 6; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_INVALID_ID);
    q.epoch = 0; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_INVALID_ID);
    q.epoch = 2; q.uuid[0] ^= 1; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_INVALID_ID);
    CHECK(candidate_catalog_lookup(NULL, &q, &e) == CANDIDATE_CATALOG_UNAVAILABLE);
    CHECK(candidate_catalog_lookup(c, NULL, &e) == CANDIDATE_CATALOG_INVALID_ID);
    CHECK(candidate_catalog_lookup(c, &q, NULL) == CANDIDATE_CATALOG_INVALID_ID);
    printf("MEMORY catalog requested=%zu peak=%zu allocations=%u; excludes loader/stack/RSS\n", m.live, m.peak, m.calls);
    candidate_catalog_close(&c); CHECK(c == NULL && m.live == 0); candidate_catalog_close(&c); candidate_catalog_close(NULL);
    for (unsigned i = 1; i <= 91; i++) { f->fail_at = i; reject_load(f, &r, &m, CANDIDATE_WAL_IO); }
    f->fail_at = 0; m.fail = true; reject_load(f, &r, &m, CANDIDATE_WAL_NOMEM); CHECK(f->calls == 1); m.fail = false;
    candidate_catalog_load_t invalid = r; invalid.root.generation = 0; reject_load(f, &invalid, &m, CANDIDATE_WAL_INVALID); CHECK(f->calls == 0);
    invalid = r; invalid.uuid[0] ^= 1; reject_load(f, &invalid, &m, CANDIDATE_WAL_UNSUPPORTED);
    invalid = r; invalid.root.generation = 2; reject_load(f, &invalid, &m, CANDIDATE_WAL_CORRUPT);
    uint8_t saved[8192]; memcpy(saved, f->pages[1], 8192);
    for (unsigned change = 0; change < 13; change++) {
        candidate_catalog_node_t n; OK(candidate_catalog_decode_node(uuid, saved, 8192, &n));
        if (change == 0) { memcpy(n.entries[1].nonce, n.entries[0].nonce, 16); n.entries[1].operation_digest[0] ^= 1; }
        if (change == 1) n.entries[4].csn = n.entries[1].csn;
        if (change == 2) n.entries[1].transaction_id = n.entries[0].transaction_id;
        if (change == 3) n.entries[1].admitting_owner_epoch = 8;
        if (change == 4) n.entries[1].csn = 4097;
        if (change == 5) n.entries[1].result.page.id = 10001;
        if (change == 6) n.entries[1].epoch = 1;
        if (change == 7) n.entries[1].epoch = 6;
        if (change == 8) n.ordinal = 1;
        if (change == 9) n.count = 45;
        if (change == 12) { n.entries[1].result_bytes = 1; n.entries[1].result_chunks = 1; }
        candidate_page_location_t l = location(2);
        if (change == 10) l.ref.generation = 2;
        if (change == 11) l.lsn = 1032;
        OK(candidate_catalog_encode_node(&l, &n, f->pages[1], 8192)); reject_load(f, &r, &m, CANDIDATE_WAL_CORRUPT);
    }
    memcpy(f->pages[1], saved, 8192);
    /* 제어/본문이 없는 표도 raw MATCH만 반환한다. 내구성이나 거래 적용의 증거로 사용하지 않는다. */
    fill(f, 1025);
    for (unsigned i = 0; i < 23; i++) {
        candidate_catalog_node_t n; OK(candidate_catalog_decode_node(uuid, f->pages[i + 1], 8192, &n));
        for (unsigned j = 0; j < n.count; j++) { n.entries[j].epoch = 2; write_le(n.entries[j].nonce, i * 46 + j, 8); }
        candidate_page_location_t l = location(i + 2); OK(candidate_catalog_encode_node(&l, &n, f->pages[i + 1], 8192));
    }
    reject_load(f, &r, &m, CANDIDATE_WAL_CORRUPT);
    fill(f, 0); OK(candidate_catalog_load(&r, &c)); CHECK(f->calls == 1 && candidate_catalog_root(c)->count == 0);
    q = (candidate_catalog_query_t){.epoch = 2}; memcpy(q.uuid, uuid, 16); CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_NOT_FOUND);
    candidate_catalog_close(&c); CHECK(m.live == 0);
}

static void persisted(fixture_t *f)
{
    fill(f, 4096); char path[PATH_MAX]; CHECK(snprintf(path, sizeof(path), "build/candidate-catalog-%ld.pages", (long)getpid()) < (int)sizeof(path));
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600); CHECK(fd >= 0);
    for (unsigned i = 0; i < 91; i++) CHECK(write(fd, f->pages[i], 8192) == 8192);
    CHECK(fsync(fd) == 0 && close(fd) == 0); f->fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); CHECK(f->fd >= 0);
    candidate_catalog_load_t r = request(f, NULL); candidate_catalog_t *c = NULL; OK(candidate_catalog_load(&r, &c));
    candidate_catalog_query_t q = {.epoch = 2}; memcpy(q.uuid, uuid, 16); memset(q.operation_digest, 0x5c, 32);
    const candidate_outcome_t *e = NULL; CHECK(candidate_catalog_lookup(c, &q, &e) == CANDIDATE_CATALOG_MATCH && e->state == CANDIDATE_OUTCOME_ADMITTED);
    candidate_catalog_close(&c); CHECK(close(f->fd) == 0); f->fd = -1;
    printf("FIXTURE catalog %s pages=91 outcomes=4096\n", path);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90); fixture_t *f = calloc(1, sizeof(*f)); CHECK(f != NULL);
    codec(f); catalog(f); persisted(f); free(f);
    CHECK(candidate_catalog_root(NULL) == NULL && candidate_catalog_resident_bytes(NULL) == 0);
    printf("PASS candidate catalog checks=%u; raw records only, no APPLIED/recovery activation\n", checks); return 0;
}
