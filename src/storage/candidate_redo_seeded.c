#include "candidate_redo_state_internal.h"

#include <stdlib.h>
#include <string.h>

#include "candidate_recovery_view_internal.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_tail_internal.h"

typedef struct {
    candidate_wal_t *wal;
    const candidate_recovery_view_t *view;
    candidate_physical_seed_t seed;
    candidate_redo_seed_t request;
    candidate_wal_memory_t memory;
} seeded_t;
typedef struct {
    candidate_redo_workspace_t *workspace;
    uint8_t digest[32];
    candidate_wal_status_t status;
} pass_t;

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static uint32_t read_crc(const uint8_t *page)
{
    return (uint32_t)page[52] | ((uint32_t)page[53] << 8) | ((uint32_t)page[54] << 16) | ((uint32_t)page[55] << 24);
}

static candidate_wal_status_t prepare(candidate_wal_t *wal, const candidate_recovery_view_t *view,
    const candidate_redo_seed_t *request, seeded_t *s)
{
    *s = (seeded_t){.wal = wal,.view = view,.request = *request,.memory = {NULL,allocate_default,release_default}};
    if (s->request.memory != NULL) s->memory = *s->request.memory;
    if (s->memory.allocate == NULL || s->memory.release == NULL) return CANDIDATE_WAL_INVALID;
    s->request.memory = &s->memory;
    candidate_wal_status_t status = candidate_recovery_physical_seed(view,&s->seed);
    return status == CANDIDATE_WAL_OK ? candidate_wal_check_uuid(wal,s->seed.uuid) : status;
}

static candidate_wal_status_t admit(const seeded_t *s, size_t work)
{
    size_t limit = s->request.budget_bytes == 0 ? CANDIDATE_REDO_BUDGET_BYTES : s->request.budget_bytes;
    size_t parts[] = {s->seed.resident_bytes,s->request.retained_bytes,candidate_wal_info(s->wal).resident_bytes,work};
    size_t total = 0;
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (parts[i] > limit - total) return CANDIDATE_WAL_LIMIT;
        total += parts[i];
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t allocate_index(const seeded_t *s, candidate_redo_workspace_t *w, size_t seeds, size_t index)
{
    w->seeds = s->memory.allocate(s->memory.context,seeds);
    if (w->seeds == NULL) return CANDIDATE_WAL_NOMEM;
    memset(w->seeds,0,seeds);
    if (index == 0) return CANDIDATE_WAL_OK;
    w->records = s->memory.allocate(s->memory.context,index);
    return w->records == NULL ? CANDIDATE_WAL_NOMEM : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t create(const seeded_t *s, candidate_redo_preview_t **out)
{
    size_t record = sizeof(candidate_redo_source_t), seeds = CANDIDATE_REDO_HISTORY_SLOTS * sizeof(uint64_t);
    size_t fixed = sizeof(candidate_redo_preview_t) + seeds;
    if (s->seed.page_records > (SIZE_MAX - fixed) / record) return CANDIDATE_WAL_LIMIT;
    size_t index = (size_t)s->seed.page_records * record;
    candidate_wal_status_t status = admit(s,fixed + index);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_redo_preview_t *p = s->memory.allocate(s->memory.context,sizeof(*p));
    if (p == NULL) return CANDIDATE_WAL_NOMEM;
    memset(p,0,sizeof(*p)); p->memory = s->memory; p->wal = s->wal;
    candidate_redo_workspace_t *w = &p->workspace;
    w->options.memory = &p->memory; memcpy(w->options.uuid,s->seed.uuid,16); w->wal = s->wal;
    w->is_preview = true; w->record_capacity = s->seed.page_records;
    w->info.workspace_bytes = fixed + index;
    *out = p;
    status = allocate_index(s,w,seeds,index);
    if (status != CANDIDATE_WAL_OK) candidate_redo_preview_close(out);
    return status;
}

static candidate_wal_status_t seed_pages(const seeded_t *s, candidate_redo_workspace_t *w)
{
    candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(s->view,i,&page); i++) {
        if (page.state != CANDIDATE_ALLOCATION_LIVE) continue;
        candidate_redo_history_t *h = candidate_redo_find_history(w,page.ref.id);
        if (h->id != 0 || page.image_lsn < s->seed.before.valid_end_lsn) return CANDIDATE_WAL_CORRUPT;
        h->id = page.ref.id; w->seeds[h - w->history] = page.image_lsn; w->info.distinct_pages++;
    }
    return CANDIDATE_WAL_OK;
}

static bool visit(void *context, const candidate_wal_record_t *r)
{
    pass_t *pass = context;
    pass->status = candidate_wal_tail_hash(pass->digest,r);
    if (pass->status == CANDIDATE_WAL_OK) pass->status = candidate_redo_visit_record(pass->workspace,r);
    return pass->status == CANDIDATE_WAL_OK;
}

static candidate_wal_status_t replay(const seeded_t *s, candidate_redo_workspace_t *w)
{
    pass_t pass = {.workspace = w};
    memcpy(pass.digest,s->seed.before_digest,32);
    candidate_wal_suffix_t range = {.before = s->seed.before,.after = s->seed.after,.visitor = visit,.context = &pass};
    candidate_wal_status_t status = candidate_wal_visit_range(s->wal,&range);
    if (status == CANDIDATE_WAL_STOPPED) return pass.status;
    if (status != CANDIDATE_WAL_OK) return status;
    return memcmp(pass.digest,s->seed.after_digest,32) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_pages(const seeded_t *s, candidate_redo_workspace_t *w)
{
    candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(s->view,i,&page); i++) {
        if (page.state != CANDIDATE_ALLOCATION_LIVE) continue;
        const candidate_redo_history_t *h = candidate_redo_find_history(w,page.ref.id);
        if (h->generation != page.ref.generation || h->last_lsn != page.end_lsn) return CANDIDATE_WAL_CORRUPT;
    }
    w->info.wal_end_lsn = s->seed.after.valid_end_lsn;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t open_seeded(const seeded_t *s, candidate_redo_preview_t **out)
{
    candidate_redo_preview_t *p = NULL;
    candidate_wal_status_t status = create(s,&p);
    if (status == CANDIDATE_WAL_OK) status = seed_pages(s,&p->workspace);
    if (status == CANDIDATE_WAL_OK) status = replay(s,&p->workspace);
    if (status == CANDIDATE_WAL_OK) status = check_pages(s,&p->workspace);
    if (status == CANDIDATE_WAL_CORRUPT) candidate_wal_reject(s->wal);
    if (status != CANDIDATE_WAL_OK) { candidate_redo_preview_close(&p); return status; }
    *out = p;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_redo_seeded_open(candidate_wal_t *wal, const candidate_recovery_view_t *view,
    const candidate_redo_seed_t *request, candidate_redo_preview_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || view == NULL || request == NULL) return CANDIDATE_WAL_INVALID;
    seeded_t s;
    candidate_wal_status_t status = prepare(wal,view,request,&s);
    return status == CANDIDATE_WAL_OK ? open_seeded(&s,out) : status;
}

static candidate_wal_status_t check_cut(const seeded_t *s)
{
    return candidate_recovery_view_check_cut(s->view,s->wal,s->seed.uuid);
}

static candidate_wal_status_t check_target(const seeded_t *s, const candidate_physical_page_t *page, uint8_t *bytes)
{
    const candidate_redo_target_t *t = &s->request.target;
    bool is_present = false;
    candidate_wal_status_t status = t->read(t->context,page->ref.id,bytes,&is_present);
    if (status != CANDIDATE_WAL_OK || !is_present || candidate_redo_page_crc(bytes) != read_crc(bytes)) return status;
    candidate_page_info_t info;
    status = candidate_page_decode_header(s->seed.uuid,bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    const candidate_page_location_t *at = &info.location;
    bool is_same_generation = at->ref.generation == page->ref.generation;
    bool is_ahead = at->ref.generation > page->ref.generation || (is_same_generation && at->lsn > page->end_lsn);
    return at->ref.id != page->ref.id || is_ahead ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_targets(const seeded_t *s, uint8_t *bytes)
{
    candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(s->view,i,&page); i++) {
        if (page.state != CANDIDATE_ALLOCATION_LIVE) continue;
        candidate_wal_status_t status = check_target(s,&page,bytes);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t restore_page(const seeded_t *s, candidate_redo_preview_t *p,
    const candidate_physical_page_t *page, uint8_t *current)
{
    const candidate_redo_target_t *t = &s->request.target;
    uint8_t expected[CANDIDATE_PAGE_BYTES];
    bool is_present = false;
    candidate_wal_status_t status = candidate_redo_preview_load(p,page->ref,expected);
    if (status == CANDIDATE_WAL_OK) status = t->read(t->context,page->ref.id,current,&is_present);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_redo_info_t *info = &p->workspace.info;
    if (is_present && memcmp(current,expected,CANDIDATE_PAGE_BYTES) == 0) { info->pages_skipped++; return CANDIDATE_WAL_OK; }
    status = check_cut(s);
    if (status == CANDIDATE_WAL_OK) status = t->write(t->context,page->ref.id,expected);
    if (status == CANDIDATE_WAL_OK) info->pages_written++;
    return status;
}

static candidate_wal_status_t restore_pages(const seeded_t *s, candidate_redo_preview_t *p, uint8_t *bytes)
{
    candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(s->view,i,&page); i++) {
        if (page.state != CANDIDATE_ALLOCATION_LIVE) continue;
        candidate_wal_status_t status = restore_page(s,p,&page,bytes);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t execute(const seeded_t *s, candidate_redo_info_t *out)
{
    uint8_t bytes[CANDIDATE_PAGE_BYTES];
    candidate_redo_preview_t *p = NULL;
    candidate_wal_status_t status = open_seeded(s,&p);
    if (status == CANDIDATE_WAL_OK) status = check_cut(s);
    if (status == CANDIDATE_WAL_OK) status = check_targets(s,bytes);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(s->wal,s->seed.after.valid_end_lsn);
    if (status == CANDIDATE_WAL_OK) status = check_cut(s);
    if (status == CANDIDATE_WAL_OK) status = restore_pages(s,p,bytes);
    if (status == CANDIDATE_WAL_OK) status = check_cut(s);
    if (status == CANDIDATE_WAL_OK) status = s->request.target.sync(s->request.target.context);
    candidate_redo_info_t info = candidate_redo_preview_info(p);
    candidate_redo_preview_close(&p);
    if (status == CANDIDATE_WAL_OK) status = check_cut(s);
    if (status == CANDIDATE_WAL_OK) *out = info;
    return status;
}

candidate_wal_status_t candidate_redo_seeded_execute(candidate_wal_t *wal, const candidate_recovery_view_t *view,
    const candidate_redo_seed_t *request, candidate_redo_info_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_redo_info_t){0};
    if (wal == NULL || view == NULL || request == NULL) return CANDIDATE_WAL_INVALID;
    candidate_redo_seed_t saved = *request;
    bool has_target = saved.target.read != NULL && saved.target.write != NULL && saved.target.sync != NULL;
    if (!has_target) return CANDIDATE_WAL_INVALID;
    seeded_t s;
    candidate_wal_status_t status = prepare(wal,view,&saved,&s);
    if (status == CANDIDATE_WAL_OK && s.seed.mode != CANDIDATE_RECOVERY_VIEW_RESOLVED) status = CANDIDATE_WAL_UNSUPPORTED;
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_check_cut(view,wal,s.seed.uuid);
    return status == CANDIDATE_WAL_OK ? execute(&s,out) : status;
}
