#include "storage/candidate_allocation.h"

#include <string.h>

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t v = 0; for (unsigned i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i); return v;
}

static void write_le(uint8_t *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i] != 0) return false;
    return true;
}

static unsigned node_count(uint32_t count)
{
    return (count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
}

static bool is_root(const candidate_allocation_root_t *r, candidate_page_ref_t self)
{
    if (r->owner_epoch == 0 || self.id == 0 || self.generation == 0) return false;
    if (r->count == 0 || r->count > r->page_highwater || self.id > r->page_highwater) return false;
    unsigned nodes = node_count(r->count);
    if (r->count < nodes + 1) return false;
    for (unsigned i = 0; i < nodes; i++) {
        candidate_page_ref_t ref = r->nodes[i];
        if (ref.id == 0 || ref.generation == 0 || ref.id == self.id || ref.id > r->page_highwater) return false;
        for (unsigned j = 0; j < i; j++) if (r->nodes[j].id == ref.id) return false;
    }
    return true;
}

static bool is_entry(const candidate_allocation_entry_t *e)
{
    if (e->ref.id == 0 || e->ref.generation == 0 || e->transition_lsn % 8 != 0) return false;
    if (e->state == CANDIDATE_ALLOCATION_LIVE) return e->release_horizon == 0;
    return e->state == CANDIDATE_ALLOCATION_FREE && e->release_horizon > 0;
}

candidate_wal_status_t candidate_allocation_encode_root(const candidate_page_location_t *location,
    const candidate_allocation_root_t *root, uint8_t *out, size_t capacity)
{
    if (location == NULL || root == NULL) return CANDIDATE_WAL_INVALID;
    if (root->count > CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
    if (!is_root(root, location->ref)) return CANDIDATE_WAL_INVALID;
    uint8_t body[40 + CANDIDATE_ALLOCATION_NODES * 16] = {0};
    memcpy(body, "SLAL", 4); write_le(body + 4, 1, 2); write_le(body + 6, 1, 2);
    write_le(body + 8, root->owner_epoch, 8); write_le(body + 16, root->page_highwater, 8);
    write_le(body + 24, root->gc_horizon, 8); write_le(body + 32, root->count, 4);
    unsigned nodes = node_count(root->count);
    for (unsigned i = 0; i < nodes; i++) {
        write_le(body + 40 + i * 16, root->nodes[i].id, 8);
        write_le(body + 48 + i * 16, root->nodes[i].generation, 8);
    }
    return candidate_page_encode_body(location, 5, body, 40 + nodes * 16, out, capacity);
}

candidate_wal_status_t candidate_allocation_encode_node(const candidate_page_location_t *location,
    const candidate_allocation_node_t *node, uint8_t *out, size_t capacity)
{
    if (node == NULL || node->count == 0 || node->ordinal >= CANDIDATE_ALLOCATION_NODES) return CANDIDATE_WAL_INVALID;
    if (node->count > CANDIDATE_ALLOCATION_NODE_ENTRIES) return CANDIDATE_WAL_LIMIT;
    uint8_t body[32 + CANDIDATE_ALLOCATION_NODE_ENTRIES * 40] = {0};
    memcpy(body, "SLAL", 4); write_le(body + 4, 1, 2); write_le(body + 6, 2, 2);
    write_le(body + 8, node->ordinal, 4); write_le(body + 12, node->count, 4);
    for (unsigned i = 0; i < node->count; i++) {
        const candidate_allocation_entry_t *e = &node->entries[i];
        if (!is_entry(e) || (i > 0 && e->ref.id <= node->entries[i - 1].ref.id)) return CANDIDATE_WAL_INVALID;
        uint8_t *p = body + 32 + i * 40;
        write_le(p, e->ref.id, 8); write_le(p + 8, e->ref.generation, 8); write_le(p + 16, e->state, 4);
        write_le(p + 24, e->release_horizon, 8); write_le(p + 32, e->transition_lsn, 8);
    }
    return candidate_page_encode_body(location, 5, body, 32 + node->count * 40, out, capacity);
}

static candidate_wal_status_t decode_header(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, unsigned kind, candidate_page_info_t *info)
{
    candidate_wal_status_t s = candidate_page_decode_header(uuid, bytes, size, info);
    if (s != CANDIDATE_WAL_OK) return s;
    if (info->type != 5 || info->body_bytes < 8) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = bytes + 64;
    if (memcmp(p, "SLAL", 4) != 0 || read_le(p + 4, 2) != 1 || read_le(p + 6, 2) != kind)
        return CANDIDATE_WAL_UNSUPPORTED;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_allocation_decode_root(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_allocation_root_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t s = decode_header(uuid, bytes, size, 1, &info);
    if (s != CANDIDATE_WAL_OK) return s;
    if (info.body_bytes < 40) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = bytes + 64;
    if (!is_zero(p + 36, 4)) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_allocation_root_t r = {.owner_epoch = read_le(p + 8, 8), .page_highwater = read_le(p + 16, 8),
        .gc_horizon = read_le(p + 24, 8), .count = (uint32_t)read_le(p + 32, 4)};
    if (r.count > CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
    unsigned nodes = node_count(r.count);
    if (info.body_bytes != 40 + nodes * 16) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < nodes; i++)
        r.nodes[i] = (candidate_page_ref_t){read_le(p + 40 + i * 16, 8), read_le(p + 48 + i * 16, 8)};
    if (!is_root(&r, info.location.ref)) return CANDIDATE_WAL_CORRUPT;
    *out = r; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_allocation_decode_node(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_allocation_node_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t s = decode_header(uuid, bytes, size, 2, &info);
    if (s != CANDIDATE_WAL_OK) return s;
    if (info.body_bytes < 32) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = bytes + 64;
    if (!is_zero(p + 16, 16)) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_allocation_node_t n = {.ordinal = (uint32_t)read_le(p + 8, 4), .count = (uint32_t)read_le(p + 12, 4)};
    if (n.ordinal >= CANDIDATE_ALLOCATION_NODES || n.count == 0 || n.count > CANDIDATE_ALLOCATION_NODE_ENTRIES)
        return CANDIDATE_WAL_CORRUPT;
    if (info.body_bytes != 32 + n.count * 40) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < n.count; i++) {
        const uint8_t *e = p + 32 + i * 40;
        if (!is_zero(e + 20, 4)) return CANDIDATE_WAL_UNSUPPORTED;
        n.entries[i] = (candidate_allocation_entry_t){.ref = {read_le(e, 8), read_le(e + 8, 8)},
            .state = (candidate_allocation_state_t)read_le(e + 16, 4),
            .release_horizon = read_le(e + 24, 8), .transition_lsn = read_le(e + 32, 8)};
        if (!is_entry(&n.entries[i]) || (i > 0 && n.entries[i].ref.id <= n.entries[i - 1].ref.id))
            return CANDIDATE_WAL_CORRUPT;
    }
    *out = n; return CANDIDATE_WAL_OK;
}
