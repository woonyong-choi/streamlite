#include "candidate_wal_identity_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t first, last; } interval_t;

struct candidate_wal_identity {
    candidate_wal_memory_t memory;
    candidate_identity_info_t info;
    interval_t *intervals;
    candidate_identity_nonce_t *tombstones;
};

static size_t interval_bytes(uint32_t count) { return (size_t)count * sizeof(interval_t); }

static size_t tombstone_bytes(uint32_t count) { return (size_t)count * sizeof(candidate_identity_nonce_t); }

static candidate_wal_status_t allocate_array(const candidate_wal_memory_t *memory, size_t bytes, void **out)
{
    *out = NULL;
    if (bytes == 0) return CANDIDATE_WAL_OK;
    *out = memory->allocate(memory->context, bytes);
    return *out == NULL ? CANDIDATE_WAL_NOMEM : CANDIDATE_WAL_OK;
}

static void release_array(const candidate_wal_memory_t *memory, void *p, size_t bytes)
{
    if (p != NULL) memory->release(memory->context, p, bytes);
}

static candidate_wal_status_t copy_arrays(candidate_wal_identity_t *identity, const candidate_wal_identity_t *source)
{
    void *intervals = NULL, *tombstones = NULL;
    candidate_wal_status_t status = allocate_array(&identity->memory, interval_bytes(source->info.intervals), &intervals);
    if (status == CANDIDATE_WAL_OK)
        status = allocate_array(&identity->memory, tombstone_bytes(source->info.tombstones), &tombstones);
    if (status != CANDIDATE_WAL_OK) {
        release_array(&identity->memory, intervals, interval_bytes(source->info.intervals));
        return status;
    }
    if (intervals != NULL) memcpy(intervals, source->intervals, interval_bytes(source->info.intervals));
    if (tombstones != NULL) memcpy(tombstones, source->tombstones, tombstone_bytes(source->info.tombstones));
    identity->intervals = intervals;
    identity->tombstones = tombstones;
    identity->info = source->info;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_identity_create(const candidate_wal_memory_t *memory,
    const candidate_wal_identity_t *source, candidate_wal_identity_t **out)
{
    *out = NULL;
    candidate_wal_memory_t allocator = *memory;
    candidate_wal_identity_t *identity = allocator.allocate(allocator.context, sizeof(*identity));
    if (identity == NULL) return CANDIDATE_WAL_NOMEM;
    *identity = (candidate_wal_identity_t){.memory = allocator,.info = {.resident_bytes = sizeof(*identity)}};
    if (source != NULL) {
        candidate_wal_status_t status = copy_arrays(identity, source);
        if (status != CANDIDATE_WAL_OK) { candidate_identity_close(&identity); return status; }
    }
    *out = identity;
    return CANDIDATE_WAL_OK;
}

static int compare_ids(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return a == b ? 0 : (a < b ? -1 : 1);
}

static int compare_nonces(const void *left, const void *right)
{
    const candidate_identity_nonce_t *a = left, *b = right;
    if (a->epoch != b->epoch) return a->epoch < b->epoch ? -1 : 1;
    return memcmp(a->nonce, b->nonce, 16);
}

static interval_t next_interval(const candidate_wal_identity_t *identity, const candidate_identity_update_t *update,
    uint32_t *old, uint32_t *added)
{
    bool is_old = *added == update->started_count ||
        (*old < identity->info.intervals && identity->intervals[*old].first <= update->started[*added]);
    if (is_old) return identity->intervals[(*old)++];
    uint64_t id = update->started[(*added)++];
    return (interval_t){id, id};
}

static bool extends(interval_t current, interval_t next)
{
    return current.last == UINT64_MAX || next.first <= current.last + 1;
}

static uint32_t merge_intervals(const candidate_wal_identity_t *identity, const candidate_identity_update_t *update,
    interval_t *out)
{
    uint32_t count = 0, old = 0, added = 0;
    interval_t current = {0};
    while (old < identity->info.intervals || added < update->started_count) {
        interval_t next = next_interval(identity, update, &old, &added);
        if (count > 0 && extends(current, next)) {
            if (next.last > current.last) current.last = next.last;
            if (out != NULL) out[count - 1] = current;
            continue;
        }
        current = next;
        if (out != NULL) out[count] = current;
        count++;
    }
    return count;
}

static candidate_identity_nonce_t next_nonce(const candidate_wal_identity_t *identity,
    const candidate_identity_update_t *update, uint32_t *old, uint32_t *added)
{
    bool is_old = *added == update->nonce_count ||
        (*old < identity->info.tombstones && compare_nonces(&identity->tombstones[*old], &update->nonces[*added]) <= 0);
    return is_old ? identity->tombstones[(*old)++] : update->nonces[(*added)++];
}

static uint32_t merge_tombstones(const candidate_wal_identity_t *identity, const candidate_identity_update_t *update,
    candidate_identity_nonce_t *out)
{
    uint32_t count = 0, old = 0, added = 0;
    candidate_identity_nonce_t last = {0};
    while (old < identity->info.tombstones || added < update->nonce_count) {
        candidate_identity_nonce_t next = next_nonce(identity, update, &old, &added);
        if (update->has_floor && next.epoch < update->retention_floor) continue;
        if (count > 0 && compare_nonces(&last, &next) == 0) continue;
        last = next;
        if (out != NULL) out[count] = next;
        count++;
    }
    return count;
}

static void replace_arrays(candidate_wal_identity_t *identity, interval_t *intervals, uint32_t interval_count,
    candidate_identity_nonce_t *tombstones, uint32_t tombstone_count)
{
    release_array(&identity->memory, identity->intervals, interval_bytes(identity->info.intervals));
    release_array(&identity->memory, identity->tombstones, tombstone_bytes(identity->info.tombstones));
    identity->intervals = intervals;
    identity->tombstones = tombstones;
    identity->info.intervals = interval_count;
    identity->info.tombstones = tombstone_count;
    identity->info.resident_bytes = sizeof(*identity) + interval_bytes(interval_count) + tombstone_bytes(tombstone_count);
    if (interval_count > 0) identity->info.max_started_id = intervals[interval_count - 1].last;
}

candidate_wal_status_t candidate_identity_record(candidate_wal_identity_t *identity,
    candidate_identity_update_t *update)
{
    if (update->started_count > 0) qsort(update->started, update->started_count, sizeof(uint64_t), compare_ids);
    if (update->nonce_count > 0)
        qsort(update->nonces, update->nonce_count, sizeof(candidate_identity_nonce_t), compare_nonces);
    uint32_t interval_count = merge_intervals(identity, update, NULL);
    uint32_t tombstone_count = merge_tombstones(identity, update, NULL);
    if (interval_count > CANDIDATE_IDENTITY_INTERVALS || tombstone_count > CANDIDATE_IDENTITY_TOMBSTONES)
        return CANDIDATE_WAL_LIMIT;
    void *intervals = NULL, *tombstones = NULL;
    candidate_wal_status_t status = allocate_array(&identity->memory, interval_bytes(interval_count), &intervals);
    if (status == CANDIDATE_WAL_OK)
        status = allocate_array(&identity->memory, tombstone_bytes(tombstone_count), &tombstones);
    if (status != CANDIDATE_WAL_OK) {
        release_array(&identity->memory, intervals, interval_bytes(interval_count));
        return status;
    }
    merge_intervals(identity, update, intervals);
    merge_tombstones(identity, update, tombstones);
    replace_arrays(identity, intervals, interval_count, tombstones, tombstone_count);
    if (update->max_owner_epoch > identity->info.max_owner_epoch) identity->info.max_owner_epoch = update->max_owner_epoch;
    return CANDIDATE_WAL_OK;
}

void candidate_identity_retire(candidate_wal_identity_t *identity, const candidate_identity_retired_t *retired)
{
    candidate_identity_retired_t *total = &identity->info.retired;
    total->transactions += retired->transactions;
    total->commits += retired->commits;
    total->aborts += retired->aborts;
    total->groups += retired->groups;
    total->events += retired->events;
}

bool candidate_identity_has_started(const candidate_wal_identity_t *identity, uint64_t id)
{
    if (identity == NULL) return false;
    uint32_t lo = 0, hi = identity->info.intervals;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (identity->intervals[mid].last < id) lo = mid + 1; else hi = mid;
    }
    return lo < identity->info.intervals && identity->intervals[lo].first <= id;
}

bool candidate_identity_has_nonce(const candidate_wal_identity_t *identity, uint64_t epoch, const uint8_t nonce[16])
{
    if (identity == NULL || identity->info.tombstones == 0) return false;
    candidate_identity_nonce_t key = {.epoch = epoch};
    memcpy(key.nonce, nonce, 16);
    return bsearch(&key, identity->tombstones, identity->info.tombstones, sizeof(key), compare_nonces) != NULL;
}

candidate_identity_info_t candidate_identity_info(const candidate_wal_identity_t *identity)
{
    return identity == NULL ? (candidate_identity_info_t){0} : identity->info;
}

void candidate_identity_close(candidate_wal_identity_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_wal_identity_t *identity = *out;
    *out = NULL;
    candidate_wal_memory_t memory = identity->memory;
    release_array(&memory, identity->intervals, interval_bytes(identity->info.intervals));
    release_array(&memory, identity->tombstones, tombstone_bytes(identity->info.tombstones));
    memory.release(memory.context, identity, sizeof(*identity));
}
