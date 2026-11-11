#include "candidate_mvcc_internal.h"

#include <string.h>

typedef struct {
    candidate_page_ref_t refs[CANDIDATE_STORE_HEIGHT + 1u];
    bool changed[CANDIDATE_STORE_HEIGHT];
    uint32_t fresh;
    uint64_t next_id;
    bool has_root;
    candidate_store_key_t separator;
} split_t;

static candidate_page_location_t location_of(const candidate_mvcc_t *m, candidate_page_ref_t ref, uint64_t lsn)
{
    candidate_page_location_t location = {.ref = ref,.lsn = lsn};
    memcpy(location.uuid,m->recovery->uuid,16);
    return location;
}

static candidate_wal_status_t encode_node(candidate_mvcc_t *m, uint32_t index, candidate_page_ref_t ref, uint64_t lsn,
    const candidate_store_node_t *node)
{
    mvcc_work_t *w = m->work;
    candidate_page_location_t location = location_of(m,ref,lsn);
    w->group[index] = (candidate_buffer_page_t){ref,w->pages[index]};
    return candidate_store_encode(&location,node,w->pages[index],CANDIDATE_PAGE_BYTES);
}

static candidate_wal_status_t encode_empty(candidate_mvcc_t *m, uint32_t index, candidate_page_ref_t ref)
{
    mvcc_work_t *w = m->work;
    candidate_page_location_t location = location_of(m,ref,0);
    w->group[index] = (candidate_buffer_page_t){ref,w->pages[index]};
    return candidate_page_encode_versions(&location,NULL,0,w->pages[index],CANDIDATE_PAGE_BYTES);
}

static uint64_t next_page(candidate_mvcc_t *m)
{
    return candidate_buffer_info(m->pool).page_highwater + 1;
}

static candidate_wal_status_t write_group(candidate_mvcc_t *m, uint32_t count, uint32_t allocated)
{
    candidate_buffer_structure_t structure = {m->work->group,count,allocated};
    candidate_buffer_admission_t receipt;
    candidate_wal_status_t status = mvcc_observe_pool(m,candidate_buffer_write_structure(m->pool,&structure,&receipt));
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m); m->structure_groups++; mvcc_state_unlock(m);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_create_store(candidate_mvcc_t *m, candidate_page_ref_t *out)
{
    candidate_store_node_t *n = &m->work->node;
    uint64_t first = next_page(m);
    candidate_page_ref_t descriptor = {first,1}, leaf = {first + 1,1}, append = {first + 2,1};
    memset(n,0,sizeof(*n));
    n->kind = CANDIDATE_STORE_DESCRIPTOR;
    n->descriptor = (candidate_store_descriptor_t){.root = leaf,.append = append,.height = 1,.result_table_id = m->result_table_id};
    candidate_wal_status_t status = encode_node(m,0,descriptor,0,n);
    memset(n,0,sizeof(*n));
    n->kind = CANDIDATE_STORE_LEAF;
    if (status == CANDIDATE_WAL_OK) status = encode_node(m,1,leaf,0,n);
    if (status == CANDIDATE_WAL_OK) status = encode_empty(m,2,append);
    if (status == CANDIDATE_WAL_OK) status = write_group(m,3,3);
    if (status == CANDIDATE_WAL_OK) *out = descriptor;
    return status;
}

static candidate_wal_status_t has_append_room(candidate_mvcc_t *m, const mvcc_store_t *store, const mvcc_active_t *a, bool *out)
{
    uint8_t *page = m->work->pages[MVCC_STRUCTURE_PAGES - 1u];
    mvcc_usage_t used;
    candidate_wal_status_t status = mvcc_observe_pool(m,mvcc_copy_page(m,store->descriptor.append,page));
    if (status == CANDIDATE_WAL_OK) status = mvcc_page_usage(m,page,&used);
    if (status != CANDIDATE_WAL_OK) return status;
    uint64_t bytes = MVCC_SLOT_BYTES + MVCC_VERSION_BYTES + CANDIDATE_MVCC_RESPONSE_BYTES;
    for (uint32_t i = 0; i < a->plan.count; i++) bytes += MVCC_SLOT_BYTES + MVCC_VERSION_BYTES + a->plan.writes[i].size;
    *out = used.slots + a->plan.count + 1 <= CANDIDATE_PAGE_MAX_SLOTS && used.body_bytes + bytes <= CANDIDATE_PAGE_BYTES - 64;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t grow_append(candidate_mvcc_t *m, const mvcc_store_t *store)
{
    candidate_store_node_t *n = &m->work->node;
    candidate_page_ref_t append = {next_page(m),1};
    memset(n,0,sizeof(*n));
    n->kind = CANDIDATE_STORE_DESCRIPTOR; n->descriptor = store->descriptor; n->descriptor.append = append;
    candidate_wal_status_t status = encode_node(m,0,m->store,store->lsn,n);
    if (status == CANDIDATE_WAL_OK) status = encode_empty(m,1,append);
    return status == CANDIDATE_WAL_OK ? write_group(m,2,1) : status;
}

static void leaf_bounds(const mvcc_path_t *path, const candidate_store_node_t *nodes, candidate_store_key_t *low,
    candidate_store_key_t *high, bool *has_high)
{
    *low = (candidate_store_key_t){0,0}; *has_high = false;
    for (uint32_t i = 0; i + 1 < path->height; i++) {
        const candidate_store_node_t *b = &nodes[i];
        uint32_t slot = path->slots[i];
        if (slot > 0) *low = b->children[slot].low;
        if (slot + 1 < b->count) { *high = b->children[slot + 1].low; *has_high = true; }
    }
}

static uint32_t count_added(const mvcc_active_t *a, const mvcc_path_t *path, const candidate_store_node_t *nodes)
{
    candidate_store_key_t low, high; bool has_high;
    leaf_bounds(path,nodes,&low,&high,&has_high);
    const candidate_store_node_t *leaf = &nodes[path->height - 1];
    uint32_t added = 0;
    for (uint32_t i = 0; i < a->plan.count; i++) {
        candidate_store_key_t key = mvcc_store_key(a->plan.writes[i].key);
        bool is_inside = candidate_store_compare(key,low) >= 0 && (!has_high || candidate_store_compare(key,high) < 0);
        uint32_t at;
        if (is_inside && !mvcc_leaf_find(leaf,key,&at)) added++;
    }
    return added;
}

static void split_leaf(candidate_mvcc_t *m, uint32_t level, split_t *s)
{
    mvcc_work_t *w = m->work;
    candidate_store_node_t *leaf = &w->nodes[level], *right = &w->fresh[s->fresh];
    uint32_t mid = leaf->count / 2;
    memset(right,0,sizeof(*right));
    right->kind = CANDIDATE_STORE_LEAF; right->count = leaf->count - mid;
    memcpy(right->entries,leaf->entries + mid,(size_t)right->count * sizeof(right->entries[0]));
    leaf->count = mid;
    s->changed[level] = true; s->separator = right->entries[0].key;
    s->refs[s->fresh++] = (candidate_page_ref_t){s->next_id++,1};
}

static bool insert_child(candidate_mvcc_t *m, const mvcc_path_t *path, uint32_t level, split_t *s)
{
    mvcc_work_t *w = m->work;
    candidate_store_node_t *parent = &w->nodes[level];
    candidate_store_child_t child = {s->separator,s->refs[s->fresh - 1]};
    uint32_t at = path->slots[level] + 1;
    s->changed[level] = true;
    if (parent->count < CANDIDATE_STORE_BRANCH_CHILDREN) {
        memmove(parent->children + at + 1,parent->children + at,(size_t)(parent->count - at) * sizeof(child));
        parent->children[at] = child; parent->count++;
        return true;
    }
    uint32_t total = parent->count + 1, mid = total / 2;
    memcpy(w->spill,parent->children,(size_t)at * sizeof(child));
    w->spill[at] = child;
    memcpy(w->spill + at + 1,parent->children + at,(size_t)(parent->count - at) * sizeof(child));
    memcpy(parent->children,w->spill,(size_t)mid * sizeof(child)); parent->count = mid;
    candidate_store_node_t *right = &w->fresh[s->fresh];
    memset(right,0,sizeof(*right));
    right->kind = CANDIDATE_STORE_BRANCH; right->level = parent->level; right->count = total - mid;
    memcpy(right->children,w->spill + mid,(size_t)right->count * sizeof(child));
    s->separator = right->children[0].low; right->children[0].low = (candidate_store_key_t){0,0};
    s->refs[s->fresh++] = (candidate_page_ref_t){s->next_id++,1};
    return false;
}

static void grow_root(candidate_mvcc_t *m, const mvcc_store_t *store, const mvcc_path_t *path, split_t *s)
{
    mvcc_work_t *w = m->work;
    candidate_store_node_t *root = &w->fresh[s->fresh];
    memset(root,0,sizeof(*root));
    root->kind = CANDIDATE_STORE_BRANCH; root->level = store->descriptor.height; root->count = 2;
    root->children[0] = (candidate_store_child_t){{0,0},path->refs[0]};
    root->children[1] = (candidate_store_child_t){s->separator,s->refs[s->fresh - 1]};
    s->refs[s->fresh++] = (candidate_page_ref_t){s->next_id++,1};
    memset(&w->node,0,sizeof(w->node));
    w->node.kind = CANDIDATE_STORE_DESCRIPTOR; w->node.descriptor = store->descriptor;
    w->node.descriptor.root = s->refs[s->fresh - 1]; w->node.descriptor.height++;
    s->has_root = true;
}

static candidate_wal_status_t write_split(candidate_mvcc_t *m, const mvcc_store_t *store, const mvcc_path_t *path, const split_t *s)
{
    mvcc_work_t *w = m->work;
    uint32_t n = 0;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < path->height; i++)
        if (s->changed[i]) status = encode_node(m,n++,path->refs[i],path->lsns[i],&w->nodes[i]);
    if (status == CANDIDATE_WAL_OK && s->has_root) status = encode_node(m,n++,m->store,store->lsn,&w->node);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < s->fresh; i++) status = encode_node(m,n++,s->refs[i],0,&w->fresh[i]);
    return status == CANDIDATE_WAL_OK ? write_group(m,n,s->fresh) : status;
}

static candidate_wal_status_t split(candidate_mvcc_t *m, const mvcc_store_t *store, const mvcc_path_t *path)
{
    split_t s = {.next_id = next_page(m)};
    uint32_t level = path->height - 1;
    split_leaf(m,level,&s);
    bool is_inserted = false;
    while (!is_inserted && level > 0) is_inserted = insert_child(m,path,--level,&s);
    if (!is_inserted && store->descriptor.height == CANDIDATE_STORE_HEIGHT) return CANDIDATE_WAL_LIMIT;
    if (!is_inserted) grow_root(m,store,path,&s);
    return write_split(m,store,path,&s);
}

static candidate_wal_status_t step(candidate_mvcc_t *m, const mvcc_active_t *a, bool *is_ready)
{
    mvcc_store_t store;
    bool has_room = false;
    candidate_wal_status_t status = mvcc_load_store(m,&store);
    if (status == CANDIDATE_WAL_OK) status = has_append_room(m,&store,a,&has_room);
    if (status != CANDIDATE_WAL_OK) return status;
    if (!has_room) return grow_append(m,&store);
    for (uint32_t i = 0; i < a->plan.count; i++) {
        mvcc_path_t path;
        status = mvcc_descend(m,&store,a->plan.writes[i].key,&path,m->work->nodes);
        if (status != CANDIDATE_WAL_OK) return status;
        uint32_t count = m->work->nodes[path.height - 1].count;
        if (count + count_added(a,&path,m->work->nodes) > CANDIDATE_STORE_LEAF_ENTRIES) return split(m,&store,&path);
    }
    *is_ready = true;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_prepare_structure(candidate_mvcc_t *m, const mvcc_active_t *a)
{
    for (uint32_t round = 0; round < MVCC_APPLY_GROUPS; round++) {
        bool is_ready = false;
        candidate_wal_status_t status = step(m,a,&is_ready);
        if (status != CANDIDATE_WAL_OK || is_ready) return status;
    }
    return CANDIDATE_WAL_LIMIT;
}
