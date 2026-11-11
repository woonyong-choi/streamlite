#include "test_candidate_recovery_closure_transport_fixture.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"

#define SEGMENT CANDIDATE_WAL_SEGMENT_BYTES
#define FINAL_READ_CALL 2u

unsigned checks;

typedef enum { KIND_READ, KIND_VISIT, KIND_SCAN } kind_t;
typedef enum { EFFECT_NONE, EFFECT_REJECT, EFFECT_MOVE } effect_t;
typedef struct {
    const char *name;
    kind_t kind;
    effect_t effect;
    bool should_continue, is_pinned;
    candidate_wal_status_t first, next;
} row_t;
typedef struct {
    candidate_wal_t *wal;
    effect_t effect;
    bool should_continue;
    unsigned visits, applied;
    char path[320], moved[330];
} probe_t;
typedef struct {
    candidate_wal_status_t status;
    unsigned visits, applied;
    uint64_t reads;
    bool is_payload_same;
} outcome_t;
typedef struct {
    uint8_t *bytes;
    size_t size;
} raw_t;
typedef struct { const char *name; bool should_continue; } owner_row_t;

static const char *KIND_NAMES[] = {"read","visit","scan"};
static const char *EFFECT_NAMES[] = {"none","reject","move"};

static const row_t ROWS[] = {
    {"read-healthy",KIND_READ,EFFECT_NONE,true,false,CANDIDATE_WAL_OK,CANDIDATE_WAL_OK},
    {"visit-healthy",KIND_VISIT,EFFECT_NONE,true,false,CANDIDATE_WAL_OK,CANDIDATE_WAL_OK},
    {"visit-stop",KIND_VISIT,EFFECT_NONE,false,false,CANDIDATE_WAL_STOPPED,CANDIDATE_WAL_OK},
    {"scan-healthy",KIND_SCAN,EFFECT_NONE,true,false,CANDIDATE_WAL_OK,CANDIDATE_WAL_OK},
    {"scan-stop",KIND_SCAN,EFFECT_NONE,false,false,CANDIDATE_WAL_STOPPED,CANDIDATE_WAL_OK},
    {"read-reject",KIND_READ,EFFECT_REJECT,true,false,CANDIDATE_WAL_POISONED,CANDIDATE_WAL_POISONED},
    {"read-reject-pinned",KIND_READ,EFFECT_REJECT,true,true,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
    {"read-move",KIND_READ,EFFECT_MOVE,true,false,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED},
    {"visit-reject-stop",KIND_VISIT,EFFECT_REJECT,false,false,CANDIDATE_WAL_POISONED,CANDIDATE_WAL_POISONED},
    {"visit-move-stop",KIND_VISIT,EFFECT_MOVE,false,false,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED},
    {"scan-reject",KIND_SCAN,EFFECT_REJECT,true,false,CANDIDATE_WAL_POISONED,CANDIDATE_WAL_POISONED},
    {"scan-reject-stop",KIND_SCAN,EFFECT_REJECT,false,false,CANDIDATE_WAL_POISONED,CANDIDATE_WAL_POISONED},
    {"scan-move-stop",KIND_SCAN,EFFECT_MOVE,false,false,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED}};

static const owner_row_t OWNER_ROWS[] = {{"first-owner-true",true},{"first-owner-false",false}};

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void open_cross(tf_cross_t *x, const char *name)
{
    char tag[96]; CHECK(snprintf(tag,sizeof(tag),"callbacks-%s",name) > 0);
    tf_cross_open(x,tag,TF_ROOM);
}

static raw_t raw_take(const tf_cross_t *x)
{
    uint8_t head[CANDIDATE_WAL_RECORD_HEADER_BYTES]; tf_read_wal(&x->c,x->first_frame,head,sizeof(head));
    raw_t r = {.size = (size_t)read_le(head + 8,4)};
    CHECK(r.size >= 96 && r.size <= CANDIDATE_WAL_RECORD_BYTES);
    r.bytes = malloc(r.size); CHECK(r.bytes != NULL);
    tf_read_wal(&x->c,x->first_frame,r.bytes,r.size);
    return r;
}

static bool raw_same(const tf_cross_t *x, const raw_t *r)
{
    uint8_t *now = malloc(r->size); CHECK(now != NULL);
    tf_read_wal(&x->c,x->first_frame,now,r->size);
    bool is_same = memcmp(now,r->bytes,r->size) == 0;
    free(now);
    return is_same;
}

static bool is_same_payload(const raw_t *raw, const candidate_wal_record_t *got)
{
    candidate_wal_record_t expected; OK(candidate_wal_decode(fx_uuid,raw->bytes,raw->size,&expected));
    bool is_same = got->type == expected.type && got->lsn == expected.lsn && got->owner_epoch == expected.owner_epoch;
    is_same = is_same && got->transaction_id == expected.transaction_id && got->payload_bytes == expected.payload_bytes;
    return is_same && (expected.payload_bytes == 0 || memcmp(got->payload,expected.payload,expected.payload_bytes) == 0);
}

static probe_t probe_of(const tf_cross_t *x, effect_t effect, bool should_continue)
{
    probe_t p = {.wal = x->c.f.wal,.effect = effect,.should_continue = should_continue};
    tf_segment_path(&x->c,x->next_segment / SEGMENT,p.path);
    CHECK(snprintf(p.moved,sizeof(p.moved),"%s.moved",p.path) > 0);
    return p;
}

static void apply(probe_t *p)
{
    p->applied++;
    if (p->effect == EFFECT_REJECT) candidate_wal_reject(p->wal);
    if (p->effect == EFFECT_MOVE) CHECK(rename(p->path,p->moved) == 0);
}

static void hook_effect(void *context) { apply(context); }

static bool visit_effect(void *context, const candidate_wal_record_t *r)
{
    probe_t *p = context; (void)r;
    if (p->visits++ == 0) apply(p);
    return p->should_continue;
}

static outcome_t run_read(const tf_cross_t *x, probe_t *p, const raw_t *raw, bool should_hook)
{
    candidate_wal_record_t got; outcome_t o = {0};
    if (should_hook) { cf_arm(0); cf_io.hook_call = FINAL_READ_CALL; cf_io.on_read = hook_effect; cf_io.context = p; }
    o.status = candidate_wal_read_at(p->wal,x->first_frame,&got);
    o.reads = should_hook ? cf_io.reads : 0; cf_disarm();
    o.is_payload_same = o.status == CANDIDATE_WAL_OK && is_same_payload(raw,&got);
    o.applied = p->applied;
    return o;
}

static outcome_t run_kind(const tf_cross_t *x, kind_t kind, probe_t *p, const raw_t *raw, bool should_hook)
{
    if (kind == KIND_READ) return run_read(x,p,raw,should_hook);
    outcome_t o = {0};
    if (kind == KIND_VISIT) {
        candidate_wal_suffix_t s = {.before = x->before,.after = candidate_wal_info(p->wal),.visitor = visit_effect,.context = p};
        o.status = candidate_wal_visit_suffix(p->wal,&s);
    } else {
        candidate_wal_info_t info; o.status = candidate_wal_scan(p->wal,visit_effect,p,&info);
    }
    o.visits = p->visits; o.applied = p->applied;
    return o;
}

static unsigned expected_visits(const tf_cross_t *x, kind_t kind, bool should_continue)
{
    if (kind == KIND_READ) return 0;
    if (!should_continue) return 1;
    candidate_wal_info_t now = candidate_wal_info(x->c.f.wal);
    return (unsigned)(kind == KIND_VISIT ? cf_suffix_records(&x->before,&now) : now.records);
}

static void expect_control(const tf_cross_t *x, kind_t kind, const raw_t *raw, const char *tag)
{
    probe_t p = probe_of(x,EFFECT_NONE,true);
    outcome_t o = run_kind(x,kind,&p,raw,true);
    unsigned visits = expected_visits(x,kind,true);
    printf("CONTROL %s kind=%s status=%s visits=%u/%u applied=%u reads=%llu payload_same=%d\n",tag,KIND_NAMES[kind],
        vf_status(o.status),o.visits,visits,o.applied,(unsigned long long)o.reads,o.is_payload_same);
    OK(o.status); CHECK(o.visits == visits);
    if (kind == KIND_READ) CHECK(o.applied == 1 && o.reads == FINAL_READ_CALL && o.is_payload_same);
}

static void print_row(const row_t *row, const outcome_t *first, const outcome_t *next, candidate_wal_status_t handle,
    unsigned visits)
{
    printf("CALLBACK %s kind=%s effect=%s continue=%d pinned=%d first=%s expected_first=%s next=%s expected_next=%s handle=%s "
        "applied=%u visits=%u/%u next_visits=%u reads=%llu payload_same=%d\n",row->name,KIND_NAMES[row->kind],
        EFFECT_NAMES[row->effect],row->should_continue,row->is_pinned,vf_status(first->status),vf_status(row->first),
        vf_status(next->status),vf_status(row->next),vf_status(handle),first->applied,first->visits,visits,next->visits,
        (unsigned long long)first->reads,first->is_payload_same);
}

static void expect_row(const tf_cross_t *x, const row_t *row, const outcome_t *first, const outcome_t *next,
    candidate_wal_status_t handle, unsigned visits)
{
    CHECK(first->status == row->first && next->status == row->next && handle == row->next);
    CHECK(first->applied == 1 && first->visits == visits);
    CHECK(next->visits == (row->next == CANDIDATE_WAL_OK ? expected_visits(x,row->kind,true) : 0));
    if (row->kind == KIND_READ) CHECK(first->reads == FINAL_READ_CALL);
    if (row->kind == KIND_READ && row->first == CANDIDATE_WAL_OK) CHECK(first->is_payload_same);
}

static void run_row(const row_t *row)
{
    tf_cross_t x; open_cross(&x,row->name);
    candidate_wal_t *wal = x.c.f.wal;
    if (row->is_pinned) OK(candidate_wal_pin(wal));
    raw_t raw = raw_take(&x); candidate_wal_info_t cut = candidate_wal_info(wal);
    expect_control(&x,row->kind,&raw,row->name);
    unsigned visits = expected_visits(&x,row->kind,row->should_continue);
    probe_t p = probe_of(&x,row->effect,row->should_continue), q = probe_of(&x,EFFECT_NONE,true);
    outcome_t first = run_kind(&x,row->kind,&p,&raw,true), next = run_kind(&x,row->kind,&q,&raw,false);
    candidate_wal_status_t handle = candidate_wal_check_uuid(wal,fx_uuid);
    print_row(row,&first,&next,handle,visits);
    expect_row(&x,row,&first,&next,handle,visits);
    if (row->effect == EFFECT_MOVE) CHECK(rename(p.moved,p.path) == 0);
    candidate_wal_info_t now = candidate_wal_info(wal);
    CHECK(raw_same(&x,&raw) && tf_same_cut(&now,&cut) && now.durable_end_lsn == cut.durable_end_lsn);
    CHECK(cf_source_same(x.source,&x.held));
    free(raw.bytes);
    tf_cross_close(&x);
}

static void lower_first_owner(const tf_cross_t *x, const raw_t *raw)
{
    candidate_wal_record_t r; OK(candidate_wal_decode(fx_uuid,raw->bytes,raw->size,&r));
    candidate_wal_info_t after = candidate_wal_info(x->c.f.wal);
    CHECK(r.owner_epoch > 1);
    candidate_wal_record_t lowered = r; lowered.owner_epoch = r.owner_epoch - 1;
    uint8_t *bytes = malloc(raw->size); CHECK(bytes != NULL); size_t written = 0;
    OK(candidate_wal_encode(fx_uuid,&lowered,bytes,raw->size,&written));
    candidate_wal_record_t back; OK(candidate_wal_decode(fx_uuid,bytes,written,&back));
    bool is_kept = written == raw->size && back.lsn == r.lsn && back.type == r.type && back.transaction_id == r.transaction_id;
    is_kept = is_kept && back.previous_lsn == r.previous_lsn && back.payload_bytes == r.payload_bytes;
    is_kept = is_kept && (r.payload_bytes == 0 || memcmp(back.payload,r.payload,r.payload_bytes) == 0);
    printf("LOWERED lsn=%llu bytes=%zu owner=%llu->%llu before_highest=%llu after_highest=%llu kept=%d\n",
        (unsigned long long)r.lsn,written,(unsigned long long)r.owner_epoch,(unsigned long long)back.owner_epoch,
        (unsigned long long)x->before.highest_owner_epoch,(unsigned long long)after.highest_owner_epoch,is_kept);
    CHECK(r.lsn == x->first_frame && r.owner_epoch == x->before.highest_owner_epoch);
    CHECK(after.highest_owner_epoch == x->before.highest_owner_epoch);
    CHECK(is_kept && back.owner_epoch == lowered.owner_epoch && memcmp(bytes,raw->bytes,raw->size) != 0);
    tf_write_wal(&x->c,x->first_frame,bytes,written);
    free(bytes);
}

static void expect_owner_controls(const tf_cross_t *x, const raw_t *raw, const char *tag)
{
    probe_t all = probe_of(x,EFFECT_NONE,true), stop = probe_of(x,EFFECT_NONE,false);
    outcome_t a = run_kind(x,KIND_VISIT,&all,raw,false), s = run_kind(x,KIND_VISIT,&stop,raw,false);
    candidate_wal_status_t handle = candidate_wal_check_uuid(x->c.f.wal,fx_uuid);
    unsigned visits = expected_visits(x,KIND_VISIT,true);
    printf("OWNER_CONTROL %s all=%s visits=%u/%u stop=%s stop_visits=%u handle=%s\n",tag,vf_status(a.status),a.visits,visits,
        vf_status(s.status),s.visits,vf_status(handle));
    OK(a.status); CHECK(a.visits == visits && visits > 1);
    CHECK(s.status == CANDIDATE_WAL_STOPPED && s.visits == 1); OK(handle);
}

static void run_owner_row(const owner_row_t *row)
{
    tf_cross_t x; open_cross(&x,row->name);
    raw_t raw = raw_take(&x); candidate_wal_info_t cut = candidate_wal_info(x.c.f.wal);
    expect_owner_controls(&x,&raw,"healthy");
    lower_first_owner(&x,&raw);
    probe_t p = probe_of(&x,EFFECT_NONE,row->should_continue), q = probe_of(&x,EFFECT_NONE,row->should_continue);
    outcome_t first = run_kind(&x,KIND_VISIT,&p,&raw,false), next = run_kind(&x,KIND_VISIT,&q,&raw,false);
    candidate_wal_status_t handle = candidate_wal_check_uuid(x.c.f.wal,fx_uuid);
    candidate_wal_info_t now = candidate_wal_info(x.c.f.wal);
    printf("FIRST_OWNER %s continue=%d first=%s visits=%u next=%s next_visits=%u expected=CORRUPT/0 handle=%s cut_same=%d\n",
        row->name,row->should_continue,vf_status(first.status),first.visits,vf_status(next.status),next.visits,vf_status(handle),
        tf_same_cut(&now,&cut));
    CHECK(first.status == CANDIDATE_WAL_CORRUPT && first.visits == 0);
    CHECK(next.status == CANDIDATE_WAL_CORRUPT && next.visits == 0);
    OK(handle); CHECK(tf_same_cut(&now,&cut));
    tf_write_wal(&x.c,x.first_frame,raw.bytes,raw.size);
    CHECK(raw_same(&x,&raw));
    expect_owner_controls(&x,&raw,"restored");
    CHECK(cf_source_same(x.source,&x.held));
    free(raw.bytes);
    tf_cross_close(&x);
}

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(1200);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++) {
        if (!is_all && strcmp(selected,ROWS[i].name) != 0) continue;
        printf("CASE %s\n",ROWS[i].name); run_row(&ROWS[i]); has_match = true;
    }
    for (unsigned i = 0; i < sizeof(OWNER_ROWS) / sizeof(OWNER_ROWS[0]); i++) {
        if (!is_all && strcmp(selected,OWNER_ROWS[i].name) != 0) continue;
        printf("CASE %s\n",OWNER_ROWS[i].name); run_owner_row(&OWNER_ROWS[i]); has_match = true;
    }
    CHECK(has_match);
    printf("PASS recovery closure callbacks case=%s checks=%u; owned-WAL raw read, suffix and scan end status only, not durable "
        "seeds, power loss or speed\n",selected,checks);
    return 0;
}
