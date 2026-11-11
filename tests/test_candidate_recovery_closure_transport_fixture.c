#include "test_candidate_recovery_closure_transport_fixture.h"

#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "storage/checksum.h"
#include "../src/storage/candidate_wal_internal.h"

#define SEGMENT CANDIDATE_WAL_SEGMENT_BYTES

static uint64_t segment_end(uint64_t lsn) { return (lsn / SEGMENT + 1) * SEGMENT; }

bool tf_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    bool is_same_end = a->valid_end_lsn == b->valid_end_lsn && a->records == b->records && a->segments == b->segments;
    bool is_same_tail = a->has_tail == b->has_tail && a->tail_bytes == b->tail_bytes;
    return is_same_end && is_same_tail && a->highest_owner_epoch == b->highest_owner_epoch;
}

void tf_segment_path(const cf_t *c, uint64_t id, char out[320])
{
    CHECK(snprintf(out,320,"%s.wal.%016" PRIx64,c->f.built.fx->wal,id) > 0);
}

static int open_segment(const cf_t *c, uint64_t lsn, size_t size, int flags)
{
    CHECK(size <= SEGMENT - lsn % SEGMENT);
    char path[320]; tf_segment_path(c,lsn / SEGMENT,path);
    int fd = open(path,flags); CHECK(fd >= 0);
    return fd;
}

void tf_read_wal(const cf_t *c, uint64_t lsn, void *out, size_t size)
{
    int fd = open_segment(c,lsn,size,O_RDONLY);
    CHECK(pread(fd,out,size,(off_t)(lsn % SEGMENT)) == (ssize_t)size);
    CHECK(close(fd) == 0);
}

void tf_write_wal(const cf_t *c, uint64_t lsn, const void *bytes, size_t size)
{
    int fd = open_segment(c,lsn,size,O_RDWR);
    CHECK(pwrite(fd,bytes,size,(off_t)(lsn % SEGMENT)) == (ssize_t)size);
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

void tf_seal_segment(uint8_t header[CANDIDATE_WAL_HEADER_BYTES])
{
    memset(header + 48,0,4);
    uint32_t crc = crc32c(0,header,CANDIDATE_WAL_HEADER_BYTES);
    for (unsigned i = 0; i < 4; i++) header[48 + i] = (uint8_t)(crc >> (8 * i));
}

static void read_delta_page(const cf_t *c, uint8_t page[CANDIDATE_PAGE_BYTES], candidate_page_info_t *info)
{
    off_t offset = (off_t)((c->delta_page - 1) * CANDIDATE_PAGE_BYTES);
    CHECK(pread(c->f.data_fd,page,CANDIDATE_PAGE_BYTES,offset) == (ssize_t)CANDIDATE_PAGE_BYTES);
    OK(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,info));
}

void tf_image_abort(cf_t *c)
{
    static fx_image_t image;
    fx_t *fx = c->f.built.fx; uint8_t page[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    fx_txn_t t; cf_begin(c,&t,c->next_id++);
    uint64_t end = fx_group_end(fx,1,0);
    read_delta_page(c,page,&info);
    candidate_page_location_t l = fx_location(c->delta_page,info.location.ref.generation,end);
    image.id = c->delta_page; image.generation = info.location.ref.generation; image.previous = fx->pages[c->delta_page].lsn;
    OK(candidate_page_encode_body(&l,info.type,page + 64,info.body_bytes,image.page,CANDIDATE_PAGE_BYTES));
    CHECK(fx_group(fx,&t,&image,1,NULL,0,true,true) == end);
    c->tally.groups++;
    cf_end(c,&t,false);
}

void tf_expect_delta_page(const cf_t *c)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    read_delta_page(c,page,&info);
    const fx_page_t *p = &c->f.built.fx->pages[c->delta_page];
    CHECK(info.location.ref.id == c->delta_page && info.location.ref.generation == p->generation && info.location.lsn == p->lsn);
    CHECK(memcmp(page + 64,c->delta_body,sizeof(c->delta_body)) == 0);
}

cf_suffix_t tf_advance(cf_t *c, candidate_recovery_view_t **view, const char *tag)
{
    candidate_wal_info_t before = candidate_recovery_view_info(*view).cut, after = candidate_wal_info(c->f.wal);
    cf_source_t held; cf_source_take(*view,&held);
    vf_reset_memory(&c->f); size_t live = c->f.memory.live;
    candidate_recovery_view_t *next = NULL;
    cf_arm(before.valid_end_lsn); candidate_wal_status_t status = cf_continue(c,*view,&next); cf_disarm();
    size_t resident = candidate_recovery_view_info(next).resident_bytes;
    printf("ADVANCE %s status=%s cut=%llu end=%llu segments=%u/%u records=%llu held=%zu resident=%zu calls=%u\n",tag,
        vf_status(status),(unsigned long long)before.valid_end_lsn,(unsigned long long)after.valid_end_lsn,before.segments,
        after.segments,(unsigned long long)cf_suffix_records(&before,&after),live,resident,c->f.memory.calls);
    OK(status);
    cf_suffix_t suffix = cf_expect_reads(tag,c,&before,&after);
    candidate_wal_info_t cut = candidate_recovery_view_info(next).cut; candidate_recovery_closure_t k = candidate_recovery_view_closure(next);
    CHECK(cf_source_same(*view,&held) && tf_same_cut(&cut,&after));
    cf_expect_history(c,&k); cf_expect_resident(c,next,0); cf_expect_outcomes(c,next);
    cf_expect_budget(tag,c,live + resident);
    cf_source_free(&held);
    candidate_recovery_view_close(view); *view = next;
    return suffix;
}

static void open_reference(tf_cross_t *x, const char *tag, uint64_t room)
{
    cf_t *c = &x->c; candidate_recovery_view_t *reference = NULL;
    cf_arm(x->before.valid_end_lsn); OK(cf_continue(c,x->source,&reference)); cf_disarm();
    candidate_wal_info_t after = candidate_wal_info(c->f.wal);
    cf_suffix_t suffix = cf_expect_reads(tag,c,&x->before,&after);
    CHECK(suffix.padding_bytes == room && suffix.header_bytes == CANDIDATE_WAL_HEADER_BYTES);
    x->closure = candidate_recovery_view_closure(reference); x->cut = candidate_recovery_view_info(reference).cut;
    CHECK(tf_same_cut(&x->cut,&after) && after.segments == x->before.segments + 1);
    cf_expect_history(c,&x->closure); cf_expect_outcomes(c,reference);
    candidate_recovery_view_close(&reference);
}

void tf_cross_open(tf_cross_t *x, const char *tag, uint64_t room)
{
    memset(x,0,sizeof(*x));
    cf_t *c = &x->c; cf_boot(c,tag,CANDIDATE_RECOVERY_VIEW_RESOLVED);
    candidate_recovery_view_t *origin = NULL; OK(cf_open(c,&origin));
    for (unsigned i = 0; i < 10; i++) cf_unit(c,false,false);
    cf_pad_to(c,room); cf_flush(c);
    OK(cf_continue(c,origin,&x->source)); candidate_recovery_view_close(&origin);
    x->before = candidate_recovery_view_info(x->source).cut;
    x->next_segment = segment_end(x->before.valid_end_lsn); x->first_frame = x->next_segment + CANDIDATE_WAL_HEADER_BYTES;
    CHECK(x->next_segment - x->before.valid_end_lsn == room);
    for (unsigned i = 0; i < TF_SUFFIX_UNITS; i++) cf_unit(c,false,false);
    cf_flush(c);
    open_reference(x,tag,room);
    cf_source_take(x->source,&x->held);
}

void tf_cross_close(tf_cross_t *x)
{
    cf_source_free(&x->held);
    candidate_recovery_view_close(&x->source);
    CHECK(x->c.f.memory.live == 0);
    cf_free(&x->c);
}

tf_attempt_t tf_attempt(tf_cross_t *x, const tf_fault_t *fault)
{
    cf_t *c = &x->c;
    vf_reset_memory(&c->f);
    tf_attempt_t a = {.view = (void *)1,.live_before = c->f.memory.live,.loads = c->f.loads};
    cf_arm(x->before.valid_end_lsn);
    if (fault != NULL) {
        cf_io.fail_from = fault->fail_from; cf_io.hook_from = fault->hook_from;
        cf_io.on_read = fault->on_read; cf_io.context = fault->context;
    }
    a.status = cf_continue(c,x->source,&a.view);
    cf_disarm();
    a.io = cf_io; a.calls = c->f.memory.calls; a.loads = c->f.loads - a.loads; a.live_after = c->f.memory.live;
    a.is_source_same = cf_source_same(x->source,&x->held);
    return a;
}

static void print_attempt(const char *kind, const char *tag, const tf_attempt_t *a)
{
    printf("%s %s status=%s out_null=%d live=%zu/%zu calls=%u loads=%u source_same=%d reads=%llu read_bytes=%llu lowest=%llu "
        "denied=%llu failed=%llu\n",kind,tag,vf_status(a->status),a->view == NULL,a->live_before,a->live_after,a->calls,a->loads,
        a->is_source_same,(unsigned long long)a->io.reads,(unsigned long long)a->io.read_bytes,(unsigned long long)a->io.lowest_lsn,
        (unsigned long long)a->io.denied,(unsigned long long)a->io.failed);
}

void tf_expect_refused(const char *tag, const tf_attempt_t *a, candidate_wal_status_t expected)
{
    print_attempt("REFUSED",tag,a);
    CHECK(a->status == expected && a->view == NULL && a->live_after == a->live_before && a->loads == 0);
    CHECK(a->is_source_same && a->io.denied == 0);
}

void tf_expect_poisoned(const char *tag, const tf_attempt_t *a)
{
    print_attempt("POISONED",tag,a);
    CHECK(a->status == CANDIDATE_WAL_POISONED && a->view == NULL && a->calls == 0 && a->loads == 0);
    CHECK(a->io.reads == 0 && a->live_after == a->live_before && a->is_source_same);
}

void tf_expect_reuse(tf_cross_t *x, const char *tag, bool should_reopen)
{
    if (should_reopen) cf_reown(&x->c,x->c.f.owner);
    tf_attempt_t a = tf_attempt(x,NULL);
    candidate_recovery_closure_t k = candidate_recovery_view_closure(a.view);
    candidate_wal_info_t cut = candidate_recovery_view_info(a.view).cut;
    print_attempt("REUSED",tag,&a);
    OK(a.status);
    CHECK(memcmp(&k,&x->closure,sizeof(k)) == 0 && tf_same_cut(&cut,&x->cut) && a.is_source_same && a.io.denied == 0);
    candidate_recovery_view_close(&a.view);
}

static uint64_t region_base(const tf_cross_t *x, tf_region_t region)
{
    if (region == TF_PADDING) return x->before.valid_end_lsn;
    return region == TF_HEADER ? x->next_segment : x->first_frame;
}

static void run_variant(tf_cross_t *x, const tf_variant_t *v)
{
    uint64_t lsn = region_base(x,v->region) + v->offset;
    uint8_t saved[TF_PATCH_BYTES], bytes[TF_PATCH_BYTES];
    CHECK(v->size <= TF_PATCH_BYTES);
    tf_read_wal(&x->c,lsn,saved,v->size); memcpy(bytes,saved,v->size);
    v->mutate(x,lsn,bytes); CHECK(memcmp(bytes,saved,v->size) != 0);
    tf_write_wal(&x->c,lsn,bytes,v->size);

    tf_attempt_t first = tf_attempt(x,NULL), next = tf_attempt(x,NULL);
    candidate_wal_status_t handle = candidate_wal_check_uuid(x->c.f.wal,fx_uuid);

    printf("VARIANT %s lsn=%llu size=%zu expected=%s handle_after=%s\n",v->name,(unsigned long long)lsn,v->size,
        vf_status(v->expected),vf_status(handle));
    tf_expect_refused(v->name,&first,v->expected);
    bool is_poisoned = v->expected == CANDIDATE_WAL_CORRUPT;
    if (is_poisoned) { tf_expect_poisoned(v->name,&next); CHECK(handle == CANDIDATE_WAL_POISONED); }
    else { tf_expect_refused(v->name,&next,v->expected); OK(handle); }
    tf_write_wal(&x->c,lsn,saved,v->size);
    tf_expect_reuse(x,v->name,is_poisoned);
}

void tf_run_variants(const char *tag, const tf_variant_t *rows, unsigned count)
{
    tf_cross_t x; tf_cross_open(&x,tag,TF_ROOM);
    for (unsigned i = 0; i < count; i++) run_variant(&x,&rows[i]);
    tf_cross_close(&x);
}
