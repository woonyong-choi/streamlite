#include "candidate_recovery_view_internal.h"

#include <string.h>

#include "candidate_wal_tail_internal.h"

#define GROUP_CAPACITY (CANDIDATE_WITNESS_GROUPS + 1u)

candidate_wal_status_t candidate_recovery_physical_begin(candidate_recovery_physical_t *p, const candidate_wal_memory_t *memory)
{
    p->groups = memory->allocate(memory->context,GROUP_CAPACITY * sizeof(*p->groups));
    if (p->groups == NULL) return CANDIDATE_WAL_NOMEM;
    p->group_count = 0;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_recovery_physical_observe(candidate_recovery_physical_t *p, const candidate_wal_record_t *r)
{
    if (r->type == 3) {
        if (p->group_count == GROUP_CAPACITY) return CANDIDATE_WAL_LIMIT;
        candidate_physical_cut_t *cut = &p->groups[p->group_count++];
        *cut = p->end; cut->lsn = r->lsn;
    }
    p->end.records++; p->end.owner_epoch = r->owner_epoch;
    if (r->type == 4 || r->type == 5) p->end.page_records++;
    return candidate_wal_tail_hash(p->end.digest,r);
}

void candidate_recovery_physical_release(candidate_recovery_physical_t *p, const candidate_wal_memory_t *memory)
{
    if (p->groups == NULL) return;
    memory->release(memory->context,p->groups,GROUP_CAPACITY * sizeof(*p->groups));
    p->groups = NULL; p->group_count = 0;
}

static uint64_t required_lsn(const candidate_recovery_graph_t *g)
{
    uint64_t lsn = UINT64_MAX;
    candidate_allocation_entry_t e;
    for (uint32_t i = 0; candidate_allocation_at(g->allocation,i,&e) == CANDIDATE_ALLOCATION_MATCH; i++) {
        if (e.state != CANDIDATE_ALLOCATION_LIVE) continue;
        uint64_t image = candidate_recovery_graph_page(g,e.ref.id)->image_lsn;
        if (image < lsn) lsn = image;
    }
    return lsn;
}

static const candidate_physical_cut_t *find_group(const candidate_recovery_physical_t *p, uint64_t lsn)
{
    uint32_t lo = 0, hi = p->group_count;
    while (lo < hi) { uint32_t mid = lo + (hi - lo) / 2; if (p->groups[mid].lsn < lsn) lo = mid + 1; else hi = mid; }
    return lo < p->group_count && p->groups[lo].lsn == lsn ? &p->groups[lo] : NULL;
}

static candidate_wal_status_t choose_range(candidate_recovery_physical_t *p, uint64_t required)
{
    if (required == UINT64_MAX) { p->range = p->end; return CANDIDATE_WAL_OK; }
    const candidate_physical_cut_t *group = find_group(p,required);
    if (group != NULL) { p->range = *group; return CANDIDATE_WAL_OK; }
    bool is_retained = p->range.lsn != 0 && p->range.lsn <= required;
    return is_retained ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_recovery_physical_settle(candidate_recovery_view_t *v)
{
    candidate_recovery_graph_t *g = v->graph;
    candidate_recovery_physical_t *p = &g->physical;
    const candidate_wal_info_t *cut = &v->info.cut;
    candidate_wal_status_t status = CANDIDATE_WAL_CORRUPT;
    if (p->end.records == cut->records && p->end.owner_epoch == cut->highest_owner_epoch) {
        p->end.lsn = cut->valid_end_lsn;
        status = choose_range(p,required_lsn(g));
    }
    candidate_recovery_physical_release(p,&v->memory);
    return status;
}

static candidate_wal_info_t range_cut(const candidate_recovery_view_t *v)
{
    const candidate_physical_cut_t *range = &v->graph->physical.range;
    if (range->lsn == v->graph->physical.end.lsn) return v->info.cut;
    return (candidate_wal_info_t){.records = range->records,.valid_end_lsn = range->lsn,
        .highest_owner_epoch = range->owner_epoch,.segments = (unsigned)(range->lsn / CANDIDATE_WAL_SEGMENT_BYTES + 1)};
}

candidate_wal_status_t candidate_recovery_physical_seed(const candidate_recovery_view_t *v, candidate_physical_seed_t *out)
{
    if (v == NULL || out == NULL) return CANDIDATE_WAL_INVALID;
    const candidate_recovery_physical_t *p = &v->graph->physical;
    *out = (candidate_physical_seed_t){.mode = v->info.mode,.before = range_cut(v),.after = v->info.cut,
        .page_records = p->end.page_records - p->range.page_records,.resident_bytes = v->info.resident_bytes};
    memcpy(out->uuid,v->info.anchor.uuid,16);
    memcpy(out->before_digest,p->range.digest,32); memcpy(out->after_digest,p->end.digest,32);
    return CANDIDATE_WAL_OK;
}

bool candidate_recovery_physical_page_at(const candidate_recovery_view_t *v, uint32_t ordinal, candidate_physical_page_t *out)
{
    candidate_allocation_entry_t e;
    if (v == NULL || out == NULL) return false;
    if (candidate_allocation_at(v->graph->allocation,ordinal,&e) != CANDIDATE_ALLOCATION_MATCH) return false;
    const candidate_recovery_page_t *page = candidate_recovery_graph_page(v->graph,e.ref.id);
    *out = (candidate_physical_page_t){e.ref,e.state,e.transition_lsn,page->lsn,page->image_lsn};
    return true;
}
