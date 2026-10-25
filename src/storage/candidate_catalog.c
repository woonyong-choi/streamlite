#include "storage/candidate_catalog.h"

#include <stdlib.h>
#include <string.h>

#define INDEX_SLOTS 8192u

static const uint8_t empty_digest[32] = {
    0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
    0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};

struct candidate_catalog {
    candidate_wal_memory_t memory;
    candidate_page_location_t location;
    candidate_catalog_root_t root;
    candidate_outcome_t entries[CANDIDATE_CATALOG_ENTRIES];
    uint32_t request_index[INDEX_SLOTS], csn_index[INDEX_SLOTS];
};

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0; for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (i * 8); return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static bool is_ref(candidate_page_ref_t ref)
{
    return (ref.id == 0 && ref.generation == 0) || (ref.id > 0 && ref.generation > 0);
}

static bool is_result_ref(candidate_version_ref_t ref)
{
    if (!is_ref(ref.page)) return false;
    if (ref.page.id == 0) return ref.slot == 0 && ref.slot_generation == 0;
    return ref.slot < CANDIDATE_PAGE_MAX_SLOTS && ref.slot_generation > 0;
}

static unsigned page_count(uint32_t count)
{
    return (count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
}

static bool is_outcome(const candidate_outcome_t *e)
{
    if (e->epoch == 0 || e->transaction_id == 0 || e->admitting_owner_epoch == 0) return false;
    if (e->state < CANDIDATE_OUTCOME_ADMITTED || e->state > CANDIDATE_OUTCOME_ABORTED) return false;
    if (!is_result_ref(e->result) || e->declared_result_limit > CANDIDATE_RESULT_MAX_BYTES) return false;
    if (e->result_bytes > e->declared_result_limit) return false;
    if (e->result_chunks != (e->result_bytes + 65535) / 65536) return false;
    if (e->state == CANDIDATE_OUTCOME_ADMITTED) {
        if (e->csn != 0 || e->terminal_end_lsn != 0 || e->result.page.id != 0) return false;
        return e->result_bytes == 0 && is_zero(e->result_digest, 32);
    }
    if (e->terminal_end_lsn == 0 || e->terminal_end_lsn % 8 != 0) return false;
    if (e->state == CANDIDATE_OUTCOME_COMMITTED) {
        if (e->csn == 0 || e->result.page.id == 0) return false;
    } else if (e->csn != 0) return false;
    if (e->result_bytes > 0 && e->result.page.id == 0) return false;
    return e->result_bytes != 0 || memcmp(e->result_digest, empty_digest, 32) == 0;
}

static bool is_root(const candidate_catalog_root_t *root, candidate_page_ref_t self)
{
    if (root->owner_epoch == 0 || root->current_epoch == 0 || root->retention_floor == 0) return false;
    if (root->retention_floor > root->current_epoch) return false;
    if (root->current_epoch - root->retention_floor >= 4) return false;
    if (root->count > CANDIDATE_CATALOG_ENTRIES || root->count > root->transaction_highwater) return false;
    if (self.id == 0 || self.generation == 0 || root->page_highwater < self.id) return false;
    unsigned pages = page_count(root->count);
    if (root->retained_bytes > CANDIDATE_CATALOG_STORAGE_BYTES) return false;
    if (root->retained_bytes < (uint64_t)(pages + 1) * CANDIDATE_PAGE_BYTES) return false;
    for (unsigned i = 0; i < pages; i++) {
        candidate_page_ref_t ref = root->pages[i];
        if (!is_ref(ref) || ref.id == 0 || ref.id == self.id) return false;
        if (ref.id > root->page_highwater) return false;
        for (unsigned j = 0; j < i; j++) if (root->pages[j].id == ref.id) return false;
    }
    return true;
}

static void encode_outcome(const candidate_outcome_t *e, uint8_t *p)
{
    write_le(p, e->epoch, 8); memcpy(p + 8, e->nonce, 16); memcpy(p + 24, e->operation_digest, 32);
    write_le(p + 56, e->transaction_id, 8); write_le(p + 64, e->state, 4);
    write_le(p + 72, e->csn, 8); write_le(p + 80, e->terminal_end_lsn, 8);
    write_le(p + 88, e->result.page.id, 8); write_le(p + 96, e->result.page.generation, 8);
    write_le(p + 104, e->result.slot, 4); write_le(p + 108, e->result.slot_generation, 4);
    write_le(p + 112, e->result_bytes, 8); memcpy(p + 120, e->result_digest, 32);
    write_le(p + 152, e->result_chunks, 4); write_le(p + 160, e->declared_result_limit, 8);
    write_le(p + 168, e->admitting_owner_epoch, 8);
}

candidate_wal_status_t candidate_catalog_encode_root(const candidate_page_location_t *location,
    const candidate_catalog_root_t *root, uint8_t *out, size_t capacity)
{
    if (location == NULL || root == NULL) return CANDIDATE_WAL_INVALID;
    if (!is_root(root, location->ref)) return CANDIDATE_WAL_INVALID;
    uint8_t body[72 + 16 * CANDIDATE_CATALOG_PAGES] = {0};
    memcpy(body, "SLMD", 4); write_le(body + 4, 1, 2); write_le(body + 6, 1, 2);
    write_le(body + 8, root->owner_epoch, 8); write_le(body + 16, root->current_epoch, 8);
    write_le(body + 24, root->retention_floor, 8); write_le(body + 32, root->transaction_highwater, 8);
    write_le(body + 40, root->page_highwater, 8); write_le(body + 48, root->visible_csn, 8);
    write_le(body + 56, root->retained_bytes, 8); write_le(body + 64, root->count, 4);
    unsigned pages = page_count(root->count);
    for (unsigned i = 0; i < pages; i++) {
        write_le(body + 72 + i * 16, root->pages[i].id, 8);
        write_le(body + 80 + i * 16, root->pages[i].generation, 8);
    }
    return candidate_page_encode_body(location, 4, body, 72 + pages * 16, out, capacity);
}

candidate_wal_status_t candidate_catalog_encode_node(const candidate_page_location_t *location,
    const candidate_catalog_node_t *node, uint8_t *out, size_t capacity)
{
    if (node == NULL) return CANDIDATE_WAL_INVALID;
    if (node->ordinal >= CANDIDATE_CATALOG_PAGES) return CANDIDATE_WAL_INVALID;
    if (node->count == 0 || node->count > CANDIDATE_CATALOG_NODE_ENTRIES) return CANDIDATE_WAL_INVALID;
    uint8_t body[32 + 176 * CANDIDATE_CATALOG_NODE_ENTRIES] = {0};
    memcpy(body, "SLMD", 4); write_le(body + 4, 1, 2); write_le(body + 6, 2, 2);
    write_le(body + 8, node->ordinal, 4); write_le(body + 12, node->count, 4);
    for (unsigned i = 0; i < node->count; i++) {
        if (!is_outcome(&node->entries[i])) return CANDIDATE_WAL_INVALID;
        encode_outcome(&node->entries[i], body + 32 + i * 176);
    }
    return candidate_page_encode_body(location, 4, body, 32 + node->count * 176, out, capacity);
}

static candidate_wal_status_t decode_header(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, unsigned kind, candidate_page_info_t *info)
{
    candidate_wal_status_t status = candidate_page_decode_header(uuid, bytes, size, info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info->type != 4) return CANDIDATE_WAL_UNSUPPORTED;
    if (info->body_bytes < 8) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(bytes + 64, "SLMD", 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (read_le(bytes + 68, 2) != 1 || read_le(bytes + 70, 2) != kind) return CANDIDATE_WAL_UNSUPPORTED;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_catalog_decode_root(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_catalog_root_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t status = decode_header(uuid, bytes, size, 1, &info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.body_bytes < 72) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = bytes + 64;
    if (read_le(p + 68, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_catalog_root_t root = {.owner_epoch = read_le(p + 8, 8), .current_epoch = read_le(p + 16, 8),
        .retention_floor = read_le(p + 24, 8), .transaction_highwater = read_le(p + 32, 8),
        .page_highwater = read_le(p + 40, 8), .visible_csn = read_le(p + 48, 8),
        .retained_bytes = read_le(p + 56, 8), .count = (uint32_t)read_le(p + 64, 4)};
    if (root.count > CANDIDATE_CATALOG_ENTRIES) return CANDIDATE_WAL_CORRUPT;
    unsigned pages = page_count(root.count);
    if (info.body_bytes != 72 + pages * 16) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < pages; i++)
        root.pages[i] = (candidate_page_ref_t){read_le(p + 72 + i * 16, 8), read_le(p + 80 + i * 16, 8)};
    if (!is_root(&root, info.location.ref)) return CANDIDATE_WAL_CORRUPT;
    *out = root; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t decode_outcome(const uint8_t *p, candidate_outcome_t *out)
{
    if (!is_zero(p + 68, 4) || !is_zero(p + 156, 4)) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t state = read_le(p + 64, 4);
    if (state < 1 || state > 3) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_outcome_t e = {.epoch = read_le(p, 8), .transaction_id = read_le(p + 56, 8),
        .state = (candidate_outcome_state_t)state, .csn = read_le(p + 72, 8), .terminal_end_lsn = read_le(p + 80, 8),
        .result = {{read_le(p + 88, 8),read_le(p + 96, 8)},(uint32_t)read_le(p + 104, 4),(uint32_t)read_le(p + 108, 4)},
        .result_bytes = read_le(p + 112, 8), .result_chunks = (uint32_t)read_le(p + 152, 4),
        .declared_result_limit = read_le(p + 160, 8), .admitting_owner_epoch = read_le(p + 168, 8)};
    memcpy(e.nonce, p + 8, 16); memcpy(e.operation_digest, p + 24, 32); memcpy(e.result_digest, p + 120, 32);
    if (!is_outcome(&e)) return CANDIDATE_WAL_CORRUPT;
    *out = e; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_catalog_decode_node(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_catalog_node_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t status = decode_header(uuid, bytes, size, 2, &info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.body_bytes < 32) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = bytes + 64;
    if (!is_zero(p + 16, 16)) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_catalog_node_t node = {.ordinal = (uint32_t)read_le(p + 8, 4), .count = (uint32_t)read_le(p + 12, 4)};
    if (node.ordinal >= CANDIDATE_CATALOG_PAGES || node.count == 0) return CANDIDATE_WAL_CORRUPT;
    if (node.count > CANDIDATE_CATALOG_NODE_ENTRIES) return CANDIDATE_WAL_CORRUPT;
    if (info.body_bytes != 32 + node.count * 176) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < node.count; i++) {
        status = decode_outcome(p + 32 + i * 176, &node.entries[i]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    *out = node; return CANDIDATE_WAL_OK;
}

static uint64_t request_hash(uint64_t epoch, const uint8_t nonce[16])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < 8; i++) hash = (hash ^ (uint8_t)(epoch >> (i * 8))) * UINT64_C(1099511628211);
    for (unsigned i = 0; i < 16; i++) hash = (hash ^ nonce[i]) * UINT64_C(1099511628211);
    return hash;
}

static bool insert_request(candidate_catalog_t *catalog, unsigned index)
{
    const candidate_outcome_t *e = &catalog->entries[index];
    unsigned slot = (unsigned)(request_hash(e->epoch, e->nonce) % INDEX_SLOTS);
    for (unsigned i = 0; i < INDEX_SLOTS; i++, slot = (slot + 1) % INDEX_SLOTS) {
        if (catalog->request_index[slot] == 0) { catalog->request_index[slot] = index + 1; return true; }
        const candidate_outcome_t *other = &catalog->entries[catalog->request_index[slot] - 1];
        if (other->epoch == e->epoch && memcmp(other->nonce, e->nonce, 16) == 0) return false;
    }
    return false;
}

static bool insert_csn(candidate_catalog_t *catalog, unsigned index)
{
    uint64_t csn = catalog->entries[index].csn;
    if (csn == 0) return true;
    unsigned slot = (unsigned)((csn ^ (csn >> 32)) % INDEX_SLOTS);
    for (unsigned i = 0; i < INDEX_SLOTS; i++, slot = (slot + 1) % INDEX_SLOTS) {
        if (catalog->csn_index[slot] == 0) { catalog->csn_index[slot] = index + 1; return true; }
        if (catalog->entries[catalog->csn_index[slot] - 1].csn == csn) return false;
    }
    return false;
}

static bool add_entry(candidate_catalog_t *c, const candidate_outcome_t *entry, unsigned index,
    unsigned epochs[4], uint64_t *bytes)
{
    if (entry->epoch < c->root.retention_floor || entry->epoch > c->root.current_epoch) return false;
    if (++epochs[entry->epoch - c->root.retention_floor] > 1024) return false;
    if (entry->transaction_id > c->root.transaction_highwater) return false;
    if (index > 0 && entry->transaction_id <= c->entries[index - 1].transaction_id) return false;
    if (entry->result.page.id > c->root.page_highwater) return false;
    if (entry->admitting_owner_epoch > c->root.owner_epoch || entry->csn > c->root.visible_csn) return false;
    *bytes += entry->result_bytes;
    if (*bytes > c->root.retained_bytes) return false;
    c->entries[index] = *entry;
    return insert_request(c, index) && insert_csn(c, index);
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static candidate_wal_status_t load_nodes(candidate_catalog_t *c, const candidate_catalog_load_t *r)
{
    unsigned epochs[4] = {0}, pages = page_count(c->root.count), index = 0;
    uint64_t bytes = (uint64_t)(pages + 1) * CANDIDATE_PAGE_BYTES;
    for (unsigned i = 0; i < pages; i++) {
        uint8_t data[CANDIDATE_PAGE_BYTES] = {0};
        candidate_page_ref_t ref = c->root.pages[i];
        candidate_wal_status_t status = r->load(r->context, ref, data);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_page_info_t info;
        status = candidate_page_decode_header(r->uuid, data, sizeof(data), &info);
        if (status != CANDIDATE_WAL_OK) return status;
        if (info.location.ref.id != ref.id || info.location.ref.generation != ref.generation) return CANDIDATE_WAL_CORRUPT;
        if (info.location.lsn > c->location.lsn) return CANDIDATE_WAL_CORRUPT;
        candidate_catalog_node_t node;
        status = candidate_catalog_decode_node(r->uuid, data, sizeof(data), &node);
        if (status != CANDIDATE_WAL_OK) return status;
        unsigned count = c->root.count - index;
        if (count > CANDIDATE_CATALOG_NODE_ENTRIES) count = CANDIDATE_CATALOG_NODE_ENTRIES;
        if (node.ordinal != i || node.count != count) return CANDIDATE_WAL_CORRUPT;
        for (unsigned j = 0; j < node.count; j++, index++)
            if (!add_entry(c, &node.entries[j], index, epochs, &bytes)) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_catalog_load(const candidate_catalog_load_t *request, candidate_catalog_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (request == NULL || request->load == NULL) return CANDIDATE_WAL_INVALID;
    candidate_catalog_load_t r = *request;
    if (r.root.id == 0 || r.root.generation == 0 || is_zero(r.uuid, 16)) return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (r.memory != NULL) memory = *r.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    uint8_t data[CANDIDATE_PAGE_BYTES] = {0};
    candidate_wal_status_t status = r.load(r.context, r.root, data);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_catalog_root_t root;
    status = candidate_catalog_decode_root(r.uuid, data, sizeof(data), &root);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info;
    status = candidate_page_decode_header(r.uuid, data, sizeof(data), &info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id != r.root.id || info.location.ref.generation != r.root.generation) return CANDIDATE_WAL_CORRUPT;
    candidate_catalog_t *catalog = memory.allocate(memory.context, sizeof(*catalog));
    if (catalog == NULL) return CANDIDATE_WAL_NOMEM;
    memset(catalog, 0, sizeof(*catalog)); catalog->memory = memory; catalog->root = root; catalog->location = info.location;
    status = load_nodes(catalog, &r);
    if (status != CANDIDATE_WAL_OK) { candidate_catalog_close(&catalog); return status; }
    *out = catalog; return CANDIDATE_WAL_OK;
}

candidate_catalog_lookup_t candidate_catalog_lookup(const candidate_catalog_t *c,
    const candidate_catalog_query_t *r, const candidate_outcome_t **out)
{
    if (out == NULL) return CANDIDATE_CATALOG_INVALID_ID;
    *out = NULL;
    if (c == NULL) return CANDIDATE_CATALOG_UNAVAILABLE;
    if (r == NULL) return CANDIDATE_CATALOG_INVALID_ID;
    if (memcmp(r->uuid, c->location.uuid, 16) != 0 || r->epoch == 0) return CANDIDATE_CATALOG_INVALID_ID;
    if (r->epoch > c->root.current_epoch) return CANDIDATE_CATALOG_INVALID_ID;
    if (r->epoch < c->root.retention_floor) return CANDIDATE_CATALOG_OUTSIDE_RETENTION;
    unsigned slot = (unsigned)(request_hash(r->epoch, r->nonce) % INDEX_SLOTS);
    for (unsigned i = 0; i < INDEX_SLOTS; i++, slot = (slot + 1) % INDEX_SLOTS) {
        unsigned index = c->request_index[slot];
        if (index == 0) return CANDIDATE_CATALOG_NOT_FOUND;
        const candidate_outcome_t *e = &c->entries[index - 1];
        if (e->epoch != r->epoch || memcmp(e->nonce, r->nonce, 16) != 0) continue;
        if (memcmp(e->operation_digest, r->operation_digest, 32) != 0) return CANDIDATE_CATALOG_CONTENT_MISMATCH;
        *out = e; return CANDIDATE_CATALOG_MATCH;
    }
    return CANDIDATE_CATALOG_NOT_FOUND;
}

const candidate_catalog_root_t *candidate_catalog_root(const candidate_catalog_t *c) { return c == NULL ? NULL : &c->root; }
const candidate_outcome_t *candidate_catalog_at(const candidate_catalog_t *c, uint32_t ordinal)
{
    return c == NULL || ordinal >= c->root.count ? NULL : &c->entries[ordinal];
}
size_t candidate_catalog_resident_bytes(const candidate_catalog_t *c) { return c == NULL ? 0 : sizeof(*c); }

void candidate_catalog_close(candidate_catalog_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_catalog_t *c = *out; candidate_wal_memory_t memory = c->memory;
    memory.release(memory.context, c, sizeof(*c)); *out = NULL;
}
