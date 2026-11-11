#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/storage/candidate_wal_identity_internal.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
#define BATCH 4096u
#define INTERVALS CANDIDATE_IDENTITY_INTERVALS
#define TOMBSTONES CANDIDATE_IDENTITY_TOMBSTONES

typedef struct {
    unsigned calls, fail_at, allocations;
    size_t live, peak, requested;
} memory_t;
typedef struct {
    candidate_identity_info_t info;
    size_t live;
} state_t;

static unsigned checks;
static memory_t memory;
static uint64_t started[BATCH];
static candidate_identity_nonce_t nonces[BATCH];

static void *allocate(void *context, size_t size)
{
    memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    void *p = malloc(size);
    if (p == NULL) return NULL;
    m->allocations++; m->live += size; m->requested += size;
    if (m->live > m->peak) m->peak = m->live;
    return p;
}

static void release(void *context, void *p, size_t size)
{
    memory_t *m = context;
    CHECK(m->live >= size);
    m->live -= size; free(p);
}

static const candidate_wal_memory_t allocator = {&memory,allocate,release};

static uint64_t id_at(uint32_t n) { return 2 * (uint64_t)n + 2; }

static void nonce_at(uint64_t epoch, uint32_t n, candidate_identity_nonce_t *out)
{
    memset(out,0,sizeof(*out)); out->epoch = epoch;
    for (unsigned i = 0; i < 4; i++) out->nonce[i] = (uint8_t)(n >> (24 - 8 * i));
    out->nonce[15] = 0x5a;
}

static candidate_wal_identity_t *create(const candidate_wal_identity_t *source)
{
    candidate_wal_identity_t *identity = NULL;
    OK(candidate_identity_create(&allocator,source,&identity));
    return identity;
}

static candidate_wal_status_t record_ids(candidate_wal_identity_t *identity, uint32_t first, uint32_t count)
{
    CHECK(count <= BATCH);
    for (uint32_t i = 0; i < count; i++) started[i] = id_at(first + count - 1 - i);
    candidate_identity_update_t u = {.started = started,.started_count = count};
    return candidate_identity_record(identity,&u);
}

static candidate_wal_status_t record_id(candidate_wal_identity_t *identity, uint64_t id)
{
    started[0] = id;
    candidate_identity_update_t u = {.started = started,.started_count = 1};
    return candidate_identity_record(identity,&u);
}

static candidate_wal_status_t record_nonces(candidate_wal_identity_t *identity, uint64_t epoch, uint32_t first, uint32_t count,
    uint64_t retention_floor)
{
    CHECK(count <= BATCH);
    for (uint32_t i = 0; i < count; i++) nonce_at(epoch,first + count - 1 - i,&nonces[i]);
    candidate_identity_update_t u = {.nonces = nonces,.nonce_count = count,.retention_floor = retention_floor,
        .has_floor = retention_floor != 0};
    return candidate_identity_record(identity,&u);
}

static void fill_ids(candidate_wal_identity_t *identity, uint32_t count)
{
    for (uint32_t at = 0; at < count; at += BATCH) OK(record_ids(identity,at,count - at < BATCH ? count - at : BATCH));
}

static void fill_nonces(candidate_wal_identity_t *identity, uint64_t epoch, uint32_t count)
{
    for (uint32_t at = 0; at < count; at += BATCH) OK(record_nonces(identity,epoch,at,count - at < BATCH ? count - at : BATCH,0));
}

static state_t state_of(const candidate_wal_identity_t *identity)
{
    return (state_t){candidate_identity_info(identity),memory.live};
}

static bool is_unchanged(const candidate_wal_identity_t *identity, const state_t *before)
{
    candidate_identity_info_t info = candidate_identity_info(identity);
    return memcmp(&info,&before->info,sizeof(info)) == 0 && memory.live == before->live;
}

static bool has_nonce_at(const candidate_wal_identity_t *identity, uint64_t epoch, uint32_t n)
{
    candidate_identity_nonce_t key; nonce_at(epoch,n,&key);
    return candidate_identity_has_nonce(identity,epoch,key.nonce);
}

static void expect_ids(const candidate_wal_identity_t *identity, uint32_t count)
{
    const uint32_t samples[] = {0,1,count / 2,count - 1};
    for (unsigned i = 0; i < 4; i++) {
        CHECK(candidate_identity_has_started(identity,id_at(samples[i])));
        CHECK(!candidate_identity_has_started(identity,id_at(samples[i]) + 1));
    }
    CHECK(!candidate_identity_has_started(identity,1) && !candidate_identity_has_started(identity,id_at(count)));
}

static void expect_sorted_ids(uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) CHECK(started[i] == id_at(first + i));
}

static void expect_sorted_nonces(uint64_t epoch, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        candidate_identity_nonce_t key; nonce_at(epoch,first + i,&key);
        CHECK(nonces[i].epoch == epoch && memcmp(nonces[i].nonce,key.nonce,16) == 0);
    }
}

static void intervals_at_capacity(void)
{
    candidate_wal_identity_t *identity = create(NULL);
    fill_ids(identity,INTERVALS - 1);
    candidate_identity_info_t before = candidate_identity_info(identity);
    OK(record_ids(identity,INTERVALS - 1,1));
    candidate_identity_info_t at = candidate_identity_info(identity);
    expect_ids(identity,INTERVALS);
    state_t full = state_of(identity); memory.calls = 0;
    candidate_wal_status_t one = record_ids(identity,INTERVALS,1);
    bool is_one_atomic = is_unchanged(identity,&full) && memory.calls == 0;
    candidate_wal_status_t two = record_ids(identity,INTERVALS,2);
    bool is_two_atomic = is_unchanged(identity,&full) && memory.calls == 0;
    printf("INTERVALS before=%u at=%u cap_plus_one=%d atomic=%d cap_plus_two=%d atomic=%d max=%llu resident=%zu\n",before.intervals,
        at.intervals,(int)one,is_one_atomic,(int)two,is_two_atomic,(unsigned long long)at.max_started_id,at.resident_bytes);
    CHECK(before.intervals == INTERVALS - 1 && at.intervals == INTERVALS && at.max_started_id == id_at(INTERVALS - 1));
    CHECK(one == CANDIDATE_WAL_LIMIT && is_one_atomic && two == CANDIDATE_WAL_LIMIT && is_two_atomic);
    CHECK(!candidate_identity_has_started(identity,id_at(INTERVALS)) && !candidate_identity_has_started(identity,id_at(INTERVALS + 1)));
    expect_sorted_ids(INTERVALS,2); expect_ids(identity,INTERVALS);
    OK(record_id(identity,id_at(INTERVALS - 1) + 1));
    candidate_identity_info_t extended = candidate_identity_info(identity);
    OK(record_id(identity,3));
    candidate_identity_info_t bridged = candidate_identity_info(identity);
    printf("INTERVALS full_extend=%u max=%llu full_bridge=%u\n",extended.intervals,(unsigned long long)extended.max_started_id,
        bridged.intervals);
    CHECK(extended.intervals == INTERVALS && extended.max_started_id == id_at(INTERVALS - 1) + 1);
    CHECK(bridged.intervals == INTERVALS - 1 && candidate_identity_has_started(identity,2) && candidate_identity_has_started(identity,3));
    CHECK(candidate_identity_has_started(identity,4) && !candidate_identity_has_started(identity,5));
    candidate_identity_close(&identity); CHECK(identity == NULL);
}

static void tombstones_at_capacity(void)
{
    candidate_wal_identity_t *identity = create(NULL);
    fill_nonces(identity,1,TOMBSTONES - 1);
    candidate_identity_info_t before = candidate_identity_info(identity);
    OK(record_nonces(identity,1,TOMBSTONES - 1,1,0));
    candidate_identity_info_t at = candidate_identity_info(identity);
    state_t full = state_of(identity); memory.calls = 0;
    candidate_wal_status_t over = record_nonces(identity,1,TOMBSTONES,2,0);
    bool is_atomic = is_unchanged(identity,&full) && memory.calls == 0;
    expect_sorted_nonces(1,TOMBSTONES,2);
    OK(record_nonces(identity,1,7,2,0));
    candidate_identity_info_t duplicate = candidate_identity_info(identity);
    candidate_wal_status_t fresh = record_nonces(identity,2,0,1,0);
    bool is_fresh_atomic = is_unchanged(identity,&full);
    printf("TOMBSTONES before=%u at=%u over=%d atomic=%d duplicate_at_cap=%u fresh_without_floor=%d resident=%zu\n",before.tombstones,
        at.tombstones,(int)over,is_atomic,duplicate.tombstones,(int)fresh,at.resident_bytes);
    CHECK(before.tombstones == TOMBSTONES - 1 && at.tombstones == TOMBSTONES && duplicate.tombstones == TOMBSTONES);
    CHECK(over == CANDIDATE_WAL_LIMIT && is_atomic && fresh == CANDIDATE_WAL_LIMIT && is_fresh_atomic);
    CHECK(has_nonce_at(identity,1,0) && has_nonce_at(identity,1,TOMBSTONES - 1) && !has_nonce_at(identity,1,TOMBSTONES));
    CHECK(!has_nonce_at(identity,2,0));
    OK(record_nonces(identity,2,0,1,2));
    candidate_identity_info_t floored = candidate_identity_info(identity);
    printf("TOMBSTONES floor=2 kept=%u old_epoch_present=%d fresh_present=%d\n",floored.tombstones,has_nonce_at(identity,1,0),
        has_nonce_at(identity,2,0));
    CHECK(floored.tombstones == 1 && !has_nonce_at(identity,1,0) && !has_nonce_at(identity,1,TOMBSTONES - 1));
    CHECK(has_nonce_at(identity,2,0) && floored.intervals == 0);
    OK(record_nonces(identity,1,9,1,2));
    CHECK(candidate_identity_info(identity).tombstones == 1 && !has_nonce_at(identity,1,9));
    candidate_identity_close(&identity);
}

static void maximum_id_adjacency(void)
{
    candidate_wal_identity_t *identity = create(NULL);
    OK(record_id(identity,UINT64_MAX));
    OK(record_id(identity,UINT64_MAX - 1));
    candidate_identity_info_t merged = candidate_identity_info(identity);
    OK(record_id(identity,UINT64_MAX));
    candidate_identity_info_t repeated = candidate_identity_info(identity);
    OK(record_id(identity,UINT64_MAX - 3));
    candidate_identity_info_t split = candidate_identity_info(identity);
    CHECK(!candidate_identity_has_started(identity,UINT64_MAX - 2));
    OK(record_id(identity,UINT64_MAX - 2));
    candidate_identity_info_t bridged = candidate_identity_info(identity);
    printf("MAXIMUM merged=%u repeated=%u split=%u bridged=%u max=%llu\n",merged.intervals,repeated.intervals,split.intervals,
        bridged.intervals,(unsigned long long)bridged.max_started_id);
    CHECK(merged.intervals == 1 && repeated.intervals == 1 && split.intervals == 2 && bridged.intervals == 1);
    CHECK(bridged.max_started_id == UINT64_MAX && candidate_identity_has_started(identity,UINT64_MAX));
    for (uint64_t id = UINT64_MAX - 3; id != 0; id++) CHECK(candidate_identity_has_started(identity,id));
    CHECK(!candidate_identity_has_started(identity,UINT64_MAX - 4) && !candidate_identity_has_started(identity,0));
    candidate_identity_close(&identity);
}

static candidate_wal_identity_t *seeded(void)
{
    candidate_wal_identity_t *identity = create(NULL);
    OK(record_ids(identity,0,8)); OK(record_nonces(identity,1,0,8,0));
    candidate_identity_retire(identity,&(candidate_identity_retired_t){1,2,3,4,5});
    started[0] = id_at(0);
    candidate_identity_update_t u = {.started = started,.started_count = 1,.max_owner_epoch = 9};
    OK(candidate_identity_record(identity,&u));
    return identity;
}

static void clone_independence(void)
{
    candidate_wal_identity_t *source = seeded(), *clone = create(source);
    candidate_identity_info_t s0 = candidate_identity_info(source), c0 = candidate_identity_info(clone);
    CHECK(memcmp(&s0,&c0,sizeof(s0)) == 0 && s0.intervals == 8 && s0.tombstones == 8 && s0.max_owner_epoch == 9);
    OK(record_ids(clone,100,4));
    candidate_identity_info_t s1 = candidate_identity_info(source);
    CHECK(memcmp(&s0,&s1,sizeof(s0)) == 0 && !candidate_identity_has_started(source,id_at(100)));
    CHECK(candidate_identity_has_started(clone,id_at(100)) && candidate_identity_info(clone).intervals == 12);
    OK(record_nonces(source,1,50,2,0));
    candidate_identity_info_t c1 = candidate_identity_info(clone);
    CHECK(c1.tombstones == 8 && !has_nonce_at(clone,1,50) && has_nonce_at(source,1,50));
    candidate_identity_close(&source);
    CHECK(source == NULL && candidate_identity_has_started(clone,id_at(7)) && has_nonce_at(clone,1,7));
    candidate_identity_info_t c2 = candidate_identity_info(clone);
    printf("CLONE intervals=%u tombstones=%u retired=%llu max_owner=%llu live_after_source_close=%zu\n",c2.intervals,c2.tombstones,
        (unsigned long long)c2.retired.transactions,(unsigned long long)c2.max_owner_epoch,memory.live);
    CHECK(c2.retired.transactions == 1 && c2.retired.events == 5 && memory.live == c2.resident_bytes);
    candidate_identity_close(&clone);
    CHECK(clone == NULL && memory.live == 0);
}

static unsigned count_calls(const candidate_wal_identity_t *source)
{
    memory.calls = 0;
    candidate_wal_identity_t *copy = create(source);
    unsigned calls = memory.calls;
    candidate_identity_close(&copy);
    return calls;
}

static void create_failures(void)
{
    candidate_wal_identity_t *source = seeded();
    unsigned full = count_calls(source), empty = count_calls(NULL), failures = 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        const candidate_wal_identity_t *from = pass == 0 ? source : NULL;
        unsigned calls = pass == 0 ? full : empty;
        for (unsigned k = 1; k <= calls; k++) {
            size_t live = memory.live; candidate_wal_identity_t *out = (void *)1;
            memory.calls = 0; memory.fail_at = k;
            candidate_wal_status_t status = candidate_identity_create(&allocator,from,&out);
            memory.fail_at = 0;
            CHECK(status == CANDIDATE_WAL_NOMEM && out == NULL && memory.live == live);
            failures++;
        }
    }
    printf("CREATE_NOMEM copy_calls=%u empty_calls=%u failures=%u\n",full,empty,failures);
    CHECK(full == 3 && empty == 1 && failures == full + empty);
    candidate_identity_close(&source);
}

static candidate_wal_status_t record_mixed(candidate_wal_identity_t *identity)
{
    for (uint32_t i = 0; i < 4; i++) { started[i] = id_at(200 + 3 - i); nonce_at(1,200 + 3 - i,&nonces[i]); }
    candidate_identity_update_t u = {.started = started,.nonces = nonces,.started_count = 4,.nonce_count = 4,.max_owner_epoch = 11};
    return candidate_identity_record(identity,&u);
}

static void record_failures(void)
{
    candidate_wal_identity_t *identity = seeded(), *probe = create(identity);
    memory.calls = 0; OK(record_mixed(probe));
    unsigned calls = memory.calls, failures = 0;
    candidate_identity_close(&probe);
    for (unsigned k = 1; k <= calls; k++) {
        state_t before = state_of(identity);
        memory.calls = 0; memory.fail_at = k;
        candidate_wal_status_t status = record_mixed(identity);
        memory.fail_at = 0;
        bool is_atomic = is_unchanged(identity,&before) && !candidate_identity_has_started(identity,id_at(200));
        CHECK(status == CANDIDATE_WAL_NOMEM && is_atomic && !has_nonce_at(identity,1,200));
        expect_sorted_ids(200,4); expect_sorted_nonces(1,200,4);
        failures++;
    }
    OK(record_mixed(identity));
    candidate_identity_info_t info = candidate_identity_info(identity);
    printf("RECORD_NOMEM calls=%u failures=%u intervals=%u tombstones=%u max_owner=%llu\n",calls,failures,info.intervals,
        info.tombstones,(unsigned long long)info.max_owner_epoch);
    CHECK(calls == 2 && failures == calls && info.intervals == 12 && info.tombstones == 12 && info.max_owner_epoch == 11);
    candidate_identity_close(&identity);
}

static void lower_owner_and_empty(void)
{
    candidate_wal_identity_t *identity = seeded();
    started[0] = id_at(1);
    candidate_identity_update_t lower = {.started = started,.started_count = 1,.max_owner_epoch = 3};
    OK(candidate_identity_record(identity,&lower));
    candidate_identity_info_t info = candidate_identity_info(identity);
    CHECK(info.max_owner_epoch == 9 && info.intervals == 8);
    candidate_identity_close(&identity);
    candidate_identity_info_t none = candidate_identity_info(NULL); uint8_t nonce[16] = {0};
    CHECK(none.intervals == 0 && none.resident_bytes == 0 && !candidate_identity_has_started(NULL,1));
    CHECK(!candidate_identity_has_nonce(NULL,1,nonce));
    candidate_identity_close(&identity); candidate_identity_close(NULL);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    intervals_at_capacity();
    tombstones_at_capacity();
    maximum_id_adjacency();
    clone_independence();
    create_failures();
    record_failures();
    lower_owner_and_empty();
    printf("IDENTITY allocations=%u live=%zu total_peak=%zu requested=%zu\n",memory.allocations,memory.live,memory.peak,
        memory.requested);
    CHECK(memory.live == 0);
    printf("PASS candidate wal identity checks=%u; internal unit tests of the new identity container only, "
        "not an old API reproduction or an actual WAL cycle\n",checks);
    return 0;
}
