#include "candidate_mvcc_internal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "candidate_buffer_internal.h"

typedef struct {
    uint32_t pages[MVCC_PLAN_PAGES], count;
    uint8_t *bases, *merged;
} mvcc_pages_t;

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

const mvcc_row_t *mvcc_find_row(const candidate_mvcc_t *m, candidate_mvcc_key_t key)
{
    for (uint32_t i = 0; i < m->row_count; i++)
        if (m->rows[i].key.table_id == key.table_id && m->rows[i].key.row_id == key.row_id) return &m->rows[i];
    return NULL;
}

static candidate_wal_status_t check_fence(const candidate_mvcc_t *m)
{
    return m->fence == CANDIDATE_WAL_OK ? CANDIDATE_WAL_OK : CANDIDATE_WAL_POISONED;
}

static mvcc_active_t *find_active(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn, candidate_wal_status_t *status)
{
    *status = check_fence(m);
    if (*status != CANDIDATE_WAL_OK) return NULL;
    *status = CANDIDATE_WAL_INVALID;
    if (txn == NULL || txn->owner_id != m->owner_id || txn->slot >= CANDIDATE_MVCC_TRANSACTIONS) return NULL;
    mvcc_active_t *a = &m->table->active[txn->slot];
    *status = CANDIDATE_WAL_STALE;
    if (a->generation == 0 || a->generation != txn->generation) return NULL;
    *status = CANDIDATE_WAL_OK;
    return a;
}

static mvcc_reader_t *find_reader(candidate_mvcc_t *m, const candidate_mvcc_reader_t *reader, candidate_wal_status_t *status)
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

static candidate_wal_status_t settle_unknown(candidate_mvcc_t *m, mvcc_active_t *a, candidate_wal_status_t status)
{
    mvcc_state_lock(m);
    m->table->outcomes[a->outcome].state = CANDIDATE_MVCC_UNKNOWN;
    release_active(m,a);
    mvcc_fence(m,status);
    mvcc_state_unlock(m);
    return status;
}

static candidate_wal_status_t observe_pool(candidate_mvcc_t *m, candidate_wal_status_t status)
{
    if (status == CANDIDATE_WAL_OK || !candidate_buffer_info(m->pool).is_fenced) return status;
    mvcc_state_lock(m); mvcc_fence(m,status); mvcc_state_unlock(m);
    return status;
}

static candidate_wal_status_t copy_pages(candidate_mvcc_t *m, mvcc_pages_t *p)
{
    p->bases = m->scratch; p->merged = m->scratch + (size_t)MVCC_PLAN_PAGES * CANDIDATE_PAGE_BYTES;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->count; i++)
        status = mvcc_page_copy(m,p->pages[i],p->bases + (size_t)i * CANDIDATE_PAGE_BYTES);
    return observe_pool(m,status);
}

static const uint8_t *base_for(const mvcc_pages_t *p, uint32_t page)
{
    for (uint32_t i = 0; i < p->count; i++) if (p->pages[i] == page) return p->bases + (size_t)i * CANDIDATE_PAGE_BYTES;
    return NULL;
}

static void page_need(const candidate_mvcc_t *m, const mvcc_plan_t *plan, uint32_t page, mvcc_usage_t *need)
{
    for (uint32_t i = 0; i < plan->count; i++) {
        if (plan->writes[i].page != page) continue;
        need->slots++; need->body_bytes += MVCC_SLOT_BYTES + MVCC_VERSION_BYTES + plan->writes[i].size;
    }
    if (page != m->result_page) return;
    need->slots++; need->body_bytes += MVCC_SLOT_BYTES + MVCC_VERSION_BYTES + CANDIDATE_MVCC_RESPONSE_BYTES;
}

static candidate_wal_status_t check_page_credit(const candidate_mvcc_t *m, const mvcc_plan_t *plan, uint32_t page,
    const uint8_t *base)
{
    mvcc_usage_t used, need = {0};
    candidate_wal_status_t status = mvcc_page_usage(m,base,&used);
    if (status != CANDIDATE_WAL_OK) return status;
    page_need(m,plan,page,&need);
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) {
        const mvcc_active_t *a = &m->table->active[i];
        if (a->generation != 0 && !a->is_applied) page_need(m,&a->plan,page,&need);
    }
    if (used.slots + need.slots > CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_LIMIT;
    return (uint64_t)used.body_bytes + need.body_bytes > CANDIDATE_PAGE_BYTES - 64 ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_rows(const candidate_mvcc_t *m, const mvcc_active_t *next, const mvcc_pages_t *p)
{
    for (uint32_t i = 0; i < next->plan.count; i++) {
        const mvcc_write_t *w = &next->plan.writes[i];
        candidate_version_ref_t head; uint64_t csn;
        candidate_wal_status_t status = mvcc_find_head(m,base_for(p,w->page),w->key,0,&head,&csn);
        if (status != CANDIDATE_WAL_OK) return status;
        if (csn > next->snapshot_csn) return CANDIDATE_WAL_STALE;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        candidate_wal_status_t status = check_page_credit(m,&next->plan,p->pages[i],p->bases + (size_t)i * CANDIDATE_PAGE_BYTES);
        if (status != CANDIDATE_WAL_OK) return status;
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
    uint64_t need = (uint64_t)unapplied + 2 + MVCC_GROUP_HEADROOM;
    uint64_t last = m->pool->observed.max_group_id, resident = m->pool->observed.witness.complete_groups;
    if (last > UINT64_MAX - need || resident > CANDIDATE_WITNESS_GROUPS) return CANDIDATE_WAL_LIMIT;
    return need > CANDIDATE_WITNESS_GROUPS - resident ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_wal(const candidate_mvcc_t *m, uint32_t pages, bool needs_reserve)
{
    uint64_t end = candidate_wal_info(m->recovery->wal).valid_end_lsn, credits = 0;
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) credits += m->table->active[i].wal_credit;
    uint64_t need = mvcc_begin_bytes(needs_reserve) + mvcc_credit_bytes(pages) + MVCC_RECOVERY_HEADROOM_BYTES;
    if (end > m->wal_budget_bytes || credits > m->wal_budget_bytes - end) return CANDIDATE_WAL_LIMIT;
    return need > m->wal_budget_bytes - end - credits ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_admission(candidate_mvcc_t *m, mvcc_active_t *next,
    const candidate_mvcc_reader_t *snapshot, const mvcc_pages_t *p, bool *needs_reserve)
{
    candidate_wal_status_t status = check_fence(m);
    if (status == CANDIDATE_WAL_OK && m->next_id == 0) status = CANDIDATE_WAL_LIMIT;
    if (status == CANDIDATE_WAL_OK) status = check_history(m);
    if (status == CANDIDATE_WAL_OK) status = check_request(m,&next->plan);
    if (status != CANDIDATE_WAL_OK) return status;
    next->snapshot_csn = m->visible_csn;
    if (snapshot != NULL) {
        const mvcc_reader_t *r = find_reader(m,snapshot,&status);
        if (r == NULL) return status;
        next->snapshot_csn = r->csn;
    }
    status = check_rows(m,next,p);
    *needs_reserve = m->next_id > m->id_end;
    return status == CANDIDATE_WAL_OK ? check_wal(m,p->count,*needs_reserve) : status;
}

static mvcc_active_t *install(candidate_mvcc_t *m, mvcc_active_t *next, uint32_t pages, candidate_mvcc_txn_t *out)
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
    next->wal_credit = mvcc_credit_bytes(pages);
    t->active[slot] = *next; t->active_count++; m->wal_transactions++;
    *out = (candidate_mvcc_txn_t){m->owner_id,next->generation,slot};
    return &t->active[slot];
}

static candidate_wal_status_t admit(candidate_mvcc_t *m, mvcc_active_t *next, const candidate_mvcc_reader_t *snapshot,
    candidate_mvcc_txn_t *out)
{
    mvcc_pages_t p = {0};
    p.count = mvcc_plan_pages(m,&next->plan,p.pages);
    mvcc_state_lock(m);
    candidate_wal_status_t status = check_fence(m);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = copy_pages(m,&p);
    bool needs_reserve = false;
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    status = check_admission(m,next,snapshot,&p,&needs_reserve);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK && needs_reserve) status = observe_pool(m,mvcc_log_reserve(m));
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_mvcc_txn_t issued;
    mvcc_state_lock(m);
    mvcc_active_t *a = install(m,next,p.count,&issued);
    mvcc_state_unlock(m);
    status = mvcc_log_begin(m,a,outcome_of(m,a));
    if (status != CANDIDATE_WAL_OK) return settle_unknown(m,a,status);
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

static candidate_wal_status_t merge_pages(candidate_mvcc_t *m, mvcc_active_t *a, mvcc_pages_t *p,
    candidate_buffer_page_t *pages)
{
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->count; i++) {
        uint8_t *merged = p->merged + (size_t)i * CANDIDATE_PAGE_BYTES;
        status = mvcc_merge_page(m,p->bases + (size_t)i * CANDIDATE_PAGE_BYTES,p->pages[i],a,merged);
        pages[i] = (candidate_buffer_page_t){m->pages[p->pages[i]],merged};
    }
    if (status == CANDIDATE_WAL_OK) return status;
    memset(a->versions,0,sizeof(a->versions)); a->result = (candidate_version_ref_t){0};
    return status;
}

static void record_group(mvcc_active_t *a, const candidate_buffer_admission_t *receipt, uint32_t pages)
{
    a->chain.last_lsn = receipt->transaction_last_lsn; a->chain.records = receipt->transaction_records;
    a->chain.crc = receipt->transaction_chain_crc;
    a->group_id = receipt->group_id; a->group_end = receipt->end_lsn; a->is_applied = true;
    uint64_t spent = mvcc_group_bytes(pages);
    a->wal_credit = a->wal_credit > spent ? a->wal_credit - spent : 0;
}

static candidate_wal_status_t apply(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn)
{
    candidate_wal_status_t status;
    mvcc_state_lock(m);
    mvcc_active_t *a = find_active(m,txn,&status);
    if (a != NULL && a->is_applied) status = CANDIDATE_WAL_INVALID;
    mvcc_state_unlock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_pages_t p = {0};
    p.count = mvcc_plan_pages(m,&a->plan,p.pages);
    status = copy_pages(m,&p);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_buffer_page_t pages[MVCC_PLAN_PAGES];
    mvcc_state_lock(m);
    status = merge_pages(m,a,&p,pages);
    mvcc_state_unlock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_buffer_group_t group = {outcome_of(m,a)->transaction_id,a->chain.last_lsn,pages,p.count};
    candidate_buffer_admission_t receipt;
    status = candidate_buffer_write_group(m->pool,&group,&receipt);
    if (status != CANDIDATE_WAL_OK && candidate_buffer_info(m->pool).is_fenced) return settle_unknown(m,a,status);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    record_group(a,&receipt,p.count);
    mvcc_state_unlock(m);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_apply(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn)
{
    if (m == NULL || txn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    status = apply(m,txn);
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
    candidate_wal_status_t status;
    candidate_outcome_t outcome = {0};
    candidate_mvcc_response_t body = {0};
    mvcc_state_lock(m);
    mvcc_active_t *a = find_active(m,txn,&status);
    if (a != NULL && !a->is_applied) status = CANDIDATE_WAL_INVALID;
    if (status == CANDIDATE_WAL_OK) status = expect_result(m,a,&outcome);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = observe_pool(m,mvcc_read_response(m,&outcome,&body));
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    outcome.csn = ++m->last_csn; outcome.state = CANDIDATE_OUTCOME_COMMITTED;
    mvcc_state_unlock(m);
    *txn = (candidate_mvcc_txn_t){0};
    status = mvcc_log_commit(m,a,&outcome);
    if (status != CANDIDATE_WAL_OK) {
        *out = (candidate_mvcc_response_t){.state = CANDIDATE_MVCC_UNKNOWN,.transaction_id = outcome.transaction_id};
        return settle_unknown(m,a,status);
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
    candidate_wal_status_t status;
    candidate_outcome_t outcome = {0};
    mvcc_state_lock(m);
    mvcc_active_t *a = find_active(m,txn,&status);
    if (a != NULL) outcome = *outcome_of(m,a);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = candidate_digest_sha256(NULL,0,outcome.result_digest);
    if (status != CANDIDATE_WAL_OK) return status;
    outcome.state = CANDIDATE_OUTCOME_ABORTED; *txn = (candidate_mvcc_txn_t){0};
    status = mvcc_log_abort(m,a,&outcome);
    if (status != CANDIDATE_WAL_OK) return settle_unknown(m,a,status);
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

static candidate_wal_status_t resolve_view(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, mvcc_view_t *view)
{
    candidate_wal_status_t status = check_fence(m);
    if (status != CANDIDATE_WAL_OK) return status;
    if (txn != NULL) {
        const mvcc_active_t *a = find_active(m,txn,&status);
        if (a != NULL) *view = (mvcc_view_t){outcome_of(m,a)->transaction_id,a->snapshot_csn};
        return status;
    }
    const mvcc_reader_t *r = find_reader(m,reader,&status);
    if (r != NULL) *view = (mvcc_view_t){0,r->csn};
    return status;
}

static candidate_wal_status_t read_view(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    const mvcc_row_t *row = mvcc_find_row(m,key);
    if (row == NULL) return CANDIDATE_WAL_UNSUPPORTED;
    mvcc_view_t view = {0};
    mvcc_state_lock(m);
    candidate_wal_status_t status = resolve_view(m,txn,reader,&view);
    mvcc_state_unlock(m);
    if (status == CANDIDATE_WAL_OK) status = observe_pool(m,mvcc_page_copy(m,row->page,m->scratch));
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_view_t current = {0};
    mvcc_state_lock(m);
    status = resolve_view(m,txn,reader,&current);
    if (status == CANDIDATE_WAL_OK) status = mvcc_find_visible(m,m->scratch,key,view,out);
    mvcc_state_unlock(m);
    return status;
}

static candidate_wal_status_t read_locked(candidate_mvcc_t *m, const candidate_mvcc_txn_t *txn,
    const candidate_mvcc_reader_t *reader, candidate_mvcc_key_t key, candidate_mvcc_value_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_mvcc_value_t){0};
    if (m == NULL || (txn == NULL && reader == NULL)) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = mvcc_gate_lock(m);
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
    mvcc_reader_t *r = find_reader(m,reader,&status);
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
    return has_body ? observe_pool(m,mvcc_read_response(m,&found.outcome,r)) : CANDIDATE_WAL_OK;
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
    mvcc_state_lock(m);
    const mvcc_active_t *a = find_active(m,txn,&status);
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
        .active = m->table->active_count,.readers = m->table->reader_count,.outcomes = m->table->outcome_count,.fence = m->fence};
    for (uint32_t i = 0; i < CANDIDATE_MVCC_TRANSACTIONS; i++) info.wal_credit_bytes += m->table->active[i].wal_credit;
    mvcc_state_unlock(m);
    info.gate_entries = __atomic_load_n(&m->gate_entries,__ATOMIC_RELAXED);
    return info;
}
