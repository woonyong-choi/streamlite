#include "candidate_buffer_internal.h"

#include <string.h>

#include "storage/checksum.h"
#include "candidate_wal_internal.h"

#define BEGIN_BYTES 16u
#define END_BYTES 24u
#define IMAGE_BYTES (32u + CANDIDATE_PAGE_BYTES)
#define DELTA_RUNS 128u
#define DELTA_GAP 16u

typedef struct {
    candidate_page_ref_t ref;
    uint64_t previous_lsn, lsn;
    size_t offset, bytes;
    uint32_t runs;
    bool is_image;
} planned_t;

typedef struct {
    uint64_t transaction_id, previous_lsn, group_id, begin_lsn, end_lsn;
    uint32_t count, crc;
    planned_t pages[CANDIDATE_BUFFER_GROUP_PAGES];
} production_t;

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t page_crc(const uint8_t *p)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,p,52);
    crc = crc32c(crc,zero,4);
    return crc32c(crc,p + 56,CANDIDATE_PAGE_BYTES - 56);
}

static uint8_t *desired(candidate_buffer_t *pool, uint32_t index) { return pool->install + (size_t)index * CANDIDATE_PAGE_BYTES; }

static uint8_t *base_of(candidate_buffer_t *pool, uint32_t index) { return pool->scratch + (size_t)index * CANDIDATE_PAGE_BYTES; }

static uint32_t count_runs(const uint8_t *base, const uint8_t *page, size_t *bytes)
{
    uint32_t runs = 0; size_t at = 64; *bytes = 40;
    while (at < CANDIDATE_PAGE_BYTES) {
        if (base[at] == page[at]) { at++; continue; }
        size_t end = at + 1, same = 0;
        for (size_t i = end; i < CANDIDATE_PAGE_BYTES && same < DELTA_GAP; i++) {
            if (base[i] == page[i]) same++;
            else { same = 0; end = i + 1; }
        }
        runs++; *bytes += 8 + (end - at); at = end;
    }
    return runs;
}

static bool needs_image(const candidate_buffer_t *pool, const buffer_page_t *page, const uint8_t *base, const uint8_t *next)
{
    if (page->image_lsn <= pool->fpi_floor_lsn) return true;
    return memcmp(base,next,40) != 0 || memcmp(base + 48,next + 48,4) != 0 || memcmp(base + 56,next + 56,8) != 0;
}

static candidate_wal_status_t plan_page(candidate_buffer_t *pool, production_t *p, uint32_t index)
{
    planned_t *k = &p->pages[index];
    uint8_t *next = desired(pool,index);
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode_header(pool->options.uuid,next,CANDIDATE_PAGE_BYTES,&info);
    if (status == CANDIDATE_WAL_CORRUPT) status = CANDIDATE_WAL_INVALID;
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type == 3) return CANDIDATE_WAL_UNSUPPORTED;
    if (info.location.ref.id != k->ref.id || info.location.ref.generation != k->ref.generation) return CANDIDATE_WAL_INVALID;
    buffer_page_t *page = candidate_buffer_page(pool,k->ref.id);
    if (page == NULL || page->generation != k->ref.generation) return CANDIDATE_WAL_UNSUPPORTED;
    if (info.location.lsn != page->last_lsn) return CANDIDATE_WAL_STALE;
    k->previous_lsn = page->last_lsn; k->is_image = true; k->bytes = IMAGE_BYTES;
    uint8_t *base = base_of(pool,index);
    if (page->frame != BUFFER_NO_FRAME) memcpy(base,candidate_buffer_frame_bytes(pool,page->frame),CANDIDATE_PAGE_BYTES);
    else if ((status = candidate_buffer_load(pool,page,base)) != CANDIDATE_WAL_OK) return status;
    if (needs_image(pool,page,base,next)) return CANDIDATE_WAL_OK;
    size_t bytes; uint32_t runs = count_runs(base,next,&bytes);
    if (runs == 0 || runs > DELTA_RUNS || bytes > IMAGE_BYTES) return CANDIDATE_WAL_OK;
    k->is_image = false; k->bytes = bytes; k->runs = runs;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_request(candidate_buffer_t *pool, const buffer_scan_t *scan, const production_t *p)
{
    const buffer_observation_t *o = &scan->observed;
    if (o->witness.transport.has_tail || o->witness.incomplete_group_lsn != 0 || o->unadmitted_groups != 0) return CANDIDATE_WAL_BUSY;
    const candidate_witness_transaction_t *t = candidate_wal_witness_find(scan->witness,p->transaction_id);
    if (t == NULL || t->state != CANDIDATE_WITNESS_UNFINISHED || !t->has_begin) return CANDIDATE_WAL_INVALID;
    if (t->last_lsn != p->previous_lsn || o->max_group_id == UINT64_MAX) return CANDIDATE_WAL_INVALID;
    if (o->highest_owner > pool->options.owner_epoch) return CANDIDATE_WAL_STALE;
    candidate_wal_cursor_t cursor = candidate_wal_cursor(pool->options.wal);
    return cursor.position == o->witness.transport.valid_end_lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t place(candidate_buffer_t *pool, production_t *p)
{
    candidate_wal_cursor_t cursor = candidate_wal_cursor(pool->options.wal);
    size_t staged = BEGIN_BYTES + END_BYTES;
    for (uint32_t i = 0; i < p->count; i++) {
        if (p->pages[i].bytes > pool->staging_bytes - staged) return CANDIDATE_WAL_LIMIT;
        staged += p->pages[i].bytes;
    }
    if (staged > pool->staging_bytes) return CANDIDATE_WAL_LIMIT;
    candidate_wal_status_t status = candidate_wal_place(pool->options.wal,&cursor,BEGIN_BYTES,&p->begin_lsn);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->count; i++)
        status = candidate_wal_place(pool->options.wal,&cursor,p->pages[i].bytes,&p->pages[i].lsn);
    uint64_t end_record = 0;
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_place(pool->options.wal,&cursor,END_BYTES,&end_record);
    p->end_lsn = cursor.position;
    return status;
}

static candidate_wal_status_t finalize_pages(candidate_buffer_t *pool, production_t *p, const candidate_wal_witness_t *witness)
{
    for (uint32_t i = 0; i < p->count; i++) {
        uint8_t *next = desired(pool,i);
        write_le(next + 40,p->end_lsn,8); write_le(next + 52,page_crc(next),4);
        candidate_page_info_t info;
        candidate_wal_status_t status = candidate_buffer_check_page(pool->options.uuid,next,&info);
        if (status == CANDIDATE_WAL_OK) status = candidate_buffer_check_versions(pool->options.uuid,next,witness);
        if (status == CANDIDATE_WAL_CORRUPT) status = CANDIDATE_WAL_INVALID;
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_capacity(candidate_buffer_t *pool, const production_t *p)
{
    buffer_capture_t *capture = (buffer_capture_t *)(pool->scratch + (size_t)CANDIDATE_BUFFER_GROUP_PAGES * CANDIDATE_PAGE_BYTES);
    memset(capture,0,sizeof(*capture));
    capture->transaction_id = p->transaction_id; capture->pages = p->count;
    for (uint32_t i = 0; i < p->count; i++)
        capture->changes[i] = (buffer_change_t){.id = p->pages[i].ref.id,.generation = p->pages[i].ref.generation,.is_image = p->pages[i].is_image};
    return candidate_buffer_check_capacity(pool,capture);
}

static size_t encode_delta(const uint8_t *base, const uint8_t *page, uint8_t *out)
{
    size_t at = 64, w = 40;
    while (at < CANDIDATE_PAGE_BYTES) {
        if (base[at] == page[at]) { at++; continue; }
        size_t end = at + 1, same = 0;
        for (size_t i = end; i < CANDIDATE_PAGE_BYTES && same < DELTA_GAP; i++) {
            if (base[i] == page[i]) same++;
            else { same = 0; end = i + 1; }
        }
        write_le(out + w,at,4); write_le(out + w + 4,end - at,4); memcpy(out + w + 8,page + at,end - at);
        w += 8 + (end - at); at = end;
    }
    return w;
}

static void encode_records(candidate_buffer_t *pool, production_t *p)
{
    uint8_t *s = pool->staging;
    memset(s,0,BEGIN_BYTES);
    write_le(s,p->group_id,8); write_le(s + 8,p->count,4); write_le(s + 12,p->count,4);
    size_t at = BEGIN_BYTES;
    for (uint32_t i = 0; i < p->count; i++) {
        planned_t *k = &p->pages[i];
        uint8_t *r = s + at; k->offset = at;
        write_le(r,p->group_id,8); write_le(r + 8,k->ref.id,8); write_le(r + 16,k->ref.generation,8); write_le(r + 24,k->previous_lsn,8);
        if (k->is_image) memcpy(r + 32,desired(pool,i),CANDIDATE_PAGE_BYTES);
        else {
            write_le(r + 32,k->runs,4); write_le(r + 36,0,4);
            k->bytes = encode_delta(base_of(pool,i),desired(pool,i),r);
        }
        at += k->bytes;
    }
}

static uint64_t frame_bytes(size_t payload) { return (96 + payload + 7) & ~(uint64_t)7; }

static candidate_wal_status_t bind_record(candidate_buffer_t *pool, production_t *p, buffer_cut_t *cut, const candidate_wal_record_t *r)
{
    if (r->type != 8) p->crc = candidate_wal_witness_crc(p->crc,r);
    return candidate_buffer_cut_add(pool,cut,r);
}

static candidate_wal_status_t append_record(candidate_buffer_t *pool, const candidate_wal_record_t *r)
{
    uint64_t end = 0;
    candidate_wal_status_t status = candidate_wal_append(pool->options.wal,r,&end);
    if (status != CANDIDATE_WAL_OK) return status;
    if (end != r->lsn + frame_bytes(r->payload_bytes)) return CANDIDATE_WAL_CORRUPT;
    return candidate_wal_info(pool->options.wal).highest_owner_epoch == r->owner_epoch ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE;
}

static candidate_wal_status_t visit_group(candidate_buffer_t *pool, production_t *p, buffer_cut_t *cut)
{
    uint8_t finish[END_BYTES] = {0};
    uint64_t previous = p->previous_lsn;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (cut != NULL) p->crc = 0;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->count + 2; i++) {
        candidate_wal_record_t r = {.type = 3,.lsn = p->begin_lsn,.transaction_id = p->transaction_id,.previous_lsn = previous,
            .owner_epoch = pool->options.owner_epoch,.payload = pool->staging,.payload_bytes = BEGIN_BYTES};
        if (i > 0 && i <= p->count) {
            const planned_t *k = &p->pages[i - 1];
            r.type = k->is_image ? 4 : 5; r.lsn = k->lsn; r.payload = pool->staging + k->offset; r.payload_bytes = k->bytes;
        } else if (i > p->count) {
            write_le(finish,p->group_id,8); write_le(finish + 8,p->count,4); write_le(finish + 12,p->count,4);
            write_le(finish + 16,p->crc,4);
            r.type = 8; r.lsn = p->end_lsn - frame_bytes(END_BYTES); r.payload = finish; r.payload_bytes = END_BYTES;
        }
        status = cut != NULL ? bind_record(pool,p,cut,&r) : append_record(pool,&r);
        previous = r.lsn;
    }
    return status;
}

static candidate_wal_status_t copy_request(candidate_buffer_t *pool, const candidate_buffer_group_t *group, production_t *p)
{
    candidate_buffer_group_t saved = *group;
    if (saved.transaction_id == 0 || saved.count == 0 || saved.count > CANDIDATE_BUFFER_GROUP_PAGES || saved.pages == NULL)
        return CANDIDATE_WAL_INVALID;
    *p = (production_t){.transaction_id = saved.transaction_id,.previous_lsn = saved.previous_lsn,.count = saved.count};
    for (uint32_t i = 0; i < saved.count; i++) {
        candidate_buffer_page_t page = saved.pages[i];
        if (page.page == NULL || page.ref.id == 0 || page.ref.generation == 0) return CANDIDATE_WAL_INVALID;
        for (uint32_t j = 0; j < i; j++) if (p->pages[j].ref.id == page.ref.id) return CANDIDATE_WAL_INVALID;
        p->pages[i].ref = page.ref;
        uint8_t *next = desired(pool,i);
        memcpy(next,page.page,CANDIDATE_PAGE_BYTES);
        write_le(next + 52,page_crc(next),4);
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t produce(candidate_buffer_t *pool, production_t *p, candidate_buffer_admission_t *out)
{
    buffer_scan_t scan = {.target_end = 0};
    candidate_wal_status_t status = candidate_buffer_scan(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = check_request(pool,&scan,p);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < p->count; i++) status = plan_page(pool,p,i);
    if (status == CANDIDATE_WAL_OK) { p->group_id = scan.observed.max_group_id + 1; status = place(pool,p); }
    if (status == CANDIDATE_WAL_OK) status = finalize_pages(pool,p,scan.witness);
    if (status == CANDIDATE_WAL_OK) status = check_capacity(pool,p);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_commit_scan(pool,&scan);
    candidate_buffer_end_scan(&scan);
    if (status == CANDIDATE_WAL_CORRUPT) return candidate_buffer_fence(pool,status);
    if (status != CANDIDATE_WAL_OK) return status;
    encode_records(pool,p);
    buffer_cut_t intended = pool->observed.cut;
    status = visit_group(pool,p,&intended);
    if (status != CANDIDATE_WAL_OK) return status;
    pool->observed.cut = intended;
    status = visit_group(pool,p,NULL);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_admit_group(pool,p->end_lsn,out);
    return status == CANDIDATE_WAL_OK ? status : candidate_buffer_fence(pool,status);
}

candidate_wal_status_t candidate_buffer_write_group(candidate_buffer_t *pool, const candidate_buffer_group_t *group,
    candidate_buffer_admission_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_admission_t){0};
    if (pool == NULL || group == NULL) return CANDIDATE_WAL_INVALID;
    buffer_operation_t previous = pool->operation;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_ADMIT);
    if (status != CANDIDATE_WAL_OK) return status;
    production_t production;
    status = copy_request(pool,group,&production);
    if (status == CANDIDATE_WAL_OK) status = produce(pool,&production,out);
    candidate_buffer_leave(pool,previous);
    return candidate_buffer_result(pool,status);
}
