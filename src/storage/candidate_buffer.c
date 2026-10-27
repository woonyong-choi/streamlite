#include "candidate_buffer_internal.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_catalog.h"
#include "storage/candidate_store.h"
#include "storage/checksum.h"
#include "candidate_recovery_internal.h"
#include "candidate_redo_internal.h"
#include "candidate_wal_internal.h"

static uint64_t next_pool_id;

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static void *meter_allocate(void *context, size_t size)
{
    buffer_meter_t *m = context;
    if (size > m->limit - m->borrowed - m->used) { m->is_refused = true; return NULL; }
    void *p = m->base.allocate(m->base.context,size);
    if (p == NULL) return NULL;
    m->used += size;
    if (m->used > m->peak) m->peak = m->used;
    return p;
}

static void meter_release(void *context, void *p, size_t size)
{
    buffer_meter_t *m = context;
    if (p == NULL) return;
    m->base.release(m->base.context,p,size);
    m->used -= size;
}

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static uint32_t page_crc(const uint8_t *p)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,p,52);
    crc = crc32c(crc,zero,4);
    return crc32c(crc,p + 56,CANDIDATE_PAGE_BYTES - 56);
}

static bool issue_pool_id(uint64_t *out)
{
    uint64_t current = __atomic_load_n(&next_pool_id,__ATOMIC_RELAXED);
    do {
        if (current >= BUFFER_COUNTER_LIMIT) return false;
    } while (!__atomic_compare_exchange_n(&next_pool_id,&current,current + 1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    *out = current + 1;
    return true;
}

void candidate_buffer_count(uint64_t *counter, uint64_t amount)
{
    *counter = amount > BUFFER_COUNTER_LIMIT - *counter ? BUFFER_COUNTER_LIMIT : *counter + amount;
}

void *candidate_buffer_allocate(candidate_buffer_t *pool, size_t size)
{
    return pool->memory.allocate(pool->memory.context,size);
}

void candidate_buffer_release(candidate_buffer_t *pool, void *bytes, size_t size)
{
    pool->memory.release(pool->memory.context,bytes,size);
}

candidate_wal_status_t candidate_buffer_enter(candidate_buffer_t *pool, buffer_operation_t operation)
{
    if (pool->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    if (pool->is_fenced) return CANDIDATE_WAL_POISONED;
    if (pool->operation != BUFFER_IDLE) {
        bool is_writeback = pool->operation == BUFFER_FLUSH || pool->operation == BUFFER_CHECKPOINT;
        if (!pool->is_writing || !is_writeback || operation != BUFFER_ADMIT) return CANDIDATE_WAL_BUSY;
        pool->is_writing = false;
    } else {
        pool->meter.peak = pool->meter.used; pool->meter.is_refused = false;
    }
    pool->operation = operation;
    return CANDIDATE_WAL_OK;
}

void candidate_buffer_leave(candidate_buffer_t *pool, buffer_operation_t previous)
{
    pool->operation = previous;
    if (previous == BUFFER_FLUSH || previous == BUFFER_CHECKPOINT) pool->is_writing = true;
}

candidate_wal_status_t candidate_buffer_result(candidate_buffer_t *pool, candidate_wal_status_t status)
{
    return status == CANDIDATE_WAL_NOMEM && pool->meter.is_refused ? CANDIDATE_WAL_LIMIT : status;
}

candidate_wal_status_t candidate_buffer_fence(candidate_buffer_t *pool, candidate_wal_status_t status)
{
    pool->is_fenced = true;
    return status;
}

static uint32_t directory_slot(uint64_t id)
{
    uint64_t hash = id * UINT64_C(11400714819323198485);
    return (uint32_t)(hash ^ (hash >> 32)) & (BUFFER_DIRECTORY_SLOTS - 1);
}

buffer_page_t *candidate_buffer_page(candidate_buffer_t *pool, uint64_t id)
{
    if (id == 0) return NULL;
    for (uint32_t slot = directory_slot(id);; slot = (slot + 1) & (BUFFER_DIRECTORY_SLOTS - 1)) {
        if (pool->directory[slot].id == id) return &pool->directory[slot];
        if (pool->directory[slot].id == 0) return NULL;
    }
}

candidate_wal_status_t candidate_buffer_add_page(candidate_buffer_t *pool, uint64_t id, buffer_page_t **out)
{
    if (id == 0) return CANDIDATE_WAL_CORRUPT;
    uint32_t slot = directory_slot(id);
    while (pool->directory[slot].id != 0 && pool->directory[slot].id != id) slot = (slot + 1) & (BUFFER_DIRECTORY_SLOTS - 1);
    if (pool->directory[slot].id == 0) {
        if (pool->known_pages == CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
        pool->directory[slot] = (buffer_page_t){.id = id,.frame = BUFFER_NO_FRAME};
        pool->known_pages++;
    }
    *out = &pool->directory[slot];
    return CANDIDATE_WAL_OK;
}

uint8_t *candidate_buffer_frame_bytes(candidate_buffer_t *pool, uint32_t slot)
{
    return pool->bytes + (size_t)slot * CANDIDATE_PAGE_BYTES;
}

const candidate_checkpoint_transaction_t *candidate_buffer_transaction(const candidate_buffer_t *pool, uint64_t id)
{
    uint32_t lo = 0, hi = pool->transaction_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (pool->transactions[mid].id < id) lo = mid + 1; else hi = mid;
    }
    return lo < pool->transaction_count && pool->transactions[lo].id == id ? &pool->transactions[lo] : NULL;
}

candidate_wal_status_t candidate_buffer_check_page(const uint8_t uuid[16], const uint8_t *p, candidate_page_info_t *info)
{
    candidate_wal_status_t status = candidate_page_decode_header(uuid,p,CANDIDATE_PAGE_BYTES,info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info->type == 1 || info->type == 2) return candidate_page_decode(uuid,p,CANDIDATE_PAGE_BYTES,info);
    if (info->type == 3) { candidate_store_node_t node; return candidate_store_decode(uuid,p,CANDIDATE_PAGE_BYTES,&node); }
    unsigned kind = (unsigned)read_le(p + 70,2);
    if (info->type == 4 && kind == 1) { candidate_catalog_root_t v; return candidate_catalog_decode_root(uuid,p,CANDIDATE_PAGE_BYTES,&v); }
    if (info->type == 4 && kind == 2) { candidate_catalog_node_t v; return candidate_catalog_decode_node(uuid,p,CANDIDATE_PAGE_BYTES,&v); }
    if (info->type == 5 && kind == 1) { candidate_allocation_root_t v; return candidate_allocation_decode_root(uuid,p,CANDIDATE_PAGE_BYTES,&v); }
    if (info->type == 5 && kind == 2) { candidate_allocation_node_t v; return candidate_allocation_decode_node(uuid,p,CANDIDATE_PAGE_BYTES,&v); }
    return CANDIDATE_WAL_UNSUPPORTED;
}

candidate_wal_status_t candidate_buffer_load(candidate_buffer_t *pool, const buffer_page_t *page, uint8_t *out)
{
    bool is_present = false;
    candidate_wal_status_t status = pool->options.target.read(pool->options.target.context,page->id,out,&is_present);
    if (status != CANDIDATE_WAL_OK) return status;
    if (!is_present || page_crc(out) != read_le(out + 52,4)) return CANDIDATE_WAL_CORRUPT;
    candidate_page_info_t info;
    status = candidate_buffer_check_page(pool->options.uuid,out,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id != page->id || info.location.ref.generation != page->generation) return CANDIDATE_WAL_CORRUPT;
    return info.location.lsn == page->last_lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static bool is_evictable(const buffer_frame_t *f)
{
    return !f->is_dirty && f->pins == 0 && !f->is_inflight;
}

static bool is_chosen(const uint32_t *slots, uint32_t count, uint32_t slot)
{
    for (uint32_t i = 0; i < count; i++) if (slots[i] == slot) return true;
    return false;
}

static bool pick_frame(const candidate_buffer_t *pool, const uint32_t *slots, uint32_t count, uint32_t *out)
{
    for (unsigned pass = 0; pass < 3; pass++) {
        for (uint32_t step = 0; step < pool->frame_count; step++) {
            uint32_t slot = (pool->clock_hand + step) % pool->frame_count;
            const buffer_frame_t *f = &pool->frames[slot];
            if (is_chosen(slots,count,slot) || f->frame_generation >= BUFFER_COUNTER_LIMIT) continue;
            bool is_match = pass == 0 ? f->ref.id == 0 : is_evictable(f) && f->is_referenced == (pass == 2);
            if (is_match) { *out = slot; return true; }
        }
    }
    return false;
}

candidate_wal_status_t candidate_buffer_plan_frames(candidate_buffer_t *pool, const uint64_t *ids, uint32_t count,
    uint32_t *slots)
{
    uint32_t resident = 0;
    for (uint32_t i = 0; i < count; i++) {
        const buffer_page_t *page = candidate_buffer_page(pool,ids[i]);
        slots[i] = page == NULL ? BUFFER_NO_FRAME : page->frame;
        if (slots[i] != BUFFER_NO_FRAME) resident++;
    }
    if (count - resident > pool->frame_count - resident) return CANDIDATE_WAL_LIMIT;
    for (uint32_t i = 0; i < count; i++) {
        if (slots[i] != BUFFER_NO_FRAME) continue;
        if (!pick_frame(pool,slots,count,&slots[i])) { for (uint32_t j = i; j < count; j++) slots[j] = BUFFER_NO_FRAME; return CANDIDATE_WAL_BUSY; }
    }
    return CANDIDATE_WAL_OK;
}

void candidate_buffer_take_frame(candidate_buffer_t *pool, uint32_t slot, buffer_page_t *page)
{
    buffer_frame_t *f = &pool->frames[slot];
    if (page->frame == slot) { f->is_referenced = true; return; }
    if (f->ref.id != 0) {
        buffer_page_t *old = candidate_buffer_page(pool,f->ref.id);
        if (old != NULL) old->frame = BUFFER_NO_FRAME;
        candidate_buffer_count(&pool->evictions,1);
    }
    for (uint32_t s = pool->clock_hand; s != slot; s = (s + 1) % pool->frame_count)
        if (is_evictable(&pool->frames[s])) pool->frames[s].is_referenced = false;
    uint64_t generation = f->frame_generation + 1;
    *f = (buffer_frame_t){.ref = {page->id,page->generation},.page_lsn = page->last_lsn,.frame_generation = generation,.is_referenced = true};
    page->frame = slot;
    pool->clock_hand = (slot + 1) % pool->frame_count;
}

static candidate_wal_status_t check_options(const candidate_buffer_options_t *o, const candidate_wal_memory_t *base)
{
    uint8_t uuid = 0; for (unsigned i = 0; i < 16; i++) uuid |= o->uuid[i];
    if (base->allocate == NULL || base->release == NULL || o->wal == NULL || o->control == NULL) return CANDIDATE_WAL_INVALID;
    if (o->target.read == NULL || o->target.write == NULL || o->target.sync == NULL) return CANDIDATE_WAL_INVALID;
    if (uuid == 0 || o->owner_epoch == 0 || o->frames == 0 || o->leases == 0) return CANDIDATE_WAL_INVALID;
    if (o->frame_budget_bytes > CANDIDATE_BUFFER_FRAME_BUDGET || o->staging_budget_bytes > CANDIDATE_BUFFER_STAGING_BUDGET)
        return CANDIDATE_WAL_INVALID;
    if (o->total_budget_bytes > CANDIDATE_BUFFER_TOTAL_BUDGET || o->staging_budget_bytes < 4096) return CANDIDATE_WAL_INVALID;
    if ((uint64_t)o->frames * (CANDIDATE_PAGE_BYTES + sizeof(buffer_frame_t)) > o->frame_budget_bytes) return CANDIDATE_WAL_LIMIT;
    return CANDIDATE_WAL_OK;
}

static void release_arrays(candidate_buffer_t *pool)
{
    candidate_buffer_release(pool,pool->frames,(size_t)pool->frame_count * sizeof(*pool->frames));
    candidate_buffer_release(pool,pool->bytes,(size_t)pool->frame_count * CANDIDATE_PAGE_BYTES);
    candidate_buffer_release(pool,pool->directory,(size_t)BUFFER_DIRECTORY_SLOTS * sizeof(*pool->directory));
    candidate_buffer_release(pool,pool->leases,(size_t)pool->lease_capacity * sizeof(*pool->leases));
    candidate_buffer_release(pool,pool->transactions,(size_t)CANDIDATE_WITNESS_TRANSACTIONS * sizeof(*pool->transactions));
    candidate_buffer_release(pool,pool->staging,pool->staging_bytes);
    candidate_buffer_release(pool,pool->install,(size_t)CANDIDATE_BUFFER_GROUP_PAGES * CANDIDATE_PAGE_BYTES);
    candidate_buffer_release(pool,pool->scratch,BUFFER_SCRATCH_BYTES);
}

static candidate_wal_status_t allocate_arrays(candidate_buffer_t *pool)
{
    pool->frames = candidate_buffer_allocate(pool,(size_t)pool->frame_count * sizeof(*pool->frames));
    if (pool->frames != NULL) pool->bytes = candidate_buffer_allocate(pool,(size_t)pool->frame_count * CANDIDATE_PAGE_BYTES);
    if (pool->bytes != NULL) pool->directory = candidate_buffer_allocate(pool,(size_t)BUFFER_DIRECTORY_SLOTS * sizeof(*pool->directory));
    if (pool->directory != NULL) pool->leases = candidate_buffer_allocate(pool,(size_t)pool->lease_capacity * sizeof(*pool->leases));
    if (pool->leases != NULL)
        pool->transactions = candidate_buffer_allocate(pool,(size_t)CANDIDATE_WITNESS_TRANSACTIONS * sizeof(*pool->transactions));
    if (pool->transactions != NULL) pool->staging = candidate_buffer_allocate(pool,pool->staging_bytes);
    if (pool->staging != NULL) pool->install = candidate_buffer_allocate(pool,(size_t)CANDIDATE_BUFFER_GROUP_PAGES * CANDIDATE_PAGE_BYTES);
    if (pool->install != NULL) pool->scratch = candidate_buffer_allocate(pool,BUFFER_SCRATCH_BYTES);
    if (pool->scratch == NULL) return candidate_buffer_result(pool,CANDIDATE_WAL_NOMEM);
    memset(pool->frames,0,(size_t)pool->frame_count * sizeof(*pool->frames));
    memset(pool->directory,0,(size_t)BUFFER_DIRECTORY_SLOTS * sizeof(*pool->directory));
    memset(pool->leases,0,(size_t)pool->lease_capacity * sizeof(*pool->leases));
    pool->frame_bytes = (size_t)pool->frame_count * (CANDIDATE_PAGE_BYTES + sizeof(buffer_frame_t));
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t compare_pages(candidate_buffer_t *pool, candidate_redo_preview_t *preview)
{
    uint8_t *canonical = pool->install, *data = pool->install + CANDIDATE_PAGE_BYTES;
    for (uint32_t i = 0; i < BUFFER_DIRECTORY_SLOTS; i++) {
        const buffer_page_t *page = &pool->directory[i];
        if (page->id == 0) continue;
        candidate_wal_status_t status = candidate_redo_preview_load(preview,(candidate_page_ref_t){page->id,page->generation},canonical);
        if (status != CANDIDATE_WAL_OK) return status;
        bool is_present = false;
        status = pool->options.target.read(pool->options.target.context,page->id,data,&is_present);
        if (status != CANDIDATE_WAL_OK) return status;
        if (!is_present || memcmp(canonical,data,CANDIDATE_PAGE_BYTES) != 0) return CANDIDATE_WAL_UNSUPPORTED;
        if (read_le(data + 40,8) != page->last_lsn) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t corroborate(candidate_buffer_t *pool)
{
    candidate_redo_options_t request = {.checkpoint = &pool->anchor,.target = pool->options.target,.memory = &pool->memory};
    memcpy(request.uuid,pool->options.uuid,16);
    candidate_redo_preview_t *preview = NULL;
    candidate_wal_status_t status = candidate_redo_preview_open(pool->options.wal,&request,&preview);
    if (status == CANDIDATE_WAL_OK && candidate_redo_preview_info(preview).distinct_pages != pool->known_pages) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = compare_pages(pool,preview);
    candidate_redo_preview_close(&preview);
    return candidate_buffer_result(pool,status);
}

candidate_wal_status_t candidate_buffer_prove(candidate_buffer_t *pool, const candidate_control_state_t *anchor)
{
    candidate_redo_options_t request = {.checkpoint = anchor,.target = pool->options.target,.memory = &pool->memory};
    memcpy(request.uuid,pool->options.uuid,16);
    candidate_redo_preview_t *preview = NULL;
    candidate_wal_status_t status = candidate_redo_preview_open(pool->options.wal,&request,&preview);
    candidate_recovery_view_request_t proof = {.anchor = *anchor,.load = {.root = anchor->outcome_root,
        .load = candidate_redo_preview_load,.context = preview,.memory = &pool->memory},
        .mode = CANDIDATE_RECOVERY_VIEW_PREVIEW,.owner_epoch = pool->options.owner_epoch};
    memcpy(proof.load.uuid,pool->options.uuid,16);
    candidate_recovery_view_t *view = NULL;
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_view_open(pool->options.wal,&proof,&view);
    candidate_redo_preview_close(&preview);
    candidate_recovery_view_close(&view);
    return candidate_buffer_result(pool,status);
}

static candidate_wal_status_t check_baseline(const buffer_scan_t *scan)
{
    const candidate_witness_info_t *w = &scan->observed.witness;
    if (w->transport.has_tail || w->unfinished != 0 || w->incomplete_group_lsn != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (!scan->observed.system.has_reservation || !scan->observed.system.has_epoch) return CANDIDATE_WAL_UNSUPPORTED;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t start(candidate_buffer_t *pool)
{
    candidate_control_info_t control = candidate_control_info(pool->options.control);
    if (control.selected < 0 || control.is_poisoned) return CANDIDATE_WAL_UNSUPPORTED;
    pool->anchor = control.slots[control.selected];
    if (memcmp(pool->anchor.uuid,pool->options.uuid,16) != 0 || pool->anchor.backend != CANDIDATE_CONTROL_WAL) return CANDIDATE_WAL_UNSUPPORTED;
    if (pool->options.owner_epoch < pool->anchor.owner_epoch) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_wal_status_t status = candidate_wal_ensure_pin(pool->options.wal);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_result(pool,status);
    buffer_scan_t scan = {.is_opening = true};
    status = candidate_buffer_scan(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = check_baseline(&scan);
    pool->observed.cut = scan.observed.cut;
    pool->admitted_lsn = scan.observed.last_group_end;
    pool->fpi_floor_lsn = scan.observed.last_checkpoint_begin;
    candidate_buffer_end_scan(&scan);
    if (status == CANDIDATE_WAL_OK) status = corroborate(pool);
    if (status == CANDIDATE_WAL_OK && pool->anchor.catalog_root.id != 0) status = candidate_buffer_prove(pool,&pool->anchor);
    buffer_scan_t again = {.target_end = 0};
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_scan(pool,&again);
    bool is_same = memcmp(&again.observed.cut,&pool->observed.cut,sizeof(pool->observed.cut)) == 0;
    if (status == CANDIDATE_WAL_OK && (!is_same || again.observed.unadmitted_groups != 0)) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&again);
    candidate_buffer_end_scan(&again);
    if (status != CANDIDATE_WAL_OK) return status;
    pool->baseline_lsn = pool->admitted_lsn;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_open(const candidate_buffer_options_t *options, candidate_buffer_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (options == NULL) return CANDIDATE_WAL_INVALID;
    candidate_buffer_options_t saved = *options;
    candidate_wal_memory_t base = {NULL,allocate_default,release_default};
    if (saved.memory != NULL) base = *saved.memory;
    if (saved.frames == 0) saved.frames = CANDIDATE_BUFFER_FRAMES;
    if (saved.leases == 0) saved.leases = CANDIDATE_BUFFER_LEASES;
    if (saved.frame_budget_bytes == 0) saved.frame_budget_bytes = CANDIDATE_BUFFER_FRAME_BUDGET;
    if (saved.staging_budget_bytes == 0) saved.staging_budget_bytes = CANDIDATE_BUFFER_STAGING_BUDGET;
    if (saved.total_budget_bytes == 0) saved.total_budget_bytes = CANDIDATE_BUFFER_TOTAL_BUDGET;
    candidate_wal_status_t status = check_options(&saved,&base);
    if (status != CANDIDATE_WAL_OK) return status;
    size_t borrowed = candidate_wal_info(saved.wal).resident_bytes + candidate_control_info(saved.control).resident_bytes;
    if (borrowed > saved.total_budget_bytes || sizeof(candidate_buffer_t) > saved.total_budget_bytes - borrowed)
        return CANDIDATE_WAL_LIMIT;
    uint64_t pool_id = 0;
    if (!issue_pool_id(&pool_id)) return CANDIDATE_WAL_LIMIT;
    candidate_buffer_t *pool = base.allocate(base.context,sizeof(*pool));
    if (pool == NULL) return CANDIDATE_WAL_NOMEM;
    memset(pool,0,sizeof(*pool));
    pool->meter = (buffer_meter_t){.base = base,.limit = saved.total_budget_bytes,.used = sizeof(*pool),.peak = sizeof(*pool),
        .borrowed = borrowed};
    pool->memory = (candidate_wal_memory_t){&pool->meter,meter_allocate,meter_release};
    saved.memory = NULL; pool->options = saved;
    pool->frame_count = saved.frames; pool->lease_capacity = saved.leases; pool->staging_bytes = saved.staging_budget_bytes;
    pool->process_id = getpid(); pool->pool_id = pool_id;
    status = allocate_arrays(pool);
    if (status == CANDIDATE_WAL_OK) { pool->operation = BUFFER_ADMIT; status = start(pool); pool->operation = BUFFER_IDLE; }
    if (status != CANDIDATE_WAL_OK) {
        pool->operation = BUFFER_CLOSE;
        release_arrays(pool); base.release(base.context,pool,sizeof(*pool));
        return status;
    }
    *out = pool;
    return CANDIDATE_WAL_OK;
}

static buffer_lease_t *find_lease(candidate_buffer_t *pool, const candidate_buffer_lease_t *lease)
{
    if (lease == NULL || lease->pool_id != pool->pool_id || lease->slot >= pool->frame_count) return NULL;
    if (pool->frames[lease->slot].frame_generation != lease->frame_generation) return NULL;
    for (uint32_t i = 0; i < pool->lease_capacity; i++) {
        buffer_lease_t *l = &pool->leases[i];
        if (l->lease_id != 0 && l->lease_id == lease->lease_id && l->slot == lease->slot) return l;
    }
    return NULL;
}

static candidate_wal_status_t pin_page(candidate_buffer_t *pool, candidate_page_ref_t ref, candidate_buffer_lease_t *out)
{
    buffer_page_t *page = candidate_buffer_page(pool,ref.id);
    if (page == NULL || page->generation != ref.generation) return CANDIDATE_WAL_INVALID;
    if (pool->active_leases == pool->lease_capacity || pool->lease_counter >= BUFFER_COUNTER_LIMIT) return CANDIDATE_WAL_LIMIT;
    uint32_t slot = page->frame;
    if (slot == BUFFER_NO_FRAME) {
        candidate_wal_status_t status = candidate_buffer_plan_frames(pool,&page->id,1,&slot);
        if (status == CANDIDATE_WAL_OK) status = candidate_buffer_load(pool,page,pool->install);
        if (status == CANDIDATE_WAL_CORRUPT) return candidate_buffer_fence(pool,status);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_buffer_take_frame(pool,slot,page);
        memcpy(candidate_buffer_frame_bytes(pool,slot),pool->install,CANDIDATE_PAGE_BYTES);
    }
    buffer_lease_t *free_lease = NULL;
    for (uint32_t i = 0; i < pool->lease_capacity && free_lease == NULL; i++) if (pool->leases[i].lease_id == 0) free_lease = &pool->leases[i];
    *free_lease = (buffer_lease_t){++pool->lease_counter,slot};
    pool->frames[slot].pins++; pool->frames[slot].is_referenced = true; pool->active_leases++;
    *out = (candidate_buffer_lease_t){slot,pool->frames[slot].frame_generation,pool->pool_id,free_lease->lease_id};
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_pin(candidate_buffer_t *pool, candidate_page_ref_t ref, candidate_buffer_lease_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_lease_t){0};
    if (pool == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_PIN);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_buffer_lease_t lease;
    status = pin_page(pool,ref,&lease);
    if (status == CANDIDATE_WAL_OK) *out = lease;
    candidate_buffer_leave(pool,BUFFER_IDLE);
    return status;
}

candidate_wal_status_t candidate_buffer_copy(candidate_buffer_t *pool, const candidate_buffer_lease_t *lease,
    uint8_t out[CANDIDATE_PAGE_BYTES], uint64_t *page_lsn)
{
    if (pool == NULL || out == NULL || page_lsn == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_PIN);
    if (status != CANDIDATE_WAL_OK) return status;
    if (find_lease(pool,lease) == NULL) status = CANDIDATE_WAL_STALE;
    else {
        memcpy(out,candidate_buffer_frame_bytes(pool,lease->slot),CANDIDATE_PAGE_BYTES);
        *page_lsn = pool->frames[lease->slot].page_lsn;
    }
    candidate_buffer_leave(pool,BUFFER_IDLE);
    return status;
}

candidate_wal_status_t candidate_buffer_unpin(candidate_buffer_t *pool, candidate_buffer_lease_t *lease)
{
    if (pool == NULL || lease == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_PIN);
    if (status != CANDIDATE_WAL_OK) return status;
    buffer_lease_t *l = find_lease(pool,lease);
    if (l == NULL) status = CANDIDATE_WAL_STALE;
    else {
        pool->frames[l->slot].pins--; pool->active_leases--;
        *l = (buffer_lease_t){0}; *lease = (candidate_buffer_lease_t){0};
    }
    candidate_buffer_leave(pool,BUFFER_IDLE);
    return status;
}

candidate_buffer_info_t candidate_buffer_info(const candidate_buffer_t *pool)
{
    if (pool == NULL) return (candidate_buffer_info_t){0};
    candidate_buffer_info_t info = {.anchor = pool->anchor,.pool_id = pool->pool_id,.baseline_lsn = pool->baseline_lsn,
        .admitted_lsn = pool->admitted_lsn,.observed_end_lsn = pool->observed.cut.end_lsn,.fpi_floor_lsn = pool->fpi_floor_lsn,
        .last_group_id = pool->observed.max_group_id,.page_highwater = pool->observed.system.page_highwater,
        .groups_admitted = pool->groups_admitted,.pages_written = pool->pages_written,.data_syncs = pool->data_syncs,
        .evictions = pool->evictions,.frames = pool->frame_count,.leases = pool->active_leases,.known_pages = pool->known_pages,
        .transactions = pool->transaction_count,.frame_bytes = pool->frame_bytes,.staging_bytes = pool->staging_bytes,
        .requested_bytes = pool->meter.used,.peak_requested_bytes = pool->meter.peak,.budget_bytes = pool->meter.limit,
        .borrowed_bytes = pool->meter.borrowed,
        .is_fenced = pool->is_fenced};
    if (pool->operation == BUFFER_CLOSE) return info;
    for (uint32_t i = 0; i < pool->frame_count; i++) {
        const buffer_frame_t *f = &pool->frames[i];
        if (f->ref.id != 0) info.resident++;
        if (f->is_dirty) info.dirty++;
        if (f->pins != 0) info.pinned++;
    }
    return info;
}

candidate_wal_status_t candidate_buffer_close(candidate_buffer_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_buffer_t *pool = *out;
    if (pool == NULL) return CANDIDATE_WAL_OK;
    if (pool->operation != BUFFER_IDLE) return CANDIDATE_WAL_BUSY;
    *out = NULL;
    pool->operation = BUFFER_CLOSE;
    candidate_wal_memory_t base = pool->meter.base;
    release_arrays(pool);
    base.release(base.context,pool,sizeof(*pool));
    return CANDIDATE_WAL_OK;
}
