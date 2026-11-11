#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_allocation.h"
#include "storage/checksum.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
static unsigned checks;
static const uint8_t uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
typedef struct {
    uint8_t pages[CANDIDATE_ALLOCATION_NODES + 1][8192];
    unsigned calls, fail_at, count;
    int fd;
    candidate_allocation_load_t *mutate;
} fixture_t;
typedef struct { size_t live, peak; bool fail; candidate_allocation_load_t *mutate; } memory_t;

static void write_le(uint8_t *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void resign(uint8_t *p)
{
    memset(p + 52, 0, 4); write_le(p + 52, crc32c(0, p, 8192), 4);
}

static candidate_page_location_t location(unsigned id, uint64_t lsn)
{
    candidate_page_location_t l = {.ref = {id,1}, .lsn = lsn}; memcpy(l.uuid, uuid, 16); return l;
}

static fixture_t *fixture(unsigned count, bool sparse, uint64_t lsn)
{
    fixture_t *f = calloc(1, sizeof(*f)); CHECK(f != NULL); f->fd = -1;
    f->count = 1 + (count + 201) / 202;
    candidate_allocation_root_t r = {.owner_epoch = 3, .page_highwater = sparse ? 1000 : count,
        .gc_horizon = 12, .count = count};
    for (unsigned i = 1; i < f->count; i++) r.nodes[i - 1] = (candidate_page_ref_t){i + 1, 1};
    candidate_page_location_t l = location(1, lsn);
    OK(candidate_allocation_encode_root(&l, &r, f->pages[0], 8192));
    const unsigned ids[5] = {1,2,10,20,30};
    for (unsigned i = 1, at = 0; i < f->count; i++) {
        unsigned n = count - at; if (n > 202) n = 202;
        candidate_allocation_node_t node = {.ordinal = i - 1, .count = n};
        for (unsigned j = 0; j < n; j++, at++) {
            uint64_t id = sparse ? ids[at] : at + 1;
            bool released = sparse ? id >= 20 : (id > f->count && id % 2 == 0);
            node.entries[j] = (candidate_allocation_entry_t){.ref = {id, sparse && id == 30 ? UINT64_MAX : 1},
                .state = released ? CANDIDATE_ALLOCATION_FREE : CANDIDATE_ALLOCATION_LIVE,
                .release_horizon = released ? 4 : 0, .transition_lsn = lsn == 0 ? 0 : 512};
        }
        l = location(i + 1, lsn); OK(candidate_allocation_encode_node(&l, &node, f->pages[i], 8192));
    }
    return f;
}

static candidate_wal_status_t load(void *context, candidate_page_ref_t ref, uint8_t *out)
{
    fixture_t *f = context; f->calls++;
    if (f->mutate != NULL) { memset(f->mutate, 0, sizeof(*f->mutate)); f->mutate = NULL; }
    if (f->calls == f->fail_at) return CANDIDATE_WAL_IO;
    if (ref.id == 0 || ref.id > f->count || ref.generation != 1) return CANDIDATE_WAL_CORRUPT;
    if (f->fd >= 0) return pread(f->fd, out, 8192, (off_t)(ref.id - 1) * 8192) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
    memcpy(out, f->pages[ref.id - 1], 8192); return CANDIDATE_WAL_OK;
}

static candidate_allocation_load_t request(fixture_t *f, const candidate_wal_memory_t *m)
{
    candidate_allocation_load_t r = {.root = {1,1}, .load = load, .context = f, .memory = m};
    memcpy(r.uuid, uuid, 16); return r;
}

static void *allocate(void *context, size_t size)
{
    memory_t *m = context;
    if (m->mutate != NULL) { memset(m->mutate, 0, sizeof(*m->mutate)); m->mutate = NULL; }
    if (m->fail) return NULL;
    void *p = malloc(size); if (p != NULL) { m->live += size; if (m->peak < m->live) m->peak = m->live; } return p;
}

static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live >= size); m->live -= size; free(p);
}

static void codec(void)
{
    fixture_t *f = fixture(5, true, 1024);
    for (unsigned kind = 0; kind < 2; kind++) {
        uint8_t *p = f->pages[kind], changed[8192];
        candidate_allocation_root_t r, before_r; memset(&r, 0xa5, sizeof(r)); before_r = r;
        candidate_allocation_node_t n, before_n; memset(&n, 0xa5, sizeof(n)); before_n = n;
        for (size_t size = 0; size < 8192; size++) {
            candidate_wal_status_t s = kind == 0 ? candidate_allocation_decode_root(uuid, p, size, &r) :
                candidate_allocation_decode_node(uuid, p, size, &n);
            CHECK(s != CANDIDATE_WAL_OK);
        }
        CHECK(memcmp(&r, &before_r, sizeof(r)) == 0 && memcmp(&n, &before_n, sizeof(n)) == 0);
        for (unsigned i = 0; i < 8192; i++) {
            memcpy(changed, p, 8192); changed[i] ^= 1;
            CHECK((kind == 0 ? candidate_allocation_decode_root(uuid, changed, 8192, &r) :
                candidate_allocation_decode_node(uuid, changed, 8192, &n)) != CANDIDATE_WAL_OK);
        }
        CHECK(memcmp(&r, &before_r, sizeof(r)) == 0 && memcmp(&n, &before_n, sizeof(n)) == 0);
    }
    const unsigned root_bad[] = {68,70,72,96,100,104,112};
    for (unsigned i = 0; i < sizeof(root_bad) / sizeof(root_bad[0]); i++) {
        uint8_t p[8192]; memcpy(p, f->pages[0], 8192); memset(p + root_bad[i], 0, 8); resign(p);
        candidate_allocation_root_t r; CHECK(candidate_allocation_decode_root(uuid, p, 8192, &r) != CANDIDATE_WAL_OK);
    }
    const unsigned node_bad[] = {68,70,76,96,104,112};
    for (unsigned i = 0; i < sizeof(node_bad) / sizeof(node_bad[0]); i++) {
        uint8_t p[8192]; memcpy(p, f->pages[1], 8192); memset(p + node_bad[i], 0, 8); resign(p);
        candidate_allocation_node_t n; CHECK(candidate_allocation_decode_node(uuid, p, 8192, &n) != CANDIDATE_WAL_OK);
    }
    candidate_allocation_root_t r; OK(candidate_allocation_decode_root(uuid, f->pages[0], 8192, &r));
    uint8_t out[8192], before[8192]; memset(out, 0xa5, sizeof(out)); memcpy(before, out, sizeof(out));
    candidate_page_location_t l = location(1,1024); r.count = CANDIDATE_ALLOCATION_ENTRIES + 1;
    CHECK(candidate_allocation_encode_root(&l, &r, out, 8192) == CANDIDATE_WAL_LIMIT);
    CHECK(memcmp(out, before, 8192) == 0);
    uint8_t p[8192]; memcpy(p,f->pages[0],8192); write_le(p+96,CANDIDATE_ALLOCATION_ENTRIES+1,4); resign(p);
    CHECK(candidate_allocation_decode_root(uuid,p,8192,&r) == CANDIDATE_WAL_LIMIT);
    memcpy(p,f->pages[0],8192); p[64] ^= 1; resign(p);
    CHECK(candidate_allocation_decode_root(uuid,p,8192,&r) == CANDIDATE_WAL_UNSUPPORTED);
    candidate_allocation_node_t n; OK(candidate_allocation_decode_node(uuid,f->pages[1],8192,&n));
    n.count = CANDIDATE_ALLOCATION_NODE_ENTRIES+1;
    CHECK(candidate_allocation_encode_node(&l,&n,out,8192) == CANDIDATE_WAL_LIMIT);
    CHECK(memcmp(out,before,8192) == 0);
    free(f);
}

static uint32_t state_crc(candidate_allocation_t *a)
{
    uint32_t crc = 0;
    for (uint32_t i = 0; i < candidate_allocation_info(a).count; i++) {
        candidate_allocation_entry_t e = {0}; CHECK(candidate_allocation_at(a, i, &e) == CANDIDATE_ALLOCATION_MATCH);
        uint64_t words[] = {e.ref.id,e.ref.generation,e.state,e.release_horizon,e.transition_lsn};
        crc = crc32c(crc, words, sizeof(words));
    }
    return crc;
}

static void unchanged(candidate_allocation_t *a, candidate_allocation_batch_t *b, candidate_wal_status_t expected)
{
    uint32_t crc = state_crc(a); candidate_allocation_info_t before = candidate_allocation_info(a);
    CHECK(candidate_allocation_apply(a, b) == expected);
    candidate_allocation_info_t after = candidate_allocation_info(a);
    CHECK(before.count == after.count && before.lsn == after.lsn && before.page_highwater == after.page_highwater && before.gc_horizon == after.gc_horizon);
    CHECK(state_crc(a) == crc);
}

static void save_small(fixture_t *f, uint64_t lsn)
{
    char path[256]; CHECK(snprintf(path,sizeof(path),"build/candidate-allocation-%ld-small-%llu.pages",(long)getpid(),(unsigned long long)lsn) > 0);
    int fd = open(path,O_CREAT|O_EXCL|O_RDWR,0600); CHECK(fd >= 0);
    for (unsigned i = 0; i < f->count; i++) CHECK(write(fd,f->pages[i],8192) == 8192);
    CHECK(fsync(fd) == 0 && close(fd) == 0); f->fd = open(path,O_RDONLY); CHECK(f->fd >= 0);
    candidate_allocation_load_t r = request(f,NULL); candidate_allocation_t *a = NULL;
    OK(candidate_allocation_load(&r,&a)); CHECK(candidate_allocation_info(a).lsn == lsn);
    candidate_allocation_close(&a); CHECK(close(f->fd) == 0); f->fd = -1;
    printf("small_snapshot=%s lsn=%llu\n",path,(unsigned long long)lsn);
}

static void transitions(uint64_t initial_lsn)
{
    fixture_t *f = fixture(5, true, initial_lsn); candidate_allocation_load_t r = request(f, NULL); candidate_allocation_t *a = NULL;
    OK(candidate_allocation_load(&r, &a));
    candidate_allocation_change_t c[3] = {{10,1,CANDIDATE_ALLOCATION_LIVE,CANDIDATE_ALLOCATION_FREE,8},
        {20,1,CANDIDATE_ALLOCATION_FREE,CANDIDATE_ALLOCATION_LIVE,0}, {25,0,0,CANDIDATE_ALLOCATION_LIVE,0}};
    candidate_allocation_batch_t b = {.lsn = initial_lsn == 0 ? 0 : 1032, .page_highwater = 1000, .gc_horizon = 12, .count = 3, .changes = c};
    OK(candidate_allocation_apply(a, &b));
    candidate_allocation_entry_t e;
    CHECK(candidate_allocation_find(a,10,&e) == CANDIDATE_ALLOCATION_MATCH && e.state == CANDIDATE_ALLOCATION_FREE && e.ref.generation == 1 && e.release_horizon == 8);
    CHECK(candidate_allocation_find(a,20,&e) == CANDIDATE_ALLOCATION_MATCH && e.state == CANDIDATE_ALLOCATION_LIVE && e.ref.generation == 2);
    CHECK(candidate_allocation_find(a,25,&e) == CANDIDATE_ALLOCATION_MATCH && e.ref.generation == 1);
    CHECK(candidate_allocation_find(a,999,&e) == CANDIDATE_ALLOCATION_NOT_FOUND);
    candidate_allocation_entry_t before = e;
    CHECK(candidate_allocation_at(a, candidate_allocation_info(a).count, &e) == CANDIDATE_ALLOCATION_NOT_FOUND);
    CHECK(memcmp(&before,&e,sizeof(e)) == 0);
    if (initial_lsn != 0) b.lsn += 8;
    unchanged(a, &b, CANDIDATE_WAL_STALE);
    c[0] = (candidate_allocation_change_t){10,1,CANDIDATE_ALLOCATION_FREE,CANDIDATE_ALLOCATION_LIVE,0};
    c[1] = (candidate_allocation_change_t){30,UINT64_MAX,CANDIDATE_ALLOCATION_FREE,CANDIDATE_ALLOCATION_LIVE,0}; b.count = 2;
    unchanged(a, &b, CANDIDATE_WAL_LIMIT);
    b.count = 1; c[0].page_id = 1; c[0].previous_state = CANDIDATE_ALLOCATION_LIVE; c[0].next_state = CANDIDATE_ALLOCATION_FREE; c[0].release_horizon = 8;
    unchanged(a, &b, CANDIDATE_WAL_STALE); c[0].page_id = 2; unchanged(a, &b, CANDIDATE_WAL_STALE);
    c[0].page_id = 20; c[0].previous_generation = 2; c[0].release_horizon = 13; unchanged(a, &b, CANDIDATE_WAL_INVALID);
    c[0].release_horizon = 8; b.page_highwater = 999; unchanged(a, &b, CANDIDATE_WAL_STALE); b.page_highwater = 1000;
    b.gc_horizon = 11; unchanged(a, &b, CANDIDATE_WAL_STALE); b.gc_horizon = 12;
    c[1] = c[0]; b.count = 2; unchanged(a, &b, CANDIDATE_WAL_INVALID); b.count = 1;
    OK(candidate_allocation_apply(a, &b));
    candidate_allocation_close(&a); CHECK(a == NULL); candidate_allocation_close(&a); save_small(f,initial_lsn); free(f);
}

static void ownership_and_invalid(void)
{
    fixture_t *f = fixture(5,true,1024); memory_t m = {0}; candidate_wal_memory_t memory = {&m,allocate,release};
    candidate_allocation_load_t r = request(f,&memory); candidate_allocation_t *a = NULL;
    for (unsigned i = 1; i <= 2; i++) {
        f->calls = 0; f->fail_at = i; CHECK(candidate_allocation_load(&r,&a) == CANDIDATE_WAL_IO && a == NULL && m.live == 0);
    }
    f->fail_at = 0; m.fail = true; CHECK(candidate_allocation_load(&r,&a) == CANDIDATE_WAL_NOMEM && a == NULL); m.fail = false;
    m.mutate = &r; OK(candidate_allocation_load(&r,&a)); CHECK(m.live == candidate_allocation_info(a).resident_bytes);
    size_t resident = m.live; candidate_allocation_close(&a); CHECK(m.live == 0);
    r = request(f,&memory); f->mutate = &r; OK(candidate_allocation_load(&r,&a)); candidate_allocation_close(&a);
    CHECK(m.live == 0); r = request(f,&memory);
    for (unsigned mode = 0; mode < 7; mode++) {
        uint8_t saved[8192]; memcpy(saved, f->pages[1],8192);
        if (mode == 0) write_le(f->pages[1]+96,3,8);
        if (mode == 1) write_le(f->pages[1]+112,CANDIDATE_ALLOCATION_FREE,4);
        if (mode == 1) write_le(f->pages[1]+120,4,8);
        if (mode == 2) write_le(f->pages[1]+104,2,8);
        if (mode == 3) write_le(f->pages[1]+40,1032,8);
        if (mode == 4) write_le(f->pages[1]+128,1032,8);
        if (mode == 5) write_le(f->pages[1]+96+3*40+24,13,8);
        if (mode == 6) write_le(f->pages[1]+72,1,4);
        resign(f->pages[1]); CHECK(candidate_allocation_load(&r,&a) == CANDIDATE_WAL_CORRUPT && a == NULL && m.live == 0);
        memcpy(f->pages[1],saved,8192);
    }
    memset(r.uuid,0,16); f->calls = 0; CHECK(candidate_allocation_load(&r,&a) == CANDIDATE_WAL_INVALID && f->calls == 0);
    printf("allocation resident=%zu; stack/loader/allocator overhead excluded\n",resident); free(f);
}

static void full_table(void)
{
    fixture_t *f = fixture(CANDIDATE_ALLOCATION_ENTRIES,false,1024);
    candidate_allocation_load_t r = request(f,NULL); candidate_allocation_t *a = NULL;
    char path[256]; CHECK(snprintf(path,sizeof(path),"build/candidate-allocation-%ld-max.pages",(long)getpid()) > 0);
    int fd = open(path,O_CREAT|O_EXCL|O_RDWR,0600); CHECK(fd >= 0);
    for (unsigned i = 0; i < f->count; i++) CHECK(write(fd,f->pages[i],8192) == 8192);
    CHECK(fsync(fd) == 0 && close(fd) == 0); f->fd = open(path,O_RDONLY); CHECK(f->fd >= 0);
    OK(candidate_allocation_load(&r,&a));
    for (unsigned i = 1; i <= CANDIDATE_ALLOCATION_ENTRIES; i++) {
        candidate_allocation_entry_t e; CHECK(candidate_allocation_find(a,i,&e) == CANDIDATE_ALLOCATION_MATCH && e.ref.id == i);
    }
    candidate_allocation_change_t c = {70000,0,0,CANDIDATE_ALLOCATION_LIVE,0};
    candidate_allocation_batch_t b = {1032,70000,12,1,&c};
    unchanged(a,&b,CANDIDATE_WAL_LIMIT); candidate_allocation_close(&a);
    const unsigned failures[] = {1,2,3,163,325,326};
    for (unsigned i = 0; i < sizeof(failures)/sizeof(failures[0]); i++) {
        f->calls = 0; f->fail_at = failures[i]; CHECK(candidate_allocation_load(&r,&a) == CANDIDATE_WAL_IO && a == NULL);
    }
    CHECK(close(f->fd) == 0); printf("snapshot=%s pages=%u entries=%u\n",path,f->count,CANDIDATE_ALLOCATION_ENTRIES); free(f);
}

static void model(void)
{
    fixture_t *f = fixture(64,false,1024); candidate_allocation_load_t r = request(f,NULL); candidate_allocation_t *a = NULL;
    OK(candidate_allocation_load(&r,&a));
    candidate_allocation_entry_t expected[64];
    for (unsigned i = 0; i < 64; i++) CHECK(candidate_allocation_find(a,i+1,&expected[i]) == CANDIDATE_ALLOCATION_MATCH);
    unsigned random = 127; uint64_t lsn = 1024;
    for (unsigned i = 0; i < 2000; i++) {
        random = random * 1664525u + 1013904223u; unsigned id = 3 + random % 62;
        candidate_allocation_entry_t *e = &expected[id-1];
        candidate_allocation_state_t next = e->state == CANDIDATE_ALLOCATION_LIVE ? CANDIDATE_ALLOCATION_FREE : CANDIDATE_ALLOCATION_LIVE;
        candidate_allocation_change_t c = {id,e->ref.generation,e->state,next,next == CANDIDATE_ALLOCATION_FREE ? 8 : 0};
        candidate_allocation_batch_t b = {lsn+=8,64,12,1,&c}; OK(candidate_allocation_apply(a,&b));
        if (next == CANDIDATE_ALLOCATION_LIVE) e->ref.generation++;
        e->state = next; e->release_horizon = c.release_horizon; e->transition_lsn = lsn;
        candidate_allocation_entry_t actual; CHECK(candidate_allocation_find(a,id,&actual) == CANDIDATE_ALLOCATION_MATCH);
        CHECK(actual.ref.generation == e->ref.generation && actual.state == e->state && actual.release_horizon == e->release_horizon && actual.transition_lsn == lsn);
    }
    candidate_allocation_change_t changes[64] = {0};
    for (unsigned i = 0; i < 64; i++) changes[i] = (candidate_allocation_change_t){65+i,0,0,CANDIDATE_ALLOCATION_LIVE,0};
    candidate_allocation_batch_t batch = {lsn+8,128,12,64,changes};
    OK(candidate_allocation_apply(a,&batch)); CHECK(candidate_allocation_info(a).count == 128);
    for (unsigned i = 65; i <= 128; i++) {
        candidate_allocation_entry_t e;
        CHECK(candidate_allocation_find(a,i,&e) == CANDIDATE_ALLOCATION_MATCH && e.ref.generation == 1 && e.state == CANDIDATE_ALLOCATION_LIVE);
    }
    batch.count = 65; unchanged(a,&batch,CANDIDATE_WAL_LIMIT);
    candidate_allocation_close(&a); free(f);
}

static void numeric_limits(void)
{
    fixture_t *f = fixture(5,true,1024);
    write_le(f->pages[0]+80,UINT64_MAX,8); write_le(f->pages[0]+88,UINT64_MAX,8);
    write_le(f->pages[1]+96+4*40,UINT64_MAX,8); resign(f->pages[0]); resign(f->pages[1]);
    candidate_allocation_load_t r = request(f,NULL); candidate_allocation_t *a = NULL;
    OK(candidate_allocation_load(&r,&a));
    candidate_allocation_entry_t e; CHECK(candidate_allocation_find(a,UINT64_MAX,&e) == CANDIDATE_ALLOCATION_MATCH && e.ref.generation == UINT64_MAX);
    candidate_allocation_change_t c = {UINT64_MAX,UINT64_MAX,CANDIDATE_ALLOCATION_FREE,CANDIDATE_ALLOCATION_LIVE,0};
    candidate_allocation_batch_t b = {UINT64_MAX-7,UINT64_MAX,UINT64_MAX,1,&c};
    unchanged(a,&b,CANDIDATE_WAL_LIMIT);
    b.count = 0; OK(candidate_allocation_apply(a,&b)); b.lsn = 0; unchanged(a,&b,CANDIDATE_WAL_STALE);
    b.lsn = UINT64_MAX; unchanged(a,&b,CANDIDATE_WAL_STALE);
    candidate_allocation_close(&a); free(f);
}

int main(void)
{
    alarm(90); codec(); transitions(1024); transitions(0); ownership_and_invalid(); full_table(); model(); numeric_limits();
    printf("PASS candidate allocation checks=%u; raw history only, no durable group/reclaim/recovery activation\n",checks); return 0;
}
