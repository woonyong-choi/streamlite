#include "candidate_mvcc_internal.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define POOL_FRAMES (MVCC_PAGES + 16u)
#define POOL_LEASES 8u
#define SCRATCH_BYTES (2u * MVCC_PLAN_PAGES * CANDIDATE_PAGE_BYTES)

static uint64_t next_owner_id;

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static bool issue_owner_id(uint64_t *out)
{
    uint64_t current = __atomic_load_n(&next_owner_id,__ATOMIC_RELAXED);
    do {
        if (current == UINT64_MAX) return false;
    } while (!__atomic_compare_exchange_n(&next_owner_id,&current,current + 1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    *out = current + 1;
    return true;
}

static candidate_wal_status_t declare_page(candidate_mvcc_t *m, candidate_page_ref_t ref, uint32_t *index)
{
    if (ref.id == 0 || ref.generation == 0) return CANDIDATE_WAL_INVALID;
    for (uint32_t i = 0; i < m->page_count; i++)
        if (same_page(m->pages[i],ref)) { *index = i; return CANDIDATE_WAL_OK; }
    if (m->page_count == MVCC_PAGES) return CANDIDATE_WAL_LIMIT;
    m->pages[m->page_count] = ref; *index = m->page_count++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t declare_row(candidate_mvcc_t *m, const candidate_mvcc_row_t *row, uint64_t result_table)
{
    candidate_mvcc_key_t key = row->key;
    if (key.table_id == 0 || key.row_id == 0 || key.table_id == result_table) return CANDIDATE_WAL_INVALID;
    if (mvcc_find_row(m,key) != NULL) return CANDIDATE_WAL_INVALID;
    uint32_t page;
    candidate_wal_status_t status = declare_page(m,row->page,&page);
    if (status == CANDIDATE_WAL_OK) m->rows[m->row_count++] = (mvcc_row_t){key,page};
    return status;
}

static candidate_wal_status_t declare(candidate_mvcc_t *m, const candidate_mvcc_options_t *o)
{
    if (o->rows == NULL || o->row_count == 0 || o->result_table_id == 0) return CANDIDATE_WAL_INVALID;
    if (o->row_count > CANDIDATE_MVCC_ROWS || o->wal_budget_bytes > CANDIDATE_WAL_MAX_BYTES) return CANDIDATE_WAL_LIMIT;
    for (uint32_t i = 0; i < o->row_count; i++) {
        candidate_mvcc_row_t row = o->rows[i];
        candidate_wal_status_t status = declare_row(m,&row,o->result_table_id);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    for (uint32_t i = 0; i < m->page_count; i++) if (same_page(m->pages[i],o->result_page)) return CANDIDATE_WAL_INVALID;
    m->result_table_id = o->result_table_id;
    m->wal_budget_bytes = o->wal_budget_bytes == 0 ? CANDIDATE_WAL_MAX_BYTES : o->wal_budget_bytes;
    return declare_page(m,o->result_page,&m->result_page);
}

static candidate_wal_status_t init_locks(candidate_mvcc_t *m, bool *has_locks)
{
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return CANDIDATE_WAL_NOMEM;
    int error = pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK);
    if (error == 0) error = pthread_mutex_init(&m->state_lock,&attr);
    if (error == 0 && (error = pthread_mutex_init(&m->io_gate,&attr)) != 0) (void)pthread_mutex_destroy(&m->state_lock);
    (void)pthread_mutexattr_destroy(&attr);
    *has_locks = error == 0;
    return error == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_NOMEM;
}

static candidate_wal_status_t allocate_state(candidate_mvcc_t *m)
{
    if (!issue_owner_id(&m->owner_id)) return CANDIDATE_WAL_LIMIT;
    m->table = m->memory.allocate(m->memory.context,sizeof(*m->table));
    if (m->table == NULL) return CANDIDATE_WAL_NOMEM;
    memset(m->table,0,sizeof(*m->table));
    m->scratch = m->memory.allocate(m->memory.context,SCRATCH_BYTES);
    return m->scratch == NULL ? CANDIDATE_WAL_NOMEM : CANDIDATE_WAL_OK;
}

static candidate_mvcc_state_t state_of(candidate_outcome_state_t state)
{
    if (state == CANDIDATE_OUTCOME_COMMITTED) return CANDIDATE_MVCC_COMMITTED;
    return state == CANDIDATE_OUTCOME_ABORTED ? CANDIDATE_MVCC_ABORTED : CANDIDATE_MVCC_UNKNOWN;
}

static candidate_wal_status_t copy_entries(candidate_mvcc_t *m, const candidate_catalog_t *catalog)
{
    const candidate_catalog_root_t *root = candidate_catalog_root(catalog);
    const candidate_control_state_t *anchor = &m->recovery->info.anchor;
    if (root->count > CANDIDATE_MVCC_HISTORY) return CANDIDATE_WAL_LIMIT;
    if (root->visible_csn != anchor->visible_csn || root->current_epoch != anchor->request_epoch) return CANDIDATE_WAL_UNSUPPORTED;
    for (uint32_t i = 0; i < root->count; i++) {
        const candidate_outcome_t *e = candidate_catalog_at(catalog,i);
        if (e == NULL || e->state == CANDIDATE_OUTCOME_ADMITTED) return CANDIDATE_WAL_UNSUPPORTED;
        if (i > 0 && e->transaction_id <= m->table->outcomes[i - 1].outcome.transaction_id) return CANDIDATE_WAL_UNSUPPORTED;
        m->table->outcomes[i] = (mvcc_outcome_t){*e,state_of(e->state)};
    }
    m->table->outcome_count = root->count;
    m->request_epoch = anchor->request_epoch;
    m->visible_csn = anchor->visible_csn; m->last_csn = anchor->visible_csn;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t copy_outcomes(candidate_mvcc_t *m)
{
    candidate_recovery_t *r = m->recovery;
    candidate_catalog_load_t load = {.root = r->info.anchor.outcome_root,.load = candidate_recovery_load,.context = r,
        .memory = &m->memory};
    memcpy(load.uuid,r->uuid,16);
    candidate_catalog_t *catalog = NULL;
    candidate_wal_status_t status = candidate_catalog_load(&load,&catalog);
    if (status == CANDIDATE_WAL_OK) status = copy_entries(m,catalog);
    candidate_catalog_close(&catalog);
    return status;
}

static candidate_wal_status_t open_pool(candidate_mvcc_t *m)
{
    candidate_recovery_t *r = m->recovery;
    candidate_buffer_options_t o = {.owner_epoch = m->owner_epoch,.wal = r->wal,.control = r->control,
        .target = candidate_recovery_target(r),.memory = &m->memory,.frames = POOL_FRAMES,.leases = POOL_LEASES};
    memcpy(o.uuid,r->uuid,16);
    return candidate_buffer_open(&o,&m->pool);
}

static candidate_wal_status_t check_pages(candidate_mvcc_t *m)
{
    for (uint32_t i = 0; i < m->page_count; i++) {
        candidate_wal_status_t status = mvcc_page_copy(m,i,m->scratch);
        if (status == CANDIDATE_WAL_INVALID) return CANDIDATE_WAL_UNSUPPORTED;
        if (status == CANDIDATE_WAL_OK) status = mvcc_check_page(m,m->scratch);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t bootstrap(candidate_mvcc_t *m, const candidate_recovery_options_t *options)
{
    candidate_wal_status_t status = candidate_recovery_recover(options,&m->recovery);
    if (status != CANDIDATE_WAL_OK) return status;
    m->owner_epoch = options->owner_epoch;
    status = copy_outcomes(m);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_recovery_view_close(&m->recovery->view);
    candidate_wal_witness_close(&m->recovery->terminal_history);
    status = open_pool(m);
    if (status == CANDIDATE_WAL_OK) status = check_pages(m);
    if (status != CANDIDATE_WAL_OK) return status;
    m->wal_transactions = candidate_buffer_info(m->pool).transactions;
    m->next_id = 1; m->id_end = 0;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t release_owner(candidate_mvcc_t *m, bool has_locks)
{
    candidate_wal_status_t status = candidate_buffer_close(&m->pool);
    candidate_wal_status_t closed = candidate_recovery_close(&m->recovery);
    if (status == CANDIDATE_WAL_OK) status = closed;
    candidate_wal_memory_t memory = m->memory;
    if (m->scratch != NULL) memory.release(memory.context,m->scratch,SCRATCH_BYTES);
    if (m->table != NULL) memory.release(memory.context,m->table,sizeof(*m->table));
    if (has_locks) { (void)pthread_mutex_destroy(&m->io_gate); (void)pthread_mutex_destroy(&m->state_lock); }
    memory.release(memory.context,m,sizeof(*m));
    return status;
}

candidate_wal_status_t candidate_mvcc_open(const candidate_mvcc_options_t *options, candidate_mvcc_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (options == NULL) return CANDIDATE_WAL_INVALID;
    candidate_mvcc_options_t saved = *options;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    if (saved.recovery.memory != NULL) memory = *saved.recovery.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    candidate_mvcc_t *m = memory.allocate(memory.context,sizeof(*m));
    if (m == NULL) return CANDIDATE_WAL_NOMEM;
    memset(m,0,sizeof(*m));
    m->memory = memory; m->process_id = getpid();
    saved.recovery.memory = &m->memory;
    bool has_locks = false;
    candidate_wal_status_t status = declare(m,&saved);
    if (status == CANDIDATE_WAL_OK) status = init_locks(m,&has_locks);
    if (status == CANDIDATE_WAL_OK) status = allocate_state(m);
    if (status == CANDIDATE_WAL_OK) status = bootstrap(m,&saved.recovery);
    if (status != CANDIDATE_WAL_OK) { (void)release_owner(m,has_locks); return status; }
    *out = m;
    return CANDIDATE_WAL_OK;
}

static bool is_closable(const candidate_mvcc_t *m)
{
    if (m->table->reader_count != 0) return false;
    return m->table->active_count == 0 || m->fence != CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_mvcc_close(candidate_mvcc_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_mvcc_t *m = *out;
    if (m == NULL) return CANDIDATE_WAL_OK;
    candidate_wal_status_t status = mvcc_gate_lock(m);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    bool can_close = is_closable(m);
    mvcc_state_unlock(m);
    mvcc_gate_unlock(m);
    if (!can_close) return CANDIDATE_WAL_BUSY;
    *out = NULL;
    return release_owner(m,true);
}
