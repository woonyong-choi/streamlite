#include "candidate_buffer_internal.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_catalog.h"
#include "candidate_control_internal.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"

#define BEGIN_BYTES 160u
#define END_BYTES 64u
#define CHUNK_HEADER_BYTES 32u
#define DIRTY_ENTRY_BYTES 24u
#define TRANSACTION_ENTRY_BYTES 56u

typedef struct {
    candidate_control_state_t next;
    candidate_checkpoint_dirty_t *dirty;
    candidate_checkpoint_transaction_t *transactions;
    uint32_t dirty_count, transaction_count, active, chunks;
    size_t dirty_bytes, transaction_bytes;
    uint64_t lsns[2 + CANDIDATE_CHECKPOINT_CHUNKS], end_lsn, previous_lsn;
    uint8_t digest[32];
    buffer_cut_t begin_cut;
} plan_t;

static int compare_dirty(const void *left, const void *right)
{
    const candidate_checkpoint_dirty_t *a = left, *b = right;
    if (a->ref.id == b->ref.id) return 0;
    return a->ref.id < b->ref.id ? -1 : 1;
}

static int compare_transactions(const void *left, const void *right)
{
    const candidate_checkpoint_transaction_t *a = left, *b = right;
    if (a->id == b->id) return 0;
    return a->id < b->id ? -1 : 1;
}

static uint32_t dirty_chunks(const plan_t *p) { return (p->dirty_count + CANDIDATE_CHECKPOINT_ENTRIES - 1) / CANDIDATE_CHECKPOINT_ENTRIES; }

static uint32_t chunk_count(const plan_t *p, uint32_t chunk)
{
    uint32_t dirty = dirty_chunks(p);
    uint32_t total = chunk < dirty ? p->dirty_count : p->transaction_count;
    uint32_t first = (chunk < dirty ? chunk : chunk - dirty) * CANDIDATE_CHECKPOINT_ENTRIES;
    return total - first < CANDIDATE_CHECKPOINT_ENTRIES ? total - first : CANDIDATE_CHECKPOINT_ENTRIES;
}

static size_t chunk_bytes(const plan_t *p, uint32_t chunk)
{
    bool is_dirty = chunk < dirty_chunks(p);
    return CHUNK_HEADER_BYTES + (size_t)chunk_count(p,chunk) * (is_dirty ? DIRTY_ENTRY_BYTES : TRANSACTION_ENTRY_BYTES);
}

static candidate_wal_status_t check_cut(const candidate_buffer_t *pool, const buffer_scan_t *scan)
{
    const buffer_observation_t *o = &scan->observed;
    if (o->witness.transport.has_tail) return CANDIDATE_WAL_UNSUPPORTED;
    if (!o->system.has_reservation || !o->system.has_epoch) return CANDIDATE_WAL_UNSUPPORTED;
    if (o->witness.incomplete_group_lsn != 0 || o->unadmitted_groups != 0) return CANDIDATE_WAL_BUSY;
    if (pool->options.owner_epoch < o->highest_owner || pool->options.owner_epoch < pool->anchor.owner_epoch) return CANDIDATE_WAL_STALE;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t collect(candidate_buffer_t *pool, const buffer_scan_t *scan, plan_t *p)
{
    for (uint32_t i = 0; i < pool->frame_count; i++) if (pool->frames[i].is_dirty) p->dirty_count++;
    p->transaction_count = scan->observed.witness.transactions;
    if (p->dirty_count > CANDIDATE_CHECKPOINT_DIRTY || p->transaction_count > CANDIDATE_CHECKPOINT_TRANSACTIONS) return CANDIDATE_WAL_LIMIT;
    p->chunks = dirty_chunks(p) + (p->transaction_count + CANDIDATE_CHECKPOINT_ENTRIES - 1) / CANDIDATE_CHECKPOINT_ENTRIES;
    if (p->chunks > CANDIDATE_CHECKPOINT_CHUNKS) return CANDIDATE_WAL_LIMIT;
    p->dirty_bytes = (size_t)p->dirty_count * sizeof(*p->dirty);
    p->transaction_bytes = (size_t)p->transaction_count * sizeof(*p->transactions);
    if (p->dirty_count > 0 && (p->dirty = candidate_buffer_allocate(pool,p->dirty_bytes)) == NULL) return CANDIDATE_WAL_NOMEM;
    if (p->transaction_count > 0 && (p->transactions = candidate_buffer_allocate(pool,p->transaction_bytes)) == NULL)
        return CANDIDATE_WAL_NOMEM;
    uint32_t n = 0;
    for (uint32_t i = 0; i < pool->frame_count; i++) {
        const buffer_frame_t *f = &pool->frames[i];
        if (f->is_dirty) p->dirty[n++] = (candidate_checkpoint_dirty_t){f->ref,f->rec_lsn};
    }
    if (n > 0) qsort(p->dirty,n,sizeof(p->dirty[0]),compare_dirty);
    for (uint32_t i = 0; i < p->transaction_count; i++) {
        if (!candidate_witness_checkpoint_at(scan->witness,i,&p->transactions[i])) return CANDIDATE_WAL_CORRUPT;
        if (p->transactions[i].state == CANDIDATE_CHECKPOINT_ACTIVE) p->active++;
    }
    if (p->transaction_count > 0) qsort(p->transactions,p->transaction_count,sizeof(p->transactions[0]),compare_transactions);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t place_tail(const candidate_buffer_t *pool, plan_t *p, candidate_wal_cursor_t *cursor)
{
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->chunks; i++)
        status = candidate_wal_place(pool->options.wal,cursor,chunk_bytes(p,i),&p->lsns[1 + i]);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_place(pool->options.wal,cursor,END_BYTES,&p->lsns[1 + p->chunks]);
    if (status == CANDIDATE_WAL_OK) { p->end_lsn = cursor->position; p->next.checkpoint_end = p->end_lsn; }
    return status;
}

static candidate_wal_status_t read_root(candidate_buffer_t *pool, candidate_page_ref_t ref, uint8_t *out)
{
    const buffer_page_t *page = candidate_buffer_page(pool,ref.id);
    if (page == NULL || page->generation != ref.generation) return CANDIDATE_WAL_UNSUPPORTED;
    if (page->frame != BUFFER_NO_FRAME) { memcpy(out,candidate_buffer_frame_bytes(pool,page->frame),CANDIDATE_PAGE_BYTES); return CANDIDATE_WAL_OK; }
    candidate_wal_status_t status = candidate_buffer_load(pool,page,out);
    return status == CANDIDATE_WAL_CORRUPT ? candidate_buffer_fence(pool,status) : status;
}

static candidate_wal_status_t check_roots(candidate_buffer_t *pool, const buffer_scan_t *scan, plan_t *p)
{
    const candidate_witness_system_info_t *s = &scan->observed.system;
    uint64_t visible = scan->observed.witness.highest_csn;
    candidate_allocation_root_t allocation; candidate_catalog_root_t catalog;
    candidate_wal_status_t status = read_root(pool,pool->anchor.allocation_root,pool->install);
    if (status == CANDIDATE_WAL_OK) status = candidate_allocation_decode_root(pool->options.uuid,pool->install,CANDIDATE_PAGE_BYTES,&allocation);
    if (status == CANDIDATE_WAL_OK) status = read_root(pool,pool->anchor.outcome_root,pool->install);
    if (status == CANDIDATE_WAL_OK) status = candidate_catalog_decode_root(pool->options.uuid,pool->install,CANDIDATE_PAGE_BYTES,&catalog);
    if (status == CANDIDATE_WAL_CORRUPT) return CANDIDATE_WAL_UNSUPPORTED;
    if (status != CANDIDATE_WAL_OK) return status;
    bool is_allocation = allocation.page_highwater == s->page_highwater && allocation.owner_epoch >= p->next.owner_epoch &&
        allocation.gc_horizon >= pool->anchor.gc_horizon && allocation.gc_horizon <= visible;
    bool is_catalog = catalog.owner_epoch <= p->next.owner_epoch && catalog.current_epoch <= s->current_epoch &&
        catalog.retention_floor <= s->retention_floor && catalog.transaction_highwater <= s->transaction_highwater;
    is_catalog = is_catalog && catalog.page_highwater <= s->page_highwater && catalog.visible_csn <= visible;
    if (!is_allocation || !is_catalog) return CANDIDATE_WAL_UNSUPPORTED;
    p->next.gc_horizon = allocation.gc_horizon;
    return CANDIDATE_WAL_OK;
}

static uint64_t redo_begin(const plan_t *p, uint64_t begin)
{
    uint64_t redo = begin;
    for (uint32_t i = 0; i < p->dirty_count; i++) if (p->dirty[i].rec_lsn < redo) redo = p->dirty[i].rec_lsn;
    for (uint32_t i = 0; i < p->transaction_count; i++) {
        const candidate_checkpoint_transaction_t *t = &p->transactions[i];
        if (t->state == CANDIDATE_CHECKPOINT_ACTIVE && t->first_lsn < redo) redo = t->first_lsn;
    }
    return redo;
}

static candidate_wal_status_t check_publication(candidate_buffer_t *pool, const plan_t *p)
{
    candidate_wal_status_t status = candidate_control_check_publication(pool->options.control,&p->next);
    return status == CANDIDATE_WAL_INVALID ? CANDIDATE_WAL_CORRUPT : status;
}

static candidate_wal_status_t prepare(candidate_buffer_t *pool, const buffer_scan_t *scan, plan_t *p)
{
    candidate_control_info_t control = candidate_control_info(pool->options.control);
    uint64_t checkpoint = 0;
    for (unsigned i = 0; i < 2; i++)
        if ((control.valid_mask & (1u << i)) != 0 && control.slots[i].checkpoint_id > checkpoint) checkpoint = control.slots[i].checkpoint_id;
    if (checkpoint == UINT64_MAX) return CANDIDATE_WAL_LIMIT;
    const buffer_observation_t *o = &scan->observed;
    p->next = (candidate_control_state_t){.backend = CANDIDATE_CONTROL_WAL,.generation = 1,.owner_epoch = pool->options.owner_epoch,
        .checkpoint_id = checkpoint + 1,.transaction_highwater = o->system.transaction_highwater,.page_highwater = o->system.page_highwater,
        .request_epoch = o->system.current_epoch,.retention_floor = o->system.retention_floor,.visible_csn = o->witness.highest_csn,
        .outcome_root = pool->anchor.outcome_root,.allocation_root = pool->anchor.allocation_root,.catalog_root = pool->anchor.catalog_root,
        .gc_horizon = pool->anchor.gc_horizon};
    memcpy(p->next.uuid,pool->options.uuid,16);
    candidate_wal_cursor_t cursor = candidate_wal_cursor(pool->options.wal);
    if (cursor.position != o->witness.transport.valid_end_lsn) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status = candidate_wal_place(pool->options.wal,&cursor,BEGIN_BYTES,&p->lsns[0]);
    if (status == CANDIDATE_WAL_OK) status = place_tail(pool,p,&cursor);
    if (status != CANDIDATE_WAL_OK) return status;
    p->next.checkpoint_begin = p->lsns[0];
    p->next.redo_begin = redo_begin(p,p->lsns[0]);
    status = check_roots(pool,scan,p);
    return status == CANDIDATE_WAL_OK ? check_publication(pool,p) : status;
}

static candidate_wal_status_t append(candidate_buffer_t *pool, plan_t *p, const candidate_wal_record_t *r)
{
    uint64_t end = 0;
    candidate_wal_status_t status = candidate_wal_append(pool->options.wal,r,&end);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_info_t info = candidate_wal_info(pool->options.wal);
    if (end - ((96 + r->payload_bytes + 7) & ~(uint64_t)7) != r->lsn || info.highest_owner_epoch != p->next.owner_epoch)
        return CANDIDATE_WAL_STALE;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t append_begin(candidate_buffer_t *pool, plan_t *p)
{
    candidate_checkpoint_summary_t summary = {.anchor = p->next,.dirty_count = p->dirty_count,.transaction_count = p->transaction_count};
    summary.anchor.generation = 1; summary.anchor.slot = 0;
    uint8_t payload[BEGIN_BYTES]; size_t size;
    candidate_wal_status_t status = candidate_checkpoint_encode_begin(&summary,payload,sizeof(payload),&size);
    candidate_wal_record_t r = {.type = 12,.lsn = p->lsns[0],.owner_epoch = p->next.owner_epoch,.payload = payload,.payload_bytes = size};
    p->begin_cut = pool->observed.cut;
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_cut_add(pool,&p->begin_cut,&r);
    if (status == CANDIDATE_WAL_OK) status = candidate_checkpoint_digest(p->digest,payload,size,p->digest);
    if (status != CANDIDATE_WAL_OK) return status;
    pool->observed.cut = p->begin_cut;
    p->previous_lsn = r.lsn;
    return append(pool,p,&r);
}

static candidate_wal_status_t encode_chunk(const plan_t *p, uint32_t i, uint8_t *payload, size_t *size)
{
    bool is_dirty = i < dirty_chunks(p);
    candidate_checkpoint_chunk_t chunk = {.checkpoint_id = p->next.checkpoint_id,.kind = is_dirty ? 1 : 2,.ordinal = i,
        .count = chunk_count(p,i)};
    uint32_t first = (is_dirty ? i : i - dirty_chunks(p)) * CANDIDATE_CHECKPOINT_ENTRIES;
    if (is_dirty) memcpy(chunk.entries.dirty,p->dirty + first,chunk.count * sizeof(p->dirty[0]));
    else memcpy(chunk.entries.transactions,p->transactions + first,chunk.count * sizeof(p->transactions[0]));
    return candidate_checkpoint_encode_chunk(&chunk,payload,CANDIDATE_CHECKPOINT_PAYLOAD,size);
}

static candidate_wal_status_t emit_tail(candidate_buffer_t *pool, plan_t *p, buffer_cut_t *cut)
{
    uint8_t payload[CANDIDATE_CHECKPOINT_PAYLOAD], digest[32]; size_t size = 0;
    memcpy(digest,p->digest,32);
    uint64_t previous = p->previous_lsn;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i <= p->chunks; i++) {
        bool is_chunk = i < p->chunks;
        if (is_chunk) status = encode_chunk(p,i,payload,&size);
        else {
            candidate_checkpoint_end_t end = {.checkpoint_id = p->next.checkpoint_id,.begin_lsn = p->lsns[0],.chunks = p->chunks};
            memcpy(end.digest,digest,32);
            status = candidate_checkpoint_encode_end(&end,payload,sizeof(payload),&size);
        }
        candidate_wal_record_t r = {.type = is_chunk ? 13 : 14,.lsn = p->lsns[1 + i],.previous_lsn = previous,
            .owner_epoch = p->next.owner_epoch,.payload = payload,.payload_bytes = size};
        if (status == CANDIDATE_WAL_OK && is_chunk) status = candidate_checkpoint_digest(digest,payload,size,digest);
        if (status == CANDIDATE_WAL_OK) status = cut != NULL ? candidate_buffer_cut_add(pool,cut,&r) : append(pool,p,&r);
        previous = r.lsn;
    }
    return status;
}

static candidate_wal_status_t check_unchanged(candidate_buffer_t *pool, plan_t *p)
{
    if (candidate_wal_info(pool->options.wal).valid_end_lsn == pool->observed.cut.end_lsn)
        return candidate_buffer_verify_cut(pool,&pool->observed.cut);
    buffer_scan_t scan = {.target_end = 0};
    candidate_wal_status_t status = candidate_buffer_scan(pool,&scan);
    const buffer_observation_t *o = &scan.observed;
    bool is_pending = o->witness.incomplete_group_lsn != 0 || o->unadmitted_groups != 0 || o->witness.transport.has_tail;
    if (status == CANDIDATE_WAL_OK && is_pending) status = CANDIDATE_WAL_BUSY;
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&scan);
    candidate_buffer_end_scan(&scan);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_cursor_t cursor = candidate_wal_cursor(pool->options.wal);
    status = place_tail(pool,p,&cursor);
    return status == CANDIDATE_WAL_OK ? check_publication(pool,p) : status;
}

static candidate_wal_status_t finish_tail(candidate_buffer_t *pool, plan_t *p)
{
    buffer_cut_t intended = pool->observed.cut;
    candidate_wal_status_t status = emit_tail(pool,p,&intended);
    if (status == CANDIDATE_WAL_OK) status = emit_tail(pool,p,NULL);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(pool->options.wal,p->end_lsn);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_verify_cut(pool,&intended);
    if (status != CANDIDATE_WAL_OK) return status;
    pool->observed.cut = intended;
    return candidate_buffer_prove(pool,&p->next);
}

static candidate_wal_status_t execute(candidate_buffer_t *pool, plan_t *p, buffer_batch_t *batch, candidate_buffer_flush_t *writeback)
{
    candidate_wal_status_t status = append_begin(pool,p);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    pool->fpi_floor_lsn = p->lsns[0];
    status = candidate_wal_force(pool->options.wal,p->begin_cut.end_lsn);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    if (batch->capacity > 0) {
        status = candidate_buffer_writeback(pool,batch,&p->begin_cut,writeback);
        if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    }
    status = check_unchanged(pool,p);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    status = finish_tail(pool,p);
    if (status == CANDIDATE_WAL_OK) status = candidate_control_publish(pool->options.control,&p->next);
    return status == CANDIDATE_WAL_OK ? status : candidate_buffer_fence(pool,status);
}

static void release_plan(candidate_buffer_t *pool, plan_t *p)
{
    candidate_buffer_release(pool,p->dirty,p->dirty_bytes);
    candidate_buffer_release(pool,p->transactions,p->transaction_bytes);
    p->dirty = NULL; p->transactions = NULL;
}

static candidate_wal_status_t run(candidate_buffer_t *pool, uint32_t max_pages, candidate_buffer_checkpoint_t *out)
{
    buffer_scan_t scan = {.target_end = 0};
    plan_t plan = {0};
    candidate_wal_status_t status = candidate_buffer_scan(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = check_cut(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = collect(pool,&scan,&plan);
    if (status == CANDIDATE_WAL_OK) status = prepare(pool,&scan,&plan);
    buffer_batch_t batch = {0};
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_prepare_writeback(pool,max_pages,&batch);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&scan);
    candidate_buffer_end_scan(&scan);
    candidate_buffer_flush_t writeback = {0};
    if (status == CANDIDATE_WAL_OK) status = execute(pool,&plan,&batch,&writeback);
    candidate_buffer_release_writeback(pool,&batch);
    if (status == CANDIDATE_WAL_OK) {
        candidate_control_info_t control = candidate_control_info(pool->options.control);
        pool->anchor = control.slots[control.selected];
        *out = (candidate_buffer_checkpoint_t){.anchor = pool->anchor,.writeback = writeback,.begin_lsn = plan.lsns[0],
            .end_lsn = plan.end_lsn,.dirty_entries = plan.dirty_count,.transactions = plan.transaction_count,.active = plan.active};
        for (uint32_t i = 0; i < pool->frame_count; i++) if (pool->frames[i].is_dirty) out->remaining_dirty++;
    }
    release_plan(pool,&plan);
    return status;
}

candidate_wal_status_t candidate_buffer_checkpoint(candidate_buffer_t *pool, uint32_t max_pages,
    candidate_buffer_checkpoint_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_checkpoint_t){0};
    if (pool == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_CHECKPOINT);
    if (status != CANDIDATE_WAL_OK) return status;
    status = run(pool,max_pages,out);
    candidate_buffer_leave(pool,BUFFER_IDLE);
    return candidate_buffer_result(pool,status);
}
