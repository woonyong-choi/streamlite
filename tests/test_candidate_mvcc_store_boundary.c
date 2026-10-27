#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_store.h"
#include "test_candidate_mvcc_fixture.h"
#include "../src/storage/candidate_buffer_internal.h"
#include "../src/storage/candidate_mvcc_internal.h"

#define PENDING_ROW 2u

unsigned checks;

static candidate_store_node_t node, check_node;
static candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS];
static uint8_t source[CANDIDATE_PAGE_BYTES], forged[CANDIDATE_PAGE_BYTES];

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b) { return a.id == b.id && a.generation == b.generation; }

static bool same_version(candidate_version_ref_t a, candidate_version_ref_t b)
{
    return same_page(a.page,b.page) && a.slot == b.slot && a.slot_generation == b.slot_generation;
}

static candidate_mvcc_store_info_t store_of(candidate_mvcc_t *m)
{
    candidate_mvcc_store_info_t s; EXPECT(candidate_mvcc_store_info(m,&s),CANDIDATE_WAL_OK); return s;
}


static uint64_t copy_current(candidate_mvcc_t *m, candidate_page_ref_t ref, uint8_t page[CANDIDATE_PAGE_BYTES], uint16_t type)
{
    candidate_buffer_lease_t lease; uint64_t lsn = 0;
    EXPECT(candidate_buffer_pin(m->pool,ref,&lease),CANDIDATE_WAL_OK);
    EXPECT(candidate_buffer_copy(m->pool,&lease,page,&lsn),CANDIDATE_WAL_OK);
    EXPECT(candidate_buffer_unpin(m->pool,&lease),CANDIDATE_WAL_OK);
    candidate_page_info_t info; EXPECT(candidate_buffer_check_page(fx_uuid,page,&info),CANDIDATE_WAL_OK);
    CHECK(info.type == type && same_page(info.location.ref,ref) && info.location.lsn == lsn && lsn != 0);
    return lsn;
}

static uint64_t copy_node(candidate_mvcc_t *m, candidate_page_ref_t ref, candidate_store_node_t *out)
{
    uint64_t lsn = copy_current(m,ref,source,3);
    EXPECT(candidate_store_decode(fx_uuid,source,CANDIDATE_PAGE_BYTES,out),CANDIDATE_WAL_OK);
    return lsn;
}

static candidate_page_location_t location_of(candidate_page_ref_t ref, uint64_t lsn)
{
    candidate_page_location_t location = {.ref = ref,.lsn = lsn};
    memcpy(location.uuid,fx_uuid,16);
    return location;
}


static void expect_body(const uint8_t page[CANDIDATE_PAGE_BYTES], candidate_page_ref_t ref, uint64_t lsn, uint16_t type)
{
    candidate_page_info_t info; EXPECT(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info),CANDIDATE_WAL_OK);
    CHECK(info.type == type && same_page(info.location.ref,ref) && info.location.lsn == lsn);
    EXPECT(candidate_buffer_check_page(fx_uuid,page,&info),CANDIDATE_WAL_OK);
}

static void find_rows(const candidate_store_node_t *leaf, uint32_t *a, uint32_t *b)
{
    CHECK(leaf->kind == CANDIDATE_STORE_LEAF);
    CHECK(mvcc_leaf_find(leaf,mvcc_store_key(mf_key(0)),a) && mvcc_leaf_find(leaf,mvcc_store_key(mf_key(1)),b) && *a != *b);
}

static void expect_seed_leaf(const mf_t *f, const candidate_store_node_t *leaf)
{
    uint32_t a, b; find_rows(leaf,&a,&b);
    CHECK(same_version(leaf->entries[a].head,f->seed_versions[0]) && same_version(leaf->entries[b].head,f->seed_versions[1]));
    CHECK(leaf->entries[a].head_transaction == f->seed_transaction && leaf->entries[b].head_transaction == f->seed_transaction);
    CHECK(!same_version(leaf->entries[a].head,leaf->entries[b].head));
}

static candidate_page_ref_t seeded_root(const mf_t *f, candidate_mvcc_t *m)
{
    candidate_mvcc_store_info_t s = store_of(m);
    CHECK(s.descriptor.height == 1 && s.root_level == 0 && s.root_count == 2);
    copy_node(m,s.descriptor.root,&node);
    expect_seed_leaf(f,&node);
    return s.descriptor.root;
}


static void begin_pending(candidate_mvcc_t *m, uint64_t nonce, candidate_mvcc_txn_t *t, candidate_mvcc_txn_info_t *info)
{
    uint8_t bytes[MF_PAYLOAD]; mf_fill((uint8_t)nonce,bytes);
    candidate_mvcc_write_t w = mf_write(PENDING_ROW,bytes,MF_PAYLOAD);
    candidate_mvcc_request_t r = mf_request(nonce,&w,1);
    EXPECT(candidate_mvcc_begin(m,&r,t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_inspect(m,t,info),CANDIDATE_WAL_OK);
    CHECK(info->state == CANDIDATE_MVCC_ACTIVE && !info->is_applied && info->transaction_id != 0 && info->last_lsn != 0);
    printf("PENDING txn=%" PRIu64 " last_lsn=%" PRIu64 " records=%" PRIu64 "\n",info->transaction_id,info->last_lsn,info->records);
}


static void expect_refused(mf_t *f, candidate_mvcc_t **m, candidate_mvcc_txn_t *t, const candidate_mvcc_txn_info_t *info,
    const char *label, const candidate_buffer_page_t *page)
{
    CHECK(candidate_buffer_page((*m)->pool,page->ref.id) != NULL);
    CHECK(candidate_buffer_page((*m)->pool,page->ref.id)->generation == page->ref.generation);
    CHECK(!candidate_buffer_info((*m)->pool).is_fenced);
    candidate_buffer_group_t group = {info->transaction_id,info->last_lsn,page,1};
    candidate_buffer_admission_t receipt;
    candidate_wal_status_t status = candidate_buffer_write_group((*m)->pool,&group,&receipt);
    bool is_fenced = candidate_buffer_info((*m)->pool).is_fenced;
    printf("GROUP %s page=%" PRIu64 "/%" PRIu64 " status=%s group=%" PRIu64 " end=%" PRIu64 " fenced=%d\n",label,page->ref.id,
        page->ref.generation,mf_status(status),receipt.group_id,receipt.end_lsn,is_fenced);
    EXPECT(status,CANDIDATE_WAL_CORRUPT);
    CHECK(receipt.group_id == 0 && receipt.begin_lsn == 0 && receipt.end_lsn == 0 && receipt.pages == 0);
    CHECK(receipt.transaction_last_lsn == 0 && receipt.transaction_records == 0);
    CHECK(is_fenced);
    candidate_mvcc_value_t v;
    candidate_wal_status_t read_status = candidate_mvcc_read(*m,t,mf_key(0),&v);
    candidate_wal_status_t abort_status = candidate_mvcc_abort(*m,t);
    candidate_wal_status_t close_status = candidate_mvcc_close(m);
    printf("QUARANTINE %s read=%s abort=%s close=%s owner_released=%d\n",label,mf_status(read_status),mf_status(abort_status),
        mf_status(close_status),*m == NULL);
    if (*m == NULL) mf_free(f);
}

static void read_rows(candidate_mvcc_t *m, const uint8_t bytes[2][MF_PAYLOAD], uint64_t transaction_id)
{
    candidate_mvcc_reader_t r; EXPECT(candidate_mvcc_reader_open(m,&r),CANDIDATE_WAL_OK);
    for (unsigned row = 0; row < 2; row++) {
        candidate_mvcc_value_t v; EXPECT(candidate_mvcc_reader_read(m,&r,mf_key(row),&v),CANDIDATE_WAL_OK);
        CHECK(v.is_found && v.size == MF_PAYLOAD && memcmp(v.bytes,bytes[row],MF_PAYLOAD) == 0 && v.transaction_id == transaction_id);
    }
    EXPECT(candidate_mvcc_reader_close(m,&r),CANDIDATE_WAL_OK);
}

static void case_healthy_existing(void)
{
    mf_t f; mf_boot(&f,"store-boundary-healthy");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_page_ref_t root = seeded_root(&f,m);
    candidate_page_ref_t store = candidate_mvcc_info(m).store;
    read_rows(m,(const uint8_t (*)[MF_PAYLOAD])f.initial,f.seed_transaction);
    uint8_t bytes[2][MF_PAYLOAD]; mf_fill(0x61,bytes[0]); mf_fill(0x62,bytes[1]);
    candidate_mvcc_write_t w[2] = {mf_write(0,bytes[0],MF_PAYLOAD),mf_write(1,bytes[1],MF_PAYLOAD)};
    candidate_mvcc_request_t r = mf_request(0x4ea1,w,2);
    candidate_mvcc_txn_t t; EXPECT(candidate_mvcc_begin(m,&r,&t),CANDIDATE_WAL_OK);
    EXPECT(candidate_mvcc_apply(m,&t),CANDIDATE_WAL_OK);
    candidate_mvcc_txn_info_t info; EXPECT(candidate_mvcc_inspect(m,&t,&info),CANDIDATE_WAL_OK);
    CHECK(info.state == CANDIDATE_MVCC_ACTIVE && info.is_applied && info.write_count == 2);
    candidate_mvcc_response_t response; EXPECT(candidate_mvcc_commit(m,&t,&response),CANDIDATE_WAL_OK);
    CHECK(response.state == CANDIDATE_MVCC_COMMITTED && response.affected_rows == 2 && response.csn != 0 && t.generation == 0);
    read_rows(m,(const uint8_t (*)[MF_PAYLOAD])bytes,info.transaction_id);
    copy_node(m,root,&node);
    uint32_t a, b; find_rows(&node,&a,&b);
    CHECK(same_version(node.entries[a].head,info.versions[0]) && same_version(node.entries[b].head,info.versions[1]));
    CHECK(node.entries[a].head_transaction == info.transaction_id && node.entries[b].head_transaction == info.transaction_id);
    CHECK(!candidate_buffer_info(m->pool).is_fenced);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    CHECK(same_page(candidate_mvcc_info(m).store,store) && same_page(store_of(m).descriptor.root,root));
    read_rows(m,(const uint8_t (*)[MF_PAYLOAD])bytes,info.transaction_id);
    candidate_mvcc_answer_t answer; EXPECT(candidate_mvcc_query(m,&r,&answer),CANDIDATE_WAL_OK);
    CHECK(answer.lookup == CANDIDATE_CATALOG_MATCH && answer.response.state == CANDIDATE_MVCC_COMMITTED);
    CHECK(answer.response.transaction_id == info.transaction_id && answer.response.csn == response.csn);
    CHECK(answer.response.size == response.size && memcmp(answer.response.bytes,response.bytes,response.size) == 0);
    CHECK(answer.response.affected_rows == 2);
    printf("HEALTHY txn=%" PRIu64 " csn=%" PRIu64 " root=%" PRIu64 " store=%" PRIu64 "\n",info.transaction_id,response.csn,root.id,
        store.id);
    EXPECT(candidate_mvcc_close(&m),CANDIDATE_WAL_OK);
    mf_free(&f);
}

static void case_leaf_head_identity(void)
{
    mf_t f; mf_boot(&f,"store-boundary-leaf-head");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_page_ref_t root = seeded_root(&f,m);
    candidate_mvcc_txn_t t; candidate_mvcc_txn_info_t info; begin_pending(m,0x1ea1,&t,&info);
    uint64_t lsn = copy_node(m,root,&node);
    uint32_t a, b; find_rows(&node,&a,&b);
    candidate_store_key_t key_a = node.entries[a].key;
    node.entries[a].head = node.entries[b].head; node.entries[a].head_transaction = node.entries[b].head_transaction;
    CHECK(candidate_store_compare(node.entries[a].key,key_a) == 0 && node.entries[a].key.row_id == mf_key(0).row_id);
    candidate_page_location_t location = location_of(root,lsn);
    EXPECT(candidate_store_encode(&location,&node,forged,CANDIDATE_PAGE_BYTES),CANDIDATE_WAL_OK);
    expect_body(forged,root,lsn,3);
    EXPECT(candidate_store_decode(fx_uuid,forged,CANDIDATE_PAGE_BYTES,&check_node),CANDIDATE_WAL_OK);
    CHECK(check_node.kind == CANDIDATE_STORE_LEAF && check_node.count == node.count);
    CHECK(check_node.entries[a].key.row_id == mf_key(0).row_id && same_version(check_node.entries[a].head,f.seed_versions[1]));
    CHECK(check_node.entries[a].head_transaction == f.seed_transaction);
    candidate_buffer_page_t page = {root,forged};
    expect_refused(&f,&m,&t,&info,"leaf-head-identity",&page);
}

static void case_descriptor_child_generation(void)
{
    mf_t f; mf_boot(&f,"store-boundary-descriptor");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_page_ref_t root = seeded_root(&f,m);
    candidate_page_ref_t descriptor = candidate_mvcc_info(m).store;
    candidate_mvcc_txn_t t; candidate_mvcc_txn_info_t info; begin_pending(m,0xdea1,&t,&info);
    uint64_t lsn = copy_node(m,descriptor,&node);
    CHECK(node.kind == CANDIDATE_STORE_DESCRIPTOR && same_page(node.descriptor.root,root));
    CHECK(root.generation != 0 && root.generation < UINT64_MAX);
    node.descriptor.root.generation = root.generation + 1;
    candidate_page_location_t location = location_of(descriptor,lsn);
    EXPECT(candidate_store_encode(&location,&node,forged,CANDIDATE_PAGE_BYTES),CANDIDATE_WAL_OK);
    expect_body(forged,descriptor,lsn,3);
    EXPECT(candidate_store_decode(fx_uuid,forged,CANDIDATE_PAGE_BYTES,&check_node),CANDIDATE_WAL_OK);
    CHECK(check_node.kind == CANDIDATE_STORE_DESCRIPTOR && check_node.descriptor.root.id == root.id);
    CHECK(check_node.descriptor.root.generation == root.generation + 1);
    candidate_buffer_page_t page = {descriptor,forged};
    expect_refused(&f,&m,&t,&info,"descriptor-child-generation",&page);
}


static void expect_preserved(const candidate_page_info_t *before, uint32_t forged_slot)
{
    candidate_page_info_t after; EXPECT(candidate_page_decode(fx_uuid,forged,CANDIDATE_PAGE_BYTES,&after),CANDIDATE_WAL_OK);
    CHECK(after.slots == before->slots && after.body_bytes == before->body_bytes);
    for (uint32_t i = 0; i < before->slots; i++) {
        candidate_version_ref_t old_ref, new_ref; candidate_page_version_t old_v, new_v;
        EXPECT(candidate_page_version_at(fx_uuid,source,CANDIDATE_PAGE_BYTES,i,&old_ref,&old_v),CANDIDATE_WAL_OK);
        EXPECT(candidate_page_version_at(fx_uuid,forged,CANDIDATE_PAGE_BYTES,i,&new_ref,&new_v),CANDIDATE_WAL_OK);
        CHECK(same_version(old_ref,new_ref) && old_v.transaction_id == new_v.transaction_id && old_v.frozen_csn == new_v.frozen_csn);
        CHECK(same_version(old_v.previous,new_v.previous) && old_v.payload_bytes == new_v.payload_bytes);
        CHECK(old_v.slot_generation == new_v.slot_generation && old_v.is_tombstone == new_v.is_tombstone);
        CHECK(same_page(old_v.overflow,new_v.overflow) && (old_v.inline_bytes == NULL) == (new_v.inline_bytes == NULL));
        if (old_v.inline_bytes != NULL) CHECK(memcmp(old_v.inline_bytes,new_v.inline_bytes,(size_t)old_v.payload_bytes) == 0);
        candidate_mvcc_key_t want = i == forged_slot ? mf_key(1) : (candidate_mvcc_key_t){old_v.table_id,old_v.row_id};
        CHECK(new_v.table_id == want.table_id && new_v.row_id == want.row_id);
    }
}

static void case_version_row_identity(void)
{
    mf_t f; mf_boot(&f,"store-boundary-version");
    candidate_mvcc_t *m = NULL; EXPECT(mf_open(&f,&m),CANDIDATE_WAL_OK);
    candidate_page_ref_t root = seeded_root(&f,m);
    candidate_mvcc_txn_t t; candidate_mvcc_txn_info_t info; begin_pending(m,0x7ea1,&t,&info);
    copy_node(m,root,&node);
    uint32_t a, b; find_rows(&node,&a,&b);
    candidate_version_ref_t head = node.entries[a].head;
    CHECK(same_version(head,f.seed_versions[0]));
    uint64_t lsn = copy_current(m,head.page,source,1);
    candidate_page_info_t before; EXPECT(candidate_page_decode(fx_uuid,source,CANDIDATE_PAGE_BYTES,&before),CANDIDATE_WAL_OK);
    CHECK(before.slots > 0 && before.slots <= CANDIDATE_PAGE_MAX_SLOTS);
    uint32_t forged_slot = UINT32_MAX;
    for (uint32_t i = 0; i < before.slots; i++) {
        candidate_version_ref_t ref;
        EXPECT(candidate_page_version_at(fx_uuid,source,CANDIDATE_PAGE_BYTES,i,&ref,&versions[i]),CANDIDATE_WAL_OK);
        CHECK(same_page(ref.page,head.page));
        if (same_version(ref,head)) forged_slot = i;
    }
    CHECK(forged_slot != UINT32_MAX);
    candidate_page_version_t *v = &versions[forged_slot];
    CHECK(v->table_id == mf_key(0).table_id && v->row_id == mf_key(0).row_id && v->transaction_id == f.seed_transaction);
    v->table_id = mf_key(1).table_id; v->row_id = mf_key(1).row_id;
    candidate_page_location_t location = location_of(head.page,lsn);
    EXPECT(candidate_page_encode_versions(&location,versions,before.slots,forged,CANDIDATE_PAGE_BYTES),CANDIDATE_WAL_OK);
    expect_body(forged,head.page,lsn,1);
    expect_preserved(&before,forged_slot);
    printf("VERSION page=%" PRIu64 "/%" PRIu64 " slots=%u forged_slot=%u row=%" PRIu64 "->%" PRIu64 "\n",head.page.id,
        head.page.generation,before.slots,forged_slot,mf_key(0).row_id,mf_key(1).row_id);
    candidate_buffer_page_t page = {head.page,forged};
    expect_refused(&f,&m,&t,&info,"version-row-identity",&page);
}

typedef struct { const char *name; void (*run)(void); } selector_t;

static const selector_t SELECTORS[] = {
    {"healthy-existing",case_healthy_existing},{"leaf-head-identity",case_leaf_head_identity},
    {"descriptor-child-generation",case_descriptor_child_generation},{"version-row-identity",case_version_row_identity}};

int main(int argc, char **argv)
{
    alarm(900);
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
