#include "storage/candidate_store.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_allocation.h"

#define SEEN_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)
#define STACK_FRAMES (CANDIDATE_STORE_HEIGHT * CANDIDATE_STORE_BRANCH_CHILDREN)

typedef struct {
    candidate_page_ref_t ref;
    uint32_t level;
    candidate_store_key_t low, high;
    bool has_high;
} frame_t;
typedef struct {
    candidate_store_check_t request;
    candidate_store_info_t info;
    uint64_t seen[SEEN_SLOTS];
    uint32_t seen_count, depth;
    frame_t stack[STACK_FRAMES];
    candidate_page_ref_t cached;
    uint8_t page[CANDIDATE_PAGE_BYTES], versions[CANDIDATE_PAGE_BYTES];
    candidate_store_node_t node;
} walk_t;

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool is_same(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static candidate_wal_status_t mark_seen(walk_t *w, uint64_t id)
{
    uint64_t hash = id * UINT64_C(11400714819323198485);
    uint32_t slot = (uint32_t)(hash ^ (hash >> 32)) & (SEEN_SLOTS - 1);
    while (w->seen[slot] != 0) {
        if (w->seen[slot] == id) return CANDIDATE_WAL_CORRUPT;
        slot = (slot + 1) & (SEEN_SLOTS - 1);
    }
    if (w->seen_count == CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
    w->seen[slot] = id; w->seen_count++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t load_checked(walk_t *w, candidate_page_ref_t ref, uint8_t *bytes)
{
    candidate_wal_status_t status = w->request.load(w->request.context,ref,bytes);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info;
    status = candidate_page_decode_header(w->request.uuid,bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    return is_same(info.location.ref,ref) ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t load_node(walk_t *w, candidate_page_ref_t ref, candidate_store_node_t *out)
{
    candidate_wal_status_t status = load_checked(w,ref,w->page);
    if (status == CANDIDATE_WAL_OK) status = candidate_store_decode(w->request.uuid,w->page,CANDIDATE_PAGE_BYTES,out);
    if (status == CANDIDATE_WAL_OK && ref.id != w->request.descriptor.id) w->info.pages++;
    return status;
}

static candidate_wal_status_t load_version(walk_t *w, candidate_version_ref_t ref, candidate_page_version_t *out)
{
    if (!is_same(w->cached,ref.page)) {
        w->cached = (candidate_page_ref_t){0};
        candidate_wal_status_t status = load_checked(w,ref.page,w->versions);
        if (status != CANDIDATE_WAL_OK) return status;
        w->cached = ref.page;
    }
    return candidate_page_get_version(w->request.uuid,w->versions,CANDIDATE_PAGE_BYTES,ref,out);
}

static bool precedes(candidate_version_ref_t older, candidate_version_ref_t newer)
{
    if (older.page.id != newer.page.id) return older.page.id < newer.page.id;
    return older.page.generation == newer.page.generation && older.slot < newer.slot;
}

static candidate_wal_status_t check_chain(walk_t *w, const candidate_store_entry_t *e)
{
    candidate_version_ref_t ref = e->head;
    for (bool is_head = true; ref.page.id != 0; is_head = false) {
        candidate_page_version_t v;
        candidate_wal_status_t status = load_version(w,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        if (v.table_id != e->key.table_id || v.row_id != e->key.row_id) return CANDIDATE_WAL_CORRUPT;
        if (is_head && v.transaction_id != e->head_transaction) return CANDIDATE_WAL_CORRUPT;
        if (v.previous.page.id != 0 && !precedes(v.previous,ref)) return CANDIDATE_WAL_CORRUPT;
        w->info.versions++;
        ref = v.previous;
    }
    return CANDIDATE_WAL_OK;
}

static bool is_within(const frame_t *f, candidate_store_key_t key)
{
    if (candidate_store_compare(key,f->low) < 0) return false;
    return !f->has_high || candidate_store_compare(key,f->high) < 0;
}

static candidate_wal_status_t check_leaf(walk_t *w, const frame_t *f, const candidate_store_node_t *n)
{
    bool is_root = is_same(f->ref,w->info.descriptor.root);
    if (n->count == 0 && !is_root) return CANDIDATE_WAL_CORRUPT;
    w->info.leaves++; w->info.entries += n->count;
    for (uint32_t i = 0; i < n->count; i++) {
        const candidate_store_entry_t *e = &n->entries[i];
        if (!is_within(f,e->key)) return CANDIDATE_WAL_CORRUPT;
        if (e->head.page.id == 0) continue;
        w->info.heads++;
        candidate_wal_status_t status = check_chain(w,e);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t push_children(walk_t *w, const frame_t *f, const candidate_store_node_t *n)
{
    w->info.branches++;
    if (n->count > STACK_FRAMES - w->depth) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < n->count; i++) {
        frame_t child = {.ref = n->children[i].child,.level = f->level - 1,.low = i == 0 ? f->low : n->children[i].low,
            .high = f->high,.has_high = f->has_high};
        if (i > 0 && !is_within(f,n->children[i].low)) return CANDIDATE_WAL_CORRUPT;
        if (i > 0 && candidate_store_compare(n->children[i].low,f->low) == 0) return CANDIDATE_WAL_CORRUPT;
        if (i + 1 < n->count) { child.high = n->children[i + 1].low; child.has_high = true; }
        w->stack[w->depth++] = child;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t visit(walk_t *w, const frame_t *f)
{
    candidate_wal_status_t status = mark_seen(w,f->ref.id);
    if (status == CANDIDATE_WAL_OK) status = load_node(w,f->ref,&w->node);
    if (status != CANDIDATE_WAL_OK) return status;
    if (f->level == 0) return w->node.kind == CANDIDATE_STORE_LEAF ? check_leaf(w,f,&w->node) : CANDIDATE_WAL_CORRUPT;
    if (w->node.kind != CANDIDATE_STORE_BRANCH || w->node.level != f->level) return CANDIDATE_WAL_CORRUPT;
    return push_children(w,f,&w->node);
}

static candidate_wal_status_t check_append(walk_t *w)
{
    candidate_wal_status_t status = load_checked(w,w->info.descriptor.append,w->versions);
    candidate_page_info_t info;
    if (status == CANDIDATE_WAL_OK) status = candidate_page_decode_header(w->request.uuid,w->versions,CANDIDATE_PAGE_BYTES,&info);
    if (status == CANDIDATE_WAL_OK && info.type != 1) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = candidate_page_decode(w->request.uuid,w->versions,CANDIDATE_PAGE_BYTES,&info);
    if (status == CANDIDATE_WAL_OK) w->cached = w->info.descriptor.append;
    return status;
}

static candidate_wal_status_t walk(walk_t *w)
{
    candidate_wal_status_t status = mark_seen(w,w->request.descriptor.id);
    if (status == CANDIDATE_WAL_OK) status = load_node(w,w->request.descriptor,&w->node);
    if (status == CANDIDATE_WAL_OK && w->node.kind != CANDIDATE_STORE_DESCRIPTOR) status = CANDIDATE_WAL_CORRUPT;
    if (status != CANDIDATE_WAL_OK) return status;
    w->info.descriptor = w->node.descriptor; w->info.pages = 1;
    status = check_append(w);
    if (status != CANDIDATE_WAL_OK) return status;
    w->stack[w->depth++] = (frame_t){.ref = w->info.descriptor.root,.level = w->info.descriptor.height - 1};
    while (status == CANDIDATE_WAL_OK && w->depth > 0) {
        frame_t f = w->stack[--w->depth];
        status = visit(w,&f);
    }
    return status;
}

candidate_wal_status_t candidate_store_check(const candidate_store_check_t *request, candidate_store_info_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_store_info_t){0};
    if (request == NULL || request->load == NULL || request->descriptor.id == 0 || request->descriptor.generation == 0)
        return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    if (request->memory != NULL) memory = *request->memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    walk_t *w = memory.allocate(memory.context,sizeof(*w));
    if (w == NULL) return CANDIDATE_WAL_NOMEM;
    memset(w,0,sizeof(*w));
    w->request = *request;
    candidate_wal_status_t status = walk(w);
    if (status == CANDIDATE_WAL_OK) *out = w->info;
    memory.release(memory.context,w,sizeof(*w));
    return status;
}
