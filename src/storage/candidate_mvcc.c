#include "candidate_mvcc_internal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "candidate_buffer_internal.h"

candidate_wal_status_t mvcc_gate_lock(candidate_mvcc_t *m)
{
    if (m->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    int error = pthread_mutex_lock(&m->io_gate);
    if (error == EDEADLK) return CANDIDATE_WAL_BUSY;
    if (error != 0) return CANDIDATE_WAL_INVALID;
    __atomic_add_fetch(&m->gate_entries,1,__ATOMIC_RELAXED);
    return CANDIDATE_WAL_OK;
}

void mvcc_gate_unlock(candidate_mvcc_t *m) { (void)pthread_mutex_unlock(&m->io_gate); }

void mvcc_state_lock(candidate_mvcc_t *m) { (void)pthread_mutex_lock(&m->state_lock); }

void mvcc_state_unlock(candidate_mvcc_t *m) { (void)pthread_mutex_unlock(&m->state_lock); }

candidate_wal_status_t mvcc_fence(candidate_mvcc_t *m, candidate_wal_status_t status)
{
    if (m->fence == CANDIDATE_WAL_OK) m->fence = status;
    return status;
}

candidate_wal_status_t mvcc_observe_pool(candidate_mvcc_t *m, candidate_wal_status_t status)
{
    if (status == CANDIDATE_WAL_OK || !candidate_buffer_info(m->pool).is_fenced) return status;
    mvcc_state_lock(m); mvcc_fence(m,status); mvcc_state_unlock(m);
    return status;
}

const mvcc_outcome_t *mvcc_find_outcome(const mvcc_table_t *table, uint64_t transaction_id)
{
    uint32_t lo = 0, hi = table->outcome_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (table->outcomes[mid].outcome.transaction_id < transaction_id) lo = mid + 1; else hi = mid;
    }
    if (lo < table->outcome_count && table->outcomes[lo].outcome.transaction_id == transaction_id) return &table->outcomes[lo];
    return NULL;
}

static candidate_wal_status_t check_fence(const candidate_mvcc_t *m)
{
    return m->fence == CANDIDATE_WAL_OK ? CANDIDATE_WAL_OK : CANDIDATE_WAL_POISONED;
}

candidate_wal_status_t mvcc_find_active(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn, mvcc_active_t **out)
{
    *out = NULL;
    candidate_wal_status_t status = check_fence(m);
    if (status != CANDIDATE_WAL_OK) return status;
    if (txn == NULL || txn->owner_id != m->owner_id || txn->slot >= CANDIDATE_MVCC_TRANSACTIONS) return CANDIDATE_WAL_INVALID;
    mvcc_active_t *a = &m->table->active[txn->slot];
    if (a->generation == 0 || a->generation != txn->generation) return CANDIDATE_WAL_STALE;
    *out = a;
    return CANDIDATE_WAL_OK;
}

mvcc_reader_t *mvcc_find_reader(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader, candidate_wal_status_t *status)
{
    *status = CANDIDATE_WAL_INVALID;
    if (reader == NULL || reader->owner_id != m->owner_id || reader->slot >= CANDIDATE_MVCC_READERS) return NULL;
    mvcc_reader_t *r = &m->table->readers[reader->slot];
    *status = CANDIDATE_WAL_STALE;
    if (r->generation == 0 || r->generation != reader->generation) return NULL;
    *status = CANDIDATE_WAL_OK;
    return r;
}

static candidate_outcome_t *outcome_of(candidate_mvcc_t *m, const mvcc_active_t *a)
{
    return &m->table->outcomes[a->outcome].outcome;
}

static void release_active(candidate_mvcc_t *m, mvcc_active_t *a)
{
    *a = (mvcc_active_t){0};
    m->table->active_count--;
}

candidate_wal_status_t mvcc_settle_unknown(candidate_mvcc_t *m, mvcc_active_t *a, candidate_wal_status_t status)
{
    mvcc_state_lock(m);
    m->table->outcomes[a->outcome].state = CANDIDATE_MVCC_UNKNOWN;
    release_active(m,a);
    mvcc_fence(m,status);
    mvcc_state_unlock(m);
    return status;
}

static candidate_wal_status_t check_rows(candidate_mvcc_t *m, const mvcc_active_t *next)
{
    for (uint32_t i = 0; i < next->plan.count; i++) {
        candidate_store_entry_t entry;
        candidate_version_ref_t head; uint64_t csn;
        candidate_wal_status_t status = mvcc_lookup(m,next->plan.writes[i].key,&entry);
        if (status == CANDIDATE_WAL_OK) status = mvcc_find_head(m,&entry,0,&head,&csn);
        if (status != CANDIDATE_WAL_OK) return status;
        if (csn > next->snapshot_csn) return CANDIDATE_WAL_STALE;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_request(const candidate_mvcc_t *m, const mvcc_plan_t *plan)
{
    const mvcc_table_t *t = m->table;
    for (uint32_t i = 0; i < t->outcome_count; i++) {
        const candidate_outcome_t *o = &t->outcomes[i].outcome;
        if (o->epoch == m->request_epoch && memcmp(o->nonce,plan->nonce,16) == 0) return CANDIDATE_WAL_EXISTS;
    }
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) {
        const mvcc_active_t *a = &t->active[i];
        if (a->generation == 0) continue;
        for (uint32_t j = 0; j < a->plan.count; j++)
            for (uint32_t k = 0; k < plan->count; k++)
                if (memcmp(&a->plan.writes[j].key,&plan->writes[k].key,sizeof(plan->writes[k].key)) == 0) return CANDIDATE_WAL_BUSY;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_history(const candidate_mvcc_t *m)
{
    const mvcc_table_t *t = m->table;
    if (t->active_count == CANDIDATE_MVCC_TRANSACTIONS || t->outcome_count == CANDIDATE_MVCC_HISTORY) return CANDIDATE_WAL_LIMIT;
    if (m->wal_transactions >= CANDIDATE_WITNESS_TRANSACTIONS) return CANDIDATE_WAL_LIMIT;
    uint32_t epoch = 0, unapplied = 0; uint64_t result_bytes = CANDIDATE_MVCC_RESPONSE_BYTES;
    for (uint32_t i = 0; i < t->outcome_count; i++) {
        epoch += t->outcomes[i].outcome.epoch == m->request_epoch;
        result_bytes += t->outcomes[i].outcome.result_bytes;
    }
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) unapplied += t->active[i].generation != 0 && !t->active[i].is_applied;
    if (epoch >= MVCC_EPOCH_OUTCOMES) return CANDIDATE_WAL_LIMIT;
    uint64_t nodes = (t->outcome_count + CANDIDATE_CATALOG_NODE_ENTRIES) / CANDIDATE_CATALOG_NODE_ENTRIES + 1;
    if (nodes * CANDIDATE_PAGE_BYTES + result_bytes > CANDIDATE_CATALOG_STORAGE_BYTES) return CANDIDATE_WAL_LIMIT;
    uint64_t need = (uint64_t)(unapplied + 1) * MVCC_APPLY_GROUPS + 1 + MVCC_GROUP_HEADROOM;
    uint64_t last = m->pool->observed.max_group_id, resident = m->pool->observed.witness.complete_groups;
    if (last > UINT64_MAX - need || resident > CANDIDATE_WITNESS_GROUPS) return CANDIDATE_WAL_LIMIT;
    return need > CANDIDATE_WITNESS_GROUPS - resident ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_wal(const candidate_mvcc_t *m, uint32_t writes, bool needs_reserve)
{
    uint64_t end = candidate_wal_info(m->recovery->wal).valid_end_lsn, credits = 0;
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) credits += m->table->active[i].wal_credit;
    uint64_t need = mvcc_begin_bytes(needs_reserve) + mvcc_credit_bytes(writes) + MVCC_RECOVERY_HEADROOM_BYTES;
    if (end > m->wal_budget_bytes || credits > m->wal_budget_bytes - end) return CANDIDATE_WAL_LIMIT;
    return need > m->wal_budget_bytes - end - credits ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_snapshot(candidate_mvcc_t *m, mvcc_active_t *next, const candidate_mvcc_reader_t *snapshot)
{
    candidate_wal_status_t status = check_fence(m);
    if (status == CANDIDATE_WAL_OK && m->next_id == 0) status = CANDIDATE_WAL_LIMIT;
    if (status == CANDIDATE_WAL_OK) status = check_history(m);
    if (status == CANDIDATE_WAL_OK) status = check_request(m,&next->plan);
    if (status != CANDIDATE_WAL_OK) return status;
    next->snapshot_csn = m->visible_csn;
    if (snapshot == NULL) return CANDIDATE_WAL_OK;
    const mvcc_reader_t *r = mvcc_find_reader(m,snapshot,&status);
    if (r != NULL) next->snapshot_csn = r->csn;
    return status;
}

static mvcc_active_t *install(candidate_mvcc_t *m, mvcc_active_t *next, candidate_mvcc_txn_t *out)
{
    mvcc_table_t *t = m->table;
    uint32_t slot = 0;
    while (t->active[slot].generation != 0) slot++;
    mvcc_outcome_t *o = &t->outcomes[t->outcome_count];
    uint64_t id = m->next_id;
    m->next_id = id == UINT64_MAX ? 0 : id + 1;
    *o = (mvcc_outcome_t){.outcome = {.epoch = m->request_epoch,.transaction_id = id,.state = CANDIDATE_OUTCOME_ADMITTED,
        .declared_result_limit = CANDIDATE_MVCC_RESPONSE_BYTES,.admitting_owner_epoch = m->owner_epoch},.state = CANDIDATE_MVCC_ACTIVE};
    memcpy(o->outcome.nonce,next->plan.nonce,16); memcpy(o->outcome.operation_digest,next->plan.digest,32);
    next->generation = ++m->next_generation; next->outcome = t->outcome_count++;
    next->wal_credit = mvcc_credit_bytes(next->plan.count);
    t->active[slot] = *next; t->active_count++; m->wal_transactions++;
    *out = (candidate_mvcc_txn_t){m->owner_id,next->generation,slot};
    return &t->active[slot];
}

static candidate_wal_status_t admit(candidate_mvcc_t *m, mvcc_active_t *next, const candidate_mvcc_reader_t *snapshot,
    candidate_mvcc_txn_t *out)
{
    mvcc_state_lock(m);
    candidate_wal_status_t status = check_snapshot(m,next,snapshot);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = check_rows(m,next);
    if (status != CANDIDATE_WAL_OK) return status;
    bool needs_reserve = false;
    mvcc_state_lock(m);
    status = check_fence(m);
    needs_reserve = m->next_id > m->id_end;
    if (status == CANDIDATE_WAL_OK) status = check_wal(m,next->plan.count,needs_reserve);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK && needs_reserve) status = mvcc_observe_pool(m,mvcc_log_reserve(m));
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_mvcc_txn_t issued;
    mvcc_state_lock(m);
    mvcc_active_t *a = install(m,next,&issued);
    mvcc_state_unlock(m);
    status = mvcc_log_begin(m,a,outcome_of(m,a));
    if (status != CANDIDATE_WAL_OK) return mvcc_settle_unknown(m,a,status);
    *out = issued;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_begin(candidate_mvcc_t *m, const candidate_mvcc_request_t *request,
    candidate_mvcc_txn_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_txn_t){0};
    if (m == NULL || request == NULL) return CANDIDATE_WAL_INVALID;
    mvcc_active_t next = {0};
    candidate_wal_status_t status = mvcc_plan_copy(m,request,&next.plan);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_mvcc_reader_t snapshot = {0};
    if (request->snapshot != NULL) snapshot = *request->snapshot;
    status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = admit(m,&next,request->snapshot != NULL ? &snapshot : NULL,out);
    mvcc_gate_unlock(m);
    return status;
}

candidate_wal_status_t candidate_mvcc_apply(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn)
{
    if (m == NULL || txn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = mvcc_apply(m,txn);
    mvcc_gate_unlock(m);
    return status;
}

static candidate_wal_status_t expect_result(candidate_mvcc_t *m, const mvcc_active_t *a, candidate_outcome_t *out)
{
    *out = *outcome_of(m,a);
    uint8_t response[CANDIDATE_MVCC_RESPONSE_BYTES];
    mvcc_encode_response(out->transaction_id,a->plan.count,response);
    out->result = a->result; out->result_bytes = sizeof(response); out->result_chunks = 1;
    return candidate_digest_sha256(response,sizeof(response),out->result_digest);
}

static void publish_terminal(candidate_mvcc_t *m, mvcc_active_t *a, const candidate_outcome_t *outcome,
    candidate_mvcc_state_t state)
{
    mvcc_state_lock(m);
    mvcc_outcome_t *o = &m->table->outcomes[a->outcome];
    o->outcome = *outcome; o->state = state;
    if (state == CANDIDATE_MVCC_COMMITTED) m->visible_csn = outcome->csn;
    release_active(m,a);
    mvcc_state_unlock(m);
}

static candidate_wal_status_t commit(candidate_mvcc_t *m, candidate_mvcc_txn_t *txn, candidate_mvcc_response_t *out)
{
    candidate_outcome_t outcome = {0};
    candidate_mvcc_response_t body = {0};
    mvcc_active_t *a;
    mvcc_state_lock(m);
    candidate_wal_status_t status = mvcc_find_active(m,txn,&a);
    if (a != NULL && !a->is_applied) status = CANDIDATE_WAL_INVALID;
    if (status == CANDIDATE_WAL_OK) status = expect_result(m,a,&outcome);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = mvcc_observe_pool(m,mvcc_read_response(m,&outcome,&body));
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    outcome.csn = ++m->last_csn; outcome.state = CANDIDATE_OUTCOME_COMMITTED;
    mvcc_state_unlock(m);
    *txn = (candidate_mvcc_txn_t){0};
    status = mvcc_log_commit(m,a,&outcome);
    if (status != CANDIDATE_WAL_OK) {
        *out = (candidate_mvcc_response_t){.state = CANDIDATE_MVCC_UNKNOWN,.transaction_id = outcome.transaction_id};
        return mvcc_settle_unknown(m,a,status);
    }
    publish_terminal(m,a,&outcome,CANDIDATE_MVCC_COMMITTED);
    *out = body;
    out->state = CANDIDATE_MVCC_COMMITTED; out->transaction_id = outcome.transaction_id; out->csn = outcome.csn;
    out->terminal_end_lsn = outcome.terminal_end_lsn;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_commit(candidate_mvcc_t *m, candidate_mvcc_txn_t *txn,
    candidate_mvcc_response_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_response_t){0};
    if (m == NULL || txn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = commit(m,txn,out);
    mvcc_gate_unlock(m);
    return status;
}

static candidate_wal_status_t abort_active(candidate_mvcc_t *m, candidate_mvcc_txn_t *txn)
{
    candidate_outcome_t outcome = {0};
    mvcc_active_t *a;
    mvcc_state_lock(m);
    candidate_wal_status_t status = mvcc_find_active(m,txn,&a);
    if (a != NULL) outcome = *outcome_of(m,a);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = candidate_digest_sha256(NULL,0,outcome.result_digest);
    if (status != CANDIDATE_WAL_OK) return status;
    outcome.state = CANDIDATE_OUTCOME_ABORTED; *txn = (candidate_mvcc_txn_t){0};
    status = mvcc_log_abort(m,a,&outcome);
    if (status != CANDIDATE_WAL_OK) return mvcc_settle_unknown(m,a,status);
    publish_terminal(m,a,&outcome,CANDIDATE_MVCC_ABORTED);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_abort(candidate_mvcc_t *m, candidate_mvcc_txn_t *txn)
{
    if (m == NULL || txn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = abort_active(m,txn);
    mvcc_gate_unlock(m);
    return status;
}

static void fill_txn_info(candidate_mvcc_t *m, const mvcc_active_t *a, candidate_mvcc_txn_info_t *out)
{
    const mvcc_outcome_t *o = &m->table->outcomes[a->outcome];
    *out = (candidate_mvcc_txn_info_t){.state = o->state,.transaction_id = o->outcome.transaction_id,
        .snapshot_csn = a->snapshot_csn,.first_lsn = a->chain.first_lsn,.last_lsn = a->chain.last_lsn,.records = a->chain.records,
        .group_id = a->group_id,.group_end_lsn = a->group_end,.chain_crc = a->chain.crc,.write_count = a->plan.count,
        .result = a->result,.is_applied = a->is_applied};
    memcpy(out->versions,a->versions,sizeof(out->versions));
}

candidate_wal_status_t candidate_mvcc_inspect(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    candidate_mvcc_txn_info_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_txn_info_t){0};
    if (m == NULL || txn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_active_t *a;
    mvcc_state_lock(m);
    status = mvcc_find_active(m,txn,&a);
    if (a != NULL) fill_txn_info(m,a,out);
    mvcc_state_unlock(m);
    mvcc_gate_unlock(m);
    return status;
}

candidate_mvcc_info_t candidate_mvcc_info(candidate_mvcc_t *m)
{
    if (m == NULL || m->process_id != getpid()) return (candidate_mvcc_info_t){0};
    mvcc_state_lock(m);
    candidate_mvcc_info_t info = {.owner_id = m->owner_id,.owner_epoch = m->owner_epoch,.request_epoch = m->request_epoch,
        .visible_csn = m->visible_csn,.last_csn = m->last_csn,.next_transaction_id = m->next_id,.transaction_end = m->id_end,
        .structure_groups = m->structure_groups,.active = m->table->active_count,.readers = m->table->reader_count,
        .outcomes = m->table->outcome_count,.store = m->store,.fence = m->fence};
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) info.wal_credit_bytes += m->table->active[i].wal_credit;
    mvcc_state_unlock(m);
    info.gate_entries = __atomic_load_n(&m->gate_entries,__ATOMIC_RELAXED);
    return info;
}
