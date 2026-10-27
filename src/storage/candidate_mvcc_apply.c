#include "candidate_mvcc_internal.h"

#include <string.h>

#define APPEND_INDEX 0u
#define BASE_INDEX (MVCC_STRUCTURE_PAGES - 1u)

typedef struct {
    candidate_page_ref_t refs[CANDIDATE_MVCC_WRITES], append;
    uint64_t lsns[CANDIDATE_MVCC_WRITES], append_lsn, own;
    uint32_t leaves, leaf_of[CANDIDATE_MVCC_WRITES], versions;
    candidate_version_ref_t heads[CANDIDATE_MVCC_WRITES], result;
    uint8_t response[CANDIDATE_MVCC_RESPONSE_BYTES];
} build_t;

static const uint8_t *uuid_of(const candidate_mvcc_t *m) { return m->recovery->uuid; }

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static candidate_wal_status_t collect_leaves(candidate_mvcc_t *m, const mvcc_active_t *a, const mvcc_store_t *store, build_t *b)
{
    mvcc_work_t *w = m->work;
    for (uint32_t i = 0; i < a->plan.count; i++) {
        mvcc_path_t path;
        candidate_wal_status_t status = mvcc_descend(m,store,a->plan.writes[i].key,&path,w->nodes);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_page_ref_t leaf = path.refs[path.height - 1];
        uint32_t at = 0;
        while (at < b->leaves && !same_page(b->refs[at],leaf)) at++;
        if (at == b->leaves) {
            b->refs[at] = leaf; b->lsns[at] = path.lsns[path.height - 1];
            w->fresh[at] = w->nodes[path.height - 1];
            b->leaves++;
        }
        b->leaf_of[i] = at;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t load_append(candidate_mvcc_t *m, const mvcc_store_t *store, build_t *b)
{
    mvcc_work_t *w = m->work;
    uint8_t *base = w->pages[BASE_INDEX];
    b->append = store->descriptor.append;
    candidate_wal_status_t status = mvcc_observe_pool(m,mvcc_copy_page(m,b->append,base));
    candidate_page_info_t info;
    if (status == CANDIDATE_WAL_OK) status = candidate_page_decode(uuid_of(m),base,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 1 || !same_page(info.location.ref,b->append)) return CANDIDATE_WAL_CORRUPT;
    b->append_lsn = info.location.lsn; b->versions = info.slots;
    for (uint32_t i = 0; i < info.slots; i++) {
        candidate_version_ref_t ref;
        status = candidate_page_version_at(uuid_of(m),base,CANDIDATE_PAGE_BYTES,i,&ref,&w->versions[i]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_version(candidate_mvcc_t *m, build_t *b, const candidate_page_version_t *v,
    candidate_version_ref_t *ref)
{
    if (b->versions == CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_LIMIT;
    m->work->versions[b->versions] = *v;
    *ref = (candidate_version_ref_t){b->append,b->versions,v->slot_generation};
    b->versions++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t set_head(candidate_store_node_t *leaf, candidate_store_key_t key, candidate_version_ref_t head,
    uint64_t own)
{
    uint32_t at;
    candidate_store_entry_t entry = {.key = key,.head = head,.head_transaction = own};
    if (mvcc_leaf_find(leaf,key,&at)) { leaf->entries[at] = entry; return CANDIDATE_WAL_OK; }
    if (leaf->count == CANDIDATE_STORE_LEAF_ENTRIES) return CANDIDATE_WAL_LIMIT;
    memmove(leaf->entries + at + 1,leaf->entries + at,(size_t)(leaf->count - at) * sizeof(entry));
    leaf->entries[at] = entry; leaf->count++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_write(candidate_mvcc_t *m, const mvcc_active_t *a, build_t *b, uint32_t index)
{
    const mvcc_write_t *write = &a->plan.writes[index];
    candidate_store_node_t *leaf = &m->work->fresh[b->leaf_of[index]];
    candidate_store_key_t key = mvcc_store_key(write->key);
    candidate_version_ref_t previous = {0};
    uint64_t csn = 0;
    uint32_t at;
    if (mvcc_leaf_find(leaf,key,&at)) {
        candidate_wal_status_t status = mvcc_find_head(m,&leaf->entries[at],b->own,&previous,&csn);
        if (status != CANDIDATE_WAL_OK) return status;
        if (csn > a->snapshot_csn) return CANDIDATE_WAL_STALE;
    }
    candidate_page_version_t v = {.transaction_id = b->own,.previous = previous,.table_id = key.table_id,.row_id = key.row_id,
        .payload_bytes = write->size,.slot_generation = 1,.inline_bytes = write->size > 0 ? write->bytes : NULL};
    candidate_wal_status_t status = add_version(m,b,&v,&b->heads[index]);
    return status == CANDIDATE_WAL_OK ? set_head(leaf,key,b->heads[index],b->own) : status;
}

static candidate_wal_status_t add_result(candidate_mvcc_t *m, const mvcc_active_t *a, build_t *b)
{
    mvcc_encode_response(b->own,a->plan.count,b->response);
    candidate_page_version_t v = {.transaction_id = b->own,.table_id = m->result_table_id,.row_id = b->own,
        .payload_bytes = sizeof(b->response),.slot_generation = 1,.inline_bytes = b->response};
    return add_version(m,b,&v,&b->result);
}

static candidate_wal_status_t encode_group(candidate_mvcc_t *m, build_t *b)
{
    mvcc_work_t *w = m->work;
    candidate_page_location_t location = {.ref = b->append,.lsn = b->append_lsn};
    memcpy(location.uuid,uuid_of(m),16);
    w->group[APPEND_INDEX] = (candidate_buffer_page_t){b->append,w->pages[APPEND_INDEX]};
    candidate_wal_status_t status = candidate_page_encode_versions(&location,w->versions,b->versions,w->pages[APPEND_INDEX],
        CANDIDATE_PAGE_BYTES);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < b->leaves; i++) {
        location.ref = b->refs[i]; location.lsn = b->lsns[i];
        w->group[1 + i] = (candidate_buffer_page_t){b->refs[i],w->pages[1 + i]};
        status = candidate_store_encode(&location,&w->fresh[i],w->pages[1 + i],CANDIDATE_PAGE_BYTES);
    }
    return status;
}

static candidate_wal_status_t build(candidate_mvcc_t *m, const mvcc_active_t *a, build_t *b)
{
    mvcc_store_t store;
    candidate_wal_status_t status = mvcc_load_store(m,&store);
    if (status == CANDIDATE_WAL_OK) status = collect_leaves(m,a,&store,b);
    if (status == CANDIDATE_WAL_OK) status = load_append(m,&store,b);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < a->plan.count; i++) status = add_write(m,a,b,i);
    if (status == CANDIDATE_WAL_OK) status = add_result(m,a,b);
    return status == CANDIDATE_WAL_OK ? encode_group(m,b) : status;
}

static void record_group(candidate_mvcc_t *m, mvcc_active_t *a, const build_t *b, const candidate_buffer_admission_t *receipt)
{
    mvcc_state_lock(m);
    a->chain.last_lsn = receipt->transaction_last_lsn; a->chain.records = receipt->transaction_records;
    a->chain.crc = receipt->transaction_chain_crc;
    a->group_id = receipt->group_id; a->group_end = receipt->end_lsn; a->is_applied = true;
    memcpy(a->versions,b->heads,sizeof(a->versions)); a->result = b->result;
    uint64_t remaining = mvcc_terminal_bytes();
    if (a->wal_credit > remaining) a->wal_credit = remaining;
    mvcc_state_unlock(m);
}

static candidate_wal_status_t write_user_group(candidate_mvcc_t *m, mvcc_active_t *a)
{
    build_t b;
    memset(&b,0,sizeof(b));
    b.own = m->table->outcomes[a->outcome].outcome.transaction_id;
    candidate_wal_status_t status = build(m,a,&b);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_buffer_group_t group = {b.own,a->chain.last_lsn,m->work->group,1 + b.leaves};
    candidate_buffer_admission_t receipt;
    status = candidate_buffer_write_group(m->pool,&group,&receipt);
    if (status != CANDIDATE_WAL_OK && candidate_buffer_info(m->pool).is_fenced) return mvcc_settle_unknown(m,a,status);
    if (status != CANDIDATE_WAL_OK) return status;
    record_group(m,a,&b,&receipt);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_apply(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn)
{
    mvcc_active_t *a;
    mvcc_state_lock(m);
    candidate_wal_status_t status = mvcc_find_active(m,txn,&a);
    if (a != NULL && a->is_applied) status = CANDIDATE_WAL_INVALID;
    mvcc_state_unlock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = mvcc_prepare_structure(m,a);
    if (status != CANDIDATE_WAL_OK && candidate_buffer_info(m->pool).is_fenced) return mvcc_settle_unknown(m,a,status);
    if (status != CANDIDATE_WAL_OK) return status;
    return write_user_group(m,a);
}
