#include "candidate_buffer_internal.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_digest.h"
#include "storage/checksum.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"

typedef struct {
    candidate_buffer_t *pool;
    buffer_scan_t *scan;
    candidate_wal_status_t status;
    uint64_t group_begin, group_id;
    uint64_t page_ids[CANDIDATE_BUFFER_GROUP_PAGES], page_generations[CANDIDATE_BUFFER_GROUP_PAGES];
    bool page_images[CANDIDATE_BUFFER_GROUP_PAGES];
    uint32_t pages;
    bool is_capturing;
} observer_t;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t frame_end(const candidate_wal_record_t *r)
{
    return r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7);
}

static uint32_t page_crc(const uint8_t *p)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,p,52);
    crc = crc32c(crc,zero,4);
    return crc32c(crc,p + 56,CANDIDATE_PAGE_BYTES - 56);
}

candidate_wal_status_t candidate_buffer_cut_add(candidate_buffer_t *pool, buffer_cut_t *cut, const candidate_wal_record_t *r)
{
    if (r->payload_bytes > BUFFER_SCRATCH_BYTES - 80) return CANDIDATE_WAL_LIMIT;
    uint8_t *p = pool->scratch;
    memcpy(p,cut->digest,32); memset(p + 32,0,48);
    write_le(p + 32,r->type,2); write_le(p + 40,r->lsn,8); write_le(p + 48,r->transaction_id,8);
    write_le(p + 56,r->previous_lsn,8); write_le(p + 64,r->owner_epoch,8); write_le(p + 72,r->payload_bytes,8);
    memcpy(p + 80,r->payload,r->payload_bytes);
    candidate_wal_status_t status = candidate_digest_sha256(p,80 + r->payload_bytes,cut->digest);
    if (status != CANDIDATE_WAL_OK) return status;
    cut->records++; cut->end_lsn = frame_end(r);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t chain_record(observer_t *o, const candidate_wal_record_t *r)
{
    buffer_cut_t *cut = &o->scan->observed.cut;
    const buffer_cut_t *known = &o->pool->observed.cut;
    candidate_wal_status_t status = candidate_buffer_cut_add(o->pool,cut,r);
    if (status != CANDIDATE_WAL_OK || known->records == 0 || cut->records != known->records) return status;
    bool is_same = memcmp(cut->digest,known->digest,32) == 0 && cut->end_lsn == known->end_lsn;
    return is_same ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t capture_record(observer_t *o, const candidate_wal_record_t *r)
{
    buffer_capture_t *c = o->scan->capture;
    if (c->records == CANDIDATE_WITNESS_RECORDS) return CANDIDATE_WAL_CORRUPT;
    if (r->payload_bytes > o->pool->staging_bytes - c->used) return CANDIDATE_WAL_LIMIT;
    buffer_record_t *k = &c->list[c->records++];
    *k = (buffer_record_t){r->type,r->lsn,r->transaction_id,r->previous_lsn,r->owner_epoch,c->used,r->payload_bytes};
    memcpy(o->pool->staging + c->used,r->payload,r->payload_bytes);
    c->used += r->payload_bytes;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t apply_opening(observer_t *o, uint64_t end)
{
    for (uint32_t i = 0; i < o->pages; i++) {
        buffer_page_t *page = NULL;
        candidate_wal_status_t status = candidate_buffer_add_page(o->pool,o->page_ids[i],&page);
        if (status != CANDIDATE_WAL_OK) return status;
        page->generation = o->page_generations[i]; page->last_lsn = end;
        if (o->page_images[i]) page->image_lsn = end;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t finish_group(observer_t *o, const candidate_wal_record_t *r)
{
    buffer_observation_t *obs = &o->scan->observed;
    uint64_t end = frame_end(r);
    if (o->scan->is_opening) {
        candidate_wal_status_t status = apply_opening(o,end);
        if (status != CANDIDATE_WAL_OK) return status;
    } else if (end > o->pool->admitted_lsn) {
        obs->unadmitted_groups++;
        if (o->is_capturing) { o->scan->capture->end_lsn = end; o->is_capturing = false; }
    }
    obs->last_group_end = end;
    if (o->group_id > obs->max_group_id) obs->max_group_id = o->group_id;
    o->group_begin = 0;
    return CANDIDATE_WAL_OK;
}

static void start_group(observer_t *o, const candidate_wal_record_t *r)
{
    o->group_begin = r->lsn; o->group_id = r->payload_bytes >= 8 ? read_le(r->payload,8) : 0;
    o->pages = 0;
    buffer_capture_t *c = o->scan->capture;
    bool is_first = !o->scan->is_opening && r->lsn >= o->pool->admitted_lsn && o->scan->observed.unadmitted_groups == 0;
    if (c == NULL || !is_first || c->begin_lsn != 0) return;
    *c = (buffer_capture_t){.group_id = o->group_id,.transaction_id = r->transaction_id,.begin_lsn = r->lsn};
    o->is_capturing = true;
}

static candidate_wal_status_t track_group(observer_t *o, const candidate_wal_record_t *r)
{
    if (r->type == 12) { o->scan->observed.last_checkpoint_begin = r->lsn; return CANDIDATE_WAL_OK; }
    if (r->type == 3) start_group(o,r);
    if (o->group_begin == 0) return CANDIDATE_WAL_OK;
    if ((r->type == 4 || r->type == 5) && r->payload_bytes >= 32 && o->pages < CANDIDATE_BUFFER_GROUP_PAGES) {
        o->page_ids[o->pages] = read_le(r->payload + 8,8); o->page_generations[o->pages] = read_le(r->payload + 16,8);
        o->page_images[o->pages++] = r->type == 4;
    }
    if (o->is_capturing) {
        candidate_wal_status_t status = capture_record(o,r);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return r->type == 8 ? finish_group(o,r) : CANDIDATE_WAL_OK;
}

static bool observe(void *context, const candidate_wal_record_t *r)
{
    observer_t *o = context;
    if (r->owner_epoch > o->scan->observed.highest_owner) o->scan->observed.highest_owner = r->owner_epoch;
    o->status = chain_record(o,r);
    if (o->status == CANDIDATE_WAL_OK) o->status = track_group(o,r);
    return o->status == CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_scan(candidate_buffer_t *pool, buffer_scan_t *scan)
{
    observer_t o = {.pool = pool,.scan = scan};
    if (scan->capture != NULL) memset(scan->capture,0,sizeof(*scan->capture));
    candidate_witness_options_t request = {.memory = &pool->memory,.enable_system_groups = true};
    memcpy(request.uuid,pool->options.uuid,16);
    candidate_wal_status_t status = candidate_witness_analyze_observed(pool->options.wal,&request,&pool->anchor,observe,&o,&scan->witness);
    if (status == CANDIDATE_WAL_STOPPED && o.status != CANDIDATE_WAL_OK) status = o.status;
    if (status == CANDIDATE_WAL_OK && scan->observed.cut.records < pool->observed.cut.records) status = CANDIDATE_WAL_CORRUPT;
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_source_failure(pool,status);
    scan->observed.witness = candidate_wal_witness_info(scan->witness);
    scan->observed.system = candidate_wal_witness_system_info(scan->witness);
    scan->observed.cut.tail_bytes = scan->observed.witness.transport.tail_bytes;
    return CANDIDATE_WAL_OK;
}

typedef struct {
    candidate_buffer_t *pool;
    const buffer_cut_t *expected;
    buffer_cut_t cut;
    candidate_wal_status_t status;
} verifier_t;

static bool verify_record(void *context, const candidate_wal_record_t *r)
{
    verifier_t *v = context;
    v->status = candidate_buffer_cut_add(v->pool,&v->cut,r);
    if (v->status == CANDIDATE_WAL_OK && v->cut.records > v->expected->records) v->status = CANDIDATE_WAL_CORRUPT;
    return v->status == CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_verify_cut(candidate_buffer_t *pool, const buffer_cut_t *expected)
{
    verifier_t v = {.pool = pool,.expected = expected};
    candidate_wal_info_t info = {0};
    candidate_wal_status_t status = candidate_wal_scan(pool->options.wal,verify_record,&v,&info);
    if (status == CANDIDATE_WAL_STOPPED && v.status != CANDIDATE_WAL_OK) status = v.status;
    if (status == CANDIDATE_WAL_OK) {
        v.cut.tail_bytes = info.tail_bytes;
        if (memcmp(&v.cut,expected,sizeof(v.cut)) != 0) status = CANDIDATE_WAL_CORRUPT;
    }
    return status == CANDIDATE_WAL_OK ? status : candidate_buffer_source_failure(pool,status);
}

candidate_wal_status_t candidate_buffer_source_failure(candidate_buffer_t *pool, candidate_wal_status_t status)
{
    bool is_established = pool->observed.cut.records != 0;
    if (status == CANDIDATE_WAL_CORRUPT && is_established) candidate_wal_reject(pool->options.wal);
    bool is_persistent = status == CANDIDATE_WAL_CORRUPT || status == CANDIDATE_WAL_IO || status == CANDIDATE_WAL_STALE ||
        status == CANDIDATE_WAL_POISONED || status == CANDIDATE_WAL_UNSUPPORTED;
    if (is_persistent && is_established) return candidate_buffer_fence(pool,status);
    return candidate_buffer_result(pool,status);
}

static int compare_transactions(const void *left, const void *right)
{
    const candidate_checkpoint_transaction_t *a = left, *b = right;
    if (a->id == b->id) return 0;
    return a->id < b->id ? -1 : 1;
}

candidate_wal_status_t candidate_buffer_commit_scan(candidate_buffer_t *pool, buffer_scan_t *scan)
{
    uint32_t count = scan->observed.witness.transactions;
    if (count > CANDIDATE_WITNESS_TRANSACTIONS) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < count; i++)
        if (!candidate_witness_checkpoint_at(scan->witness,i,&pool->transactions[i])) return CANDIDATE_WAL_CORRUPT;
    qsort(pool->transactions,count,sizeof(pool->transactions[0]),compare_transactions);
    pool->transaction_count = count;
    pool->observed = scan->observed;
    return CANDIDATE_WAL_OK;
}

void candidate_buffer_end_scan(buffer_scan_t *scan)
{
    candidate_wal_witness_close(&scan->witness);
}

static candidate_wal_status_t apply_delta(uint8_t *page, const uint8_t *p, size_t size)
{
    if (size < 40 || read_le(p + 36,4) != 0) return CANDIDATE_WAL_CORRUPT;
    unsigned count = (unsigned)read_le(p + 32,4);
    if (count == 0 || count > 128) return CANDIDATE_WAL_CORRUPT;
    size_t at = 40, previous_end = 64;
    for (unsigned i = 0; i < count; i++) {
        if (size - at < 8) return CANDIDATE_WAL_CORRUPT;
        size_t offset = (size_t)read_le(p + at,4), bytes = (size_t)read_le(p + at + 4,4); at += 8;
        if (offset < previous_end || offset > CANDIDATE_PAGE_BYTES || bytes == 0) return CANDIDATE_WAL_CORRUPT;
        if (bytes > CANDIDATE_PAGE_BYTES - offset || bytes > size - at) return CANDIDATE_WAL_CORRUPT;
        memcpy(page + offset,p + at,bytes); at += bytes; previous_end = offset + bytes;
    }
    return at == size ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_history(const candidate_buffer_t *pool, const buffer_page_t *page, const buffer_change_t *c)
{
    if (c->id == 0 || c->generation == 0) return CANDIDATE_WAL_CORRUPT;
    bool is_same = page != NULL && page->generation == c->generation;
    if (is_same) {
        if (c->previous_lsn != page->last_lsn) return CANDIDATE_WAL_CORRUPT;
        if (!c->is_image && page->image_lsn <= pool->fpi_floor_lsn) return CANDIDATE_WAL_CORRUPT;
        return CANDIDATE_WAL_OK;
    }
    if (!c->is_image || c->previous_lsn != 0) return CANDIDATE_WAL_CORRUPT;
    if (page != NULL && page->generation != 0 && (page->generation == UINT64_MAX || c->generation != page->generation + 1))
        return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_check_versions(const uint8_t uuid[16], const uint8_t *bytes,
    const candidate_wal_witness_t *witness)
{
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode_header(uuid,bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK || info.type != 1) return status;
    status = candidate_page_decode(uuid,bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    for (uint32_t i = 0; i < info.slots; i++) {
        candidate_version_ref_t ref = {info.location.ref,i,(uint32_t)read_le(bytes + 76 + i * 8,4)};
        candidate_page_version_t v;
        status = candidate_page_get_version(uuid,bytes,CANDIDATE_PAGE_BYTES,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(witness,v.transaction_id);
        if (t == NULL) return CANDIDATE_WAL_CORRUPT;
        if (v.frozen_csn != 0 && (t->state != CANDIDATE_WITNESS_COMMIT || t->csn != v.frozen_csn)) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t build_page(candidate_buffer_t *pool, const buffer_scan_t *scan, uint32_t index)
{
    const buffer_capture_t *capture = scan->capture;
    const buffer_change_t *c = &capture->changes[index];
    const buffer_record_t *r = &capture->list[c->record];
    const uint8_t *payload = pool->staging + r->offset;
    uint8_t *out = pool->install + (size_t)index * CANDIDATE_PAGE_BYTES;
    buffer_page_t *page = candidate_buffer_page(pool,c->id);
    candidate_wal_status_t status = check_history(pool,page,c);
    if (status != CANDIDATE_WAL_OK) return status;
    if (c->is_image) memcpy(out,payload + 32,CANDIDATE_PAGE_BYTES);
    else {
        if (page->frame != BUFFER_NO_FRAME) memcpy(out,candidate_buffer_frame_bytes(pool,page->frame),CANDIDATE_PAGE_BYTES);
        else if ((status = candidate_buffer_load(pool,page,out)) != CANDIDATE_WAL_OK) return status;
        status = apply_delta(out,payload,r->bytes);
        if (status != CANDIDATE_WAL_OK) return status;
        write_le(out + 40,capture->end_lsn,8); write_le(out + 52,page_crc(out),4);
    }
    candidate_page_info_t info;
    status = candidate_buffer_check_page(pool->options.uuid,out,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id != c->id || info.location.ref.generation != c->generation) return CANDIDATE_WAL_CORRUPT;
    if (info.location.lsn != capture->end_lsn) return CANDIDATE_WAL_CORRUPT;
    return candidate_buffer_check_versions(pool->options.uuid,out,scan->witness);
}

static bool has_contributor(const buffer_frame_t *f, uint64_t id)
{
    for (uint32_t i = 0; i < f->contributor_count; i++) if (f->contributors[i] == id) return true;
    return false;
}

candidate_wal_status_t candidate_buffer_check_capacity(candidate_buffer_t *pool, buffer_capture_t *capture)
{
    uint64_t ids[CANDIDATE_BUFFER_GROUP_PAGES]; uint32_t added = 0;
    for (uint32_t i = 0; i < capture->pages; i++) {
        const buffer_page_t *page = candidate_buffer_page(pool,capture->changes[i].id);
        ids[i] = capture->changes[i].id;
        if (page == NULL) { added++; continue; }
        if (page->frame == BUFFER_NO_FRAME) continue;
        const buffer_frame_t *f = &pool->frames[page->frame];
        bool is_regenerated = page->generation != capture->changes[i].generation;
        if (f->pins != 0 && is_regenerated) return CANDIDATE_WAL_BUSY;
        if (f->dirty_generation >= BUFFER_COUNTER_LIMIT || (is_regenerated && f->frame_generation >= BUFFER_COUNTER_LIMIT))
            return CANDIDATE_WAL_LIMIT;
        bool adds = capture->transaction_id != 0 && !(f->is_dirty && has_contributor(f,capture->transaction_id));
        uint32_t kept = f->is_dirty ? f->contributor_count : 0;
        if (adds && kept == CANDIDATE_BUFFER_CONTRIBUTORS) return CANDIDATE_WAL_LIMIT;
    }
    if (added > CANDIDATE_ALLOCATION_ENTRIES - pool->known_pages) return candidate_buffer_fence(pool,CANDIDATE_WAL_LIMIT);
    return candidate_buffer_plan_frames(pool,ids,capture->pages,capture->slots);
}

static candidate_wal_status_t collect_changes(candidate_buffer_t *pool, buffer_capture_t *capture)
{
    capture->pages = 0;
    for (uint32_t i = 0; i < capture->records; i++) {
        const buffer_record_t *r = &capture->list[i];
        if (r->type != 4 && r->type != 5) continue;
        if (r->bytes < 32 || capture->pages == CANDIDATE_BUFFER_GROUP_PAGES) return CANDIDATE_WAL_CORRUPT;
        const uint8_t *p = pool->staging + r->offset;
        if (read_le(p,8) != capture->group_id) return CANDIDATE_WAL_CORRUPT;
        capture->changes[capture->pages++] = (buffer_change_t){read_le(p + 8,8),read_le(p + 16,8),read_le(p + 24,8),i,r->type == 4};
    }
    return CANDIDATE_WAL_OK;
}

static bool has_new_page(candidate_buffer_t *pool, const buffer_capture_t *capture)
{
    for (uint32_t i = 0; i < capture->pages; i++) {
        const buffer_page_t *page = candidate_buffer_page(pool,capture->changes[i].id);
        if (page == NULL || page->generation != capture->changes[i].generation) return true;
    }
    return false;
}

static candidate_wal_status_t prove_live(candidate_buffer_t *pool)
{
    candidate_wal_status_t status = candidate_buffer_prove(pool,&pool->anchor);
    if (status == CANDIDATE_WAL_OK || status == CANDIDATE_WAL_NOMEM || status == CANDIDATE_WAL_LIMIT) return status;
    return candidate_buffer_fence(pool,status == CANDIDATE_WAL_IO ? status : CANDIDATE_WAL_CORRUPT);
}

candidate_wal_status_t candidate_buffer_check_capture(candidate_buffer_t *pool, const buffer_scan_t *scan,
    candidate_buffer_admission_t *out)
{
    buffer_capture_t *capture = scan->capture;
    if (scan->observed.unadmitted_groups == 0 || capture->end_lsn == 0) return CANDIDATE_WAL_INVALID;
    if (capture->end_lsn != scan->target_end) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = collect_changes(pool,capture);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < capture->pages; i++) status = build_page(pool,scan,i);
    if (status == CANDIDATE_WAL_CORRUPT || status == CANDIDATE_WAL_UNSUPPORTED) return candidate_buffer_fence(pool,status);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_check_capacity(pool,capture);
    if (status == CANDIDATE_WAL_OK && has_new_page(pool,capture)) status = prove_live(pool);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = (candidate_buffer_admission_t){.group_id = capture->group_id,.begin_lsn = capture->begin_lsn,.end_lsn = capture->end_lsn,
        .pages = capture->pages};
    for (uint32_t i = 0; i < capture->pages; i++) { if (capture->changes[i].is_image) out->images++; else out->deltas++; }
    const candidate_witness_transaction_t *t = candidate_wal_witness_find(scan->witness,capture->transaction_id);
    if (t != NULL) { out->transaction_last_lsn = t->last_lsn; out->transaction_records = t->records; out->transaction_chain_crc = t->chain_crc; }
    return CANDIDATE_WAL_OK;
}

static void add_contributor(buffer_frame_t *f, uint64_t id)
{
    if (id == 0 || has_contributor(f,id)) return;
    f->contributors[f->contributor_count++] = id;
}

candidate_wal_status_t candidate_buffer_install(candidate_buffer_t *pool, const buffer_scan_t *scan)
{
    const buffer_capture_t *capture = scan->capture;
    for (uint32_t i = 0; i < capture->pages; i++) {
        const buffer_change_t *c = &capture->changes[i];
        buffer_page_t *page = NULL;
        if (candidate_buffer_add_page(pool,c->id,&page) != CANDIDATE_WAL_OK) return candidate_buffer_fence(pool,CANDIDATE_WAL_CORRUPT);
        bool is_regenerated = page->frame == capture->slots[i] && page->generation != c->generation;
        candidate_buffer_take_frame(pool,capture->slots[i],page);
        buffer_frame_t *f = &pool->frames[capture->slots[i]];
        if (is_regenerated) f->frame_generation++;
        memcpy(candidate_buffer_frame_bytes(pool,capture->slots[i]),pool->install + (size_t)i * CANDIDATE_PAGE_BYTES,CANDIDATE_PAGE_BYTES);
        if (!f->is_dirty) { f->rec_lsn = capture->begin_lsn; f->contributor_count = 0; }
        else if (f->is_inflight && (is_regenerated || f->pending_rec_lsn == 0)) f->pending_rec_lsn = capture->begin_lsn;
        else if (is_regenerated) f->rec_lsn = capture->begin_lsn;
        add_contributor(f,capture->transaction_id);
        f->ref = (candidate_page_ref_t){c->id,c->generation}; f->page_lsn = capture->end_lsn;
        f->is_dirty = true; f->dirty_generation++; f->is_referenced = true;
        page->generation = c->generation; page->last_lsn = capture->end_lsn;
        if (c->is_image) page->image_lsn = capture->end_lsn;
    }
    pool->admitted_lsn = capture->end_lsn; candidate_buffer_count(&pool->groups_admitted,1);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_buffer_admit_group(candidate_buffer_t *pool, uint64_t end_lsn, candidate_buffer_admission_t *out)
{
    buffer_capture_t capture; candidate_buffer_admission_t receipt;
    buffer_scan_t scan = {.capture = &capture,.target_end = end_lsn};
    candidate_wal_status_t status = candidate_buffer_scan(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_check_capture(pool,&scan,&receipt);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_install(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&scan);
    candidate_buffer_end_scan(&scan);
    if (status == CANDIDATE_WAL_OK) *out = receipt;
    return status;
}

candidate_wal_status_t candidate_buffer_admit(candidate_buffer_t *pool, uint64_t end_lsn, candidate_buffer_admission_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_admission_t){0};
    if (pool == NULL) return CANDIDATE_WAL_INVALID;
    buffer_operation_t previous = pool->operation;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_ADMIT);
    if (status != CANDIDATE_WAL_OK) return status;
    status = candidate_buffer_admit_group(pool,end_lsn,out);
    candidate_buffer_leave(pool,previous);
    return candidate_buffer_result(pool,status);
}
