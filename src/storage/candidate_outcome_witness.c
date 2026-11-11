#include "storage/candidate_outcome_witness.h"
#include "storage/candidate_outcome_rebuild.h"
#include "storage/candidate_digest.h"

#include <stdlib.h>
#include <string.h>

#include "candidate_outcome_internal.h"
#include "candidate_outcome_witness_internal.h"
#include "candidate_wal_witness_internal.h"
#include "candidate_wal_tail_internal.h"

typedef struct {
    uint64_t transaction_id, owner_epoch, result_limit;
    candidate_version_ref_t result;
    bool has_result;
} admission_t;
typedef struct {
    admission_t admissions[CANDIDATE_WITNESS_TRANSACTIONS];
    uint32_t count;
    candidate_wal_status_t status;
} admissions_t;
struct candidate_admissions {
    candidate_wal_memory_t memory;
    admissions_t state;
};
struct candidate_outcome_witness {
    candidate_wal_memory_t memory;
    candidate_catalog_t *catalog;
    candidate_outcome_witness_info_t info;
};
struct candidate_outcome_rebuild {
    candidate_wal_memory_t memory;
    candidate_outcome_rebuild_info_t info;
    uint8_t uuid[16];
    candidate_outcome_t entries[CANDIDATE_CATALOG_ENTRIES];
};
typedef struct {
    admissions_t *admissions;
    uint8_t digest[32];
    candidate_wal_status_t status;
} rebuild_scan_t;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static admission_t *find_admission(admissions_t *a, uint64_t transaction_id)
{
    for (unsigned i = 0; i < a->count; i++)
        if (a->admissions[i].transaction_id == transaction_id) return &a->admissions[i];
    return NULL;
}

static bool same_result(candidate_version_ref_t a, candidate_version_ref_t b)
{
    bool same_page = a.page.id == b.page.id && a.page.generation == b.page.generation;
    return same_page && a.slot == b.slot && a.slot_generation == b.slot_generation;
}

static bool observe_admission(void *context, const candidate_wal_record_t *r)
{
    admissions_t *a = context;
    if (r->type == 1 && r->payload_bytes == 64) {
        if (a->count == CANDIDATE_WITNESS_TRANSACTIONS) {
            a->status = CANDIDATE_WAL_LIMIT; return false;
        }
        a->admissions[a->count++] = (admission_t){.transaction_id = r->transaction_id,
            .owner_epoch = r->owner_epoch, .result_limit = read_le(r->payload + 56, 8)};
    }
    if (r->type != 9 || r->payload_bytes != 112) return true;
    admission_t *entry = find_admission(a, r->transaction_id);
    if (entry == NULL) { a->status = CANDIDATE_WAL_CORRUPT; return false; }
    candidate_version_ref_t ref = {{read_le(r->payload + 72, 8), read_le(r->payload + 80, 8)},
        (uint32_t)read_le(r->payload + 88, 4), (uint32_t)read_le(r->payload + 92, 4)};
    if (entry->has_result && !same_result(entry->result, ref)) {
        a->status = CANDIDATE_WAL_CORRUPT; return false;
    }
    entry->has_result = true; entry->result = ref;
    return true;
}

candidate_wal_status_t candidate_admissions_create(const candidate_wal_memory_t *memory,
    const candidate_admissions_t *source, candidate_admissions_t **out)
{
    *out = NULL;
    candidate_wal_memory_t allocator = *memory;
    candidate_admissions_t *a = allocator.allocate(allocator.context, sizeof(*a));
    if (a == NULL) return CANDIDATE_WAL_NOMEM;
    if (source == NULL) memset(a, 0, sizeof(*a));
    else memcpy(a, source, sizeof(*a));
    a->memory = allocator;
    *out = a;
    return CANDIDATE_WAL_OK;
}

bool candidate_admissions_observe(void *context, const candidate_wal_record_t *record)
{
    candidate_admissions_t *a = context;
    return observe_admission(&a->state, record);
}

candidate_wal_status_t candidate_admissions_status(const candidate_admissions_t *a) { return a->state.status; }

uint32_t candidate_admissions_count(const candidate_admissions_t *a) { return a == NULL ? 0 : a->state.count; }

bool candidate_admissions_at(const candidate_admissions_t *a, uint32_t index, candidate_semantic_admission_t *out)
{
    if (a == NULL || out == NULL || index >= a->state.count) return false;
    const admission_t *e = &a->state.admissions[index];
    *out = (candidate_semantic_admission_t){e->transaction_id, e->owner_epoch, e->result_limit, e->result, e->has_result};
    return true;
}

size_t candidate_admissions_resident_bytes(const candidate_admissions_t *a) { return a == NULL ? 0 : sizeof(*a); }

void candidate_admissions_retain(candidate_admissions_t *a, const candidate_wal_witness_t *witness)
{
    admissions_t *s = &a->state;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        if (candidate_wal_witness_find(witness, s->admissions[i].transaction_id) == NULL) continue;
        if (kept != i) s->admissions[kept] = s->admissions[i];
        kept++;
    }
    s->count = kept;
}

void candidate_admissions_close(candidate_admissions_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_admissions_t *a = *out;
    candidate_wal_memory_t memory = a->memory;
    *out = NULL;
    memory.release(memory.context, a, sizeof(*a));
}

static bool same_request(const candidate_outcome_t *e, const candidate_witness_transaction_t *t)
{
    return e->epoch == t->request_epoch && memcmp(e->nonce, t->request_nonce, 16) == 0 &&
        memcmp(e->operation_digest, t->operation_digest, 32) == 0;
}

static bool resultless_abort(const candidate_outcome_t *e)
{
    /* catalog codec이 NULL 참조·0 bytes/chunks·빈 SHA-256을 이미 검증했다. */
    return e->state == CANDIDATE_OUTCOME_ABORTED && e->result.page.id == 0;
}

static candidate_wal_status_t compare_terminal(const candidate_outcome_t *e,
    const candidate_witness_transaction_t *t)
{
    if (e->state == CANDIDATE_OUTCOME_ADMITTED)
        return t->state == CANDIDATE_WITNESS_UNFINISHED ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE;
    if (t->state == CANDIDATE_WITNESS_UNFINISHED) return CANDIDATE_WAL_STALE;
    bool is_commit = e->state == CANDIDATE_OUTCOME_COMMITTED;
    if (is_commit != (t->state == CANDIDATE_WITNESS_COMMIT)) return CANDIDATE_WAL_CORRUPT;
    if (e->csn != t->csn || e->terminal_end_lsn != t->terminal_end_lsn) return CANDIDATE_WAL_CORRUPT;
    if (resultless_abort(e)) return CANDIDATE_WAL_OK;
    if (e->result_bytes != t->result_bytes || e->result_chunks != t->result_chunks) return CANDIDATE_WAL_CORRUPT;
    if (memcmp(e->result_digest, t->result_digest, 32) != 0) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t read_body(const candidate_catalog_load_t *load, const candidate_outcome_t *e)
{
    if (e->result.page.id == 0) return CANDIDATE_WAL_OK;
    candidate_result_read_t request = {.ref = e->result, .transaction_id = e->transaction_id,
        .result_bytes = e->result_bytes, .load = load->load, .context = load->context, .memory = load->memory};
    memcpy(request.uuid, load->uuid, 16); memcpy(request.digest, e->result_digest, 32);
    candidate_page_result_t *body = NULL;
    candidate_wal_status_t status = candidate_page_read_result(&request, &body);
    candidate_page_result_close(&body);
    return status;
}

static candidate_wal_status_t compare_entry(const candidate_outcome_t *e,
    const admission_t *admission, const candidate_witness_transaction_t *t)
{
    if (admission == NULL || t == NULL || !t->has_admission) return CANDIDATE_WAL_CORRUPT;
    if (!same_request(e, t)) return CANDIDATE_WAL_CORRUPT;
    if (e->admitting_owner_epoch != admission->owner_epoch) return CANDIDATE_WAL_CORRUPT;
    if (e->declared_result_limit != admission->result_limit) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status = compare_terminal(e, t);
    if (status != CANDIDATE_WAL_OK) return status;
    if (e->state != CANDIDATE_OUTCOME_ADMITTED && !resultless_abort(e) && admission->has_result)
        if (!same_result(e->result, admission->result)) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t compare_root(const candidate_catalog_root_t *root,
    const candidate_wal_witness_t *wal)
{
    candidate_witness_info_t info = candidate_wal_witness_info(wal);
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(wal);
    if (root->visible_csn != info.highest_csn) return CANDIDATE_WAL_STALE;
    if (system.has_epoch)
        if (root->current_epoch != system.current_epoch || root->retention_floor != system.retention_floor)
            return CANDIDATE_WAL_STALE;
    if (system.has_reservation)
        if (root->transaction_highwater != system.transaction_highwater || root->page_highwater != system.page_highwater)
            return CANDIDATE_WAL_STALE;
    for (unsigned i = 0; i < info.transactions; i++) {
        candidate_checkpoint_transaction_t t;
        if (!candidate_witness_checkpoint_at(wal, i, &t)) return CANDIDATE_WAL_CORRUPT;
        if (t.id > root->transaction_highwater || t.owner_epoch > root->owner_epoch)
            return CANDIDATE_WAL_STALE;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t verify_coverage(const candidate_catalog_t *catalog,
    const candidate_wal_witness_t *wal, admissions_t *admissions)
{
    const candidate_catalog_root_t *root = candidate_catalog_root(catalog);
    for (unsigned i = 0; i < admissions->count; i++) {
        const admission_t *a = &admissions->admissions[i];
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(wal, a->transaction_id);
        if (t == NULL) return CANDIDATE_WAL_CORRUPT;
        if (t->request_epoch < root->retention_floor) continue;
        if (t->request_epoch > root->current_epoch) return CANDIDATE_WAL_STALE;
        const candidate_outcome_t *found = NULL;
        for (unsigned j = 0; j < root->count; j++) {
            const candidate_outcome_t *e = candidate_catalog_at(catalog, j);
            if (e->transaction_id == t->transaction_id) { found = e; break; }
        }
        if (found == NULL) return CANDIDATE_WAL_STALE;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t verify_entries(const candidate_catalog_t *catalog, const candidate_catalog_load_t *load,
    const candidate_wal_witness_t *wal, admissions_t *admissions, candidate_outcome_witness_info_t *info)
{
    const candidate_catalog_root_t *root = candidate_catalog_root(catalog);
    candidate_wal_status_t status = compare_root(root, wal);
    if (status == CANDIDATE_WAL_OK) status = verify_coverage(catalog, wal, admissions);
    if (status != CANDIDATE_WAL_OK) return status;
    for (unsigned i = 0; i < root->count; i++) {
        const candidate_outcome_t *e = candidate_catalog_at(catalog, i);
        status = compare_entry(e, find_admission(admissions, e->transaction_id),
            candidate_wal_witness_find(wal, e->transaction_id));
        if (status != CANDIDATE_WAL_OK) return status;
        if (e->state == CANDIDATE_OUTCOME_ADMITTED) { info->admitted++; continue; }
        status = read_body(load, e);
        if (status != CANDIDATE_WAL_OK) return status;
        if (e->state == CANDIDATE_OUTCOME_COMMITTED) info->committed++;
        else info->aborted++;
        info->result_bytes += e->result_bytes;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_outcome_check_resolved(const candidate_catalog_t *catalog,
    const candidate_catalog_load_t *load, const candidate_wal_witness_t *history,
    candidate_admissions_t *admissions, candidate_outcome_witness_info_t *info)
{
    return verify_entries(catalog, load, history, &admissions->state, info);
}

candidate_wal_status_t candidate_outcome_witness_load(candidate_wal_t *wal,
    const candidate_outcome_witness_options_t *options, candidate_outcome_witness_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (options == NULL || wal == NULL) return CANDIDATE_WAL_INVALID;
    candidate_outcome_witness_options_t saved = *options;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (saved.catalog.memory != NULL) memory = *saved.catalog.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_state_t anchor;
    if (saved.checkpoint != NULL) { anchor = *saved.checkpoint; saved.checkpoint = &anchor; }
    saved.catalog.memory = &memory;
    candidate_outcome_witness_t *w = memory.allocate(memory.context, sizeof(*w));
    if (w == NULL) return CANDIDATE_WAL_NOMEM;
    *w = (candidate_outcome_witness_t){.memory = memory};
    candidate_wal_status_t status = candidate_catalog_load(&saved.catalog, &w->catalog);
    admissions_t *admissions = NULL;
    candidate_wal_witness_t *history = NULL;
    if (status == CANDIDATE_WAL_OK) {
        admissions = memory.allocate(memory.context, sizeof(*admissions));
        if (admissions == NULL) status = CANDIDATE_WAL_NOMEM;
        else memset(admissions, 0, sizeof(*admissions));
    }
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_options_t wo = {.memory = &memory, .enable_system_groups = true};
        memcpy(wo.uuid, saved.catalog.uuid, 16);
        status = candidate_witness_analyze_observed(wal, &wo, saved.checkpoint, observe_admission, admissions, &history);
        if (status == CANDIDATE_WAL_STOPPED && admissions->status != CANDIDATE_WAL_OK) status = admissions->status;
    }
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_info_t info = candidate_wal_witness_info(history);
        w->info.wal_end_lsn = info.transport.valid_end_lsn;
        if (info.transport.has_tail) status = CANDIDATE_WAL_TAIL;
        else if (candidate_wal_info(wal).durable_end_lsn < info.transport.valid_end_lsn) status = CANDIDATE_WAL_STALE;
        else status = verify_entries(w->catalog, &saved.catalog, history, admissions, &w->info);
    }
    candidate_wal_witness_close(&history);
    if (admissions != NULL) memory.release(memory.context, admissions, sizeof(*admissions));
    if (status != CANDIDATE_WAL_OK) { candidate_outcome_witness_close(&w); return status; }
    w->info.resident_bytes = sizeof(*w) + candidate_catalog_resident_bytes(w->catalog);
    *out = w;
    return CANDIDATE_WAL_OK;
}

candidate_catalog_lookup_t candidate_outcome_witness_lookup(const candidate_outcome_witness_t *w,
    const candidate_catalog_query_t *query, const candidate_outcome_t **out)
{
    return candidate_catalog_lookup(w == NULL ? NULL : w->catalog, query, out);
}

candidate_outcome_witness_info_t candidate_outcome_witness_info(const candidate_outcome_witness_t *w)
{
    return w == NULL ? (candidate_outcome_witness_info_t){0} : w->info;
}

void candidate_outcome_witness_close(candidate_outcome_witness_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_outcome_witness_t *w = *out;
    candidate_wal_memory_t memory = w->memory;
    candidate_catalog_close(&w->catalog);
    memory.release(memory.context, w, sizeof(*w)); *out = NULL;
}

static bool observe_rebuild(void *context, const candidate_wal_record_t *record)
{
    rebuild_scan_t *scan = context;
    if (!observe_admission(scan->admissions,record)) { scan->status = scan->admissions->status; return false; }
    scan->status = candidate_wal_tail_hash(scan->digest,record); return scan->status == CANDIDATE_WAL_OK;
}

static const candidate_outcome_t *stored_outcome(const candidate_catalog_t *catalog, uint64_t transaction)
{
    for (unsigned i = 0; i < candidate_catalog_root(catalog)->count; i++) {
        const candidate_outcome_t *e = candidate_catalog_at(catalog,i);
        if (e->transaction_id == transaction) return e;
    }
    return NULL;
}

static candidate_wal_status_t check_bounds(candidate_outcome_rebuild_info_t *i, const candidate_catalog_root_t *base,
    const candidate_control_state_t *anchor, const candidate_wal_witness_t *history)
{
    candidate_witness_info_t w = candidate_wal_witness_info(history);
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(history);
    i->current_epoch = s.has_epoch ? s.current_epoch : anchor->request_epoch;
    i->retention_floor = s.has_epoch ? s.retention_floor : anchor->retention_floor;
    i->transaction_highwater = s.has_reservation ? s.transaction_highwater : anchor->transaction_highwater;
    i->page_highwater = s.has_reservation ? s.page_highwater : anchor->page_highwater;
    i->visible_csn = w.highest_csn; i->wal_end_lsn = w.transport.valid_end_lsn;
    if (i->owner_epoch < w.transport.highest_owner_epoch || i->owner_epoch < anchor->owner_epoch) return CANDIDATE_WAL_STALE;
    if (base->owner_epoch > i->owner_epoch || base->current_epoch > i->current_epoch ||
        base->retention_floor > i->retention_floor || base->transaction_highwater > i->transaction_highwater ||
        base->page_highwater > i->page_highwater || base->visible_csn > i->visible_csn) return CANDIDATE_WAL_CORRUPT;
    for (unsigned n = 0; n < w.transactions; n++) {
        candidate_checkpoint_transaction_t t;
        if (!candidate_witness_checkpoint_at(history,n,&t) || t.id > i->transaction_highwater) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_stored(const candidate_catalog_t *catalog, admissions_t *admissions,
    const candidate_wal_witness_t *history)
{
    for (unsigned i = 0; i < candidate_catalog_root(catalog)->count; i++) {
        const candidate_outcome_t *e = candidate_catalog_at(catalog,i);
        const admission_t *a = find_admission(admissions,e->transaction_id);
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(history,e->transaction_id);
        if (a == NULL || t == NULL || !same_request(e,t) || e->admitting_owner_epoch != a->owner_epoch ||
            e->declared_result_limit != a->result_limit) return CANDIDATE_WAL_CORRUPT;
        if (e->state == CANDIDATE_OUTCOME_ADMITTED) continue;
        candidate_wal_status_t status = compare_entry(e,a,t);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_stored_bodies(const candidate_catalog_t *catalog, const candidate_catalog_load_t *load)
{
    for (unsigned i = 0; i < candidate_catalog_root(catalog)->count; i++) {
        const candidate_outcome_t *e = candidate_catalog_at(catalog,i);
        if (e->state == CANDIDATE_OUTCOME_ADMITTED) continue;
        candidate_wal_status_t status = read_body(load,e);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_preview(const candidate_catalog_t *catalog, const candidate_catalog_load_t *load,
    const candidate_control_state_t *anchor, const candidate_wal_witness_t *history, admissions_t *admissions,
    uint64_t owner_epoch)
{
    candidate_outcome_rebuild_info_t bounds = {.owner_epoch = owner_epoch};
    candidate_wal_status_t status = check_bounds(&bounds,candidate_catalog_root(catalog),anchor,history);
    if (status == CANDIDATE_WAL_OK) status = check_stored(catalog,admissions,history);
    if (status == CANDIDATE_WAL_OK) status = check_stored_bodies(catalog,load);
    return status;
}

candidate_wal_status_t candidate_outcome_check_preview(const candidate_catalog_t *catalog,
    const candidate_catalog_load_t *load, const candidate_control_state_t *anchor,
    const candidate_wal_witness_t *history, candidate_admissions_t *admissions, uint64_t owner_epoch)
{
    return check_preview(catalog,load,anchor,history,&admissions->state,owner_epoch);
}

candidate_wal_status_t candidate_outcome_preview(candidate_wal_t *wal,
    const candidate_outcome_witness_options_t *options, uint64_t owner_epoch)
{
    if (wal == NULL || options == NULL || options->checkpoint == NULL || owner_epoch == 0) return CANDIDATE_WAL_INVALID;
    candidate_outcome_witness_options_t saved = *options; candidate_control_state_t anchor = *saved.checkpoint;
    saved.checkpoint = &anchor;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    if (saved.catalog.memory != NULL) memory = *saved.catalog.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    saved.catalog.memory = &memory;
    candidate_catalog_t *catalog = NULL; candidate_wal_witness_t *history = NULL;
    admissions_t *admissions = memory.allocate(memory.context,sizeof(*admissions));
    if (admissions == NULL) return CANDIDATE_WAL_NOMEM;
    memset(admissions,0,sizeof(*admissions));
    candidate_wal_status_t status = candidate_catalog_load(&saved.catalog,&catalog);
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_options_t request = {.memory = &memory,.enable_system_groups = true};
        memcpy(request.uuid,saved.catalog.uuid,16);
        status = candidate_witness_analyze_observed(wal,&request,&anchor,observe_admission,admissions,&history);
        if (status == CANDIDATE_WAL_STOPPED && admissions->status != CANDIDATE_WAL_OK) status = admissions->status;
    }
    if (status == CANDIDATE_WAL_OK) status = check_preview(catalog,&saved.catalog,&anchor,history,admissions,owner_epoch);
    candidate_wal_witness_close(&history); candidate_catalog_close(&catalog);
    memory.release(memory.context,admissions,sizeof(*admissions));
    return status;
}

static candidate_wal_status_t project_outcome(const candidate_outcome_t *old, const admission_t *a,
    const candidate_witness_transaction_t *t, candidate_outcome_t *e)
{
    if (old != NULL && old->state != CANDIDATE_OUTCOME_ADMITTED) { *e = *old; return CANDIDATE_WAL_OK; }
    *e = (candidate_outcome_t){.epoch = t->request_epoch,.transaction_id = t->transaction_id,
        .state = t->state == CANDIDATE_WITNESS_COMMIT ? CANDIDATE_OUTCOME_COMMITTED : CANDIDATE_OUTCOME_ABORTED,
        .csn = t->csn,.terminal_end_lsn = t->terminal_end_lsn,.declared_result_limit = a->result_limit,.admitting_owner_epoch = a->owner_epoch};
    memcpy(e->nonce,t->request_nonce,16); memcpy(e->operation_digest,t->operation_digest,32);
    if (t->state == CANDIDATE_WITNESS_ABORT) return candidate_digest_sha256(NULL,0,e->result_digest);
    if (!a->has_result) return CANDIDATE_WAL_STALE;
    e->result = a->result; e->result_bytes = t->result_bytes; e->result_chunks = t->result_chunks;
    memcpy(e->result_digest,t->result_digest,32); return CANDIDATE_WAL_OK;
}

static int compare_outcomes(const void *left, const void *right)
{
    const candidate_outcome_t *a = left, *b = right;
    if (a->transaction_id == b->transaction_id) return 0;
    return a->transaction_id < b->transaction_id ? -1 : 1;
}

static candidate_wal_status_t rebuild_entries(candidate_outcome_rebuild_t *p, const candidate_catalog_load_t *load,
    const candidate_catalog_t *catalog, admissions_t *admissions, const candidate_wal_witness_t *history)
{
    unsigned epochs[4] = {0};
    for (unsigned n = 0; n < admissions->count; n++) {
        const admission_t *a = &admissions->admissions[n];
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(history,a->transaction_id);
        if (t == NULL || t->request_epoch > p->info.current_epoch) return CANDIDATE_WAL_CORRUPT;
        if (t->request_epoch < p->info.retention_floor) continue;
        unsigned epoch = (unsigned)(t->request_epoch - p->info.retention_floor);
        if (epoch >= 4) return CANDIDATE_WAL_CORRUPT;
        if (++epochs[epoch] > 1024 || p->info.count == CANDIDATE_CATALOG_ENTRIES) return CANDIDATE_WAL_LIMIT;
        candidate_outcome_t *e = &p->entries[p->info.count];
        candidate_wal_status_t status = project_outcome(stored_outcome(catalog,a->transaction_id),a,t,e);
        if (status == CANDIDATE_WAL_OK && e->result.page.id > p->info.page_highwater) status = CANDIDATE_WAL_CORRUPT;
        if (status == CANDIDATE_WAL_OK) status = compare_entry(e,a,t);
        if (status != CANDIDATE_WAL_OK) return status;
        p->info.count++; p->info.result_bytes += e->result_bytes;
    }
    qsort(p->entries,p->info.count,sizeof(p->entries[0]),compare_outcomes);
    p->info.nodes = (p->info.count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    p->info.minimum_retained_bytes = (uint64_t)(p->info.nodes + 1) * 8192 + p->info.result_bytes;
    if (p->info.minimum_retained_bytes > CANDIDATE_CATALOG_STORAGE_BYTES) return CANDIDATE_WAL_LIMIT;
    for (unsigned n = 0; n < p->info.count; n++) {
        candidate_wal_status_t status = read_body(load,&p->entries[n]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_outcome_rebuild_load(candidate_wal_t *wal,
    const candidate_outcome_rebuild_options_t *options, candidate_outcome_rebuild_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || options == NULL || options->source.checkpoint == NULL || options->owner_epoch == 0) return CANDIDATE_WAL_INVALID;
    candidate_outcome_rebuild_options_t saved = *options; candidate_control_state_t anchor = *saved.source.checkpoint;
    saved.source.checkpoint = &anchor;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    if (saved.source.catalog.memory != NULL) memory = *saved.source.catalog.memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    saved.source.catalog.memory = &memory;
    candidate_outcome_rebuild_t *p = memory.allocate(memory.context,sizeof(*p));
    if (p == NULL) return CANDIDATE_WAL_NOMEM;
    memset(p,0,sizeof(*p)); p->memory = memory; p->info.owner_epoch = saved.owner_epoch;
    memcpy(p->uuid,saved.source.catalog.uuid,16);
    candidate_catalog_t *catalog = NULL; admissions_t *admissions = NULL; candidate_wal_witness_t *history = NULL;
    candidate_wal_status_t status = candidate_catalog_load(&saved.source.catalog,&catalog);
    if (status == CANDIDATE_WAL_OK) {
        admissions = memory.allocate(memory.context,sizeof(*admissions));
        if (admissions == NULL) status = CANDIDATE_WAL_NOMEM; else memset(admissions,0,sizeof(*admissions));
    }
    rebuild_scan_t scan = {.admissions = admissions};
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_options_t request = {.memory = &memory,.enable_system_groups = true}; memcpy(request.uuid,p->uuid,16);
        status = candidate_witness_analyze_observed(wal,&request,&anchor,observe_rebuild,&scan,&history);
        if (status == CANDIDATE_WAL_STOPPED && scan.status != CANDIDATE_WAL_OK) status = scan.status;
    }
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_info_t info = candidate_wal_witness_info(history);
        if (info.transport.has_tail) status = CANDIDATE_WAL_TAIL;
        else if (info.unfinished != 0 || info.incomplete_group_lsn != 0 ||
            candidate_wal_info(wal).durable_end_lsn < info.transport.valid_end_lsn) status = CANDIDATE_WAL_STALE;
        else status = check_bounds(&p->info,candidate_catalog_root(catalog),&anchor,history);
    }
    if (status == CANDIDATE_WAL_OK) status = check_stored(catalog,admissions,history);
    if (status == CANDIDATE_WAL_OK) status = rebuild_entries(p,&saved.source.catalog,catalog,admissions,history);
    candidate_wal_witness_close(&history); candidate_catalog_close(&catalog);
    if (admissions != NULL) memory.release(memory.context,admissions,sizeof(*admissions));
    if (status != CANDIDATE_WAL_OK) { candidate_outcome_rebuild_close(&p); return status; }
    memcpy(p->info.wal_digest,scan.digest,32); p->info.resident_bytes = sizeof(*p); *out = p;
    return CANDIDATE_WAL_OK;
}

candidate_outcome_rebuild_info_t candidate_outcome_rebuild_info(const candidate_outcome_rebuild_t *p)
{
    return p == NULL ? (candidate_outcome_rebuild_info_t){0} : p->info;
}

const candidate_outcome_t *candidate_outcome_rebuild_at(const candidate_outcome_rebuild_t *p, uint32_t index)
{
    return p != NULL && index < p->info.count ? &p->entries[index] : NULL;
}

candidate_wal_status_t candidate_outcome_rebuild_encode_node(const candidate_outcome_rebuild_t *p,
    uint32_t index, const candidate_page_location_t *location, uint8_t *out, size_t capacity)
{
    if (p == NULL || location == NULL || index >= p->info.nodes || memcmp(location->uuid,p->uuid,16) != 0) return CANDIDATE_WAL_INVALID;
    candidate_catalog_node_t node = {.ordinal = index};
    uint32_t start = index * CANDIDATE_CATALOG_NODE_ENTRIES, left = p->info.count - start;
    node.count = left < CANDIDATE_CATALOG_NODE_ENTRIES ? left : CANDIDATE_CATALOG_NODE_ENTRIES;
    for (unsigned n = 0; n < node.count; n++) node.entries[n] = p->entries[start + n];
    return candidate_catalog_encode_node(location,&node,out,capacity);
}

candidate_wal_status_t candidate_outcome_rebuild_encode_root(const candidate_outcome_rebuild_t *p,
    const candidate_page_location_t *location, const candidate_page_ref_t *nodes, uint32_t count,
    uint64_t retained_bytes, uint8_t *out, size_t capacity)
{
    if (p == NULL || location == NULL || count != p->info.nodes || (count != 0 && nodes == NULL) ||
        memcmp(location->uuid,p->uuid,16) != 0) return CANDIDATE_WAL_INVALID;
    if (retained_bytes < p->info.minimum_retained_bytes || retained_bytes > CANDIDATE_CATALOG_STORAGE_BYTES) return CANDIDATE_WAL_LIMIT;
    candidate_catalog_root_t root = {.owner_epoch = p->info.owner_epoch,.current_epoch = p->info.current_epoch,
        .retention_floor = p->info.retention_floor,.transaction_highwater = p->info.transaction_highwater,
        .page_highwater = p->info.page_highwater,.visible_csn = p->info.visible_csn,.retained_bytes = retained_bytes,.count = p->info.count};
    for (unsigned n = 0; n < count; n++) root.pages[n] = nodes[n];
    return candidate_catalog_encode_root(location,&root,out,capacity);
}

void candidate_outcome_rebuild_close(candidate_outcome_rebuild_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_outcome_rebuild_t *p = *out; candidate_wal_memory_t memory = p->memory; *out = NULL;
    memory.release(memory.context,p,sizeof(*p));
}
