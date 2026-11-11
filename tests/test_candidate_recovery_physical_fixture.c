#include "test_candidate_recovery_physical_fixture.h"

#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_wal_witness.h"

#define FRAME_BYTES 16384u

static fx_image_t pf_images[PF_PAGES];

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static fx_t *fx_of(const pf_t *p) { return p->c.f.built.fx; }

static off_t page_offset(uint64_t id) { return (off_t)((id - 1) * CANDIDATE_PAGE_BYTES); }

void pf_read_data(const pf_t *p, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    CHECK(pread(p->c.f.data_fd,out,CANDIDATE_PAGE_BYTES,page_offset(id)) == (ssize_t)CANDIDATE_PAGE_BYTES);
}

static void write_data(pf_t *p, uint64_t id, const uint8_t page[CANDIDATE_PAGE_BYTES])
{
    CHECK(pwrite(p->c.f.data_fd,page,CANDIDATE_PAGE_BYTES,page_offset(id)) == (ssize_t)CANDIDATE_PAGE_BYTES);
    CHECK(fsync(p->c.f.data_fd) == 0);
}

static void restamp(const uint8_t *page, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_page_info_t info; OK(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info));
    candidate_page_location_t l = info.location; l.lsn = lsn;
    OK(candidate_page_encode_body(&l,info.type,page + 64,info.body_bytes,out,CANDIDATE_PAGE_BYTES));
}

static void encode_nonce(const uint8_t *base, uint8_t seed, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_catalog_node_t node; OK(candidate_catalog_decode_node(fx_uuid,base,CANDIDATE_PAGE_BYTES,&node));
    candidate_page_info_t info; OK(candidate_page_decode_header(fx_uuid,base,CANDIDATE_PAGE_BYTES,&info));
    CHECK(node.count == 1);
    for (unsigned i = 0; i < 8; i++) node.entries[0].nonce[i] = (uint8_t)(seed + i);
    candidate_page_location_t l = info.location; l.lsn = lsn;
    OK(candidate_catalog_encode_node(&l,&node,out,CANDIDATE_PAGE_BYTES));
}

void pf_expected_patch(const pf_t *p, uint8_t seed, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    encode_nonce(p->delta_base,seed,lsn,out);
}

void pf_expected_empty(uint64_t id, uint64_t generation, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_page_location_t l = fx_location(id,generation,lsn);
    OK(candidate_page_encode_versions(&l,NULL,0,out,CANDIDATE_PAGE_BYTES));
}

static uint64_t refresh(pf_t *p)
{
    cf_t *c = &p->c; fx_t *fx = fx_of(p);
    fx_txn_t t; cf_begin(c,&t,c->next_id++);
    uint32_t count = 0;
    for (uint32_t i = 0; i < fx->alloc_count; i++) count += fx->alloc[i].state == CANDIDATE_ALLOCATION_LIVE;
    CHECK(count > 0 && count <= PF_PAGES);
    uint64_t begin, end = fx_group_end(fx,count,0); fx_place(fx_position(fx),16,&begin);
    uint32_t n = 0;
    for (uint32_t i = 0; i < fx->alloc_count; i++) {
        const candidate_allocation_entry_t *e = &fx->alloc[i];
        if (e->state != CANDIDATE_ALLOCATION_LIVE) continue;
        uint8_t page[CANDIDATE_PAGE_BYTES]; pf_read_data(p,e->ref.id,page);
        fx_image_t *image = &pf_images[n++];
        image->id = e->ref.id; image->generation = e->ref.generation; image->previous = fx->pages[e->ref.id].lsn;
        restamp(page,end,image->page);
    }
    CHECK(fx_group(fx,&t,pf_images,count,NULL,0,true,true) == end);
    c->tally.groups++; cf_end(c,&t,false);
    p->refresh_end = end;
    return begin;
}

uint64_t pf_patch(pf_t *p, uint8_t seed, pf_group_t *out)
{
    cf_t *c = &p->c; fx_t *fx = fx_of(p); uint64_t id = c->delta_page;
    uint8_t before[CANDIDATE_PAGE_BYTES], after[CANDIDATE_PAGE_BYTES];
    pf_read_data(p,id,before); encode_nonce(before,seed,read_le(before + 40,8),after);
    size_t first = 64, last = CANDIDATE_PAGE_BYTES;
    while (first < last && before[first] == after[first]) first++;
    while (last > first && before[last - 1] == after[last - 1]) last--;
    CHECK(first < last);
    fx_txn_t t; cf_begin(c,&t,c->next_id++);
    uint64_t at = fx_place(fx_position(fx),16,&out->begin);
    at = fx_place(at,48 + (last - first),&out->record);
    out->end = fx_place(at,24,&out->end_record);
    CHECK(fx_delta_group(fx,&t,id,(uint32_t)first,after + first,(uint32_t)(last - first)) == out->end);
    c->tally.groups++; cf_end(c,&t,false);
    encode_nonce(before,seed,out->end,after);
    write_data(p,id,after); memcpy(c->delta_body,after + 64,sizeof(c->delta_body));
    return out->end;
}

static uint64_t transition(pf_t *p, const fx_event_t *event, unsigned images)
{
    cf_t *c = &p->c; fx_t *fx = fx_of(p);
    uint64_t end = fx_group_end(fx,images,1);
    candidate_allocation_entry_t *e = fx_alloc_find(fx,event->a); CHECK(e != NULL);
    e->state = event->type == 7 ? CANDIDATE_ALLOCATION_FREE : CANDIDATE_ALLOCATION_LIVE;
    e->ref.generation = event->c; e->release_horizon = event->d; e->transition_lsn = end;
    const candidate_page_ref_t nodes[1] = {{2,1}};
    pf_images[0].id = 1; pf_images[0].generation = 1; pf_images[0].previous = fx->pages[1].lsn;
    fx_encode_alloc_root(fx,1,end,fx->owner,c->page_highwater,1,nodes,pf_images[0].page);
    pf_images[1].id = 2; pf_images[1].generation = 1; pf_images[1].previous = fx->pages[2].lsn;
    fx_encode_alloc_node(fx,0,2,end,pf_images[1].page);
    if (images == 3) {
        pf_images[2].id = event->a; pf_images[2].generation = event->c; pf_images[2].previous = 0;
        pf_expected_empty(event->a,event->c,end,pf_images[2].page);
    }
    CHECK(fx_group(fx,NULL,pf_images,images,event,1,true,true) == end);
    c->tally.groups++; c->tally.system_groups++; c->tally.events++;
    return end;
}

void pf_image(pf_t *p, uint64_t previous)
{
    cf_t *c = &p->c; fx_t *fx = fx_of(p); uint64_t id = c->delta_page;
    fx_txn_t t; cf_begin(c,&t,c->next_id++);
    uint64_t end = fx_group_end(fx,1,0);
    uint8_t page[CANDIDATE_PAGE_BYTES]; pf_read_data(p,id,page);
    pf_images[0].id = id; pf_images[0].generation = fx->pages[id].generation; pf_images[0].previous = previous;
    restamp(page,end,pf_images[0].page);
    CHECK(fx_group(fx,&t,pf_images,1,NULL,0,true,true) == end);
    c->tally.groups++; cf_end(c,&t,false);
}

void pf_plain(pf_t *p)
{
    fx_txn_t t; cf_begin(&p->c,&t,p->c.next_id++); cf_end(&p->c,&t,false);
}

void pf_snapshot(pf_t *p, pf_snapshot_t *out)
{
    fx_t *fx = fx_of(p);
    memset(out,0,sizeof(*out));
    out->bytes = malloc((size_t)PF_PAGES * CANDIDATE_PAGE_BYTES); CHECK(out->bytes != NULL);
    for (uint32_t i = 0; i < fx->alloc_count; i++) {
        const candidate_allocation_entry_t *e = &fx->alloc[i];
        if (e->state != CANDIDATE_ALLOCATION_LIVE) continue;
        CHECK(out->count < PF_PAGES);
        out->ids[out->count] = e->ref.id; pf_read_data(p,e->ref.id,out->bytes[out->count]); out->count++;
    }
}

const uint8_t *pf_snapshot_page(const pf_snapshot_t *s, uint64_t id)
{
    for (uint32_t i = 0; i < s->count; i++) if (s->ids[i] == id) return s->bytes[i];
    CHECK(false);
    return NULL;
}

void pf_build(pf_t *p, const char *tag, candidate_recovery_view_mode_t mode)
{
    memset(p,0,sizeof(*p));
    cf_t *c = &p->c; cf_boot(c,tag,mode);
    pf_read_data(p,c->delta_page,p->delta_base);
    p->refresh_lsn = refresh(p);
    pf_patch(p,PF_FIRST_NONCE,&p->first_patch);
    p->free_id = c->churn_first; p->realloc_id = c->churn_first + 1;
    p->free_end = transition(p,&(fx_event_t){7,p->free_id,1,1,1},2);
    transition(p,&(fx_event_t){7,p->realloc_id,1,1,1},2);
    fx_place(fx_position(fx_of(p)),16,&p->realloc_begin);
    p->realloc_end = transition(p,&(fx_event_t){6,p->realloc_id,1,2,0},3);
    for (unsigned i = 0; i < 2; i++) cf_unit(c,mode == CANDIDATE_RECOVERY_VIEW_PREVIEW,false);
    cf_flush(c);
    OK(cf_open(c,&p->source));
    p->b = candidate_recovery_view_info(p->source).cut;
    pf_snapshot(p,&p->at_b);
    printf("PHYSICAL_FIXTURE %s refresh=%llu refresh_end=%llu patch=%llu free=%llu@%llu realloc=%llu@%llu..%llu b=%llu records=%llu "
        "live=%u\n",tag,(unsigned long long)p->refresh_lsn,(unsigned long long)p->refresh_end,
        (unsigned long long)p->first_patch.begin,(unsigned long long)p->free_id,(unsigned long long)p->free_end,
        (unsigned long long)p->realloc_id,(unsigned long long)p->realloc_begin,(unsigned long long)p->realloc_end,
        (unsigned long long)p->b.valid_end_lsn,(unsigned long long)p->b.records,p->at_b.count);
}

void pf_extend(pf_t *p)
{
    cf_t *c = &p->c;
    bool is_preview = c->mode == CANDIDATE_RECOVERY_VIEW_PREVIEW;
    pf_patch(p,PF_SECOND_NONCE,&p->second_patch);
    if (is_preview) cf_epoch(c,c->floor + 1);
    for (unsigned i = 0; i < 2; i++) cf_unit(c,is_preview,false);
    cf_flush(c);
    cf_arm(p->b.valid_end_lsn);
    candidate_wal_status_t status = cf_continue(c,p->source,&p->next);
    cf_io_t io = cf_io; cf_disarm();
    printf("PHYSICAL_EXTEND status=%s b=%llu t=%llu reads=%llu lowest=%llu denied=%llu epoch=%llu floor=%llu\n",vf_status(status),
        (unsigned long long)p->b.valid_end_lsn,(unsigned long long)candidate_wal_info(c->f.wal).valid_end_lsn,
        (unsigned long long)io.reads,(unsigned long long)io.lowest_lsn,(unsigned long long)io.denied,
        (unsigned long long)c->epoch,(unsigned long long)c->floor);
    OK(status); CHECK(io.denied == 0 && io.reads > 0 && io.lowest_lsn == p->b.valid_end_lsn);
    p->t = candidate_recovery_view_info(p->next).cut;
    pf_snapshot(p,&p->at_t);
}

void pf_free(pf_t *p)
{
    candidate_recovery_view_close(&p->next); candidate_recovery_view_close(&p->source);
    free(p->at_b.bytes); free(p->at_t.bytes); p->at_b.bytes = NULL; p->at_t.bytes = NULL;
    cf_free(&p->c);
}

void pf_tear(pf_t *p, uint64_t id)
{
    uint8_t garbage[CANDIDATE_PAGE_BYTES / 2]; memset(garbage,0xa5,sizeof(garbage));
    CHECK(pwrite(p->c.f.data_fd,garbage,sizeof(garbage),page_offset(id) + (off_t)sizeof(garbage)) == (ssize_t)sizeof(garbage));
    CHECK(fsync(p->c.f.data_fd) == 0);
}

void pf_forge_body(pf_t *p, uint64_t id)
{
    uint8_t page[CANDIDATE_PAGE_BYTES], body[CANDIDATE_PAGE_BYTES], forged[CANDIDATE_PAGE_BYTES];
    pf_read_data(p,id,page);
    candidate_page_info_t info; OK(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info));
    CHECK(info.body_bytes > 0);
    memcpy(body,page + 64,info.body_bytes); body[info.body_bytes / 2] ^= 0x5a;
    OK(candidate_page_encode_body(&info.location,info.type,body,info.body_bytes,forged,sizeof(forged)));
    write_data(p,id,forged);
}

static void segment_path(const pf_t *p, uint64_t lsn, char path[320])
{
    CHECK(snprintf(path,320,"%s.wal.%016" PRIx64,fx_of(p)->wal,lsn / CANDIDATE_WAL_SEGMENT_BYTES) > 0);
}

static size_t read_frame(const pf_t *p, uint64_t lsn, uint8_t *frame, candidate_wal_record_t *out)
{
    char path[320]; segment_path(p,lsn,path);
    int fd = open(path,O_RDONLY); CHECK(fd >= 0);
    off_t at = (off_t)(lsn % CANDIDATE_WAL_SEGMENT_BYTES);
    CHECK(pread(fd,frame,CANDIDATE_WAL_RECORD_HEADER_BYTES,at) == (ssize_t)CANDIDATE_WAL_RECORD_HEADER_BYTES);
    size_t total = (size_t)read_le(frame + 8,4); CHECK(total >= 96 && total <= FRAME_BYTES);
    CHECK(pread(fd,frame,total,at) == (ssize_t)total); CHECK(close(fd) == 0);
    OK(candidate_wal_decode(fx_uuid,frame,total,out)); CHECK(out->lsn == lsn);
    return total;
}

static void write_frame(const pf_t *p, const candidate_wal_record_t *r, size_t total)
{
    static uint8_t frame[FRAME_BYTES]; size_t written;
    OK(candidate_wal_encode(fx_uuid,r,frame,sizeof(frame),&written)); CHECK(written == total);
    char path[320]; segment_path(p,r->lsn,path);
    int fd = open(path,O_RDWR); CHECK(fd >= 0);
    CHECK(pwrite(fd,frame,total,(off_t)(r->lsn % CANDIDATE_WAL_SEGMENT_BYTES)) == (ssize_t)total);
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

void pf_rewrite_payload(pf_t *p, const pf_group_t *g)
{
    static uint8_t begin_frame[FRAME_BYTES], delta_frame[FRAME_BYTES], end_frame[FRAME_BYTES], payload[FRAME_BYTES];
    candidate_wal_record_t begin, delta, end; uint8_t finish[24];
    read_frame(p,g->begin,begin_frame,&begin);
    size_t delta_bytes = read_frame(p,g->record,delta_frame,&delta), end_bytes = read_frame(p,g->end_record,end_frame,&end);
    CHECK(begin.type == 3 && delta.type == 5 && end.type == 8 && end.payload_bytes == sizeof(finish));
    memcpy(payload,delta.payload,delta.payload_bytes); payload[delta.payload_bytes - 1] ^= 0x5a; delta.payload = payload;
    memcpy(finish,end.payload,sizeof(finish));
    uint32_t crc = candidate_wal_witness_crc(candidate_wal_witness_crc(0,&begin),&delta);
    write_le(finish + 16,crc,4); end.payload = finish;
    write_frame(p,&delta,delta_bytes); write_frame(p,&end,end_bytes);
    printf("WAL_REWRITE delta=%llu end=%llu payload=%zu group_crc=%u\n",(unsigned long long)delta.lsn,(unsigned long long)end.lsn,
        delta.payload_bytes,crc);
}

void pf_rewrite_records(pf_t *p, const pf_group_t *g, int32_t change)
{
    static uint8_t begin_frame[FRAME_BYTES], delta_frame[FRAME_BYTES], end_frame[FRAME_BYTES];
    candidate_wal_record_t begin, delta, end; uint8_t start[16], finish[24];
    size_t begin_bytes = read_frame(p,g->begin,begin_frame,&begin), end_bytes = read_frame(p,g->end_record,end_frame,&end);
    read_frame(p,g->record,delta_frame,&delta);
    CHECK(begin.type == 3 && begin.payload_bytes == sizeof(start) && delta.type == 5);
    CHECK(end.type == 8 && end.payload_bytes == sizeof(finish));
    memcpy(start,begin.payload,sizeof(start)); write_le(start + 12,(uint64_t)((int64_t)read_le(start + 12,4) + change),4);
    begin.payload = start; memcpy(finish,end.payload,sizeof(finish));
    uint32_t crc = candidate_wal_witness_crc(candidate_wal_witness_crc(0,&begin),&delta);
    write_le(finish + 16,crc,4); end.payload = finish;
    write_frame(p,&begin,begin_bytes); write_frame(p,&end,end_bytes);
    read_frame(p,g->begin,begin_frame,&begin); read_frame(p,g->end_record,end_frame,&end);
    printf("WAL_REWRITE begin=%llu end=%llu records=%llu group_crc=%u\n",(unsigned long long)begin.lsn,(unsigned long long)end.lsn,
        (unsigned long long)read_le(begin.payload + 12,4),crc);
}

void pf_replace_segment(pf_t *p)
{
    char path[320], copy[340]; segment_path(p,CANDIDATE_WAL_HEADER_BYTES,path);
    CHECK(snprintf(copy,sizeof(copy),"%s-replacement",fx_of(p)->wal) > 0);
    fx_copy_file(path,copy); CHECK(rename(copy,path) == 0);
}

candidate_physical_seed_t pf_seed_of(const candidate_recovery_view_t *view)
{
    candidate_physical_seed_t seed; OK(candidate_recovery_physical_seed(view,&seed));
    return seed;
}

static candidate_wal_status_t target_read(void *context, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES], bool *is_present)
{
    pf_t *p = context; pf_target_t *t = &p->target; t->reads++;
    if (t->patch_after != 0 && t->reads > t->patch_after && id == p->c.delta_page) {
        t->patch_after = 0; pf_patch(p,PF_LATE_NONCE,&t->patched);
        OK(candidate_wal_force(p->c.f.wal,candidate_wal_info(p->c.f.wal).valid_end_lsn));
    }
    *is_present = id != p->target.absent_id;
    if (*is_present) pf_read_data(p,id,out);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t target_write(void *context, uint64_t id, const uint8_t page[CANDIDATE_PAGE_BYTES])
{
    pf_t *p = context; pf_target_t *t = &p->target;
    if (++t->writes == 1) t->durable_at_write = candidate_wal_info(p->c.f.wal).durable_end_lsn;
    if (t->writes == t->fail_write_at) return CANDIDATE_WAL_IO;
    CHECK(pwrite(p->c.f.data_fd,page,CANDIDATE_PAGE_BYTES,page_offset(id)) == (ssize_t)CANDIDATE_PAGE_BYTES);
    if (id == t->absent_id) t->absent_id = 0;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t target_sync(void *context)
{
    pf_t *p = context; p->target.syncs++;
    if (p->target.should_fail_sync) return CANDIDATE_WAL_IO;
    return fsync(p->c.f.data_fd) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

candidate_redo_target_t pf_target(pf_t *p)
{
    return (candidate_redo_target_t){p,target_read,target_write,target_sync};
}

candidate_redo_seed_t pf_seed(pf_t *p, size_t retained_bytes)
{
    return (candidate_redo_seed_t){.target = pf_target(p),.memory = &p->c.f.allocator,.retained_bytes = retained_bytes};
}

uint32_t pf_live_pages(const candidate_recovery_view_t *view)
{
    uint32_t count = 0; candidate_physical_page_t page;
    for (uint32_t i = 0; candidate_recovery_physical_page_at(view,i,&page); i++) count += page.state == CANDIDATE_ALLOCATION_LIVE;
    return count;
}

uint32_t pf_expect_loads(candidate_redo_preview_t *preview, const candidate_recovery_view_t *view, const pf_snapshot_t *expected)
{
    uint32_t same = 0; candidate_physical_page_t page; uint8_t bytes[CANDIDATE_PAGE_BYTES];
    for (uint32_t i = 0; candidate_recovery_physical_page_at(view,i,&page); i++) {
        candidate_wal_status_t status = candidate_redo_preview_load(preview,page.ref,bytes);
        if (page.state != CANDIDATE_ALLOCATION_LIVE) { CHECK(status == CANDIDATE_WAL_CORRUPT); continue; }
        OK(status);
        same += memcmp(bytes,pf_snapshot_page(expected,page.ref.id),CANDIDATE_PAGE_BYTES) == 0;
    }
    return same;
}

uint32_t pf_expect_data(const pf_t *p, const pf_snapshot_t *expected)
{
    uint32_t same = 0; uint8_t bytes[CANDIDATE_PAGE_BYTES];
    for (uint32_t i = 0; i < expected->count; i++) {
        pf_read_data(p,expected->ids[i],bytes);
        same += memcmp(bytes,expected->bytes[i],CANDIDATE_PAGE_BYTES) == 0;
    }
    return same;
}
