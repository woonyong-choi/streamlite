#include "storage/candidate_store.h"

#include <string.h>

#define STORE_MAGIC "SLST"
#define STORE_VERSION 1u
#define HEADER_BYTES 16u
#define DESCRIPTOR_BYTES 56u
#define ENTRY_BYTES 48u
#define CHILD_BYTES 32u

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_ref(candidate_page_ref_t ref)
{
    return ref.id > 0 && ref.generation > 0;
}

static bool is_key(candidate_store_key_t key)
{
    return key.table_id > 0 && key.row_id > 0;
}

static bool is_head(const candidate_store_entry_t *e)
{
    const candidate_version_ref_t *h = &e->head;
    if (h->page.id == 0) return h->page.generation == 0 && h->slot == 0 && h->slot_generation == 0 && e->head_transaction == 0;
    return h->page.generation > 0 && h->slot < CANDIDATE_PAGE_MAX_SLOTS && h->slot_generation > 0 && e->head_transaction > 0;
}

int candidate_store_compare(candidate_store_key_t left, candidate_store_key_t right)
{
    if (left.table_id != right.table_id) return left.table_id < right.table_id ? -1 : 1;
    if (left.row_id != right.row_id) return left.row_id < right.row_id ? -1 : 1;
    return 0;
}

static bool is_descriptor(const candidate_store_descriptor_t *d, candidate_page_ref_t self)
{
    if (!is_ref(d->root) || !is_ref(d->append) || d->result_table_id == 0) return false;
    if (d->height == 0 || d->height > CANDIDATE_STORE_HEIGHT) return false;
    return d->root.id != d->append.id && d->root.id != self.id && d->append.id != self.id;
}

static bool is_leaf(const candidate_store_node_t *n)
{
    if (n->level != 0 || n->count > CANDIDATE_STORE_LEAF_ENTRIES) return false;
    for (uint32_t i = 0; i < n->count; i++) {
        const candidate_store_entry_t *e = &n->entries[i];
        if (!is_key(e->key) || !is_head(e)) return false;
        if (i > 0 && candidate_store_compare(n->entries[i - 1].key,e->key) >= 0) return false;
    }
    return true;
}

static bool is_branch(const candidate_store_node_t *n, candidate_page_ref_t self)
{
    if (n->level == 0 || n->level >= CANDIDATE_STORE_HEIGHT) return false;
    if (n->count < 2 || n->count > CANDIDATE_STORE_BRANCH_CHILDREN) return false;
    if (n->children[0].low.table_id != 0 || n->children[0].low.row_id != 0) return false;
    for (uint32_t i = 0; i < n->count; i++) {
        const candidate_store_child_t *c = &n->children[i];
        if (!is_ref(c->child) || c->child.id == self.id) return false;
        if (i > 0 && (!is_key(c->low) || candidate_store_compare(n->children[i - 1].low,c->low) >= 0)) return false;
        for (uint32_t j = 0; j < i; j++) if (n->children[j].child.id == c->child.id) return false;
    }
    return true;
}

static bool is_node(const candidate_store_node_t *n, candidate_page_ref_t self)
{
    if (n->kind == CANDIDATE_STORE_DESCRIPTOR) return n->count == 0 && n->level == 0 && is_descriptor(&n->descriptor,self);
    if (n->kind == CANDIDATE_STORE_LEAF) return is_leaf(n);
    return n->kind == CANDIDATE_STORE_BRANCH && is_branch(n,self);
}

static size_t body_bytes(const candidate_store_node_t *n)
{
    if (n->kind == CANDIDATE_STORE_DESCRIPTOR) return DESCRIPTOR_BYTES;
    return HEADER_BYTES + (size_t)n->count * (n->kind == CANDIDATE_STORE_LEAF ? ENTRY_BYTES : CHILD_BYTES);
}

static void encode_ref(uint8_t *p, candidate_page_ref_t ref)
{
    write_le(p,ref.id,8); write_le(p + 8,ref.generation,8);
}

static candidate_page_ref_t decode_ref(const uint8_t *p)
{
    return (candidate_page_ref_t){read_le(p,8),read_le(p + 8,8)};
}

static void encode_body(const candidate_store_node_t *n, uint8_t *body)
{
    memcpy(body,STORE_MAGIC,4); write_le(body + 4,STORE_VERSION,2); write_le(body + 6,n->kind,2);
    if (n->kind == CANDIDATE_STORE_DESCRIPTOR) {
        const candidate_store_descriptor_t *d = &n->descriptor;
        write_le(body + 8,d->height,4); encode_ref(body + 16,d->root); encode_ref(body + 32,d->append);
        write_le(body + 48,d->result_table_id,8);
        return;
    }
    write_le(body + 8,n->count,4); write_le(body + 12,n->level,4);
    for (uint32_t i = 0; i < n->count; i++) {
        if (n->kind == CANDIDATE_STORE_BRANCH) {
            uint8_t *c = body + HEADER_BYTES + (size_t)i * CHILD_BYTES;
            write_le(c,n->children[i].low.table_id,8); write_le(c + 8,n->children[i].low.row_id,8);
            encode_ref(c + 16,n->children[i].child);
            continue;
        }
        const candidate_store_entry_t *e = &n->entries[i];
        uint8_t *p = body + HEADER_BYTES + (size_t)i * ENTRY_BYTES;
        write_le(p,e->key.table_id,8); write_le(p + 8,e->key.row_id,8); encode_ref(p + 16,e->head.page);
        write_le(p + 32,e->head.slot,4); write_le(p + 36,e->head.slot_generation,4); write_le(p + 40,e->head_transaction,8);
    }
}

candidate_wal_status_t candidate_store_encode(const candidate_page_location_t *location,
    const candidate_store_node_t *node, uint8_t *out, size_t capacity)
{
    if (location == NULL || node == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    if (node->kind == CANDIDATE_STORE_LEAF && node->count > CANDIDATE_STORE_LEAF_ENTRIES) return CANDIDATE_WAL_LIMIT;
    if (node->kind == CANDIDATE_STORE_BRANCH && node->count > CANDIDATE_STORE_BRANCH_CHILDREN) return CANDIDATE_WAL_LIMIT;
    if (!is_node(node,location->ref)) return CANDIDATE_WAL_INVALID;
    uint8_t body[CANDIDATE_PAGE_BYTES - 64] = {0};
    encode_body(node,body);
    return candidate_page_encode_body(location,3,body,body_bytes(node),out,capacity);
}

static candidate_wal_status_t decode_descriptor(const uint8_t *body, uint32_t size, candidate_store_node_t *n)
{
    if (size != DESCRIPTOR_BYTES) return CANDIDATE_WAL_CORRUPT;
    if (read_le(body + 12,4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    n->descriptor = (candidate_store_descriptor_t){.root = decode_ref(body + 16),.append = decode_ref(body + 32),
        .height = (uint32_t)read_le(body + 8,4),.result_table_id = read_le(body + 48,8)};
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t decode_items(const uint8_t *body, uint32_t size, candidate_store_node_t *n)
{
    if (size < HEADER_BYTES) return CANDIDATE_WAL_CORRUPT;
    n->count = (uint32_t)read_le(body + 8,4); n->level = (uint32_t)read_le(body + 12,4);
    bool is_leaf_kind = n->kind == CANDIDATE_STORE_LEAF;
    uint32_t limit = is_leaf_kind ? CANDIDATE_STORE_LEAF_ENTRIES : CANDIDATE_STORE_BRANCH_CHILDREN;
    if (n->count > limit) return CANDIDATE_WAL_CORRUPT;
    if (size != body_bytes(n)) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < n->count; i++) {
        const uint8_t *p = body + HEADER_BYTES + (size_t)i * (is_leaf_kind ? ENTRY_BYTES : CHILD_BYTES);
        candidate_store_key_t key = {read_le(p,8),read_le(p + 8,8)};
        if (!is_leaf_kind) { n->children[i] = (candidate_store_child_t){key,decode_ref(p + 16)}; continue; }
        n->entries[i] = (candidate_store_entry_t){.key = key,.head = {decode_ref(p + 16),(uint32_t)read_le(p + 32,4),
            (uint32_t)read_le(p + 36,4)},.head_transaction = read_le(p + 40,8)};
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_store_decode(const uint8_t uuid[16], const uint8_t *bytes, size_t size,
    candidate_store_node_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode_header(uuid,bytes,size,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 3 || info.body_bytes < 8) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *body = bytes + 64;
    if (memcmp(body,STORE_MAGIC,4) != 0 || read_le(body + 4,2) != STORE_VERSION) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t kind = read_le(body + 6,2);
    if (kind < CANDIDATE_STORE_DESCRIPTOR || kind > CANDIDATE_STORE_BRANCH) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_store_node_t node = {.kind = (candidate_store_kind_t)kind};
    status = node.kind == CANDIDATE_STORE_DESCRIPTOR ? decode_descriptor(body,info.body_bytes,&node) :
        decode_items(body,info.body_bytes,&node);
    if (status != CANDIDATE_WAL_OK) return status;
    if (!is_node(&node,info.location.ref)) return CANDIDATE_WAL_CORRUPT;
    *out = node;
    return CANDIDATE_WAL_OK;
}
