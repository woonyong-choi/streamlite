#include "candidate_recovery_view_internal.h"

#include <string.h>

#include "candidate_wal_internal.h"
#include "candidate_wal_witness_state_internal.h"

typedef struct {
    candidate_recovery_view_t *view;
    candidate_wal_status_t status;
} replay_t;

static candidate_wal_status_t check_lineage(const candidate_recovery_view_request_t *r, const candidate_recovery_view_t *source)
{
    if (r->mode != source->info.mode) return CANDIDATE_WAL_INVALID;
    return memcmp(&r->anchor, &source->info.anchor, sizeof(r->anchor)) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_UNSUPPORTED;
}

static candidate_wal_status_t check_order(const candidate_wal_info_t *cut, const candidate_wal_info_t *transport)
{
    bool is_ahead = cut->valid_end_lsn > transport->valid_end_lsn || cut->records > transport->records;
    return is_ahead ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t create_view(const candidate_recovery_view_request_t *r, candidate_recovery_view_t **out)
{
    candidate_wal_memory_t memory = *r->load.memory;
    candidate_recovery_view_t *v = memory.allocate(memory.context, sizeof(*v));
    if (v == NULL) return CANDIDATE_WAL_NOMEM;
    memset(v, 0, sizeof(*v));
    v->memory = memory;
    v->info = (candidate_recovery_view_info_t){.anchor = r->anchor,.mode = r->mode,.owner_epoch = r->owner_epoch};
    *out = v;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t clone_evidence(candidate_recovery_view_t *v, const candidate_recovery_view_t *source)
{
    candidate_wal_status_t status = candidate_recovery_graph_clone(source->graph, &v->memory, &v->graph);
    if (status == CANDIDATE_WAL_OK) status = candidate_admissions_create(&v->memory, source->admissions, &v->admissions);
    if (status == CANDIDATE_WAL_OK) status = candidate_witness_clone(source->witness, &v->memory, &v->witness);
    if (status == CANDIDATE_WAL_OK) status = candidate_identity_create(&v->memory, source->identity, &v->identity);
    return status;
}

static candidate_wal_status_t apply(candidate_recovery_view_t *v, const candidate_wal_record_t *r)
{
    if (r->type >= 12 && r->type <= 14) return CANDIDATE_WAL_UNSUPPORTED;
    if (!candidate_recovery_view_observe(v, r)) return v->graph->status;
    candidate_wal_status_t status = candidate_witness_check_retired(v->witness, v->identity, r);
    if (status == CANDIDATE_WAL_OK) status = candidate_witness_apply(v->witness, r);
    if (status == CANDIDATE_WAL_OK) status = candidate_witness_check_nonce(v->identity, r);
    return status;
}

static bool visit(void *context, const candidate_wal_record_t *r)
{
    replay_t *state = context;
    state->status = apply(state->view, r);
    return state->status == CANDIDATE_WAL_OK;
}

static candidate_wal_status_t replay(candidate_recovery_view_t *v, candidate_wal_t *wal, const candidate_wal_info_t *cut,
    const candidate_wal_info_t *transport)
{
    replay_t state = {v, CANDIDATE_WAL_OK};
    candidate_wal_suffix_t suffix = {.before = *cut,.after = *transport,.visitor = visit,.context = &state};
    candidate_wal_status_t status = candidate_wal_visit_suffix(wal, &suffix);
    if (status == CANDIDATE_WAL_STOPPED) return state.status;
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_witness_settle(v->witness, transport);
    v->info.cut = *transport;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t validate(candidate_recovery_view_t *v, candidate_wal_t *wal,
    const candidate_catalog_load_t *load, candidate_recovery_marks_t *marks)
{
    marks->witness = v->witness;
    v->graph->mark = candidate_recovery_mark_live;
    v->graph->mark_context = marks;
    candidate_wal_status_t status = candidate_recovery_graph_check(v->graph, &v->info.anchor, load, v->witness);
    v->graph->mark = NULL;
    v->graph->mark_context = NULL;
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_verify(v, wal, load);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_check_cut(v, wal, load->uuid);
    return status;
}

static candidate_wal_status_t advance(candidate_recovery_view_t *v, candidate_wal_t *wal,
    const candidate_catalog_load_t *load, const candidate_recovery_view_t *source, const candidate_wal_info_t *transport)
{
    candidate_recovery_marks_t marks = {0};
    candidate_wal_status_t status = clone_evidence(v, source);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_physical_begin(&v->graph->physical, &v->memory);
    if (status == CANDIDATE_WAL_OK) status = replay(v, wal, &source->info.cut, transport);
    if (status == CANDIDATE_WAL_OK) status = validate(v, wal, load, &marks);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_compact(v, &marks);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_physical_settle(v);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_check_cut(v, wal, load->uuid);
    return status;
}

candidate_wal_status_t candidate_recovery_view_continue(candidate_wal_t *wal,
    const candidate_recovery_view_request_t *request, const candidate_recovery_view_t *source,
    candidate_recovery_view_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || request == NULL || source == NULL) return CANDIDATE_WAL_INVALID;
    candidate_recovery_view_request_t saved = *request;
    candidate_wal_status_t status = candidate_recovery_view_check_request(&saved);
    if (status == CANDIDATE_WAL_OK) status = check_lineage(&saved, source);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_check_uuid(wal, saved.load.uuid);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_check_uuid(wal, saved.anchor.uuid);
    candidate_wal_info_t transport = candidate_wal_info(wal);
    if (status == CANDIDATE_WAL_OK) status = check_order(&source->info.cut, &transport);
    candidate_recovery_view_t *v = NULL;
    if (status == CANDIDATE_WAL_OK) status = create_view(&saved, &v);
    if (status != CANDIDATE_WAL_OK) return status;
    saved.load.memory = &v->memory;
    status = advance(v, wal, &saved.load, source, &transport);
    if (status != CANDIDATE_WAL_OK) { candidate_recovery_view_close(&v); return status; }
    candidate_recovery_view_settle(v);
    *out = v;
    return CANDIDATE_WAL_OK;
}
