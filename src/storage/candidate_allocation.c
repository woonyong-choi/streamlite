#include "storage/candidate_allocation.h"

#include <stdlib.h>
#include <string.h>

struct candidate_allocation {
    candidate_wal_memory_t memory;
    candidate_allocation_info_t info;
    candidate_page_ref_t protected[CANDIDATE_ALLOCATION_NODES + 1];
    unsigned protected_count;
    candidate_allocation_entry_t entries[CANDIDATE_ALLOCATION_ENTRIES];
};

static unsigned lower_bound(const candidate_allocation_t *a, uint64_t id)
{
    unsigned lo = 0, hi = a->info.count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (a->entries[mid].ref.id < id) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static bool is_protected(const candidate_allocation_t *a, uint64_t id)
{
    for (unsigned i = 0; i < a->protected_count; i++) if (a->protected[i].id == id) return true;
    return false;
}

candidate_allocation_lookup_t candidate_allocation_find(const candidate_allocation_t *a,
    uint64_t id, candidate_allocation_entry_t *out)
{
    if (a == NULL || id == 0 || out == NULL) return CANDIDATE_ALLOCATION_INVALID;
    unsigned at = lower_bound(a, id);
    if (at == a->info.count || a->entries[at].ref.id != id) return CANDIDATE_ALLOCATION_NOT_FOUND;
    *out = a->entries[at]; return CANDIDATE_ALLOCATION_MATCH;
}

candidate_allocation_lookup_t candidate_allocation_at(const candidate_allocation_t *a,
    uint32_t ordinal, candidate_allocation_entry_t *out)
{
    if (a == NULL || out == NULL) return CANDIDATE_ALLOCATION_INVALID;
    if (ordinal >= a->info.count) return CANDIDATE_ALLOCATION_NOT_FOUND;
    *out = a->entries[ordinal]; return CANDIDATE_ALLOCATION_MATCH;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool same_ref(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static candidate_wal_status_t load_nodes(candidate_allocation_t *a,
    const candidate_allocation_load_t *r, const candidate_allocation_root_t *root)
{
    unsigned index = 0;
    for (unsigned i = 1; i < a->protected_count; i++) {
        uint8_t bytes[CANDIDATE_PAGE_BYTES] = {0};
        candidate_page_ref_t ref = a->protected[i];
        candidate_wal_status_t s = r->load(r->context, ref, bytes);
        if (s != CANDIDATE_WAL_OK) return s;
        candidate_page_info_t info; candidate_allocation_node_t node;
        s = candidate_page_decode_header(r->uuid, bytes, sizeof(bytes), &info);
        if (s != CANDIDATE_WAL_OK) return s;
        if (!same_ref(info.location.ref, ref) || info.location.lsn > a->info.lsn) return CANDIDATE_WAL_CORRUPT;
        s = candidate_allocation_decode_node(r->uuid, bytes, sizeof(bytes), &node);
        if (s != CANDIDATE_WAL_OK) return s;
        unsigned count = root->count - index;
        if (count > CANDIDATE_ALLOCATION_NODE_ENTRIES) count = CANDIDATE_ALLOCATION_NODE_ENTRIES;
        if (node.ordinal != i - 1 || node.count != count) return CANDIDATE_WAL_CORRUPT;
        for (unsigned j = 0; j < node.count; j++, index++) {
            candidate_allocation_entry_t e = node.entries[j];
            if (e.ref.id > root->page_highwater || e.transition_lsn > info.location.lsn || e.release_horizon > root->gc_horizon)
                return CANDIDATE_WAL_CORRUPT;
            if (index > 0 && e.ref.id <= a->entries[index - 1].ref.id) return CANDIDATE_WAL_CORRUPT;
            a->entries[index] = e;
        }
    }
    for (unsigned i = 0; i < a->protected_count; i++) {
        candidate_allocation_entry_t e;
        if (candidate_allocation_find(a, a->protected[i].id, &e) != CANDIDATE_ALLOCATION_MATCH) return CANDIDATE_WAL_CORRUPT;
        if (!same_ref(e.ref, a->protected[i]) || e.state != CANDIDATE_ALLOCATION_LIVE) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_allocation_load(const candidate_allocation_load_t *request,
    candidate_allocation_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (request == NULL) return CANDIDATE_WAL_INVALID;
    candidate_allocation_load_t r = *request;
    if (r.load == NULL || r.root.id == 0 || r.root.generation == 0) return CANDIDATE_WAL_INVALID;
    uint8_t uuid_bits = 0;
    for (unsigned i = 0; i < 16; i++) uuid_bits |= r.uuid[i];
    if (uuid_bits == 0) return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (r.memory != NULL) memory = *r.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    uint8_t bytes[CANDIDATE_PAGE_BYTES] = {0};
    candidate_wal_status_t s = r.load(r.context, r.root, bytes);
    if (s != CANDIDATE_WAL_OK) return s;
    candidate_allocation_root_t root; candidate_page_info_t info;
    s = candidate_allocation_decode_root(r.uuid, bytes, sizeof(bytes), &root);
    if (s != CANDIDATE_WAL_OK) return s;
    s = candidate_page_decode_header(r.uuid, bytes, sizeof(bytes), &info);
    if (s != CANDIDATE_WAL_OK) return s;
    if (!same_ref(info.location.ref, r.root)) return CANDIDATE_WAL_CORRUPT;
    candidate_allocation_t *a = memory.allocate(memory.context, sizeof(*a));
    if (a == NULL) return CANDIDATE_WAL_NOMEM;
    memset(a, 0, sizeof(*a)); a->memory = memory;
    a->info = (candidate_allocation_info_t){root.owner_epoch, root.page_highwater, root.gc_horizon,
        info.location.lsn, root.count, sizeof(*a)};
    a->protected_count = 1 + (root.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    a->protected[0] = r.root;
    for (unsigned i = 1; i < a->protected_count; i++) a->protected[i] = root.nodes[i - 1];
    s = load_nodes(a, &r, &root);
    if (s != CANDIDATE_WAL_OK) { candidate_allocation_close(&a); return s; }
    *out = a; return CANDIDATE_WAL_OK;
}

candidate_allocation_info_t candidate_allocation_info(const candidate_allocation_t *a)
{
    return a == NULL ? (candidate_allocation_info_t){0} : a->info;
}

static candidate_wal_status_t validate_change(const candidate_allocation_t *a,
    const candidate_allocation_batch_t *b, const candidate_allocation_change_t *c, unsigned *added)
{
    if (c->page_id == 0 || c->page_id > b->page_highwater) return CANDIDATE_WAL_INVALID;
    if (c->next_state != CANDIDATE_ALLOCATION_LIVE && c->next_state != CANDIDATE_ALLOCATION_FREE) return CANDIDATE_WAL_INVALID;
    if (c->next_state == CANDIDATE_ALLOCATION_LIVE && c->release_horizon != 0) return CANDIDATE_WAL_INVALID;
    if (c->next_state == CANDIDATE_ALLOCATION_FREE && (c->release_horizon == 0 || c->release_horizon > b->gc_horizon))
        return CANDIDATE_WAL_INVALID;
    candidate_allocation_entry_t old;
    candidate_allocation_lookup_t s = candidate_allocation_find(a, c->page_id, &old);
    if (s == CANDIDATE_ALLOCATION_NOT_FOUND) {
        if (c->previous_generation != 0 || c->previous_state != 0 || c->next_state != CANDIDATE_ALLOCATION_LIVE)
            return CANDIDATE_WAL_STALE;
        (*added)++; return CANDIDATE_WAL_OK;
    }
    if (s != CANDIDATE_ALLOCATION_MATCH) return CANDIDATE_WAL_INVALID;
    if (old.ref.generation != c->previous_generation || old.state != c->previous_state) return CANDIDATE_WAL_STALE;
    if (old.state == c->next_state || is_protected(a, c->page_id)) return CANDIDATE_WAL_STALE;
    if (old.state == CANDIDATE_ALLOCATION_FREE && old.ref.generation == UINT64_MAX) return CANDIDATE_WAL_LIMIT;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_allocation_apply(candidate_allocation_t *a, const candidate_allocation_batch_t *b)
{
    if (a == NULL || b == NULL || (b->count > 0 && b->changes == NULL)) return CANDIDATE_WAL_INVALID;
    if (b->count > CANDIDATE_ALLOCATION_BATCH) return CANDIDATE_WAL_LIMIT;
    if (b->lsn % 8 != 0 || (b->lsn <= a->info.lsn && (b->lsn != 0 || a->info.lsn != 0)) ||
        b->page_highwater < a->info.page_highwater || b->gc_horizon < a->info.gc_horizon)
        return CANDIDATE_WAL_STALE;
    unsigned added = 0;
    for (unsigned i = 0; i < b->count; i++) {
        if (i > 0 && b->changes[i].page_id <= b->changes[i - 1].page_id) return CANDIDATE_WAL_INVALID;
        candidate_wal_status_t s = validate_change(a, b, &b->changes[i], &added);
        if (s != CANDIDATE_WAL_OK) return s;
    }
    if (added > CANDIDATE_ALLOCATION_ENTRIES - a->info.count) return CANDIDATE_WAL_LIMIT;
    for (unsigned i = 0; i < b->count; i++) {
        const candidate_allocation_change_t *c = &b->changes[i];
        unsigned at = lower_bound(a, c->page_id);
        uint64_t generation;
        if (at == a->info.count || a->entries[at].ref.id != c->page_id) {
            memmove(a->entries + at + 1, a->entries + at, (a->info.count - at) * sizeof(a->entries[0]));
            a->info.count++; generation = 1;
        } else generation = a->entries[at].ref.generation + (c->next_state == CANDIDATE_ALLOCATION_LIVE);
        a->entries[at] = (candidate_allocation_entry_t){{c->page_id, generation}, c->next_state, c->release_horizon, b->lsn};
    }
    a->info.page_highwater = b->page_highwater; a->info.gc_horizon = b->gc_horizon; a->info.lsn = b->lsn;
    return CANDIDATE_WAL_OK;
}

void candidate_allocation_close(candidate_allocation_t **allocation)
{
    if (allocation == NULL || *allocation == NULL) return;
    candidate_allocation_t *a = *allocation; *allocation = NULL;
    candidate_wal_memory_t memory = a->memory;
    memory.release(memory.context, a, sizeof(*a));
}
