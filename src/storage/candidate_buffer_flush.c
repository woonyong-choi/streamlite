#include "candidate_buffer_internal.h"

#include <string.h>

#include "storage/candidate_store.h"

typedef enum { FRAME_CLEAN, FRAME_ELIGIBLE, FRAME_PINNED, FRAME_BUSY, FRAME_UNRESOLVED } eligibility_t;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static bool require_terminal(const candidate_buffer_t *pool, uint64_t id, uint64_t *required)
{
    const candidate_checkpoint_transaction_t *t = candidate_buffer_transaction(pool,id);
    if (t == NULL || t->state == CANDIDATE_CHECKPOINT_ACTIVE) return false;
    if (t->terminal_end_lsn > *required) *required = t->terminal_end_lsn;
    return true;
}

static candidate_wal_status_t check_versions(const candidate_buffer_t *pool, const uint8_t *bytes, uint64_t *required,
    bool *is_resolved)
{
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode(pool->options.uuid,bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    for (uint32_t i = 0; i < info.slots; i++) {
        candidate_version_ref_t ref = {info.location.ref,i,(uint32_t)read_le(bytes + 76 + i * 8,4)};
        candidate_page_version_t v;
        status = candidate_page_get_version(pool->options.uuid,bytes,CANDIDATE_PAGE_BYTES,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        const candidate_checkpoint_transaction_t *t = candidate_buffer_transaction(pool,v.transaction_id);
        if (v.frozen_csn != 0 && t != NULL && (t->state != CANDIDATE_CHECKPOINT_COMMIT || t->csn != v.frozen_csn))
            return CANDIDATE_WAL_CORRUPT;
        if (!require_terminal(pool,v.transaction_id,required)) *is_resolved = false;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_heads(const candidate_buffer_t *pool, const uint8_t *bytes, uint64_t *required,
    bool *is_resolved)
{
    candidate_store_node_t node;
    candidate_wal_status_t status = candidate_store_decode(pool->options.uuid,bytes,CANDIDATE_PAGE_BYTES,&node);
    if (status != CANDIDATE_WAL_OK || node.kind != CANDIDATE_STORE_LEAF) return status;
    for (uint32_t i = 0; i < node.count; i++) {
        uint64_t transaction_id = node.entries[i].head_transaction;
        if (transaction_id != 0 && !require_terminal(pool,transaction_id,required)) *is_resolved = false;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t classify(candidate_buffer_t *pool, uint32_t slot, uint64_t *required, eligibility_t *out)
{
    const buffer_frame_t *f = &pool->frames[slot];
    if (!f->is_dirty) { *out = FRAME_CLEAN; return CANDIDATE_WAL_OK; }
    if (f->pins != 0) { *out = FRAME_PINNED; return CANDIDATE_WAL_OK; }
    if (f->is_inflight) { *out = FRAME_BUSY; return CANDIDATE_WAL_OK; }
    *required = f->page_lsn;
    bool is_resolved = true;
    for (uint32_t i = 0; i < f->contributor_count; i++)
        if (!require_terminal(pool,f->contributors[i],required)) is_resolved = false;
    const uint8_t *bytes = candidate_buffer_frame_bytes(pool,slot);
    unsigned type = (unsigned)read_le(bytes + 22,2);
    if (type < 1 || type > 5) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (type == 1) status = check_versions(pool,bytes,required,&is_resolved);
    if (type == 3) status = check_heads(pool,bytes,required,&is_resolved);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = is_resolved ? FRAME_ELIGIBLE : FRAME_UNRESOLVED;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t select_frames(candidate_buffer_t *pool, uint32_t max_pages, buffer_snapshot_t *list, uint32_t *count,
    candidate_buffer_flush_t *out)
{
    *count = 0;
    for (uint32_t slot = 0; slot < pool->frame_count; slot++) {
        uint64_t required = 0; eligibility_t e;
        candidate_wal_status_t status = classify(pool,slot,&required,&e);
        if (status != CANDIDATE_WAL_OK) return status;
        if (e == FRAME_PINNED) out->pinned++;
        if (e == FRAME_BUSY) out->busy++;
        if (e == FRAME_UNRESOLVED) out->unresolved++;
        if (e != FRAME_ELIGIBLE || *count == max_pages) continue;
        const buffer_frame_t *f = &pool->frames[slot];
        if (list != NULL) list[*count] = (buffer_snapshot_t){slot,f->ref.id,f->frame_generation,f->dirty_generation,required};
        (*count)++;
    }
    return CANDIDATE_WAL_OK;
}

static void finish_frames(candidate_buffer_t *pool, const buffer_snapshot_t *list, uint32_t count, bool is_written,
    candidate_buffer_flush_t *out)
{
    for (uint32_t i = 0; i < count; i++) {
        buffer_frame_t *f = &pool->frames[list[i].slot];
        f->is_inflight = false;
        if (!is_written) { f->pending_rec_lsn = 0; continue; }
        bool is_same = f->frame_generation == list[i].frame_generation && f->dirty_generation == list[i].dirty_generation;
        if (is_same) { f->is_dirty = false; f->rec_lsn = 0; f->pending_rec_lsn = 0; f->contributor_count = 0; continue; }
        if (f->pending_rec_lsn != 0) f->rec_lsn = f->pending_rec_lsn;
        f->pending_rec_lsn = 0; out->redirtied++;
    }
}

static candidate_wal_status_t write_frames(candidate_buffer_t *pool, const buffer_snapshot_t *list, uint8_t *bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) pool->frames[list[i].slot].is_inflight = true;
    for (uint32_t i = 0; i < count; i++) {
        pool->is_writing = true;
        candidate_wal_status_t status = pool->options.target.write(pool->options.target.context,list[i].id,
            bytes + (size_t)i * CANDIDATE_PAGE_BYTES);
        pool->is_writing = false;
        if (status != CANDIDATE_WAL_OK) return status;
        if (pool->is_fenced) return CANDIDATE_WAL_POISONED;
    }
    return pool->options.target.sync(pool->options.target.context);
}

candidate_wal_status_t candidate_buffer_prepare_writeback(candidate_buffer_t *pool, uint32_t max_pages, buffer_batch_t *batch)
{
    *batch = (buffer_batch_t){0};
    uint32_t dirty = 0;
    for (uint32_t i = 0; i < pool->frame_count; i++) if (pool->frames[i].is_dirty) dirty++;
    batch->capacity = dirty < max_pages ? dirty : max_pages;
    if (batch->capacity == 0) return CANDIDATE_WAL_OK;
    batch->bytes_size = (size_t)batch->capacity * CANDIDATE_PAGE_BYTES;
    batch->list_size = (size_t)batch->capacity * sizeof(*batch->list);
    batch->bytes = candidate_buffer_allocate(pool,batch->bytes_size);
    if (batch->bytes != NULL) batch->list = candidate_buffer_allocate(pool,batch->list_size);
    if (batch->list != NULL) return CANDIDATE_WAL_OK;
    candidate_buffer_release_writeback(pool,batch);
    return CANDIDATE_WAL_NOMEM;
}

void candidate_buffer_release_writeback(candidate_buffer_t *pool, buffer_batch_t *batch)
{
    candidate_buffer_release(pool,batch->list,batch->list_size);
    candidate_buffer_release(pool,batch->bytes,batch->bytes_size);
    *batch = (buffer_batch_t){0};
}

candidate_wal_status_t candidate_buffer_writeback(candidate_buffer_t *pool, buffer_batch_t *batch, const buffer_cut_t *expected,
    candidate_buffer_flush_t *out)
{
    *out = (candidate_buffer_flush_t){0};
    uint32_t count = 0;
    candidate_buffer_flush_t counts = {0};
    candidate_wal_status_t status = select_frames(pool,batch->capacity,batch->list,&count,&counts);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    if (count == 0) { *out = counts; return CANDIDATE_WAL_OK; }
    uint64_t required = 0;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(batch->bytes + (size_t)i * CANDIDATE_PAGE_BYTES,candidate_buffer_frame_bytes(pool,batch->list[i].slot),CANDIDATE_PAGE_BYTES);
        if (batch->list[i].required_lsn > required) required = batch->list[i].required_lsn;
    }
    status = candidate_wal_force(pool->options.wal,required);
    if (status == CANDIDATE_WAL_OK && candidate_wal_info(pool->options.wal).durable_end_lsn < required) status = CANDIDATE_WAL_IO;
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_verify_cut(pool,expected);
    if (status == CANDIDATE_WAL_OK) status = write_frames(pool,batch->list,batch->bytes,count);
    finish_frames(pool,batch->list,count,status == CANDIDATE_WAL_OK,&counts);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,status);
    counts.written = count; counts.forced_lsn = required;
    candidate_buffer_count(&pool->pages_written,count); candidate_buffer_count(&pool->data_syncs,1);
    *out = counts;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_flush(candidate_buffer_t *pool, uint32_t max_pages, candidate_buffer_flush_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_flush_t){0};
    if (pool == NULL || max_pages == 0) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_FLUSH);
    if (status != CANDIDATE_WAL_OK) return status;
    buffer_scan_t scan = {.target_end = 0};
    status = candidate_buffer_scan(pool,&scan);
    buffer_batch_t batch = {0};
    candidate_buffer_flush_t receipt = {0};
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_prepare_writeback(pool,max_pages,&batch);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&scan);
    candidate_buffer_end_scan(&scan);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_writeback(pool,&batch,&pool->observed.cut,&receipt);
    candidate_buffer_release_writeback(pool,&batch);
    if (status == CANDIDATE_WAL_OK) *out = receipt;
    candidate_buffer_leave(pool,BUFFER_IDLE);
    return candidate_buffer_result(pool,status);
}
