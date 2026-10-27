#include "candidate_mvcc_internal.h"

#include <string.h>

static const uint8_t *uuid_of(const candidate_mvcc_t *m) { return m->recovery->uuid; }

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static candidate_wal_status_t corrupt(candidate_mvcc_t *m)
{
    mvcc_state_lock(m); mvcc_fence(m,CANDIDATE_WAL_CORRUPT); mvcc_state_unlock(m);
    return CANDIDATE_WAL_CORRUPT;
}

candidate_store_key_t mvcc_store_key(candidate_mvcc_key_t key)
{
    return (candidate_store_key_t){key.table_id,key.row_id};
}

candidate_wal_status_t mvcc_load_node(candidate_mvcc_t *m, candidate_page_ref_t ref, candidate_store_node_t *out, uint64_t *lsn)
{
    uint8_t *page = m->work->node_page;
    candidate_wal_status_t status = mvcc_observe_pool(m,mvcc_copy_page(m,ref,page));
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info = {0};
    status = candidate_page_decode_header(uuid_of(m),page,CANDIDATE_PAGE_BYTES,&info);
    if (status == CANDIDATE_WAL_OK) status = candidate_store_decode(uuid_of(m),page,CANDIDATE_PAGE_BYTES,out);
    if (status != CANDIDATE_WAL_OK) return corrupt(m);
    if (!same_page(info.location.ref,ref)) return corrupt(m);
    *lsn = info.location.lsn;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_load_store(candidate_mvcc_t *m, mvcc_store_t *out)
{
    candidate_store_node_t *node = &m->work->node;
    candidate_wal_status_t status = mvcc_load_node(m,m->store,node,&out->lsn);
    if (status != CANDIDATE_WAL_OK) return status;
    if (node->kind != CANDIDATE_STORE_DESCRIPTOR) return corrupt(m);
    out->descriptor = node->descriptor;
    return CANDIDATE_WAL_OK;
}

static uint32_t pick_child(const candidate_store_node_t *branch, candidate_store_key_t key)
{
    uint32_t lo = 1, hi = branch->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (candidate_store_compare(branch->children[mid].low,key) <= 0) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

candidate_wal_status_t mvcc_descend(candidate_mvcc_t *m, const mvcc_store_t *store, candidate_mvcc_key_t key, mvcc_path_t *path,
    candidate_store_node_t *nodes)
{
    uint32_t height = store->descriptor.height;
    candidate_page_ref_t ref = store->descriptor.root;
    path->height = height;
    for (uint32_t i = 0; i < height; i++) {
        path->refs[i] = ref; path->slots[i] = 0;
        candidate_wal_status_t status = mvcc_load_node(m,ref,&nodes[i],&path->lsns[i]);
        if (status != CANDIDATE_WAL_OK) return status;
        const candidate_store_node_t *n = &nodes[i];
        bool is_leaf = i + 1 == height;
        if (is_leaf) return n->kind == CANDIDATE_STORE_LEAF ? CANDIDATE_WAL_OK : corrupt(m);
        if (n->kind != CANDIDATE_STORE_BRANCH || n->level != height - 1 - i) return corrupt(m);
        path->slots[i] = pick_child(n,mvcc_store_key(key));
        ref = n->children[path->slots[i]].child;
    }
    return corrupt(m);
}

bool mvcc_leaf_find(const candidate_store_node_t *leaf, candidate_store_key_t key, uint32_t *at)
{
    uint32_t lo = 0, hi = leaf->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (candidate_store_compare(leaf->entries[mid].key,key) < 0) lo = mid + 1; else hi = mid;
    }
    *at = lo;
    return lo < leaf->count && candidate_store_compare(leaf->entries[lo].key,key) == 0;
}

candidate_wal_status_t mvcc_lookup(candidate_mvcc_t *m, candidate_mvcc_key_t key, candidate_store_entry_t *out)
{
    *out = (candidate_store_entry_t){0};
    mvcc_store_t store; mvcc_path_t path;
    candidate_wal_status_t status = mvcc_load_store(m,&store);
    if (status == CANDIDATE_WAL_OK) status = mvcc_descend(m,&store,key,&path,m->work->nodes);
    if (status != CANDIDATE_WAL_OK) return status;
    const candidate_store_node_t *leaf = &m->work->nodes[path.height - 1];
    uint32_t at;
    if (mvcc_leaf_find(leaf,mvcc_store_key(key),&at)) *out = leaf->entries[at];
    return CANDIDATE_WAL_OK;
}

static bool precedes(candidate_version_ref_t older, candidate_version_ref_t newer)
{
    if (older.page.id != newer.page.id) return older.page.id < newer.page.id;
    return older.page.generation == newer.page.generation && older.slot < newer.slot;
}

static candidate_wal_status_t load_version(candidate_mvcc_t *m, const candidate_store_entry_t *entry, candidate_version_ref_t ref,
    candidate_page_version_t *out)
{
    mvcc_work_t *w = m->work;
    if (!same_page(w->chain_ref,ref.page)) {
        w->chain_ref = (candidate_page_ref_t){0};
        candidate_wal_status_t status = mvcc_observe_pool(m,mvcc_copy_page(m,ref.page,w->chain));
        if (status != CANDIDATE_WAL_OK) return status;
        w->chain_ref = ref.page;
    }
    candidate_wal_status_t status = candidate_page_get_version(uuid_of(m),w->chain,CANDIDATE_PAGE_BYTES,ref,out);
    if (status != CANDIDATE_WAL_OK) return corrupt(m);
    if (out->table_id != entry->key.table_id || out->row_id != entry->key.row_id) return corrupt(m);
    if (out->previous.page.id != 0 && !precedes(out->previous,ref)) return corrupt(m);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t fill_value(const candidate_page_version_t *v, candidate_version_ref_t ref, uint64_t csn,
    candidate_mvcc_value_t *out)
{
    *out = (candidate_mvcc_value_t){0};
    if (v->is_tombstone) return CANDIDATE_WAL_OK;
    if (v->overflow.id != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (v->payload_bytes > CANDIDATE_MVCC_PAYLOAD_BYTES) return CANDIDATE_WAL_LIMIT;
    *out = (candidate_mvcc_value_t){.is_found = true,.transaction_id = v->transaction_id,.csn = csn,.ref = ref,
        .size = (uint32_t)v->payload_bytes};
    if (out->size > 0) memcpy(out->bytes,v->inline_bytes,out->size);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_find_visible(candidate_mvcc_t *m, const candidate_store_entry_t *entry, mvcc_view_t view,
    candidate_mvcc_value_t *out)
{
    *out = (candidate_mvcc_value_t){0};
    m->work->chain_ref = (candidate_page_ref_t){0};
    for (candidate_version_ref_t ref = entry->head; ref.page.id != 0;) {
        candidate_page_version_t v;
        candidate_wal_status_t status = load_version(m,entry,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        uint64_t csn = 0;
        mvcc_state_lock(m);
        bool is_visible = mvcc_is_visible(m,&v,view,&csn);
        mvcc_state_unlock(m);
        if (is_visible) return fill_value(&v,ref,csn,out);
        ref = v.previous;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_find_head(candidate_mvcc_t *m, const candidate_store_entry_t *entry, uint64_t own_transaction,
    candidate_version_ref_t *ref, uint64_t *csn)
{
    *ref = (candidate_version_ref_t){0}; *csn = 0;
    m->work->chain_ref = (candidate_page_ref_t){0};
    mvcc_view_t committed = {.snapshot_csn = UINT64_MAX};
    for (candidate_version_ref_t at = entry->head; at.page.id != 0;) {
        candidate_page_version_t v;
        candidate_wal_status_t status = load_version(m,entry,at,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        uint64_t found = 0;
        mvcc_state_lock(m);
        bool is_pending = mvcc_is_pending_other(m,&v,own_transaction);
        bool is_committed = !is_pending && mvcc_is_visible(m,&v,committed,&found);
        mvcc_state_unlock(m);
        if (is_pending) return CANDIDATE_WAL_BUSY;
        if (is_committed) { *ref = at; *csn = found; return CANDIDATE_WAL_OK; }
        at = v.previous;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_chain(candidate_mvcc_t *m, const candidate_store_entry_t *entry)
{
    m->work->chain_ref = (candidate_page_ref_t){0};
    for (candidate_version_ref_t ref = entry->head; ref.page.id != 0;) {
        candidate_page_version_t v;
        candidate_wal_status_t status = load_version(m,entry,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        mvcc_state_lock(m);
        bool is_known = mvcc_find_outcome(m->table,v.transaction_id) != NULL || v.frozen_csn != 0;
        mvcc_state_unlock(m);
        if (!is_known) return CANDIDATE_WAL_UNSUPPORTED;
        ref = v.previous;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_node(candidate_mvcc_t *m, candidate_page_ref_t ref, uint32_t *depth)
{
    mvcc_work_t *w = m->work;
    uint64_t lsn;
    candidate_wal_status_t status = mvcc_load_node(m,ref,&w->node,&lsn);
    if (status != CANDIDATE_WAL_OK) return status;
    if (w->node.kind == CANDIDATE_STORE_BRANCH) {
        if (w->node.count > MVCC_TREE_STACK - *depth) return corrupt(m);
        for (uint32_t i = 0; i < w->node.count; i++) w->stack[(*depth)++] = w->node.children[i].child;
        return CANDIDATE_WAL_OK;
    }
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < w->node.count; i++) status = check_chain(m,&w->node.entries[i]);
    return status;
}

candidate_wal_status_t mvcc_check_store(candidate_mvcc_t *m)
{
    mvcc_store_t store;
    candidate_wal_status_t status = mvcc_load_store(m,&store);
    if (status != CANDIDATE_WAL_OK) return status;
    uint32_t depth = 0;
    m->work->stack[depth++] = store.descriptor.root;
    while (status == CANDIDATE_WAL_OK && depth > 0) status = check_node(m,m->work->stack[--depth],&depth);
    return status;
}
