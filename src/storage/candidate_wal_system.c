#include "candidate_wal_system_internal.h"

#include <string.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_store.h"

#define SYSTEM_GROUP_LIMIT 64u
typedef struct {
    candidate_page_info_t info;
    uint64_t previous_lsn;
    uint8_t bytes[CANDIDATE_PAGE_BYTES];
} image_t;
struct candidate_wal_system {
    candidate_wal_memory_t memory;
    uint8_t uuid[16];
    candidate_witness_system_info_t info;
    candidate_witness_system_event_t events[CANDIDATE_SYSTEM_EVENTS], pending[SYSTEM_GROUP_LIMIT];
    image_t images[SYSTEM_GROUP_LIMIT];
    unsigned pending_count, image_count;
};

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

candidate_wal_status_t candidate_system_create(const uint8_t uuid[16],
    const candidate_wal_memory_t *memory, candidate_wal_system_t **out)
{
    candidate_wal_memory_t saved = *memory;
    uint8_t saved_uuid[16]; memcpy(saved_uuid, uuid, 16);
    *out = saved.allocate(saved.context, sizeof(**out));
    if (*out == NULL) return CANDIDATE_WAL_NOMEM;
    memset(*out, 0, sizeof(**out)); (*out)->memory = saved; memcpy((*out)->uuid, saved_uuid, 16);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_system_clone(const candidate_wal_system_t *source,
    const candidate_wal_memory_t *memory, candidate_wal_system_t **out)
{
    candidate_wal_memory_t saved = *memory;
    *out = saved.allocate(saved.context, sizeof(**out));
    if (*out == NULL) return CANDIDATE_WAL_NOMEM;
    memcpy(*out, source, sizeof(**out)); (*out)->memory = saved;
    return CANDIDATE_WAL_OK;
}

void candidate_system_close(candidate_wal_system_t **system)
{
    if (system == NULL || *system == NULL) return;
    candidate_wal_system_t *s = *system; *system = NULL;
    candidate_wal_memory_t memory = s->memory; memory.release(memory.context, s, sizeof(*s));
}

size_t candidate_system_resident_bytes(const candidate_wal_system_t *s) { return s == NULL ? 0 : sizeof(*s); }
candidate_witness_system_info_t candidate_system_info(const candidate_wal_system_t *s)
{
    return s == NULL ? (candidate_witness_system_info_t){0} : s->info;
}

const candidate_witness_system_event_t *candidate_system_at(const candidate_wal_system_t *s, uint32_t index)
{
    return s == NULL || index >= s->info.events ? NULL : &s->events[index];
}

void candidate_system_begin(candidate_wal_system_t *s) { s->pending_count = 0; s->image_count = 0; }

candidate_wal_status_t candidate_system_bind_checkpoint(candidate_wal_system_t *s,
    const candidate_control_state_t *anchor)
{
    if (s == NULL || anchor == NULL) return CANDIDATE_WAL_INVALID;
    candidate_witness_system_info_t *info = &s->info;
    if (info->has_reservation && (info->transaction_highwater != anchor->transaction_highwater ||
        info->page_highwater != anchor->page_highwater)) return CANDIDATE_WAL_CORRUPT;
    if (info->has_epoch && (info->current_epoch != anchor->request_epoch ||
        info->retention_floor != anchor->retention_floor)) return CANDIDATE_WAL_CORRUPT;
    info->has_reservation = true;
    info->transaction_highwater = anchor->transaction_highwater;
    info->page_highwater = anchor->page_highwater;
    info->has_epoch = true;
    info->current_epoch = anchor->request_epoch;
    info->retention_floor = anchor->retention_floor;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_system_image(candidate_wal_system_t *s, const candidate_wal_record_t *r)
{
    if (r->payload_bytes != 8224) return CANDIDATE_WAL_CORRUPT;
    if (s->image_count == SYSTEM_GROUP_LIMIT) return CANDIDATE_WAL_LIMIT;
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode_header(s->uuid, r->payload + 32, 8192, &info);
    if (status != CANDIDATE_WAL_OK) return status;
    const uint8_t *bytes = r->payload + 32;
    if (info.type <= 2) status = candidate_page_decode(s->uuid, bytes, 8192, &info);
    else if (info.type == 3) { candidate_store_node_t node; status = candidate_store_decode(s->uuid, bytes, 8192, &node); }
    else if (info.body_bytes < 8) status = CANDIDATE_WAL_CORRUPT;
    else if (info.type == 4) {
        if (read_le(bytes + 70, 2) == 1) { candidate_catalog_root_t root; status = candidate_catalog_decode_root(s->uuid, bytes, 8192, &root); }
        else { candidate_catalog_node_t node; status = candidate_catalog_decode_node(s->uuid, bytes, 8192, &node); }
    } else if (info.type == 5) {
        if (read_le(bytes + 70, 2) == 1) { candidate_allocation_root_t root; status = candidate_allocation_decode_root(s->uuid, bytes, 8192, &root); }
        else { candidate_allocation_node_t node; status = candidate_allocation_decode_node(s->uuid, bytes, 8192, &node); }
    } else status = CANDIDATE_WAL_UNSUPPORTED;
    if (status != CANDIDATE_WAL_OK) return status;
    image_t *image = &s->images[s->image_count++]; image->info = info;
    image->previous_lsn = read_le(r->payload + 24, 8); memcpy(image->bytes, bytes, 8192);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_system_event(candidate_wal_system_t *s, const candidate_wal_record_t *r)
{
    if (r->payload_bytes != 40) return CANDIDATE_WAL_CORRUPT;
    if ((r->type == 15 || r->type == 16) && r->transaction_id != 0) return CANDIDATE_WAL_CORRUPT;
    if (s->pending_count == SYSTEM_GROUP_LIMIT) return CANDIDATE_WAL_LIMIT;
    candidate_witness_system_event_t e = {.type = r->type, .group_id = read_le(r->payload, 8)};
    uint64_t a = read_le(r->payload + 8, 8), b = read_le(r->payload + 16, 8);
    uint64_t c = read_le(r->payload + 24, 8), d = read_le(r->payload + 32, 8);
    if (r->type == 6 || r->type == 7) {
        if (a == 0 || c == 0) return CANDIDATE_WAL_CORRUPT;
        if (r->type == 6) {
            if (b == UINT64_MAX) return CANDIDATE_WAL_LIMIT;
            if (c != b + 1 || d != 0) return CANDIDATE_WAL_CORRUPT;
        } else if (b != c || d == 0) return CANDIDATE_WAL_CORRUPT;
        e.value.allocation.page_id = a; e.value.allocation.previous_generation = b;
        e.value.allocation.next_generation = c; e.value.allocation.release_horizon = d;
    } else if (r->type == 16) {
        if (b < a || d < c || (a == b && c == d)) return CANDIDATE_WAL_CORRUPT;
        e.value.reserve.previous_transaction = a; e.value.reserve.next_transaction = b;
        e.value.reserve.previous_page = c; e.value.reserve.next_page = d;
    } else if (r->type == 15) {
        if (a == 0 || c == 0 || c > a || a - c >= 4 || b < a || d < c || d > b || b - d >= 4 || (a == b && c == d))
            return CANDIDATE_WAL_CORRUPT;
        e.value.epoch.previous_epoch = a; e.value.epoch.next_epoch = b;
        e.value.epoch.previous_floor = c; e.value.epoch.next_floor = d;
    } else return CANDIDATE_WAL_UNSUPPORTED;
    for (unsigned i = 0; i < s->pending_count; i++) {
        const candidate_witness_system_event_t *old = &s->pending[i];
        if (r->type >= 15 && old->type == r->type) return CANDIDATE_WAL_CORRUPT;
        if (r->type <= 7 && old->type <= 7 && old->value.allocation.page_id == a) return CANDIDATE_WAL_CORRUPT;
    }
    s->pending[s->pending_count++] = e; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t roots(const candidate_wal_system_t *s,
    candidate_catalog_root_t *catalog, candidate_allocation_root_t *allocation, bool *has_catalog, bool *has_allocation)
{
    *has_catalog = false; *has_allocation = false;
    for (unsigned i = 0; i < s->image_count; i++) {
        const image_t *p = &s->images[i];
        if (read_le(p->bytes + 70, 2) != 1) continue;
        if (p->info.type == 4) {
            if (*has_catalog) return CANDIDATE_WAL_CORRUPT;
            *has_catalog = true;
            candidate_wal_status_t status = candidate_catalog_decode_root(s->uuid, p->bytes, 8192, catalog);
            if (status != CANDIDATE_WAL_OK) return status;
        } else if (p->info.type == 5) {
            if (*has_allocation) return CANDIDATE_WAL_CORRUPT;
            *has_allocation = true;
            candidate_wal_status_t status = candidate_allocation_decode_root(s->uuid, p->bytes, 8192, allocation);
            if (status != CANDIDATE_WAL_OK) return status;
        }
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t allocation_entry(const candidate_wal_system_t *s,
    const candidate_allocation_root_t *root, const candidate_witness_system_event_t *e, uint64_t end)
{
    uint64_t id = e->value.allocation.page_id, generation = e->value.allocation.next_generation;
    bool found = false, image = false;
    unsigned nodes = (root->count + 201) / 202;
    for (unsigned i = 0; i < s->image_count; i++) {
        const image_t *p = &s->images[i];
        if (p->info.location.ref.id == id && p->info.location.ref.generation == generation && p->previous_lsn == 0) image = true;
        if (p->info.type != 5 || read_le(p->bytes + 70, 2) != 2) continue;
        candidate_allocation_node_t node;
        candidate_wal_status_t status = candidate_allocation_decode_node(s->uuid, p->bytes, 8192, &node);
        if (status != CANDIDATE_WAL_OK) return status;
        if (node.ordinal >= nodes || root->nodes[node.ordinal].id != p->info.location.ref.id ||
            root->nodes[node.ordinal].generation != p->info.location.ref.generation) continue;
        unsigned count = root->count - node.ordinal * 202;
        if (count > 202) count = 202;
        if (node.count != count) return CANDIDATE_WAL_CORRUPT;
        for (unsigned j = 0; j < node.count; j++) {
            const candidate_allocation_entry_t *entry = &node.entries[j];
            if (entry->ref.id != id) continue;
            if (found || entry->ref.generation != generation || entry->transition_lsn != end ||
                entry->state != (e->type == 6 ? CANDIDATE_ALLOCATION_LIVE : CANDIDATE_ALLOCATION_FREE) ||
                entry->release_horizon != e->value.allocation.release_horizon) return CANDIDATE_WAL_CORRUPT;
            found = true;
        }
    }
    if (!found || (e->type == 6 && !image) || id > root->page_highwater || e->value.allocation.release_horizon > root->gc_horizon)
        return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = s->info.events; i > 0; i--) {
        const candidate_witness_system_event_t *old = &s->events[i - 1];
        if (old->type > 7 || old->value.allocation.page_id != id) continue;
        if (old->type == e->type || old->value.allocation.next_generation != e->value.allocation.previous_generation)
            return CANDIDATE_WAL_CORRUPT;
        break;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_system_finish(candidate_wal_system_t *s, uint64_t group,
    uint64_t end, uint64_t owner, uint64_t minimum_active_epoch, bool is_system)
{
    if (s->pending_count == 0 && is_system) return CANDIDATE_WAL_CORRUPT;
    if (s->pending_count > CANDIDATE_SYSTEM_EVENTS - s->info.events) return CANDIDATE_WAL_LIMIT;
    candidate_catalog_root_t catalog = {0}; candidate_allocation_root_t allocation = {0}; bool has_catalog, has_allocation;
    candidate_wal_status_t status = roots(s, &catalog, &allocation, &has_catalog, &has_allocation);
    if (status != CANDIDATE_WAL_OK) return status;
    if ((has_catalog && catalog.owner_epoch != owner) || (has_allocation && allocation.owner_epoch != owner)) return CANDIDATE_WAL_CORRUPT;
    candidate_witness_system_info_t next = s->info;
    for (unsigned i = 0; i < s->pending_count; i++) {
        const candidate_witness_system_event_t *e = &s->pending[i];
        if (e->group_id != group) return CANDIDATE_WAL_CORRUPT;
        if (e->type == 16) {
            if (!has_catalog || !has_allocation || catalog.transaction_highwater != e->value.reserve.next_transaction ||
                catalog.page_highwater != e->value.reserve.next_page || allocation.page_highwater != catalog.page_highwater)
                return CANDIDATE_WAL_CORRUPT;
            if (next.has_reservation && (next.transaction_highwater != e->value.reserve.previous_transaction || next.page_highwater != e->value.reserve.previous_page))
                return CANDIDATE_WAL_CORRUPT;
            next.has_reservation = true; next.transaction_highwater = e->value.reserve.next_transaction; next.page_highwater = e->value.reserve.next_page;
        } else if (e->type == 15) {
            if (!has_catalog || catalog.current_epoch != e->value.epoch.next_epoch || catalog.retention_floor != e->value.epoch.next_floor ||
                (minimum_active_epoch > 0 && catalog.retention_floor > minimum_active_epoch)) return CANDIDATE_WAL_CORRUPT;
            if (next.has_epoch && (next.current_epoch != e->value.epoch.previous_epoch || next.retention_floor != e->value.epoch.previous_floor))
                return CANDIDATE_WAL_CORRUPT;
            next.has_epoch = true; next.current_epoch = catalog.current_epoch; next.retention_floor = catalog.retention_floor;
        }
    }
    if (!next.has_reservation && has_catalog) {
        next.has_reservation = true; next.transaction_highwater = catalog.transaction_highwater; next.page_highwater = catalog.page_highwater;
    }
    if (!next.has_epoch && has_catalog) {
        next.has_epoch = true; next.current_epoch = catalog.current_epoch; next.retention_floor = catalog.retention_floor;
    }
    if (next.has_reservation && ((has_catalog && (catalog.transaction_highwater != next.transaction_highwater || catalog.page_highwater != next.page_highwater)) ||
        (has_allocation && allocation.page_highwater != next.page_highwater))) return CANDIDATE_WAL_CORRUPT;
    if (next.has_epoch && has_catalog && (catalog.current_epoch != next.current_epoch || catalog.retention_floor != next.retention_floor)) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < s->pending_count; i++) {
        if (s->pending[i].type > 7) continue;
        if (!has_allocation) return CANDIDATE_WAL_CORRUPT;
        status = allocation_entry(s, &allocation, &s->pending[i], end);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    for (unsigned i = 0; i < s->pending_count; i++) {
        candidate_witness_system_event_t e = s->pending[i]; e.end_lsn = end; e.owner_epoch = owner;
        s->events[next.events++] = e;
    }
    if (is_system) next.system_groups++;
    s->info = next; return CANDIDATE_WAL_OK;
}

static bool is_latest_transition(const candidate_wal_system_t *s, uint32_t index)
{
    uint64_t page = s->events[index].value.allocation.page_id;
    for (uint32_t i = index + 1; i < s->info.events; i++)
        if (s->events[i].type <= 7 && s->events[i].value.allocation.page_id == page) return false;
    return true;
}

static bool is_retained_event(const candidate_wal_system_t *s, uint32_t index, uint64_t max_allocated_id)
{
    const candidate_witness_system_event_t *e = &s->events[index];
    if (e->type == 6 || e->type == 7) return is_latest_transition(s, index);
    if (e->type != 16) return false;
    uint64_t first = e->value.reserve.previous_page > max_allocated_id ? e->value.reserve.previous_page : max_allocated_id;
    return e->value.reserve.next_page > first;
}

uint32_t candidate_system_retain(candidate_wal_system_t *s, uint64_t max_allocated_id)
{
    uint32_t kept = 0, total = s->info.events;
    for (uint32_t i = 0; i < total; i++) {
        if (!is_retained_event(s, i, max_allocated_id)) continue;
        if (kept != i) s->events[kept] = s->events[i];
        kept++;
    }
    s->info.events = kept;
    return total - kept;
}
