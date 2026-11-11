#include "test_candidate_buffer_cases.h"

#include <string.h>

#define FRAME_BEGIN 112u
#define BODY_BYTES 48u

static uint64_t selected_id(bf_t *b)
{
    candidate_control_info_t info = candidate_control_info(b->control);
    CHECK(info.selected >= 0);
    return info.slots[info.selected].checkpoint_id;
}

static void expect_poisoned(bf_t *b)
{
    candidate_buffer_admission_t a; candidate_buffer_checkpoint_t c; candidate_buffer_flush_t f;
    EXPECT(candidate_buffer_flush(b->pool,4,&f),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_checkpoint(b->pool,4,&c),CANDIDATE_WAL_POISONED);
    EXPECT(candidate_buffer_admit(b->pool,candidate_wal_info(b->wal).valid_end_lsn,&a),CANDIDATE_WAL_POISONED);
    CHECK(f.written == 0 && candidate_buffer_info(b->pool).is_fenced);
}

static void expect_alternative_body(const uint8_t *page, uint64_t transaction, uint64_t seed)
{
    uint8_t body[BODY_BYTES]; fx_payload(seed,sizeof(body),body);
    CHECK(bf_has_version(page,transaction) && !bf_has_body(page,transaction,seed,BODY_BYTES));
    const uint8_t *found = page + bf_body_offset(page,transaction);
    body[0] ^= 0x5a;
    CHECK(memcmp(found,body,BODY_BYTES) == 0);
}

typedef struct {
    bf_t b;
    fx_txn_t t;
    uint64_t lsn, old;
    size_t offset;
} prepared_t;

static void prepare(prepared_t *p, const char *label)
{
    bf_boot(&p->b,label,4);
    candidate_buffer_admission_t a;
    bf_begin(&p->b,&p->t);
    OK(bf_write_version(&p->b,&p->t,p->b.pages[0],51,&a)); bf_commit(&p->b,&p->t);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bf_copy(&p->b,p->b.pages[0],page);
    p->lsn = a.begin_lsn + FRAME_BEGIN; p->offset = 32 + bf_body_offset(page,p->t.id); p->old = selected_id(&p->b);
}

static void swap_checkpoint(const char *label, bool on_verify)
{
    prepared_t p; prepare(&p,label);
    bf_t *b = &p.b;
    unsigned verifies = b->verifies;
    if (on_verify) bf_swap_on_verify(b,p.lsn,p.offset); else bf_swap_at_scan(b,p.lsn,p.offset,2,2);
    bf_trace(b,true);
    candidate_buffer_checkpoint_t c;
    candidate_wal_status_t status = candidate_buffer_checkpoint(b->pool,0,&c);
    bf_trace(b,false);
    unsigned writes = b->counts[EV_CONTROL_WRITE];
    bool is_published = selected_id(b) != p.old;
    printf("SWAP-RESULT %s status=%s phase=%s verifies_before=%u verifies_at_swap=%u verifies=%u control_writes=%u published=%d reads=%u\n",
        label,bf_status(status),b->swap.phase == NULL ? "none" : b->swap.phase,verifies,b->swap.verifies,b->verifies,writes,
        is_published,b->view.reads);
    CHECK(b->swap.phase != NULL && strcmp(b->swap.phase,on_verify ? "control" : "proof") == 0 && b->view.reads > 0);
    CHECK(b->swap.verifies == verifies + (on_verify ? 1u : 0u));
    bf_snapshot_all(b,"swapped");
    uint8_t alternative[CANDIDATE_PAGE_BYTES];
    OK(bf_recover_view(b,"alternative",b->owner + 1,b->pages[0],alternative));
    expect_alternative_body(alternative,p.t.id,51);
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(writes == 0 && !is_published && candidate_buffer_info(b->pool).is_fenced && b->data_writes == 0);
    bf_view_set(b,VIEW_OFF,0);
    EXPECT(candidate_wal_force(b->wal,candidate_wal_info(b->wal).valid_end_lsn),CANDIDATE_WAL_CORRUPT);
    expect_poisoned(b);
    OK(candidate_buffer_close(&b->pool));
    EXPECT(bf_open_pool(b),CANDIDATE_WAL_CORRUPT); CHECK(b->pool == NULL);
    bf_close(b);
    OK(bf_recover(b,9));
    bf_reattach(b,9);
    bf_expect_body(b,b->pages[0],p.t.id,51,BODY_BYTES);
    bf_report(b,label,status);
    bf_free(b);
}

static void case_swap_proof(void) { swap_checkpoint("swap-proof",false); }
static void case_swap_control(void) { swap_checkpoint("swap-control",true); }

static void case_swap_unchanged(void)
{
    prepared_t p; prepare(&p,"swap-unchanged");
    bf_t *b = &p.b;
    unsigned verifies = b->verifies;
    bf_trace(b,true);
    candidate_buffer_checkpoint_t c;
    OK(candidate_buffer_checkpoint(b->pool,0,&c));
    bf_trace(b,false);
    CHECK(b->counts[EV_CONTROL_WRITE] == 1 && b->verifies == verifies + 1 && selected_id(b) == p.old + 1);
    CHECK(c.dirty_entries == 1 && c.remaining_dirty == 1 && b->data_writes == 0 && b->swap.phase == NULL);
    bf_snapshot_all(b,"published");
    candidate_buffer_flush_t f; OK(candidate_buffer_flush(b->pool,4,&f)); CHECK(f.written == 1);
    bf_restart(b,9);
    CHECK(selected_id(b) > p.old + 1);
    bf_expect_body(b,b->pages[0],p.t.id,51,BODY_BYTES);
    bf_report(b,"swap-unchanged",CANDIDATE_WAL_OK);
    bf_free(b);
}

typedef struct {
    fx_txn_t *t;
    uint64_t end;
    bool is_complete;
} appended_t;

static void append_during_write(bf_t *b, uint64_t id, unsigned index)
{
    (void)id; (void)index;
    b->on_write = NULL;
    appended_t *x = b->hook_context;
    x->end = bf_external(b,x->t,&b->pages[1],1,77,x->is_complete);
}

static void callback_group(const char *label, bool is_complete)
{
    prepared_t p; prepare(&p,label);
    bf_t *b = &p.b;
    fx_txn_t u; bf_begin(b,&u);
    appended_t x = {.t = &u,.is_complete = is_complete};
    b->on_write = append_during_write; b->hook_context = &x;
    bf_trace(b,true);
    candidate_buffer_checkpoint_t c = {.dirty_entries = 9};
    candidate_wal_status_t status = candidate_buffer_checkpoint(b->pool,4,&c);
    bf_trace(b,false);
    candidate_buffer_info_t info = candidate_buffer_info(b->pool);
    printf("CALLBACK-GROUP %s status=%s fenced=%d control_writes=%u data_writes=%u end=%llu\n",label,bf_status(status),info.is_fenced,
        b->counts[EV_CONTROL_WRITE],b->data_writes,(unsigned long long)x.end);
    CHECK(b->on_write == NULL && x.end != 0 && b->data_writes == 1);
    bf_snapshot_all(b,"unpublished");
    EXPECT(status,CANDIDATE_WAL_BUSY);
    CHECK(c.dirty_entries == 0 && b->counts[EV_CONTROL_WRITE] == 0 && selected_id(b) == p.old && info.is_fenced);
    expect_poisoned(b);
    candidate_buffer_lease_t lease;
    EXPECT(candidate_buffer_pin(b->pool,bf_ref(b,b->pages[0]),&lease),CANDIDATE_WAL_POISONED);
    if (is_complete) {
        bf_restart(b,9);
        bf_expect_body(b,b->pages[0],p.t.id,51,BODY_BYTES); bf_expect_body(b,b->pages[1],u.id,77,BODY_BYTES);
    }
    bf_report(b,label,status);
    bf_free(b);
}

static void case_callback_complete(void) { callback_group("callback-complete-group",true); }
static void case_callback_incomplete(void) { callback_group("callback-incomplete-group",false); }

const bf_case_t BF_SOURCE_CASES[] = {
    {"swap-proof",case_swap_proof},{"swap-control",case_swap_control},{"swap-unchanged",case_swap_unchanged},
    {"callback-complete-group",case_callback_complete},{"callback-incomplete-group",case_callback_incomplete},{NULL,NULL}
};
