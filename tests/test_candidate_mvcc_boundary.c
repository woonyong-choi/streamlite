#include "test_candidate_mvcc_fixture.h"

#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "../src/storage/candidate_mvcc_internal.h"
#include "../src/storage/candidate_wal_internal.h"

#define ROW_A 0u
#define ROW_B 1u
#define FOREIGN_TXN 60u
#define GROUP_TXN 60u
#define RECORDS 16u
#define SCAN_RECORDS 64u
#define ID_BATCH 64u

unsigned checks;

typedef enum { EFFECT_NONE, EFFECT_REJECT, EFFECT_APPEND } effect_t;
typedef struct {
    candidate_wal_t *wal;
    effect_t effect;
    uint64_t armed_end, seen_valid, seen_durable, append_end, foreign;
    unsigned hits;
    candidate_wal_status_t append_status;
} force_hook_t;
typedef struct { uint64_t last, begin, end; } group_seed_t;

static candidate_mvcc_value_t read_row(candidate_mvcc_t *m, unsigned row)
{
    candidate_mvcc_reader_t r; EXPECT(candidate_mvcc_reader_open(m,&r),CANDIDATE_WAL_OK);
    candidate_mvcc_value_t v; EXPECT(candidate_mvcc_reader_read(m,&r,mf_key(row),&v),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_OK);
    return v;
}

static void expect_bytes(const candidate_mvcc_value_t *v, const uint8_t *bytes)
{
    CHECK(v->is_found && v->size == MF_PAYLOAD && memcmp(v->bytes,bytes,MF_PAYLOAD) == 0);
}

static candidate_wal_status_t begin_row(candidate_mvcc_t *m, uint64_t nonce, unsigned row, const uint8_t *bytes,
    candidate_mvcc_txn_t *out)
{
    candidate_mvcc_write_t w = mf_write(row,bytes,MF_PAYLOAD);
    candidate_mvcc_request_t r = mf_request(nonce,&w,1);
    return candidate_mvcc_begin(m,&r,out);
}

static candidate_mvcc_answer_t query_row(candidate_mvcc_t *m, uint64_t nonce, unsigned row, const uint8_t *bytes)
{
    candidate_mvcc_write_t w = mf_write(row,bytes,MF_PAYLOAD);
    candidate_mvcc_request_t r = mf_request(nonce,&w,1);
    candidate_mvcc_answer_t a; EXPECT(candidate_mvcc_query(m,&r,&a),CANDIDATE_WAL_OK);
    return a;
}

static void commit_row(candidate_mvcc_t *m, candidate_mvcc_txn_t *t, candidate_mvcc_response_t *out)
{
    EXPECT(candidate_mvcc_apply(m,t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_commit(m,t,out),CANDIDATE_WAL_OK);
    CHECK(out->state == CANDIDATE_MVCC_COMMITTED && t->generation == 0 && out->terminal_end_lsn != 0);
}

static void open_calibrated(mf_t *f, const char *label, candidate_mvcc_t **out)
{
    candidate_wal_status_t status = mf_open(f,out);
    if (status == CANDIDATE_WAL_OK) return;
    printf("CALIBRATION-FAILURE %s open=%s\n",label,mf_status(status));
    exit(3);
}

static void recover_full(mf_t *f)
{
    candidate_recovery_options_t o = fx_options(f->built.fx,f->next_owner++);
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    printf("RECOVER owner=%" PRIu64 " status=%s\n",o.owner_epoch,mf_status(status));
    EXPECT(status,CANDIDATE_WAL_OK);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    printf("RECOVER-PUBLICATION outcomes=%u system_groups=%u appended_aborts=%u\n",p.outcomes,p.system_groups,
        p.repair.terminals.appended_aborts);
    EXPECT(candidate_recovery_close(&r),CANDIDATE_WAL_OK);
}

static void expect_refused(const mf_t *f, candidate_mvcc_t *m, candidate_wal_status_t status, const candidate_mvcc_txn_t *t,
    const candidate_mvcc_info_t *before, const mf_wal_t *wal_before)
{
    candidate_mvcc_info_t after = candidate_mvcc_info(m);
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(f,0,&chain,&wal);
    printf("REFUSAL status=%s handle=%" PRIu64 "/%" PRIu64 " records=%u->%u end=%" PRIu64 "->%" PRIu64 " last=%" PRIu64 "\n",
        mf_status(status),t->owner_id,t->generation,wal_before->records,wal.records,wal_before->end_lsn,wal.end_lsn,after.last_csn);
    EXPECT(status,CANDIDATE_WAL_LIMIT);
    CHECK(t->owner_id == 0 && t->generation == 0 && t->slot == 0);
    CHECK(wal.records == wal_before->records && wal.end_lsn == wal_before->end_lsn && wal.reserves == wal_before->reserves);
    CHECK(after.active == before->active && after.outcomes == before->outcomes && after.last_csn == before->last_csn);
    CHECK(after.next_transaction_id == before->next_transaction_id && after.transaction_end == before->transaction_end);
    CHECK(after.wal_credit_bytes == before->wal_credit_bytes && after.fence == CANDIDATE_WAL_OK);
}

static void begin_refused(const mf_t *f, candidate_mvcc_t *m, uint64_t nonce, const uint8_t *bytes)
{
    candidate_mvcc_info_t before = candidate_mvcc_info(m);
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(f,0,&chain,&wal);
    candidate_mvcc_txn_t t;
    candidate_wal_status_t status = begin_row(m,nonce,ROW_A,bytes,&t);
    expect_refused(f,m,status,&t,&before,&wal);
}

static uint64_t begin_abort(candidate_mvcc_t *m, uint64_t nonce, const uint8_t *bytes)
{
    candidate_mvcc_txn_t t; EXPECT(begin_row(m,nonce,ROW_A,bytes,&t),CANDIDATE_WAL_OK);
    candidate_mvcc_txn_info_t i; EXPECT(candidate_mvcc_inspect(m,&t,&i),CANDIDATE_WAL_OK);
    CHECK(i.state == CANDIDATE_MVCC_ACTIVE && !i.is_applied && i.transaction_id != 0);
    EXPECT(candidate_mvcc_abort(m,&t),CANDIDATE_WAL_OK);
    CHECK(t.owner_id == 0 && t.generation == 0);
    return i.transaction_id;
}

static void expect_aborted(candidate_mvcc_t *m, uint64_t nonce, const uint8_t *bytes, uint64_t transaction_id)
{
    candidate_mvcc_answer_t a = query_row(m,nonce,ROW_A,bytes);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.state == CANDIDATE_MVCC_ABORTED);
    CHECK(a.response.transaction_id == transaction_id);
}

static void print_ids(const char *label, candidate_mvcc_t *m)
{
    candidate_mvcc_info_t i = candidate_mvcc_info(m);
    printf("IDS %s owner=%" PRIu64 " next=%" PRIu64 " end=%" PRIu64 " max-next=%" PRIu64 " outcomes=%u active=%u fence=%s\n",label,
        i.owner_epoch,i.next_transaction_id,i.transaction_end,UINT64_MAX - i.next_transaction_id,i.outcomes,i.active,mf_status(i.fence));
}

static void case_id_control(void)
{
    mf_t f; mf_boot_highwater(&f,"id-control",UINT64_MAX - 2 * ID_BATCH);
    CHECK(f.built.anchor.transaction_highwater == UINT64_MAX - 2 * ID_BATCH);
    candidate_mvcc_t *m = NULL; open_calibrated(&f,"id-control",&m);
    print_ids("id-control-open",m);
    uint8_t value[MF_PAYLOAD]; mf_fill(0xe1,value);
    uint64_t first = begin_abort(m,0x7401,value);
    CHECK(first > UINT64_MAX - 2 * ID_BATCH && first <= UINT64_MAX - ID_BATCH);
    print_ids("id-control-aborted",m);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    open_calibrated(&f,"id-control-reopen",&m);
    print_ids("id-control-reopen",m);
    expect_aborted(m,0x7401,value,first);
    uint64_t second = begin_abort(m,0x7402,value);
    CHECK(second > first && second > UINT64_MAX - ID_BATCH);
    expect_aborted(m,0x7402,value,second);
    candidate_mvcc_value_t v = read_row(m,ROW_A); mf_expect_initial(&f,&v,ROW_A);
    printf("ID-CONTROL first=%" PRIu64 " max-first=%" PRIu64 " second=%" PRIu64 " max-second=%" PRIu64 "\n",first,UINT64_MAX - first,
        second,UINT64_MAX - second);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_id_exhaust(void)
{
    mf_t f; mf_boot_highwater(&f,"id-exhaust",UINT64_MAX - ID_BATCH);
    CHECK(f.built.anchor.transaction_highwater == UINT64_MAX - ID_BATCH);
    candidate_mvcc_t *m = NULL; open_calibrated(&f,"id-exhaust",&m);
    print_ids("id-exhaust-open",m);
    uint32_t outcomes = candidate_mvcc_info(m).outcomes;
    uint8_t value[MF_PAYLOAD]; mf_fill(0xe2,value);
    uint64_t ids[ID_BATCH];
    for (unsigned i = 0; i < ID_BATCH; i++) {
        ids[i] = begin_abort(m,0x7500 + i,value);
        CHECK(i == 0 || ids[i] == ids[i - 1] + 1);
    }
    print_ids("id-exhaust-aborted",m);
    printf("ID-EXHAUST first=%" PRIu64 " max-first=%" PRIu64 " last=%" PRIu64 " max-last=%" PRIu64 "\n",ids[0],UINT64_MAX - ids[0],
        ids[ID_BATCH - 1],UINT64_MAX - ids[ID_BATCH - 1]);
    CHECK(ids[0] == UINT64_MAX - (ID_BATCH - 1) && ids[ID_BATCH - 1] == UINT64_MAX);
    candidate_mvcc_info_t info = candidate_mvcc_info(m);
    CHECK(info.outcomes == outcomes + ID_BATCH && info.active == 0 && info.fence == CANDIDATE_WAL_OK);
    begin_refused(&f,m,0x7600,value);
    candidate_mvcc_value_t v = read_row(m,ROW_A); mf_expect_initial(&f,&v,ROW_A);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    recover_full(&f);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    print_ids("id-exhaust-reopen",m);
    for (unsigned i = 0; i < ID_BATCH; i++) expect_aborted(m,0x7500 + i,value,ids[i]);
    v = read_row(m,ROW_A); mf_expect_initial(&f,&v,ROW_A);
    v = read_row(m,ROW_B); mf_expect_initial(&f,&v,ROW_B);
    begin_refused(&f,m,0x7601,value);
    printf("ID-EXHAUST-REOPEN aborted=%u last=%" PRIu64 "\n",ID_BATCH,ids[ID_BATCH - 1]);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void push_group(void *built, void *context)
{
    built_t *b = built; group_seed_t *g = context; fx_t *fx = b->fx;
    uint8_t page[CANDIDATE_PAGE_BYTES];
    CHECK(pread(fx->data_fd,page,sizeof(page),(off_t)((b->root_id - 1) * CANDIDATE_PAGE_BYTES)) == (ssize_t)sizeof(page));
    candidate_catalog_root_t root; OK(candidate_catalog_decode_root(fx_uuid,page,sizeof(page),&root));
    CHECK(fx->group_id < g->last && b->carrier_txn < GROUP_TXN && GROUP_TXN <= root.transaction_highwater);
    fx_txn_t t = {0};
    g->begin = fx_position(fx);
    fx_begin(fx,&t,GROUP_TXN);
    fx->group_id = g->last - 1;
    static fx_image_t image;
    image.id = b->root_id; image.generation = 1; image.previous = fx->pages[b->root_id].lsn;
    uint64_t end = fx_group_end(fx,1,0);
    candidate_page_location_t l = fx_location(b->root_id,1,end);
    OK(candidate_catalog_encode_root(&l,&root,image.page,CANDIDATE_PAGE_BYTES));
    g->end = fx_group(fx,&t,&image,1,NULL,0,true,true);
    CHECK(g->end == end && fx->group_id == g->last && t.groups == 1 && t.group_ids[0] == g->last);
    fx_abort(fx,&t);
}

static void case_group_credit(void)
{
    group_seed_t g = {.last = UINT64_MAX - 8};
    mf_t f; mf_boot_boundary(&f,"group-credit",push_group,&g);
    mf_record_t records[SCAN_RECORDS];
    unsigned count = mf_scan_records(&f,g.begin,records,SCAN_RECORDS);
    CHECK(count >= 5 && records[0].type == 2 && records[0].transaction_id == GROUP_TXN);
    CHECK(records[1].type == 3 && records[1].transaction_id == GROUP_TXN && records[1].words[0] == g.last);
    CHECK(records[2].type == 4 && records[2].transaction_id == GROUP_TXN && records[2].words[0] == g.last &&
        records[2].words[1] == f.built.root_id);
    CHECK(records[3].type == 8 && records[3].transaction_id == GROUP_TXN && records[3].words[0] == g.last && records[3].end == g.end);
    CHECK(records[4].type == 11 && records[4].transaction_id == GROUP_TXN);
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(&f,GROUP_TXN,&chain,&wal);
    CHECK(chain.count == 5 && chain.types[4] == 11);
    printf("GROUP-SEED last=%" PRIu64 " max-last=%" PRIu64 " group=%" PRIu64 "->%" PRIu64 " root=%" PRIu64 " records_after=%u\n",g.last,
        UINT64_MAX - g.last,g.begin,g.end,f.built.root_id,count);
    candidate_mvcc_t *m = NULL; open_calibrated(&f,"group-credit",&m);
    candidate_buffer_info_t pool = candidate_buffer_info(m->pool);
    printf("GROUP-OPENED last_group=%" PRIu64 " max-last_group=%" PRIu64 " fenced=%d\n",pool.last_group_id,UINT64_MAX - pool.last_group_id,
        pool.is_fenced);
    CHECK(!pool.is_fenced && pool.last_group_id >= g.last && candidate_mvcc_info(m).fence == CANDIDATE_WAL_OK);
    candidate_mvcc_value_t v = read_row(m,ROW_A); mf_expect_initial(&f,&v,ROW_A);
    v = read_row(m,ROW_B); mf_expect_initial(&f,&v,ROW_B);
    uint8_t value[MF_PAYLOAD]; mf_fill(0xe3,value);
    begin_refused(&f,m,0x7701,value);
    CHECK(candidate_buffer_info(m->pool).last_group_id == pool.last_group_id);
    v = read_row(m,ROW_A); mf_expect_initial(&f,&v,ROW_A);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static bool on_force(void *context)
{
    force_hook_t *h = context;
    candidate_wal_info_t info = candidate_wal_info(h->wal);
    if (info.valid_end_lsn <= h->armed_end) return false;
    h->hits++; h->seen_valid = info.valid_end_lsn; h->seen_durable = info.durable_end_lsn;
    if (h->effect == EFFECT_REJECT) candidate_wal_reject(h->wal);
    if (h->effect == EFFECT_APPEND) {
        uint8_t body[16] = {0};
        candidate_wal_record_t r = {.type = 2,.transaction_id = h->foreign != 0 ? h->foreign : FOREIGN_TXN,
            .owner_epoch = info.highest_owner_epoch,.payload = body,.payload_bytes = sizeof(body)};
        h->append_status = candidate_wal_append(h->wal,&r,&h->append_end);
    }
    return true;
}

static bool is_zero(const candidate_buffer_reservation_t *r)
{
    return r->group_id == 0 && r->end_lsn == 0 && r->previous_highwater == 0 && r->next_highwater == 0;
}

static candidate_wal_status_t reserve_hooked(candidate_mvcc_t *m, force_hook_t *h, candidate_buffer_reservation_t *out)
{
    h->wal = m->recovery->wal; h->armed_end = candidate_wal_info(h->wal).valid_end_lsn;
    unsigned fsyncs = mf_faults.fsyncs;
    mf_arm_fsync_hook(on_force,h);
    candidate_wal_status_t status = candidate_buffer_reserve_transactions(m->pool,MVCC_ID_BATCH,out);
    mf_arm_fsync_hook(NULL,NULL);
    candidate_buffer_info_t info = candidate_buffer_info(m->pool);
    printf("RESERVE effect=%d status=%s hits=%u fsyncs=%u armed=%" PRIu64 " seen=%" PRIu64 "/%" PRIu64 " receipt=%" PRIu64 "@%" PRIu64
        " %" PRIu64 "->%" PRIu64 " fenced=%d append=%s@%" PRIu64 "\n",(int)h->effect,mf_status(status),h->hits,mf_faults.fsyncs - fsyncs,
        h->armed_end,h->seen_valid,h->seen_durable,out->group_id,out->end_lsn,out->previous_highwater,out->next_highwater,info.is_fenced,
        mf_status(h->append_status),h->append_end);
    return status;
}

static void expect_reserve_group(const mf_t *f, candidate_mvcc_t *m, const force_hook_t *h, const candidate_buffer_reservation_t *r)
{
    mf_record_t records[RECORDS];
    unsigned count = mf_scan_records(f,h->armed_end,records,RECORDS);
    static const uint16_t types[5] = {3,4,4,16,8};
    CHECK(count == 5 && records[0].lsn == h->armed_end && records[4].end == r->end_lsn);
    for (unsigned i = 0; i < 5; i++) CHECK(records[i].type == types[i] && records[i].transaction_id == 0 && records[i].words[0] == r->group_id);
    candidate_control_state_t anchor = candidate_buffer_info(m->pool).anchor;
    CHECK(records[1].words[1] == anchor.outcome_root.id && records[2].words[1] == anchor.allocation_root.id);
    CHECK(records[3].words[1] == r->previous_highwater && records[3].words[2] == r->next_highwater);
    printf("RESERVE-GROUP group=%" PRIu64 " images=%" PRIu64 ",%" PRIu64 " event=%" PRIu64 "->%" PRIu64 " end=%" PRIu64 "\n",r->group_id,
        records[1].words[1],records[2].words[1],records[3].words[1],records[3].words[2],records[4].end);
}

static void case_reserve_control(void)
{
    mf_t f; mf_boot_boundary(&f,"reserve-control",NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    force_hook_t h = {.effect = EFFECT_NONE};
    candidate_buffer_reservation_t r;
    EXPECT(reserve_hooked(m,&h,&r),CANDIDATE_WAL_OK);
    CHECK(h.hits == 1 && h.seen_valid == r.end_lsn && h.seen_durable < r.end_lsn);
    CHECK(r.group_id != 0 && r.next_highwater == r.previous_highwater + MVCC_ID_BATCH);
    CHECK(candidate_wal_info(h.wal).durable_end_lsn >= r.end_lsn && candidate_wal_info(h.wal).valid_end_lsn == r.end_lsn);
    CHECK(!candidate_buffer_info(m->pool).is_fenced && candidate_buffer_info(m->pool).last_group_id == r.group_id);
    expect_reserve_group(&f,m,&h,&r);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t value[MF_PAYLOAD]; mf_fill(0xd1,value);
    candidate_mvcc_txn_t t; EXPECT(begin_row(m,0x7301,ROW_A,value,&t),CANDIDATE_WAL_OK);
    candidate_mvcc_response_t response; commit_row(m,&t,&response);
    CHECK(response.transaction_id > r.next_highwater);
    printf("RESERVE-CONTROL reopened next_id_after=%" PRIu64 " highwater=%" PRIu64 "\n",response.transaction_id,r.next_highwater);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}


static void expect_fenced(candidate_mvcc_t *m, candidate_wal_status_t status, const candidate_buffer_reservation_t *r)
{
    CHECK(status != CANDIDATE_WAL_OK);
    CHECK(is_zero(r));
    CHECK(candidate_buffer_info(m->pool).is_fenced);
    candidate_buffer_reservation_t again;
    EXPECT(candidate_buffer_reserve_transactions(m->pool,MVCC_ID_BATCH,&again),CANDIDATE_WAL_POISONED);
    CHECK(is_zero(&again));
}

static void case_reserve_reject(void)
{
    mf_t f; mf_boot_boundary(&f,"reserve-reject",NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    force_hook_t h = {.effect = EFFECT_REJECT};
    candidate_buffer_reservation_t r;
    candidate_wal_status_t status = reserve_hooked(m,&h,&r);
    CHECK(h.hits == 1 && h.seen_valid > h.armed_end && h.seen_durable <= h.armed_end);
    candidate_wal_status_t after = candidate_wal_force(h.wal,candidate_wal_info(h.wal).valid_end_lsn);
    printf("REJECTED handle force=%s\n",mf_status(after));
    CHECK(after != CANDIDATE_WAL_OK);
    expect_fenced(m,status,&r);
    printf("REJECT-CLOSE status=%s\n",mf_status(candidate_mvcc_close(&m)));
    mf_free(&f);
}

static void case_reserve_append(void)
{
    mf_t f; mf_boot_boundary(&f,"reserve-append",NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    force_hook_t h = {.effect = EFFECT_APPEND};
    candidate_buffer_reservation_t r;
    candidate_wal_status_t status = reserve_hooked(m,&h,&r);
    CHECK(h.hits == 1 && h.seen_valid > h.armed_end && h.seen_durable <= h.armed_end);
    EXPECT(h.append_status,CANDIDATE_WAL_OK);
    CHECK(h.append_end > h.seen_valid && candidate_wal_info(h.wal).valid_end_lsn == h.append_end);
    mf_record_t records[RECORDS];
    unsigned count = mf_scan_records(&f,h.seen_valid,records,RECORDS);
    CHECK(count == 1 && records[0].type == 2 && records[0].transaction_id == FOREIGN_TXN && records[0].end == h.append_end);
    expect_fenced(m,status,&r);
    printf("APPEND-CLOSE status=%s\n",mf_status(candidate_mvcc_close(&m)));
    mf_free(&f);
}

static void expect_unknown_body(const char *label, const candidate_mvcc_response_t *r)
{
    static const uint8_t zero[CANDIDATE_MVCC_RESPONSE_BYTES];
    bool is_empty = r->size == 0 && r->affected_rows == 0 && memcmp(r->bytes,zero,sizeof(zero)) == 0;
    bool has_no_result = r->result.page.id == 0 && r->result.page.generation == 0 && r->result.slot == 0 &&
        r->result.slot_generation == 0;
    printf("RESPONSE %s state=%d id=%" PRIu64 " csn=%" PRIu64 " terminal=%" PRIu64 " size=%u affected=%" PRIu64 " result=%" PRIu64
        ":%u body0=%02x empty=%d\n",label,(int)r->state,r->transaction_id,r->csn,r->terminal_end_lsn,r->size,r->affected_rows,
        r->result.page.id,r->result.slot,r->bytes[0],is_empty && has_no_result);
    CHECK(r->state == CANDIDATE_MVCC_UNKNOWN);
    CHECK(is_empty && has_no_result);
}

static void expect_owner_fenced(candidate_mvcc_t *m, const uint8_t *bytes)
{
    candidate_mvcc_info_t info = candidate_mvcc_info(m);
    candidate_mvcc_txn_t t;
    candidate_wal_status_t begin = begin_row(m,0x7fff,ROW_B,bytes,&t);
    candidate_mvcc_reader_t r;
    candidate_wal_status_t reader = candidate_mvcc_reader_open(m,&r);
    printf("FENCED fence=%s pool=%d begin=%s reader=%s\n",mf_status(info.fence),candidate_buffer_info(m->pool).is_fenced,
        mf_status(begin),mf_status(reader));
    CHECK(info.fence != CANDIDATE_WAL_OK && begin != CANDIDATE_WAL_OK && reader != CANDIDATE_WAL_OK);
}

static void expect_resolved(mf_t *f, candidate_mvcc_t *m, uint64_t nonce, const uint8_t *bytes,
    const candidate_mvcc_txn_info_t *ti, bool is_committed)
{
    uint64_t transaction_id = ti->transaction_id;
    candidate_mvcc_answer_t a = query_row(m,nonce,ROW_A,bytes);
    candidate_mvcc_value_t v = read_row(m,ROW_A);
    printf("RESOLVED id=%" PRIu64 " lookup=%d state=%d committed_on_disk=%d row_matches_new=%d\n",transaction_id,(int)a.lookup,
        (int)a.response.state,is_committed,v.is_found && memcmp(v.bytes,bytes,MF_PAYLOAD) == 0);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.transaction_id == transaction_id);
    CHECK(a.response.state == (is_committed ? CANDIDATE_MVCC_COMMITTED : CANDIDATE_MVCC_ABORTED));
    expect_bytes(&v,is_committed ? bytes : f->initial[0]);
    v = read_row(m,ROW_B); expect_bytes(&v,f->initial[1]);
    uint8_t page[CANDIDATE_PAGE_BYTES]; mf_slot_t s;
    mf_read_version(f,ti->versions[0],page,&s);
    CHECK(s.transaction_id == transaction_id && s.table_id == 1 && s.row_id == mf_key(ROW_A).row_id);
    CHECK(memcmp(s.bytes,bytes,MF_PAYLOAD) == 0);
}

static void case_terminal(const char *tag, effect_t effect, bool is_commit, uint64_t nonce)
{
    mf_t f; mf_boot_boundary(&f,tag,NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t value[MF_PAYLOAD]; mf_fill((uint8_t)nonce,value);
    candidate_mvcc_txn_t t; EXPECT(begin_row(m,nonce,ROW_A,value,&t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    candidate_mvcc_txn_info_t ti; EXPECT(candidate_mvcc_inspect(m,&t,&ti),CANDIDATE_WAL_OK);
    CHECK(ti.state == CANDIDATE_MVCC_ACTIVE && ti.is_applied && ti.group_id != 0);
    force_hook_t h = {.effect = effect,.foreign = ti.transaction_id + 1,.wal = m->recovery->wal};
    h.armed_end = candidate_wal_info(h.wal).valid_end_lsn;
    candidate_mvcc_response_t response; memset(&response,0xa5,sizeof(response));
    unsigned fsyncs = mf_faults.fsyncs;
    mf_arm_fsync_hook(on_force,&h);
    candidate_wal_status_t status = is_commit ? candidate_mvcc_commit(m,&t,&response) : candidate_mvcc_abort(m,&t);
    mf_arm_fsync_hook(NULL,NULL);
    mf_chain_t chain; mf_wal_t wal; mf_scan_wal(&f,ti.transaction_id,&chain,&wal);
    uint16_t terminal = chain.count == 0 ? 0 : chain.types[chain.count - 1];
    printf("TERMINAL %s status=%s id=%" PRIu64 " group=%" PRIu64 "@%" PRIu64 " hits=%u fsyncs=%u armed=%" PRIu64 " seen=%" PRIu64 "/%" PRIu64
        " terminal=%u@%" PRIu64 " terminal_bytes=%" PRIu64 " disk_end=%" PRIu64 " append=%s@%" PRIu64 " handle=%" PRIu64 "\n",tag,
        mf_status(status),ti.transaction_id,ti.group_id,ti.group_end_lsn,h.hits,mf_faults.fsyncs - fsyncs,h.armed_end,h.seen_valid,
        h.seen_durable,terminal,chain.terminal_end,chain.terminal_end - h.armed_end,wal.end_lsn,mf_status(h.append_status),h.append_end,
        t.generation);
    CHECK(h.hits == 1 && h.seen_valid > h.armed_end && h.seen_durable <= h.armed_end);
    CHECK(ti.group_end_lsn <= h.armed_end);
    if (effect == EFFECT_APPEND) { EXPECT(h.append_status,CANDIDATE_WAL_OK); CHECK(h.append_end > h.seen_valid); }
    CHECK(status != CANDIDATE_WAL_OK);
    CHECK(t.generation == 0);
    if (is_commit) expect_unknown_body(tag,&response);
    expect_owner_fenced(m,value);
    printf("TERMINAL-CLOSE status=%s\n",mf_status(candidate_mvcc_close(&m)));
    CHECK(m == NULL);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    expect_resolved(&f,m,nonce,value,&ti,terminal == 10);
    CHECK(is_commit || terminal == 11);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_commit_reject(void) { case_terminal("commit-reject",EFFECT_REJECT,true,0x7801); }
static void case_commit_append(void) { case_terminal("commit-append",EFFECT_APPEND,true,0x7802); }
static void case_abort_reject(void) { case_terminal("abort-reject",EFFECT_REJECT,false,0x7803); }
static void case_abort_append(void) { case_terminal("abort-append",EFFECT_APPEND,false,0x7804); }

static void case_unknown_body(void)
{
    mf_t f; mf_boot_boundary(&f,"unknown-body",NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t value[MF_PAYLOAD]; mf_fill(0xe5,value);
    candidate_mvcc_txn_t t; EXPECT(begin_row(m,0x7901,ROW_A,value,&t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    candidate_mvcc_txn_info_t ti; EXPECT(candidate_mvcc_inspect(m,&t,&ti),CANDIDATE_WAL_OK);
    candidate_mvcc_response_t response; memset(&response,0xa5,sizeof(response));
    mf_arm_fsync(1);
    candidate_wal_status_t status = candidate_mvcc_commit(m,&t,&response);
    mf_disarm();
    printf("UNKNOWN-COMMIT status=%s id=%" PRIu64 " handle=%" PRIu64 "\n",mf_status(status),ti.transaction_id,t.generation);
    EXPECT(status,CANDIDATE_WAL_IO);
    CHECK(t.generation == 0);
    expect_unknown_body("unknown-body",&response);
    CHECK(candidate_mvcc_info(m).fence == CANDIDATE_WAL_IO);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_answer_t a = query_row(m,0x7901,ROW_A,value);
    printf("UNKNOWN-QUERY lookup=%d state=%d id=%" PRIu64 " size=%u\n",(int)a.lookup,(int)a.response.state,a.response.transaction_id,
        a.response.size);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.transaction_id == ti.transaction_id);
    CHECK(a.response.state == CANDIDATE_MVCC_COMMITTED || a.response.state == CANDIDATE_MVCC_ABORTED);
    candidate_mvcc_value_t v = read_row(m,ROW_A);
    expect_bytes(&v,a.response.state == CANDIDATE_MVCC_COMMITTED ? value : f.initial[0]);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

typedef struct {
    candidate_wal_t *wal;
    unsigned hits, skipped;
    candidate_wal_record_t seen;
} write_hook_t;

static bool on_begin_write(void *context, const void *bytes, size_t size)
{
    write_hook_t *h = context;
    const uint8_t *p = bytes;
    candidate_wal_record_t r;
    bool is_frame = size >= 96 && (p[8] | (size_t)p[9] << 8 | (size_t)p[10] << 16 | (size_t)p[11] << 24) == size &&
        candidate_wal_decode(fx_uuid,p,size,&r) == CANDIDATE_WAL_OK;
    if (!is_frame || r.type != 2) { h->skipped++; return false; }
    h->hits++; h->seen = r; h->seen.payload = NULL;
    candidate_wal_reject(h->wal);
    return true;
}

static void case_begin_reject(void)
{
    mf_t f; mf_boot_boundary(&f,"begin-reject",NULL,NULL);
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    uint8_t value[MF_PAYLOAD]; mf_fill(0xe6,value);
    write_hook_t h = {.wal = m->recovery->wal};
    mf_arm_pwrite_hook(on_begin_write,&h);
    candidate_mvcc_txn_t t;
    candidate_wal_status_t status = begin_row(m,0x7a01,ROW_A,value,&t);
    mf_arm_pwrite_hook(NULL,NULL);
    candidate_mvcc_info_t info = candidate_mvcc_info(m);
    printf("BEGIN-REJECT status=%s handle=%" PRIu64 "/%" PRIu64 "/%u hits=%u skipped=%u begin=%u@%" PRIu64 " id=%" PRIu64 " owner=%" PRIu64
        " previous=%" PRIu64 " fence=%s pool=%d active=%u\n",mf_status(status),t.owner_id,t.generation,t.slot,h.hits,h.skipped,
        h.seen.type,h.seen.lsn,h.seen.transaction_id,h.seen.owner_epoch,h.seen.previous_lsn,mf_status(info.fence),
        candidate_buffer_info(m->pool).is_fenced,info.active);
    CHECK(h.hits == 1 && h.seen.type == 2 && h.seen.transaction_id != 0 && h.seen.previous_lsn != 0);
    CHECK(status != CANDIDATE_WAL_OK);
    CHECK(t.owner_id == 0 && t.generation == 0 && t.slot == 0);
    CHECK(candidate_buffer_info(m->pool).is_fenced && info.active == 0);
    expect_owner_fenced(m,value);
    printf("BEGIN-REJECT-CLOSE status=%s\n",mf_status(candidate_mvcc_close(&m)));
    CHECK(m == NULL);
    recover_full(&f);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_mvcc_answer_t a = query_row(m,0x7a01,ROW_A,value);
    printf("BEGIN-REJECT-RESOLVED lookup=%d state=%d id=%" PRIu64 "\n",(int)a.lookup,(int)a.response.state,a.response.transaction_id);
    CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.response.state == CANDIDATE_MVCC_ABORTED);
    CHECK(a.response.transaction_id == h.seen.transaction_id);
    candidate_mvcc_value_t v = read_row(m,ROW_A); expect_bytes(&v,f.initial[0]);
    v = read_row(m,ROW_B); expect_bytes(&v,f.initial[1]);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

typedef struct { const char *name; void (*run)(void); } selector_t;

static const selector_t SELECTORS[] = {
    {"id-control",case_id_control},{"id-exhaust",case_id_exhaust},{"group-credit",case_group_credit},
    {"reserve-control",case_reserve_control},{"reserve-reject",case_reserve_reject},{"reserve-append",case_reserve_append},
    {"commit-reject",case_commit_reject},{"commit-append",case_commit_append},{"abort-reject",case_abort_reject},
    {"abort-append",case_abort_append},{"unknown-body",case_unknown_body},{"begin-reject",case_begin_reject}};

int main(int argc, char **argv)
{
    alarm(600);
    setvbuf(stdout,NULL,_IOLBF,0);
    if (argc != 2) { fprintf(stderr,"usage: %s <selector>\n",argv[0]); return 2; }
    for (unsigned i = 0; i < sizeof(SELECTORS) / sizeof(SELECTORS[0]); i++) {
        if (strcmp(argv[1],SELECTORS[i].name) != 0) continue;
        SELECTORS[i].run();
        printf("PASS %s checks=%u\n",argv[1],checks);
        return 0;
    }
    fprintf(stderr,"unknown selector %s\n",argv[1]);
    return 2;
}
