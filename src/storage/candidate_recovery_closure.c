#include "candidate_recovery_view_internal.h"

#include "candidate_wal_witness_state_internal.h"

static void keep_transaction(candidate_recovery_marks_t *marks, uint64_t transaction_id)
{
    uint32_t index;
    if (candidate_witness_index(marks->witness, transaction_id, &index)) marks->keep[index] = true;
}

void candidate_recovery_mark_live(void *context, const candidate_witness_transaction_t *transaction)
{
    keep_transaction(context, transaction->transaction_id);
}

static void keep_catalog(candidate_recovery_marks_t *marks, const candidate_catalog_t *catalog)
{
    const candidate_catalog_root_t *root = candidate_catalog_root(catalog);
    for (uint32_t i = 0; i < root->count; i++) keep_transaction(marks, candidate_catalog_at(catalog, i)->transaction_id);
}

static void keep_retained_admissions(candidate_recovery_marks_t *marks, const candidate_control_state_t *anchor)
{
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(marks->witness);
    uint64_t retention_floor = system.has_epoch ? system.retention_floor : anchor->retention_floor;
    candidate_semantic_transaction_t t;
    for (uint32_t i = 0; candidate_witness_transaction_at(marks->witness, i, &t); i++)
        if (t.value.has_admission && t.value.request_epoch >= retention_floor) marks->keep[i] = true;
}

static uint64_t max_allocated_id(const candidate_allocation_t *allocation)
{
    candidate_allocation_info_t info = candidate_allocation_info(allocation);
    candidate_allocation_entry_t last;
    if (info.count == 0 || candidate_allocation_at(allocation, info.count - 1, &last) != CANDIDATE_ALLOCATION_MATCH)
        return 0;
    return last.ref.id;
}

candidate_wal_status_t candidate_recovery_view_compact(candidate_recovery_view_t *v, candidate_recovery_marks_t *marks)
{
    keep_catalog(marks, v->catalog);
    keep_retained_admissions(marks, &v->info.anchor);
    uint64_t allocated = max_allocated_id(v->graph->allocation);
    candidate_wal_status_t status = candidate_witness_retain(v->witness, marks->keep, allocated, v->identity);
    if (status == CANDIDATE_WAL_OK) candidate_admissions_retain(v->admissions, v->witness);
    return status;
}

static void count_resident(const candidate_wal_witness_t *witness, candidate_recovery_closure_t *closure,
    uint64_t *max_started_id)
{
    candidate_semantic_transaction_t t;
    for (uint32_t i = 0; candidate_witness_transaction_at(witness, i, &t); i++)
        if (t.value.transaction_id > *max_started_id) *max_started_id = t.value.transaction_id;
    candidate_semantic_group_t g;
    for (uint32_t i = 0; candidate_witness_group_at(witness, i, &g); i++)
        if (g.transaction_id == 0) closure->system_groups++;
}

candidate_recovery_closure_t candidate_recovery_view_closure(const candidate_recovery_view_t *v)
{
    if (v == NULL) return (candidate_recovery_closure_t){0};
    candidate_witness_info_t w = candidate_wal_witness_info(v->witness);
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(v->witness);
    candidate_identity_info_t identity = candidate_identity_info(v->identity);
    candidate_recovery_closure_t c = {.transactions = w.transactions,.unfinished = w.unfinished,.commits = w.commits,
        .aborts = w.aborts,.groups = w.complete_groups,.events = s.events,.admissions = candidate_admissions_count(v->admissions),
        .started_intervals = identity.intervals,.tombstones = identity.tombstones,.identity_bytes = identity.resident_bytes};
    uint64_t max_started_id = identity.max_started_id;
    count_resident(v->witness, &c, &max_started_id);
    const candidate_identity_retired_t *retired = &identity.retired;
    c.history = (candidate_recovery_history_t){.transactions = retired->transactions + w.transactions,
        .commits = retired->commits + w.commits,.aborts = retired->aborts + w.aborts,
        .groups = retired->groups + w.complete_groups,.system_groups = s.system_groups,.events = retired->events + s.events,
        .max_started_id = max_started_id};
    return c;
}

const candidate_wal_witness_t *candidate_recovery_view_witness(const candidate_recovery_view_t *v)
{
    return v == NULL ? NULL : v->witness;
}
