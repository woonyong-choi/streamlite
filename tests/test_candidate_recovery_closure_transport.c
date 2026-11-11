#include "test_candidate_recovery_closure_transport_fixture.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"

#define RESOLVED CANDIDATE_RECOVERY_VIEW_RESOLVED
#define SEGMENT CANDIDATE_WAL_SEGMENT_BYTES
#define NATURAL_ROOM 5000u
#define NATURAL_PADDING (NATURAL_ROOM - 224u)
#define SHORT_ROOM_MAX 104u
#define IMAGE_BATCHES 7u
#define IMAGE_TRANSACTIONS 600u
#define IMAGE_CHURN_EVERY 50u
#define LEGACY_LIMIT 4096u

unsigned checks;

static void add_units(cf_t *c, unsigned count)
{
    for (unsigned i = 0; i < count; i++) cf_unit(c,false,false);
    cf_flush(c);
}

static void healthy_suffixes_continue(void)
{
    cf_t c; cf_boot(&c,"transport-healthy",RESOLVED);
    candidate_recovery_view_t *v = NULL; OK(cf_open(&c,&v));

    cf_suffix_t empty = tf_advance(&c,&v,"healthy-empty");
    add_units(&c,20); cf_suffix_t same = tf_advance(&c,&v,"healthy-same-segment");
    cf_pad_to(&c,NATURAL_ROOM); cf_flush(&c); cf_suffix_t inside = tf_advance(&c,&v,"healthy-fill-inside");
    cf_cross_segment(&c); cf_flush(&c); cf_suffix_t natural = tf_advance(&c,&v,"healthy-natural-padding");
    cf_pad(&c); cf_flush(&c); cf_suffix_t filled = tf_advance(&c,&v,"healthy-fill-boundary");
    uint64_t boundary = candidate_recovery_view_info(v).cut.valid_end_lsn;
    add_units(&c,20); cf_suffix_t exact = tf_advance(&c,&v,"healthy-exact-boundary");

    printf("HEALTHY empty=%llu same=%llu inside=%llu natural_padding=%llu natural_header=%llu boundary=%llu exact_header=%llu\n",
        (unsigned long long)empty.records,(unsigned long long)same.records,(unsigned long long)inside.records,
        (unsigned long long)natural.padding_bytes,(unsigned long long)natural.header_bytes,(unsigned long long)boundary,
        (unsigned long long)exact.header_bytes);
    CHECK(empty.records == 0 && empty.header_bytes == 0 && empty.padding_bytes == 0);
    CHECK(same.records > 0 && same.header_bytes == 0 && same.padding_bytes == 0 && inside.header_bytes == 0);
    CHECK(natural.padding_bytes == NATURAL_PADDING && natural.header_bytes == CANDIDATE_WAL_HEADER_BYTES);
    CHECK(filled.header_bytes == 0 && filled.padding_bytes == 0 && boundary % SEGMENT == 0);
    CHECK(exact.records > 0 && exact.header_bytes == CANDIDATE_WAL_HEADER_BYTES && exact.padding_bytes == 0);
    candidate_recovery_view_close(&v); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

static void short_padding_reaches_next_header(void)
{
    cf_t c; cf_boot(&c,"transport-short-padding",RESOLVED);
    candidate_recovery_view_t *v = NULL; OK(cf_open(&c,&v));
    unsigned crossed = 0;
    for (uint64_t room = 8; room <= SHORT_ROOM_MAX; room += 8) {
        char tag[64]; CHECK(snprintf(tag,sizeof(tag),"short-padding-%llu",(unsigned long long)room) > 0);
        cf_pad_to(&c,room); cf_flush(&c); tf_advance(&c,&v,tag);
        uint64_t cut = candidate_recovery_view_info(v).cut.valid_end_lsn;
        add_units(&c,5); cf_suffix_t s = tf_advance(&c,&v,tag);
        printf("SHORT_PADDING room=%llu cut_offset=%llu padding=%llu header=%llu records=%llu\n",(unsigned long long)room,
            (unsigned long long)(cut % SEGMENT),(unsigned long long)s.padding_bytes,(unsigned long long)s.header_bytes,
            (unsigned long long)s.records);
        CHECK(cut % SEGMENT == SEGMENT - room && s.padding_bytes == room && s.header_bytes == CANDIDATE_WAL_HEADER_BYTES);
        crossed++;
    }
    CHECK(crossed == SHORT_ROOM_MAX / 8);
    candidate_recovery_view_close(&v); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

static void flip(const tf_cross_t *x, uint64_t lsn, uint8_t *bytes) { (void)x; (void)lsn; bytes[0] ^= 0xff; }

static void bait_frame(const tf_cross_t *x, uint64_t lsn, uint8_t *bytes)
{
    candidate_wal_record_t r = {.type = 2,.lsn = lsn,.transaction_id = 1,.owner_epoch = x->c.f.owner};
    size_t written = 0; OK(candidate_wal_encode(fx_uuid,&r,bytes,TF_PATCH_BYTES,&written));
    candidate_wal_record_t decoded; OK(candidate_wal_decode(fx_uuid,bytes,written,&decoded)); CHECK(written == 96);
}

static void padding_refuses_nonzero(void)
{
    static const tf_variant_t rows[] = {{"padding-first-byte",TF_PADDING,0,1,flip,CANDIDATE_WAL_CORRUPT},
        {"padding-after-head",TF_PADDING,96,1,flip,CANDIDATE_WAL_CORRUPT},
        {"padding-valid-frame-bait",TF_PADDING,8,96,bait_frame,CANDIDATE_WAL_CORRUPT}};
    tf_run_variants("transport-padding",rows,sizeof(rows) / sizeof(rows[0]));
}

static void put_le(uint8_t *p, uint64_t value, unsigned n) { for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i)); }

static void header_version(const tf_cross_t *x, uint64_t lsn, uint8_t *h) { (void)x; (void)lsn; put_le(h + 8,2,2); tf_seal_segment(h); }

static void header_uuid(const tf_cross_t *x, uint64_t lsn, uint8_t *h) { (void)x; (void)lsn; h[16] ^= 0x01; tf_seal_segment(h); }

static void header_id(const tf_cross_t *x, uint64_t lsn, uint8_t *h) { (void)x; put_le(h + 32,lsn / SEGMENT + 1,8); tf_seal_segment(h); }

static void header_base(const tf_cross_t *x, uint64_t lsn, uint8_t *h) { (void)x; put_le(h + 40,lsn + SEGMENT,8); tf_seal_segment(h); }

static void header_reserved(const tf_cross_t *x, uint64_t lsn, uint8_t *h) { (void)x; (void)lsn; h[56] = 1; tf_seal_segment(h); }

static void header_refuses_variants(void)
{
    static const tf_variant_t rows[] = {{"header-magic",TF_HEADER,0,1,flip,CANDIDATE_WAL_UNSUPPORTED},
        {"header-version",TF_HEADER,0,64,header_version,CANDIDATE_WAL_UNSUPPORTED},
        {"header-uuid",TF_HEADER,0,64,header_uuid,CANDIDATE_WAL_UNSUPPORTED},
        {"header-id",TF_HEADER,0,64,header_id,CANDIDATE_WAL_CORRUPT},
        {"header-base",TF_HEADER,0,64,header_base,CANDIDATE_WAL_CORRUPT},
        {"header-reserved",TF_HEADER,0,64,header_reserved,CANDIDATE_WAL_CORRUPT},
        {"header-crc",TF_HEADER,48,1,flip,CANDIDATE_WAL_CORRUPT}};
    tf_run_variants("transport-header",rows,sizeof(rows) / sizeof(rows[0]));
}

static void frame_refuses_variants(void)
{
    static const tf_variant_t rows[] = {{"frame-magic",TF_FRAME,0,1,flip,CANDIDATE_WAL_CORRUPT},
        {"frame-version",TF_FRAME,4,1,flip,CANDIDATE_WAL_UNSUPPORTED},
        {"frame-length",TF_FRAME,8,1,flip,CANDIDATE_WAL_CORRUPT},
        {"frame-lsn",TF_FRAME,16,1,flip,CANDIDATE_WAL_CORRUPT},
        {"frame-uuid",TF_FRAME,40,1,flip,CANDIDATE_WAL_UNSUPPORTED},
        {"frame-crc",TF_FRAME,68,1,flip,CANDIDATE_WAL_CORRUPT},
        {"frame-payload",TF_FRAME,80,1,flip,CANDIDATE_WAL_CORRUPT},
        {"frame-footer",TF_FRAME,100,1,flip,CANDIDATE_WAL_CORRUPT}};
    tf_run_variants("transport-frame",rows,sizeof(rows) / sizeof(rows[0]));
}

static void suffix_eio_poisons(tf_cross_t *x)
{
    tf_fault_t fault = {.fail_from = x->first_frame + 8};
    tf_attempt_t failed = tf_attempt(x,&fault), next = tf_attempt(x,NULL);
    printf("IO suffix fail_from=%llu failed_reads=%llu calls=%u\n",(unsigned long long)fault.fail_from,
        (unsigned long long)failed.io.failed,failed.calls);
    tf_expect_refused("io-suffix",&failed,CANDIDATE_WAL_IO);
    CHECK(failed.io.failed == 1 && failed.calls > 0 && failed.io.reads > 3 && failed.io.lowest_lsn == x->before.valid_end_lsn);
    tf_expect_poisoned("io-suffix-next",&next);
    tf_expect_reuse(x,"io-suffix",true);
}

static void prefix_calibration_poisons(tf_cross_t *x)
{
    cf_t *c = &x->c; vf_reset_memory(&c->f);
    candidate_recovery_view_t *full = (void *)1;
    cf_arm(x->before.valid_end_lsn); candidate_wal_status_t status = cf_open(c,&full); cf_disarm();
    uint64_t denied = cf_io.denied;
    tf_attempt_t next = tf_attempt(x,NULL);
    printf("IO prefix_calibration full_open=%s denied=%llu out_null=%d\n",vf_status(status),(unsigned long long)denied,full == NULL);
    CHECK(status != CANDIDATE_WAL_OK && denied > 0 && full == NULL);
    tf_expect_poisoned("io-prefix-next",&next);
    tf_expect_reuse(x,"io-prefix",true);
}

static void io_failures_poison(void)
{
    tf_cross_t x; tf_cross_open(&x,"transport-io",TF_ROOM);
    suffix_eio_poisons(&x);
    prefix_calibration_poisons(&x);
    tf_cross_close(&x);
}

typedef struct {
    tf_cross_t *x;
    candidate_wal_status_t inner_continue, inner_read, inner_append, inner_visit, inner_close;
    unsigned inner_calls, inner_loads, visits;
    bool is_inner_null, is_info_same, is_handle_kept, has_run;
} reentry_t;

static bool count_visit(void *context, const candidate_wal_record_t *r) { (void)r; (*(unsigned *)context)++; return true; }

static void reenter_wal(void *context)
{
    reentry_t *e = context; cf_t *c = &e->x->c; candidate_wal_t *wal = c->f.wal;
    candidate_wal_info_t info = candidate_wal_info(wal); unsigned calls = c->f.memory.calls, loads = c->f.loads;
    candidate_recovery_view_t *inner = (void *)1; candidate_wal_record_t r; uint64_t end = 0; uint8_t body[16] = {0};
    e->inner_continue = cf_continue(c,e->x->source,&inner); e->is_inner_null = inner == NULL;
    e->inner_read = candidate_wal_read_at(wal,e->x->first_frame,&r);
    candidate_wal_record_t record = {.type = 2,.transaction_id = CF_TRANSACTION_HIGHWATER - 1,.payload = body,.payload_bytes = 16};
    e->inner_append = candidate_wal_append(wal,&record,&end);
    candidate_wal_suffix_t suffix = {.before = e->x->before,.after = info,.visitor = count_visit,.context = &e->visits};
    e->inner_visit = candidate_wal_visit_suffix(wal,&suffix);
    e->inner_close = CANDIDATE_WAL_OK;
    candidate_wal_t *copy = wal;
    if (e->inner_read == CANDIDATE_WAL_INVALID) e->inner_close = candidate_wal_close(&copy);
    candidate_wal_info_t now = candidate_wal_info(wal);
    e->is_handle_kept = copy == wal; e->is_info_same = tf_same_cut(&info,&now) && info.durable_end_lsn == now.durable_end_lsn;
    e->inner_calls = c->f.memory.calls - calls; e->inner_loads = c->f.loads - loads; e->has_run = true;
}

static void expect_inner_refused(const char *tag, const reentry_t *e)
{
    printf("REENTRY %s run=%d continue=%s read_at=%s append=%s visit=%s close=%s inner_null=%d calls=%u loads=%u visits=%u "
        "info_same=%d handle_kept=%d\n",tag,e->has_run,vf_status(e->inner_continue),vf_status(e->inner_read),
        vf_status(e->inner_append),vf_status(e->inner_visit),vf_status(e->inner_close),e->is_inner_null,e->inner_calls,
        e->inner_loads,e->visits,e->is_info_same,e->is_handle_kept);
    CHECK(e->has_run && e->inner_continue == CANDIDATE_WAL_INVALID && e->inner_read == CANDIDATE_WAL_INVALID);
    CHECK(e->inner_append == CANDIDATE_WAL_INVALID && e->inner_visit == CANDIDATE_WAL_INVALID);
    CHECK(e->inner_close == CANDIDATE_WAL_INVALID && e->is_handle_kept && e->is_inner_null);
    CHECK(e->inner_calls == 0 && e->inner_loads == 0 && e->visits == 0 && e->is_info_same);
}

static void continue_reentry_refused(tf_cross_t *x)
{
    reentry_t e = {.x = x};
    tf_fault_t fault = {.hook_from = x->first_frame,.on_read = reenter_wal,.context = &e};
    tf_attempt_t a = tf_attempt(x,&fault);
    candidate_recovery_closure_t k = candidate_recovery_view_closure(a.view);
    candidate_wal_info_t cut = candidate_recovery_view_info(a.view).cut;
    expect_inner_refused("continue",&e);
    OK(a.status);
    CHECK(memcmp(&k,&x->closure,sizeof(k)) == 0 && tf_same_cut(&cut,&x->cut) && a.is_source_same && a.io.denied == 0);
    candidate_recovery_view_close(&a.view);
}

typedef struct { uint64_t from, lsn; } delta_at_t;

static bool find_delta(void *context, const candidate_wal_record_t *r)
{
    delta_at_t *d = context;
    if (d->lsn == 0 && r->type == 5 && r->lsn >= d->from) d->lsn = r->lsn;
    return true;
}

static void read_at_reentry_refused(tf_cross_t *x)
{
    cf_t *c = &x->c; delta_at_t d = {.from = x->next_segment}; candidate_wal_info_t info;
    OK(candidate_wal_scan(c->f.wal,find_delta,&d,&info)); CHECK(d.lsn != 0);
    candidate_wal_record_t expected, got; OK(candidate_wal_read_at(c->f.wal,d.lsn,&expected));
    uint8_t *payload = malloc(expected.payload_bytes); CHECK(payload != NULL);
    memcpy(payload,expected.payload,expected.payload_bytes);
    reentry_t e = {.x = x};
    cf_arm(0); cf_io.hook_from = d.lsn; cf_io.on_read = reenter_wal; cf_io.context = &e;
    candidate_wal_status_t status = candidate_wal_read_at(c->f.wal,d.lsn,&got);
    cf_disarm();
    expect_inner_refused("read_at",&e);
    bool is_same_frame = got.type == expected.type && got.lsn == expected.lsn && got.transaction_id == expected.transaction_id;
    bool is_same = is_same_frame && got.payload_bytes == expected.payload_bytes && memcmp(got.payload,payload,got.payload_bytes) == 0;
    printf("REENTRY read_at outer=%s lsn=%llu payload=%zu same=%d\n",vf_status(status),(unsigned long long)d.lsn,
        expected.payload_bytes,is_same);
    OK(status); CHECK(is_same);
    free(payload);
}

typedef struct {
    candidate_wal_t *wal;
    unsigned visits;
    candidate_wal_status_t first, second;
    char path[320], moved[330];
    bool should_move;
} guard_visit_t;

static bool mutate_visit(void *context, const candidate_wal_record_t *r)
{
    guard_visit_t *g = context; candidate_wal_record_t inner;
    if (g->visits++ > 0) return true;
    g->first = candidate_wal_read_at(g->wal,r->lsn,&inner); g->second = candidate_wal_read_at(g->wal,r->lsn,&inner);
    if (g->should_move) CHECK(rename(g->path,g->moved) == 0);
    else candidate_wal_reject(g->wal);
    return true;
}

static void visitor_effects_kept(tf_cross_t *x, bool should_move)
{
    cf_t *c = &x->c; guard_visit_t g = {.wal = c->f.wal,.should_move = should_move};
    tf_segment_path(c,x->next_segment / SEGMENT,g.path); CHECK(snprintf(g.moved,sizeof(g.moved),"%s.moved",g.path) > 0);
    candidate_wal_info_t after = candidate_wal_info(c->f.wal);
    candidate_wal_suffix_t suffix = {.before = x->before,.after = after,.visitor = mutate_visit,.context = &g};
    candidate_wal_status_t status = candidate_wal_visit_suffix(c->f.wal,&suffix);
    if (should_move) CHECK(rename(g.moved,g.path) == 0);
    tf_attempt_t next = tf_attempt(x,NULL);
    candidate_wal_status_t expected = should_move ? CANDIDATE_WAL_IO : CANDIDATE_WAL_POISONED;
    printf("VISITOR %s status=%s expected=%s visits=%u records=%llu nested_first=%s nested_second=%s\n",
        should_move ? "ownership-moved" : "rejected",vf_status(status),vf_status(expected),g.visits,
        (unsigned long long)cf_suffix_records(&x->before,&after),vf_status(g.first),vf_status(g.second));
    CHECK(status == expected && g.visits == cf_suffix_records(&x->before,&after));
    CHECK(g.first == CANDIDATE_WAL_INVALID && g.second == CANDIDATE_WAL_INVALID);
    tf_expect_poisoned(should_move ? "visitor-moved-next" : "visitor-rejected-next",&next);
    tf_expect_reuse(x,should_move ? "visitor-moved" : "visitor-rejected",true);
}

static void reentry_refused(void)
{
    tf_cross_t x; tf_cross_open(&x,"transport-reentry",TF_ROOM);
    continue_reentry_refused(&x);
    read_at_reentry_refused(&x);
    visitor_effects_kept(&x,false);
    visitor_effects_kept(&x,true);
    tf_cross_close(&x);
}

typedef struct { const char *name; void (*edit)(candidate_wal_suffix_t *s); candidate_wal_status_t expected; } captured_row_t;

static void after_records(candidate_wal_suffix_t *s) { s->after.records++; }

static void after_end(candidate_wal_suffix_t *s) { s->after.valid_end_lsn -= 8; }

static void after_owner(candidate_wal_suffix_t *s) { s->after.highest_owner_epoch++; }

static void before_misaligned(candidate_wal_suffix_t *s) { s->before.valid_end_lsn += 4; }

static void before_beyond(candidate_wal_suffix_t *s) { s->before.valid_end_lsn = s->after.valid_end_lsn + 8; }

static void before_owner_above(candidate_wal_suffix_t *s) { s->before.highest_owner_epoch = s->after.highest_owner_epoch + 1; }

static void before_more(candidate_wal_suffix_t *s) { s->before.records++; }

static void before_fewer(candidate_wal_suffix_t *s) { s->before.records--; }

static void no_visitor(candidate_wal_suffix_t *s) { s->visitor = NULL; }

static void keep(candidate_wal_suffix_t *s) { (void)s; }

static void run_captured(tf_cross_t *x, const captured_row_t *row)
{
    candidate_wal_t *wal = x->c.f.wal; unsigned visits = 0;
    candidate_wal_info_t current = candidate_wal_info(wal);
    candidate_wal_suffix_t suffix = {.before = x->before,.after = current,.visitor = count_visit,.context = &visits};
    row->edit(&suffix);
    candidate_wal_status_t status = candidate_wal_visit_suffix(wal,&suffix);
    candidate_wal_status_t handle = candidate_wal_check_uuid(wal,fx_uuid); candidate_wal_info_t now = candidate_wal_info(wal);
    bool reads_all = status == CANDIDATE_WAL_OK || status == CANDIDATE_WAL_CORRUPT;
    printf("CAPTURED %s status=%s expected=%s visits=%u handle=%s\n",row->name,vf_status(status),vf_status(row->expected),visits,
        vf_status(handle));
    CHECK(status == row->expected && handle == CANDIDATE_WAL_OK && tf_same_cut(&now,&current));
    CHECK(visits == (reads_all ? cf_suffix_records(&x->before,&current) : 0));
}

static void checkpoint_keeps_transport(tf_cross_t *x)
{
    uint8_t body[16] = {0}; cf_append_raw(&x->c,12,0,body,sizeof(body)); add_units(&x->c,2);
    tf_attempt_t first = tf_attempt(x,NULL), next = tf_attempt(x,NULL);
    candidate_wal_status_t handle = candidate_wal_check_uuid(x->c.f.wal,fx_uuid);
    printf("CAPTURED checkpoint handle=%s\n",vf_status(handle));
    tf_expect_refused("checkpoint",&first,CANDIDATE_WAL_UNSUPPORTED);
    tf_expect_refused("checkpoint-next",&next,CANDIDATE_WAL_UNSUPPORTED);
    OK(handle);
}

static void owner_regression_refused(tf_cross_t *x)
{
    cf_reown(&x->c,x->c.f.owner + 1); add_units(&x->c,2);
    candidate_wal_t *wal = x->c.f.wal; unsigned visits = 0;
    candidate_wal_suffix_t suffix = {.before = x->before,.after = candidate_wal_info(wal),.visitor = count_visit,.context = &visits};
    OK(candidate_wal_visit_suffix(wal,&suffix));
    unsigned healthy = visits; visits = 0;
    suffix.before.highest_owner_epoch = suffix.after.highest_owner_epoch;
    candidate_wal_status_t raised = candidate_wal_visit_suffix(wal,&suffix);
    candidate_wal_status_t handle = candidate_wal_check_uuid(wal,fx_uuid);
    printf("CAPTURED owner-regression before_owner=%llu after_owner=%llu status=%s visits=%u/%u handle=%s\n",
        (unsigned long long)suffix.before.highest_owner_epoch,(unsigned long long)suffix.after.highest_owner_epoch,
        vf_status(raised),visits,healthy,vf_status(handle));
    CHECK(suffix.after.highest_owner_epoch == x->before.highest_owner_epoch + 1 && healthy > 0 && visits == 0);
    CHECK(raised == CANDIDATE_WAL_CORRUPT && handle == CANDIDATE_WAL_OK);
}

static void captured_cuts_fail_closed(void)
{
    static const captured_row_t rows[] = {{"healthy",keep,CANDIDATE_WAL_OK},{"after-records",after_records,CANDIDATE_WAL_STALE},
        {"after-end",after_end,CANDIDATE_WAL_STALE},{"after-owner",after_owner,CANDIDATE_WAL_STALE},
        {"before-misaligned",before_misaligned,CANDIDATE_WAL_INVALID},{"before-beyond",before_beyond,CANDIDATE_WAL_INVALID},
        {"before-owner-above-after",before_owner_above,CANDIDATE_WAL_INVALID},{"before-more-records",before_more,CANDIDATE_WAL_CORRUPT},
        {"before-fewer-records",before_fewer,CANDIDATE_WAL_CORRUPT},{"no-visitor",no_visitor,CANDIDATE_WAL_INVALID}};
    tf_cross_t x; tf_cross_open(&x,"transport-captured",TF_ROOM);
    checkpoint_keeps_transport(&x);
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) run_captured(&x,&rows[i]);
    CHECK(candidate_wal_visit_suffix(x.c.f.wal,NULL) == CANDIDATE_WAL_INVALID);
    owner_regression_refused(&x);
    tf_cross_close(&x);
}

typedef struct { uint64_t image_groups, churn_groups, crossings, padding_bytes; } image_tally_t;

static void fill_image_batch(cf_t *c, image_tally_t *t)
{
    for (unsigned i = 1; i <= IMAGE_TRANSACTIONS; i++) {
        tf_image_abort(c); t->image_groups++;
        if (i % IMAGE_CHURN_EVERY == 0) { cf_churn(c); t->churn_groups++; }
    }
    cf_flush(c);
}

static void expect_image_batch(const cf_t *c, const candidate_recovery_view_t *v, size_t *baseline)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v);
    size_t resident = candidate_recovery_view_info(v).resident_bytes - k.identity_bytes;
    if (*baseline == 0) *baseline = resident;
    CHECK(k.groups == 1 && k.events == CF_CHURN_PAGES && k.started_intervals == 2 && resident == *baseline);
    tf_expect_delta_page(c);
}

static void print_images(const cf_t *c, const image_tally_t *t, candidate_wal_status_t legacy, size_t baseline)
{
    printf("IMAGES batches=%u image_groups=%llu churn_groups=%llu txns=%llu terminals=%llu groups=%llu events=%llu crossings=%llu "
        "padding_bytes=%llu segments=%u wal_end=%llu legacy_full_open=%s steady_resident_without_identity=%zu\n",IMAGE_BATCHES,
        (unsigned long long)t->image_groups,(unsigned long long)t->churn_groups,(unsigned long long)c->tally.transactions,
        (unsigned long long)(c->tally.commits + c->tally.aborts),(unsigned long long)c->tally.groups,
        (unsigned long long)c->tally.events,(unsigned long long)t->crossings,(unsigned long long)t->padding_bytes,
        candidate_wal_info(c->f.wal).segments,(unsigned long long)candidate_wal_info(c->f.wal).valid_end_lsn,vf_status(legacy),
        baseline);
}

static void images_cross_natural_segments(void)
{
    cf_t c; cf_boot(&c,"transport-images",RESOLVED);
    candidate_recovery_view_t *v = NULL; OK(cf_open(&c,&v));
    image_tally_t t = {0}; size_t baseline = 0;
    for (unsigned batch = 1; batch <= IMAGE_BATCHES; batch++) {
        unsigned segments = candidate_wal_info(c.f.wal).segments;
        fill_image_batch(&c,&t);
        char tag[64]; CHECK(snprintf(tag,sizeof(tag),"images-%u",batch) > 0);
        cf_suffix_t s = tf_advance(&c,&v,tag);
        t.crossings += candidate_wal_info(c.f.wal).segments - segments; t.padding_bytes += s.padding_bytes;
        expect_image_batch(&c,v,&baseline); cf_print(tag,&c,v);
    }
    vf_reset_memory(&c.f); candidate_recovery_view_t *full = (void *)1; candidate_wal_status_t legacy = cf_open(&c,&full);
    print_images(&c,&t,legacy,baseline);
    CHECK(t.image_groups > LEGACY_LIMIT && c.tally.transactions > LEGACY_LIMIT && c.tally.aborts > LEGACY_LIMIT);
    CHECK(c.tally.events > LEGACY_LIMIT && t.crossings >= IMAGE_BATCHES && t.padding_bytes > 0);
    CHECK(legacy == CANDIDATE_WAL_LIMIT && full == NULL);
    candidate_recovery_view_close(&v); CHECK(c.f.memory.live == 0);
    cf_free(&c);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;

static const test_case_t CASES[] = {{"healthy",healthy_suffixes_continue},{"short-padding",short_padding_reaches_next_header},
    {"padding",padding_refuses_nonzero},{"header",header_refuses_variants},{"frame",frame_refuses_variants},
    {"io",io_failures_poison},{"reentry",reentry_refused},{"captured",captured_cuts_fail_closed},
    {"images",images_cross_natural_segments}};

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(1200);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        if (!is_all && strcmp(selected,CASES[i].name) != 0) continue;
        printf("CASE %s\n",CASES[i].name); CASES[i].run(); has_match = true;
    }
    CHECK(has_match);
    printf("PASS recovery closure transport case=%s checks=%u; owned-WAL suffix cursor only, not durable seeds, WAL reuse, "
        "power loss or speed\n",selected,checks);
    return 0;
}
