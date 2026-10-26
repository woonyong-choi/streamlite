#include "test_candidate_semantic_fixture.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"

#define GUARD_SEGMENTS 4u

typedef struct { dev_t device; ino_t inode; bool is_bound; } guarded_segment_t;

unsigned semantic_checks;
const char *fixture_base;
const uint8_t fixture_uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
fixture_guard_t fixture_guard;
static guarded_segment_t guarded_segments[GUARD_SEGMENTS];
static uint8_t images[4][8192];
static const uint8_t empty_digest[32] = {
    0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
    0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};

static ssize_t guarded_pread(int fd, void *out, size_t size, off_t offset);
const candidate_wal_io_t fixture_guarded_io = {guarded_pread, pwrite, fsync, ftruncate};

static void put(uint8_t *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t get(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static uint64_t frame_bytes(size_t payload) { return (96 + payload + 7) & ~(uint64_t)7; }

static candidate_wal_options_t wal_options(uint64_t owner_epoch, const candidate_wal_io_t *io)
{
    candidate_wal_options_t o = {.owner_epoch = owner_epoch, .io = io};
    memcpy(o.uuid, fixture_uuid, 16);
    return o;
}

void fixture_create(fixture_t *f, const char *name, bool has_system)
{
    static unsigned serial;
    memset(f, 0, sizeof(*f));
    int n = snprintf(f->path, sizeof(f->path), "%s-%s-%u", fixture_base, name, ++serial);
    CHECK(n > 0 && n < (int)sizeof(f->path) - 22);
    f->has_system = has_system;
    f->owner_epoch = 7;
    f->end_lsn = 64;
    candidate_wal_options_t o = wal_options(7, NULL);
    OK(candidate_wal_create(f->path, &o, &f->wal));
}

void fixture_open(fixture_t *f, uint64_t owner_epoch)
{
    candidate_wal_options_t o = wal_options(owner_epoch, NULL);
    f->owner_epoch = owner_epoch;
    OK(candidate_wal_open(f->path, &o, &f->wal));
    CHECK(candidate_wal_info(f->wal).valid_end_lsn == f->end_lsn);
}

void fixture_close(fixture_t *f)
{
    candidate_wal_info_t info = candidate_wal_info(f->wal);
    if (info.valid_end_lsn > info.durable_end_lsn) OK(candidate_wal_force(f->wal, info.valid_end_lsn));
    OK(candidate_wal_close(&f->wal));
}

candidate_wal_t *fixture_reader(const fixture_t *f, uint64_t owner_epoch)
{
    fixture_guard_disarm();
    candidate_wal_options_t o = wal_options(owner_epoch, &fixture_guarded_io);
    candidate_wal_t *wal = NULL;
    OK(candidate_wal_open(f->path, &o, &wal));
    return wal;
}

void fixture_mark(fixture_t *f)
{
    f->read_bytes = 0;
    f->read_records = 0;
}

uint64_t fixture_append(fixture_t *f, fixture_chain_t *chain, unsigned type, const uint8_t *body, size_t bytes,
    uint32_t *group_crc)
{
    candidate_wal_record_t r = {.type = (uint16_t)type, .lsn = candidate_wal_info(f->wal).valid_end_lsn,
        .transaction_id = chain->id, .previous_lsn = chain->last, .owner_epoch = f->owner_epoch,
        .payload = body, .payload_bytes = bytes};
    uint64_t end = 0;
    OK(candidate_wal_append(f->wal, &r, &end));
    CHECK(end == r.lsn + frame_bytes(bytes));
    if (chain->count == 0) chain->first = r.lsn;
    chain->last = r.lsn;
    chain->count++;
    chain->crc = candidate_wal_witness_crc(chain->crc, &r);
    if (group_crc != NULL) *group_crc = candidate_wal_witness_crc(*group_crc, &r);
    f->records++;
    f->end_lsn = end;
    f->highest_owner = f->owner_epoch;
    f->read_records++;
    f->read_bytes += 80 + frame_bytes(bytes);
    return end;
}

fixture_transaction_t *fixture_transaction(fixture_t *f, uint64_t id)
{
    for (unsigned i = 0; i < f->transaction_count; i++)
        if (f->transactions[i].chain.id == id) return &f->transactions[i];
    CHECK(f->transaction_count < FIXTURE_TRANSACTIONS);
    fixture_transaction_t *t = &f->transactions[f->transaction_count++];
    memset(t, 0, sizeof(*t));
    t->chain.id = id;
    t->state = CANDIDATE_WITNESS_UNFINISHED;
    return t;
}

static void start_chain(const fixture_t *f, fixture_transaction_t *t)
{
    if (t->chain.count == 0) t->owner_epoch = f->owner_epoch;
}

void fixture_admit(fixture_t *f, fixture_transaction_t *t, uint8_t tag, uint64_t limit)
{
    uint8_t body[64] = {0};
    start_chain(f, t);
    t->epoch = 1;
    memset(t->nonce, 0, 16);
    t->nonce[0] = tag;
    t->nonce[1] = 0x5e;
    memset(t->operation, tag ^ 0xa0, 32);
    memcpy(t->result_digest, empty_digest, 32);
    t->limit = limit;
    t->has_admission = true;
    put(body, t->epoch, 8);
    memcpy(body + 8, t->nonce, 16);
    memcpy(body + 24, t->operation, 32);
    put(body + 56, limit, 8);
    fixture_append(f, &t->chain, 1, body, sizeof(body), NULL);
    CHECK(f->admission_count < FIXTURE_TRANSACTIONS);
    f->admitted[f->admission_count++] = (unsigned)(t - f->transactions);
}

void fixture_begin(fixture_t *f, fixture_transaction_t *t)
{
    uint8_t body[16] = {0};
    start_chain(f, t);
    fixture_append(f, &t->chain, 2, body, sizeof(body), NULL);
    t->has_begin = true;
}

uint64_t fixture_group_begin(fixture_t *f, fixture_transaction_t *t, uint64_t id, uint32_t *crc)
{
    uint8_t begin[16] = {0};
    put(begin, id, 8);
    put(begin + 8, 1, 4);
    put(begin + 12, 1, 4);
    uint64_t lsn = candidate_wal_info(f->wal).valid_end_lsn;
    fixture_append(f, &t->chain, 3, begin, sizeof(begin), crc);
    return lsn;
}

void fixture_group_finish(fixture_t *f, fixture_transaction_t *t, uint64_t id, uint32_t crc)
{
    uint8_t delta[49] = {0};
    put(delta, id, 8);
    put(delta + 8, 100 + t->chain.id, 8);
    put(delta + 16, 1, 8);
    put(delta + 32, 1, 4);
    put(delta + 40, 64, 4);
    put(delta + 44, 1, 4);
    delta[48] = (uint8_t)id;
    fixture_append(f, &t->chain, 5, delta, sizeof(delta), &crc);
    uint8_t end[24] = {0};
    put(end, id, 8);
    put(end + 8, 1, 4);
    put(end + 12, 1, 4);
    put(end + 16, crc, 4);
    uint64_t group_end = fixture_append(f, &t->chain, 8, end, sizeof(end), NULL);
    CHECK(t->groups < 4 && f->group_count < FIXTURE_GROUPS);
    t->group_ids[t->groups] = id;
    t->group_ends[t->groups++] = group_end;
    f->groups[f->group_count++] = (candidate_semantic_group_t){id, t->chain.id, group_end};
}

void fixture_delta_group(fixture_t *f, fixture_transaction_t *t, uint64_t id)
{
    uint32_t crc = 0;
    fixture_group_begin(f, t, id, &crc);
    fixture_group_finish(f, t, id, crc);
}

void fixture_result(fixture_transaction_t *t, uint64_t total, uint8_t tag, candidate_version_ref_t ref)
{
    t->total = total;
    memset(t->chunk_digest, tag, 32);
    t->ref = ref;
}

void fixture_chunk_body(const fixture_transaction_t *t, uint8_t out[112])
{
    uint64_t left = t->total - t->chunk_bytes;
    memset(out, 0, 112);
    put(out, t->epoch, 8);
    memcpy(out + 8, t->nonce, 16);
    put(out + 24, t->chunks, 4);
    put(out + 28, (t->total + 65535) / 65536, 4);
    put(out + 32, t->total, 8);
    memcpy(out + 40, t->chunk_digest, 32);
    put(out + 72, t->ref.page.id, 8);
    put(out + 80, t->ref.page.generation, 8);
    put(out + 88, t->ref.slot, 4);
    put(out + 92, t->ref.slot_generation, 4);
    put(out + 96, t->chunk_bytes, 8);
    put(out + 104, left < 65536 ? left : 65536, 4);
}

void fixture_append_chunk(fixture_t *f, fixture_transaction_t *t, const uint8_t body[112])
{
    fixture_append(f, &t->chain, 9, body, 112, NULL);
    t->chunks++;
    t->chunk_bytes += get(body + 104, 4);
    t->result_bytes = get(body + 32, 8);
    memcpy(t->result_digest, body + 40, 32);
}

void fixture_chunk(fixture_t *f, fixture_transaction_t *t)
{
    uint8_t body[112];
    fixture_chunk_body(t, body);
    fixture_append_chunk(f, t, body);
}

size_t fixture_commit_body(const fixture_transaction_t *t, uint64_t csn, uint8_t *out)
{
    size_t bytes = 152 + (size_t)t->groups * 16;
    memset(out, 0, bytes);
    put(out, t->chain.first, 8);
    put(out + 8, t->chain.last, 8);
    put(out + 16, t->chain.count, 8);
    put(out + 24, t->chain.crc, 4);
    put(out + 32, csn, 8);
    if (t->has_admission) {
        put(out + 40, t->epoch, 8);
        memcpy(out + 48, t->nonce, 16);
        memcpy(out + 64, t->operation, 32);
        memcpy(out + 96, t->result_digest, 32);
        put(out + 128, t->result_bytes, 8);
        put(out + 136, t->chunks, 4);
    }
    put(out + 148, t->groups, 4);
    for (unsigned i = 0; i < t->groups; i++) {
        put(out + 152 + i * 16, t->group_ids[i], 8);
        put(out + 160 + i * 16, t->group_ends[i], 8);
    }
    return bytes;
}

void fixture_append_commit(fixture_t *f, fixture_transaction_t *t, uint64_t csn, const uint8_t *body, size_t bytes)
{
    t->terminal_end = fixture_append(f, &t->chain, 10, body, bytes, NULL);
    t->state = CANDIDATE_WITNESS_COMMIT;
    t->csn = csn;
}

void fixture_commit(fixture_t *f, fixture_transaction_t *t, uint64_t csn)
{
    uint8_t body[152 + 4 * 16];
    size_t bytes = fixture_commit_body(t, csn, body);
    fixture_append_commit(f, t, csn, body, bytes);
}

void fixture_abort(fixture_t *f, fixture_transaction_t *t)
{
    uint8_t body[32] = {0};
    put(body, t->chain.first, 8);
    put(body + 8, t->chain.last, 8);
    put(body + 16, t->chain.count, 8);
    put(body + 24, t->chain.crc, 4);
    t->terminal_end = fixture_append(f, &t->chain, 11, body, sizeof(body), NULL);
    t->state = CANDIDATE_WITNESS_ABORT;
}

static fixture_system_t next_system(fixture_system_t s, unsigned kind)
{
    if (kind == 0) return (fixture_system_t){100, 100, 1, 1};
    if (kind == 15) { s.epoch++; s.floor++; }
    if (kind == 16) { s.transaction_highwater += 100; s.page_highwater += 100; }
    return s;
}

static candidate_page_location_t location(uint64_t id, uint64_t generation, uint64_t lsn)
{
    candidate_page_location_t l = {.ref = {id, generation}, .lsn = lsn};
    memcpy(l.uuid, fixture_uuid, 16);
    return l;
}

static void encode_node(unsigned kind, uint64_t generation, uint64_t end, uint8_t *out)
{
    candidate_allocation_node_t node = {.count = 4};
    for (unsigned i = 0; i < 3; i++)
        node.entries[i] = (candidate_allocation_entry_t){.ref = {i + 1, 1}, .state = CANDIDATE_ALLOCATION_LIVE};
    bool is_free = kind == 7;
    node.entries[3] = (candidate_allocation_entry_t){.ref = {10, generation},
        .state = is_free ? CANDIDATE_ALLOCATION_FREE : CANDIDATE_ALLOCATION_LIVE,
        .release_horizon = is_free ? 5 : 0, .transition_lsn = end};
    candidate_page_location_t l = location(2, 1, end);
    OK(candidate_allocation_encode_node(&l, &node, out, 8192));
}

static unsigned encode_images(const fixture_t *f, unsigned kind, uint64_t generation, uint64_t end,
    fixture_system_t next)
{
    unsigned count = 0;
    candidate_page_location_t l;
    if (kind == 0 || kind == 15 || kind == 16) {
        candidate_catalog_root_t root = {.owner_epoch = f->owner_epoch, .current_epoch = next.epoch,
            .retention_floor = next.floor, .transaction_highwater = next.transaction_highwater,
            .page_highwater = next.page_highwater, .retained_bytes = 8192};
        l = location(3, 1, end);
        OK(candidate_catalog_encode_root(&l, &root, images[count++], 8192));
    }
    if (kind != 15) {
        candidate_allocation_root_t root = {.owner_epoch = f->owner_epoch, .page_highwater = next.page_highwater,
            .gc_horizon = 5, .count = 4, .nodes = {{2, 1}}};
        l = location(1, 1, end);
        OK(candidate_allocation_encode_root(&l, &root, images[count++], 8192));
    }
    if (kind == 0 || kind == 6 || kind == 7) encode_node(kind, generation, end, images[count++]);
    if (kind == 0 || kind == 6) {
        uint8_t value = 0x51;
        l = location(10, generation, end);
        OK(candidate_page_encode_overflow(&l, (candidate_page_ref_t){0}, &value, 1, images[count++], 8192));
    }
    return count;
}

static void set_event(candidate_witness_system_event_t *e, uint16_t type, uint64_t a, uint64_t b, uint64_t c,
    uint64_t d)
{
    e->type = type;
    e->value.allocation.page_id = a;
    e->value.allocation.previous_generation = b;
    e->value.allocation.next_generation = c;
    e->value.allocation.release_horizon = d;
}

static unsigned build_events(fixture_system_t old, fixture_system_t next, unsigned kind, uint64_t generation,
    candidate_witness_system_event_t out[2])
{
    unsigned n = 0;
    memset(out, 0, 2 * sizeof(*out));
    if (kind == 0 || kind == 16)
        set_event(&out[n++], 16, old.transaction_highwater, next.transaction_highwater, old.page_highwater,
            next.page_highwater);
    if (kind == 15) set_event(&out[n++], 15, old.epoch, next.epoch, old.floor, next.floor);
    if (kind == 0 || kind == 6) set_event(&out[n++], 6, 10, generation - 1, generation, 0);
    if (kind == 7) set_event(&out[n++], 7, 10, generation, generation, 5);
    return n;
}

static void append_system_records(fixture_t *f, uint64_t id, unsigned pages,
    const candidate_witness_system_event_t *events, unsigned count, uint64_t end)
{
    fixture_chain_t chain = {0};
    uint32_t crc = 0;
    uint8_t begin[16] = {0};
    put(begin, id, 8);
    put(begin + 8, pages, 4);
    put(begin + 12, pages + count, 4);
    fixture_append(f, &chain, 3, begin, sizeof(begin), &crc);
    static uint8_t body[8224];
    for (unsigned i = 0; i < pages; i++) {
        memset(body, 0, sizeof(body));
        put(body, id, 8);
        memcpy(body + 8, images[i] + 24, 16);
        memcpy(body + 32, images[i], 8192);
        fixture_append(f, &chain, 4, body, sizeof(body), &crc);
    }
    for (unsigned i = 0; i < count; i++) {
        uint8_t event[40] = {0};
        const candidate_witness_system_event_t *e = &events[i];
        put(event, id, 8);
        put(event + 8, e->value.allocation.page_id, 8);
        put(event + 16, e->value.allocation.previous_generation, 8);
        put(event + 24, e->value.allocation.next_generation, 8);
        put(event + 32, e->value.allocation.release_horizon, 8);
        fixture_append(f, &chain, e->type, event, sizeof(event), &crc);
    }
    uint8_t finish[24] = {0};
    put(finish, id, 8);
    put(finish + 8, pages, 4);
    put(finish + 12, pages + count, 4);
    put(finish + 16, crc, 4);
    CHECK(fixture_append(f, &chain, 8, finish, sizeof(finish), NULL) == end);
}

uint64_t fixture_system_group(fixture_t *f, uint64_t id, unsigned kind, uint64_t generation)
{
    fixture_system_t next = next_system(f->system, kind);
    candidate_witness_system_event_t events[2];
    unsigned count = build_events(f->system, next, kind, generation, events);
    unsigned pages = kind == 0 ? 4 : (kind == 6 ? 3 : (kind == 15 ? 1 : 2));
    uint64_t end = f->end_lsn + frame_bytes(16) + pages * frame_bytes(8224) + count * frame_bytes(40) + frame_bytes(24);
    CHECK(encode_images(f, kind, generation, end, next) == pages);
    append_system_records(f, id, pages, events, count, end);
    CHECK(f->group_count < FIXTURE_GROUPS && f->event_count + count <= FIXTURE_EVENTS);
    f->groups[f->group_count++] = (candidate_semantic_group_t){id, 0, end};
    for (unsigned i = 0; i < count; i++) {
        events[i].group_id = id;
        events[i].end_lsn = end;
        events[i].owner_epoch = f->owner_epoch;
        f->events[f->event_count++] = events[i];
    }
    f->system = next;
    f->system_groups++;
    return end;
}

void fixture_prefix(fixture_t *f)
{
    if (f->has_system) {
        fixture_system_group(f, ++f->next_group, 0, 1);
        fixture_system_group(f, ++f->next_group, 7, 1);
    }
    fixture_transaction_t *t11 = fixture_transaction(f, 11);
    fixture_admit(f, t11, 0x11, 70000);
    fixture_begin(f, t11);
    fixture_delta_group(f, t11, ++f->next_group);
    fixture_result(t11, 70000, 0xb1, (candidate_version_ref_t){{20, 1}, 1, 1});
    fixture_chunk(f, t11);
    fixture_transaction_t *t12 = fixture_transaction(f, 12);
    fixture_admit(f, t12, 0x12, 131072);
    fixture_begin(f, t12);
    fixture_delta_group(f, t12, ++f->next_group);
    fixture_result(t12, 100000, 0xb2, (candidate_version_ref_t){{21, 1}, 2, 1});
    fixture_chunk(f, t12);
    fixture_transaction_t *t13 = fixture_transaction(f, 13);
    fixture_admit(f, t13, 0x13, 50);
    fixture_begin(f, t13);
    fixture_delta_group(f, t13, ++f->next_group);
    fixture_result(t13, 50, 0xb3, (candidate_version_ref_t){{22, 1}, 3, 1});
    fixture_chunk(f, t13);
    fixture_commit(f, t13, 1);
    fixture_begin(f, fixture_transaction(f, 14));
    fixture_transaction_t *t15 = fixture_transaction(f, 15);
    fixture_admit(f, t15, 0x15, 1000);
    fixture_begin(f, t15);
}

void fixture_suffix_first(fixture_t *f)
{
    if (f->has_system) fixture_system_group(f, ++f->next_group, 6, 2);
    fixture_transaction_t *t11 = fixture_transaction(f, 11);
    fixture_delta_group(f, t11, ++f->next_group);
    fixture_chunk(f, t11);
    fixture_commit(f, t11, 2);
}

void fixture_suffix_rest(fixture_t *f)
{
    fixture_abort(f, fixture_transaction(f, 12));
    fixture_transaction_t *t15 = fixture_transaction(f, 15);
    fixture_result(t15, 1000, 0xb5, (candidate_version_ref_t){{23, 1}, 5, 1});
    fixture_chunk(f, t15);
    fixture_commit(f, t15, 3);
    fixture_commit(f, fixture_transaction(f, 14), 4);
    if (f->has_system) {
        fixture_system_group(f, ++f->next_group, 15, 0);
        fixture_system_group(f, ++f->next_group, 16, 0);
    }
    fixture_transaction_t *t150 = fixture_transaction(f, 150);
    fixture_begin(f, t150);
    fixture_commit(f, t150, 5);
}

candidate_witness_options_t fixture_options(bool has_system, const candidate_wal_memory_t *memory)
{
    candidate_witness_options_t o = {.memory = memory, .enable_system_groups = has_system};
    memcpy(o.uuid, fixture_uuid, 16);
    return o;
}

static void *allocate(void *context, size_t size)
{
    fixture_memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    void *p = malloc(size);
    if (p != NULL) m->live += size;
    return p;
}

static void release(void *context, void *p, size_t size)
{
    fixture_memory_t *m = context;
    CHECK(m->live >= size);
    m->live -= size;
    free(p);
}

candidate_wal_memory_t fixture_memory(fixture_memory_t *memory)
{
    return (candidate_wal_memory_t){memory, allocate, release};
}

static unsigned guarded_segment(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0) return GUARD_SEGMENTS;
    for (unsigned i = 0; i < GUARD_SEGMENTS; i++) {
        const guarded_segment_t *s = &guarded_segments[i];
        if (s->is_bound && s->device == st.st_dev && s->inode == st.st_ino) return i;
    }
    return GUARD_SEGMENTS;
}

static ssize_t guarded_pread(int fd, void *out, size_t size, off_t offset)
{
    if (!fixture_guard.is_armed) return pread(fd, out, size, offset);
    unsigned id = guarded_segment(fd);
    uint64_t lsn = (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES + (uint64_t)offset;
    if (id == GUARD_SEGMENTS || offset < 0 || lsn < fixture_guard.floor_lsn) {
        fixture_guard.forbidden++;
        errno = EIO;
        return -1;
    }
    ssize_t n = pread(fd, out, size, offset);
    if (n <= 0) return n;
    if (fixture_guard.reads == 0 || lsn < fixture_guard.lowest_lsn) fixture_guard.lowest_lsn = lsn;
    if (lsn + (uint64_t)n > fixture_guard.highest_end) fixture_guard.highest_end = lsn + (uint64_t)n;
    fixture_guard.reads++;
    fixture_guard.bytes += (uint64_t)n;
    return n;
}

void fixture_guard_arm(const char *path, uint64_t floor_lsn)
{
    memset(guarded_segments, 0, sizeof(guarded_segments));
    for (unsigned i = 0; i < GUARD_SEGMENTS; i++) {
        char name[300];
        struct stat st;
        CHECK(snprintf(name, sizeof(name), "%s.wal.%016x", path, i) > 0);
        if (stat(name, &st) != 0) break;
        guarded_segments[i] = (guarded_segment_t){st.st_dev, st.st_ino, true};
    }
    CHECK(guarded_segments[0].is_bound);
    fixture_guard = (fixture_guard_t){.floor_lsn = floor_lsn, .is_armed = true};
}

void fixture_guard_disarm(void)
{
    fixture_guard.is_armed = false;
}
