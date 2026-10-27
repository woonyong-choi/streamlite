#include "candidate_mvcc_internal.h"

#include <string.h>
#include <unistd.h>

static candidate_wal_status_t check_fence(const candidate_mvcc_t *m)
{
    return m->fence == CANDIDATE_WAL_OK ? CANDIDATE_WAL_OK : CANDIDATE_WAL_POISONED;
}

static candidate_wal_status_t check_key(const candidate_mvcc_t *m, candidate_mvcc_key_t key)
{
    if (key.table_id == 0 || key.row_id == 0 || key.table_id == m->result_table_id) return CANDIDATE_WAL_INVALID;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t resolve_view(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, mvcc_view_t *view)
{
    candidate_wal_status_t status = check_fence(m);
    if (status != CANDIDATE_WAL_OK) return status;
    if (txn != NULL) {
        mvcc_active_t *a;
        status = mvcc_find_active(m,txn,&a);
        if (a != NULL) *view = (mvcc_view_t){m->table->outcomes[a->outcome].outcome.transaction_id,a->snapshot_csn};
        return status;
    }
    const mvcc_reader_t *r = mvcc_find_reader(m,reader,&status);
    if (r != NULL) *view = (mvcc_view_t){0,r->csn};
    return status;
}

static candidate_wal_status_t read_view(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    mvcc_view_t view = {0};
    mvcc_state_lock(m);
    candidate_wal_status_t status = resolve_view(m,txn,reader,&view);
    mvcc_state_unlock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_store_entry_t entry;
    status = mvcc_lookup(m,key,&entry);
    if (status == CANDIDATE_WAL_OK) status = mvcc_find_visible(m,&entry,view,out);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_view_t current = {0};
    mvcc_state_lock(m);
    status = resolve_view(m,txn,reader,&current);
    mvcc_state_unlock(m);
    return status;
}

static candidate_wal_status_t read_locked(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_value_t){0};
    if (m == NULL || (txn == NULL && reader == NULL)) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = check_key(m,key);
    if (status != CANDIDATE_WAL_OK) return status;
    status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_mvcc_value_t value;
    status = read_view(m,txn,reader,key,&value);
    mvcc_gate_unlock(m);
    if (status == CANDIDATE_WAL_OK) *out = value;
    return status;
}

candidate_wal_status_t candidate_mvcc_read(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    return txn == NULL ? CANDIDATE_WAL_INVALID : read_locked(m,txn,NULL,key,out);
}

candidate_wal_status_t candidate_mvcc_reader_read(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader,
    candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    return reader == NULL ? CANDIDATE_WAL_INVALID : read_locked(m,NULL,reader,key,out);
}

candidate_wal_status_t candidate_mvcc_reader_open(candidate_mvcc_t *m, candidate_mvcc_reader_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_reader_t){0};
    if (m == NULL) return CANDIDATE_WAL_INVALID;
    if (m->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    mvcc_state_lock(m);
    mvcc_table_t *t = m->table;
    candidate_wal_status_t status = check_fence(m);
    if (status == CANDIDATE_WAL_OK && t->reader_count == CANDIDATE_MVCC_READERS) status = CANDIDATE_WAL_LIMIT;
    if (status == CANDIDATE_WAL_OK) {
        uint32_t slot = 0;
        while (t->readers[slot].generation != 0) slot++;
        t->readers[slot] = (mvcc_reader_t){++m->next_generation,m->visible_csn};
        t->reader_count++;
        *out = (candidate_mvcc_reader_t){m->owner_id,t->readers[slot].generation,slot};
    }
    mvcc_state_unlock(m);
    return status;
}

candidate_wal_status_t candidate_mvcc_reader_close(candidate_mvcc_t *m, candidate_mvcc_reader_t *reader)
{
    if (m == NULL || reader == NULL) return CANDIDATE_WAL_INVALID;
    if (m->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    candidate_wal_status_t status;
    mvcc_state_lock(m);
    mvcc_reader_t *r = mvcc_find_reader(m,reader,&status);
    if (r != NULL) { *r = (mvcc_reader_t){0}; m->table->reader_count--; *reader = (candidate_mvcc_reader_t){0}; }
    mvcc_state_unlock(m);
    return status;
}

static candidate_catalog_lookup_t lookup(const candidate_mvcc_t *m, const mvcc_plan_t *plan, mvcc_outcome_t *out)
{
    for (uint32_t i = 0; i < m->table->outcome_count; i++) {
        const mvcc_outcome_t *o = &m->table->outcomes[i];
        if (o->outcome.epoch != m->request_epoch || memcmp(o->outcome.nonce,plan->nonce,16) != 0) continue;
        if (memcmp(o->outcome.operation_digest,plan->digest,32) != 0) return CANDIDATE_CATALOG_CONTENT_MISMATCH;
        *out = *o;
        return CANDIDATE_CATALOG_MATCH;
    }
    return CANDIDATE_CATALOG_NOT_FOUND;
}

static candidate_wal_status_t query(candidate_mvcc_t *m, const mvcc_plan_t *plan, candidate_mvcc_answer_t *out)
{
    mvcc_outcome_t found = {0};
    mvcc_state_lock(m);
    candidate_wal_status_t status = check_fence(m);
    if (status == CANDIDATE_WAL_OK) out->lookup = lookup(m,plan,&found);
    mvcc_state_unlock(m);
    if (status != CANDIDATE_WAL_OK || out->lookup != CANDIDATE_CATALOG_MATCH) return status;
    candidate_mvcc_response_t *r = &out->response;
    *r = (candidate_mvcc_response_t){.state = found.state,.transaction_id = found.outcome.transaction_id,
        .csn = found.outcome.csn,.terminal_end_lsn = found.outcome.terminal_end_lsn};
    bool has_body = found.state == CANDIDATE_MVCC_COMMITTED || found.state == CANDIDATE_MVCC_ABORTED;
    return has_body ? mvcc_observe_pool(m,mvcc_read_response(m,&found.outcome,r)) : CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_query(candidate_mvcc_t *m, const candidate_mvcc_request_t *request,
    candidate_mvcc_answer_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_answer_t){.lookup = CANDIDATE_CATALOG_UNAVAILABLE};
    if (m == NULL || request == NULL) return CANDIDATE_WAL_INVALID;
    mvcc_plan_t plan;
    candidate_wal_status_t status = mvcc_plan_copy(m,request,&plan);
    if (status != CANDIDATE_WAL_OK) return status;
    status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_mvcc_answer_t answer = {.lookup = CANDIDATE_CATALOG_UNAVAILABLE};
    status = query(m,&plan,&answer);
    mvcc_gate_unlock(m);
    if (status == CANDIDATE_WAL_OK) *out = answer;
    return status;
}

static candidate_wal_status_t store_info(candidate_mvcc_t *m, candidate_mvcc_store_info_t *out)
{
    mvcc_store_t store;
    candidate_wal_status_t status = mvcc_load_store(m,&store);
    uint64_t lsn = 0;
    if (status == CANDIDATE_WAL_OK) status = mvcc_load_node(m,store.descriptor.root,&m->work->node,&lsn);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = (candidate_mvcc_store_info_t){.descriptor = store.descriptor,.descriptor_lsn = store.lsn,
        .root_count = m->work->node.count,.root_level = m->work->node.level};
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_store_info(candidate_mvcc_t *m, candidate_mvcc_store_info_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_store_info_t){0};
    if (m == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    status = check_fence(m);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = store_info(m,out);
    mvcc_gate_unlock(m);
    return status;
}
