#include "candidate_wal_witness_state_internal.h"

#include <string.h>

#include "candidate_wal_witness_data_internal.h"

#define ACTIVE_LIMIT 64u

static uint64_t read_le(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static const transaction_t *find_resident(const candidate_wal_witness_t *w, uint64_t id)
{
    for (unsigned i = 0; i < w->info.transactions; i++)
        if (w->transactions[i].value.transaction_id == id) return &w->transactions[i];
    return NULL;
}

static bool is_reservation_below_started(const candidate_wal_witness_t *w,
    const candidate_wal_identity_t *identity, const candidate_wal_record_t *r)
{
    bool is_reservation = r->type == 16 && r->payload_bytes == 40 && w->system != NULL;
    return is_reservation && candidate_identity_info(identity).max_started_id > read_le(r->payload + 8, 8);
}

candidate_wal_status_t candidate_witness_check_retired(const candidate_wal_witness_t *w,
    const candidate_wal_identity_t *identity, const candidate_wal_record_t *r)
{
    if (identity == NULL) return CANDIDATE_WAL_OK;
    if (r->transaction_id == 0)
        return is_reservation_below_started(w, identity, r) ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
    if (r->type >= 12 || find_resident(w, r->transaction_id) != NULL) return CANDIDATE_WAL_OK;
    return candidate_identity_has_started(identity, r->transaction_id) ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_witness_check_nonce(const candidate_wal_identity_t *identity,
    const candidate_wal_record_t *r)
{
    bool is_admission = r->transaction_id != 0 && r->type == 1 && r->payload_bytes == 64;
    if (identity == NULL || !is_admission) return CANDIDATE_WAL_OK;
    return candidate_identity_has_nonce(identity, read_le(r->payload, 8), r->payload + 8) ?
        CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
}

bool candidate_witness_index(const candidate_wal_witness_t *w, uint64_t transaction_id, uint32_t *index)
{
    const transaction_t *t = find_resident(w, transaction_id);
    if (t == NULL) return false;
    *index = (uint32_t)(t - w->transactions);
    return true;
}

static void add_update(candidate_identity_update_t *u, const transaction_t *t)
{
    u->started[u->started_count++] = t->value.transaction_id;
    if (t->owner_epoch > u->max_owner_epoch) u->max_owner_epoch = t->owner_epoch;
    bool is_allowed_epoch = !u->has_floor || t->value.request_epoch >= u->retention_floor;
    if (!t->value.has_admission || !is_allowed_epoch) return;
    candidate_identity_nonce_t *n = &u->nonces[u->nonce_count++];
    n->epoch = t->value.request_epoch;
    memcpy(n->nonce, t->value.request_nonce, 16);
}

static candidate_wal_status_t record_identity(const candidate_wal_witness_t *w, candidate_wal_identity_t *identity)
{
    candidate_witness_system_info_t system = candidate_system_info(w->system);
    candidate_identity_update_t update = {.retention_floor = system.retention_floor,.has_floor = system.has_epoch};
    uint32_t count = w->info.transactions;
    size_t started_bytes = (size_t)count * sizeof(uint64_t), nonce_bytes = (size_t)count * sizeof(candidate_identity_nonce_t);
    const candidate_wal_memory_t *memory = &w->memory;
    if (count > 0) {
        update.started = memory->allocate(memory->context, started_bytes);
        update.nonces = update.started == NULL ? NULL : memory->allocate(memory->context, nonce_bytes);
        if (update.nonces == NULL) {
            if (update.started != NULL) memory->release(memory->context, update.started, started_bytes);
            return CANDIDATE_WAL_NOMEM;
        }
    }
    for (uint32_t i = 0; i < count; i++) add_update(&update, &w->transactions[i]);
    candidate_wal_status_t status = candidate_identity_record(identity, &update);
    if (count > 0) {
        memory->release(memory->context, update.nonces, nonce_bytes);
        memory->release(memory->context, update.started, started_bytes);
    }
    return status;
}

static bool contains(const uint64_t *ids, unsigned count, uint64_t id)
{
    for (unsigned i = 0; i < count; i++) if (ids[i] == id) return true;
    return false;
}

static void retain_groups(candidate_wal_witness_t *w, candidate_identity_retired_t *retired)
{
    uint64_t active[ACTIVE_LIMIT];
    unsigned active_count = 0;
    for (uint32_t i = 0; i < w->info.transactions && active_count < ACTIVE_LIMIT; i++)
        if (w->transactions[i].value.state == CANDIDATE_WITNESS_UNFINISHED)
            active[active_count++] = w->transactions[i].value.transaction_id;
    uint32_t kept = 0, total = w->info.complete_groups;
    for (uint32_t i = 0; i < total; i++) {
        bool is_last = i + 1 == total;
        if (!is_last && !contains(active, active_count, w->groups[i].transaction_id)) { retired->groups++; continue; }
        if (kept != i) w->groups[kept] = w->groups[i];
        kept++;
    }
    w->info.complete_groups = kept;
}

static void retain_transactions(candidate_wal_witness_t *w, const bool *keep, candidate_identity_retired_t *retired)
{
    uint32_t kept = 0, commits = 0, aborts = 0, total = w->info.transactions;
    for (uint32_t i = 0; i < total; i++) {
        candidate_witness_state_t state = w->transactions[i].value.state;
        bool is_commit = state == CANDIDATE_WITNESS_COMMIT, is_abort = state == CANDIDATE_WITNESS_ABORT;
        if (!keep[i] && state != CANDIDATE_WITNESS_UNFINISHED) {
            retired->transactions++; retired->commits += is_commit; retired->aborts += is_abort;
            continue;
        }
        commits += is_commit; aborts += is_abort;
        if (kept != i) w->transactions[kept] = w->transactions[i];
        kept++;
    }
    w->info.transactions = kept;
    w->info.commits = commits;
    w->info.aborts = aborts;
}

candidate_wal_status_t candidate_witness_retain(candidate_wal_witness_t *w, const bool *keep,
    uint64_t max_allocated_id, candidate_wal_identity_t *identity)
{
    candidate_wal_status_t status = record_identity(w, identity);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_identity_retired_t retired = {0};
    retain_groups(w, &retired);
    retain_transactions(w, keep, &retired);
    if (w->system != NULL) retired.events = candidate_system_retain(w->system, max_allocated_id);
    candidate_identity_retire(identity, &retired);
    return CANDIDATE_WAL_OK;
}
