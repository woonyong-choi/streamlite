#include "test_candidate_mvcc_store.h"

#include <string.h>

#include "storage/checksum.h"

#define SET_PAGES 12u
#define BODY 64u
#define ITEMS (BODY + 16u)

enum { PAGE_D = 1, PAGE_B, PAGE_L1, PAGE_L2, PAGE_A, PAGE_V, PAGE_R, PAGE_C, PAGE_X, PAGE_Y, PAGE_ABSENT };

typedef struct {
    uint8_t pages[SET_PAGES + 1][CANDIDATE_PAGE_BYTES];
    bool has[SET_PAGES + 1];
} image_set_t;

static candidate_store_node_t scratch_node, decoded_node;
static image_set_t set;

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

static void reseal(uint8_t *page)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,page,52); crc = crc32c(crc,zero,4); crc = crc32c(crc,page + 56,CANDIDATE_PAGE_BYTES - 56);
    write_le(page + 52,crc,4);
}

static candidate_page_location_t at(uint64_t id, uint64_t generation)
{
    candidate_page_location_t location = {.ref = {id,generation},.lsn = 64};
    memcpy(location.uuid,fx_uuid,16);
    return location;
}

static candidate_store_node_t *leaf_node(void)
{
    memset(&scratch_node,0,sizeof(scratch_node));
    scratch_node.kind = CANDIDATE_STORE_LEAF;
    return &scratch_node;
}

static void add_entry(candidate_store_node_t *n, uint64_t row, candidate_version_ref_t head, uint64_t transaction_id)
{
    n->entries[n->count++] = (candidate_store_entry_t){.key = {7,row},.head = head,.head_transaction = transaction_id};
}

static candidate_store_node_t *branch_node(uint32_t level)
{
    memset(&scratch_node,0,sizeof(scratch_node));
    scratch_node.kind = CANDIDATE_STORE_BRANCH; scratch_node.level = level;
    return &scratch_node;
}

static void add_child(candidate_store_node_t *n, uint64_t row, uint64_t id, uint64_t generation)
{
    n->children[n->count++] = (candidate_store_child_t){.low = {row == 0 ? 0 : 7,row},.child = {id,generation}};
}

static candidate_store_node_t *descriptor_node(uint64_t root, uint64_t append, uint32_t height)
{
    memset(&scratch_node,0,sizeof(scratch_node));
    scratch_node.kind = CANDIDATE_STORE_DESCRIPTOR;
    scratch_node.descriptor = (candidate_store_descriptor_t){.root = {root,1},.append = {append,1},.height = height,.result_table_id = 900};
    return &scratch_node;
}

static void put(uint64_t id, const candidate_store_node_t *n)
{
    candidate_page_location_t location = at(id,1);
    OK(candidate_store_encode(&location,n,set.pages[id],CANDIDATE_PAGE_BYTES));
    set.has[id] = true;
}

static candidate_version_ref_t vref(uint32_t slot) { return (candidate_version_ref_t){{PAGE_V,1},slot,1}; }

static void put_versions(candidate_version_ref_t first_previous)
{
    static const uint8_t payload[4] = {1,2,3,4};
    candidate_page_version_t v[3] = {
        {.transaction_id = 10,.previous = first_previous,.table_id = 7,.row_id = 50,.payload_bytes = 4,.slot_generation = 1,.inline_bytes = payload},
        {.transaction_id = 11,.table_id = 7,.row_id = 150,.payload_bytes = 4,.slot_generation = 1,.inline_bytes = payload},
        {.transaction_id = 12,.previous = vref(0),.table_id = 7,.row_id = 50,.payload_bytes = 4,.slot_generation = 1,.inline_bytes = payload}};
    candidate_page_location_t location = at(PAGE_V,1);
    OK(candidate_page_encode_versions(&location,v,3,set.pages[PAGE_V],CANDIDATE_PAGE_BYTES));
    set.has[PAGE_V] = true;
}

static void build_valid(void)
{
    memset(&set,0,sizeof(set));
    put(PAGE_D,descriptor_node(PAGE_B,PAGE_A,2));
    candidate_store_node_t *b = branch_node(1);
    add_child(b,0,PAGE_L1,1); add_child(b,100,PAGE_L2,1); put(PAGE_B,b);
    candidate_store_node_t *l1 = leaf_node(); add_entry(l1,50,vref(2),12); put(PAGE_L1,l1);
    candidate_store_node_t *l2 = leaf_node(); add_entry(l2,150,vref(1),11); put(PAGE_L2,l2);
    candidate_page_location_t location = at(PAGE_A,1);
    OK(candidate_page_encode_versions(&location,NULL,0,set.pages[PAGE_A],CANDIDATE_PAGE_BYTES)); set.has[PAGE_A] = true;
    put_versions((candidate_version_ref_t){0});
}

static candidate_wal_status_t load_set(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    image_set_t *s = context;
    if (ref.id == 0 || ref.id > SET_PAGES || !s->has[ref.id]) return CANDIDATE_WAL_CORRUPT;
    memcpy(out,s->pages[ref.id],CANDIDATE_PAGE_BYTES);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_set(uint64_t descriptor, candidate_store_info_t *info)
{
    candidate_store_check_t request = {.descriptor = {descriptor,1},.load = load_set,.context = &set};
    memcpy(request.uuid,fx_uuid,16);
    return candidate_store_check(&request,info);
}

static void expect_graph(const char *label, candidate_wal_status_t expected)
{
    candidate_store_info_t info;
    candidate_wal_status_t status = check_set(PAGE_D,&info);
    printf("GRAPH %s status=%s\n",label,mf_status(status));
    EXPECT(status,expected);
}

static void check_valid_graph(void)
{
    build_valid();
    candidate_store_info_t info;
    EXPECT(check_set(PAGE_D,&info),CANDIDATE_WAL_OK);
    CHECK(info.pages == 4 && info.leaves == 2 && info.branches == 1 && info.entries == 2 && info.heads == 2 && info.versions == 3);
    CHECK(info.descriptor.height == 2 && info.descriptor.root.id == PAGE_B && info.descriptor.append.id == PAGE_A);
    EXPECT(check_set(PAGE_B,&info),CANDIDATE_WAL_CORRUPT);
}

static void check_bound_violations(void)
{
    build_valid();
    candidate_store_node_t *l2 = leaf_node(); add_entry(l2,90,vref(1),11); put(PAGE_L2,l2);
    expect_graph("separator-below-child",CANDIDATE_WAL_CORRUPT);
    build_valid();
    candidate_store_node_t *l1 = leaf_node(); add_entry(l1,50,vref(1),11); put(PAGE_L1,l1);
    expect_graph("identity-key-mismatch",CANDIDATE_WAL_CORRUPT);
    build_valid();
    l1 = leaf_node(); add_entry(l1,50,vref(2),99); put(PAGE_L1,l1);
    expect_graph("head-transaction-mismatch",CANDIDATE_WAL_CORRUPT);
    build_valid();
    candidate_store_node_t *b = branch_node(1); add_child(b,0,PAGE_L1,2); add_child(b,100,PAGE_L2,1); put(PAGE_B,b);
    expect_graph("child-generation-mismatch",CANDIDATE_WAL_CORRUPT);
    build_valid();
    put_versions(vref(2));
    expect_graph("version-order-forward",CANDIDATE_WAL_CORRUPT);
}

static void check_shape_violations(void)
{
    build_valid();
    candidate_store_node_t *b = branch_node(2); add_child(b,0,PAGE_L1,1); add_child(b,100,PAGE_L2,1); put(PAGE_B,b);
    expect_graph("branch-level-mismatch",CANDIDATE_WAL_CORRUPT);
    build_valid();
    put(PAGE_L2,leaf_node());
    expect_graph("empty-nonroot-leaf",CANDIDATE_WAL_CORRUPT);
    build_valid();
    put(PAGE_D,descriptor_node(PAGE_B,PAGE_L1,2));
    expect_graph("append-not-version-page",CANDIDATE_WAL_CORRUPT);
    build_valid();
    b = branch_node(1); add_child(b,0,PAGE_L1,1); add_child(b,100,PAGE_ABSENT,1); put(PAGE_B,b);
    expect_graph("missing-child",CANDIDATE_WAL_CORRUPT);
    build_valid();
    put(PAGE_D,descriptor_node(PAGE_B,PAGE_A,1));
    expect_graph("descriptor-height-mismatch",CANDIDATE_WAL_CORRUPT);
}

static void check_cycle(void)
{
    build_valid();
    put(PAGE_D,descriptor_node(PAGE_R,PAGE_A,3));
    candidate_store_node_t *r = branch_node(2); add_child(r,0,PAGE_B,1); add_child(r,120,PAGE_C,1); put(PAGE_R,r);
    candidate_store_node_t *c = branch_node(1); add_child(c,0,PAGE_R,1); add_child(c,130,PAGE_X,1); put(PAGE_C,c);
    candidate_store_node_t *x = leaf_node(); add_entry(x,140,(candidate_version_ref_t){0},0); put(PAGE_X,x);
    expect_graph("back-edge-cycle",CANDIDATE_WAL_CORRUPT);
    c = branch_node(1); add_child(c,0,PAGE_X,1); add_child(c,130,PAGE_L2,1); put(PAGE_C,c);
    x = leaf_node(); add_entry(x,125,(candidate_version_ref_t){0},0); put(PAGE_X,x);
    expect_graph("shared-child-across-branches",CANDIDATE_WAL_CORRUPT);
    c = branch_node(1); add_child(c,0,PAGE_X,1); add_child(c,130,PAGE_Y,1); put(PAGE_C,c);
    candidate_store_node_t *y = leaf_node(); add_entry(y,135,(candidate_version_ref_t){0},0); put(PAGE_Y,y);
    candidate_store_node_t *l2 = leaf_node(); add_entry(l2,110,(candidate_version_ref_t){0},0); put(PAGE_L2,l2);
    candidate_store_info_t info;
    EXPECT(check_set(PAGE_D,&info),CANDIDATE_WAL_OK);
    CHECK(info.pages == 8 && info.branches == 3 && info.leaves == 4 && info.heads == 1 && info.entries == 4 && info.versions == 2);
}

void case_store_graph(void)
{
    check_valid_graph();
    check_bound_violations();
    check_shape_violations();
    check_cycle();
    printf("STORE-GRAPH validators exercised\n");
}

static void expect_encode(const char *label, const candidate_store_node_t *n, uint64_t id, candidate_wal_status_t expected)
{
    uint8_t page[CANDIDATE_PAGE_BYTES];
    candidate_page_location_t location = at(id,1);
    candidate_wal_status_t status = candidate_store_encode(&location,n,page,sizeof(page));
    printf("ENCODE %s status=%s\n",label,mf_status(status));
    EXPECT(status,expected);
}

static void check_encode_refusals(void)
{
    candidate_store_node_t *n = leaf_node(); add_entry(n,9,(candidate_version_ref_t){0},0); add_entry(n,8,(candidate_version_ref_t){0},0);
    expect_encode("leaf-unsorted",n,3,CANDIDATE_WAL_INVALID);
    n = leaf_node(); add_entry(n,0,(candidate_version_ref_t){0},0); expect_encode("leaf-zero-key",n,3,CANDIDATE_WAL_INVALID);
    n = leaf_node(); add_entry(n,4,vref(0),0); expect_encode("leaf-head-without-transaction",n,3,CANDIDATE_WAL_INVALID);
    n = leaf_node(); add_entry(n,4,(candidate_version_ref_t){0},5); expect_encode("leaf-null-head-transaction",n,3,CANDIDATE_WAL_INVALID);
    n = leaf_node(); n->count = CANDIDATE_STORE_LEAF_ENTRIES + 1; expect_encode("leaf-over-capacity",n,3,CANDIDATE_WAL_LIMIT);
    n = branch_node(1); add_child(n,5,4,1); add_child(n,9,5,1); expect_encode("branch-first-low",n,3,CANDIDATE_WAL_INVALID);
    n = branch_node(1); add_child(n,0,4,1); add_child(n,9,4,1); expect_encode("branch-duplicate-child",n,3,CANDIDATE_WAL_INVALID);
    n = branch_node(1); add_child(n,0,3,1); add_child(n,9,4,1); expect_encode("branch-self-child",n,3,CANDIDATE_WAL_INVALID);
    n = branch_node(0); add_child(n,0,4,1); add_child(n,9,5,1); expect_encode("branch-level-zero",n,3,CANDIDATE_WAL_INVALID);
    n = branch_node(1); add_child(n,0,4,1); expect_encode("branch-single-child",n,3,CANDIDATE_WAL_INVALID);
    n = branch_node(1); n->count = CANDIDATE_STORE_BRANCH_CHILDREN + 1; expect_encode("branch-over-capacity",n,3,CANDIDATE_WAL_LIMIT);
    expect_encode("descriptor-root-is-append",descriptor_node(4,4,1),3,CANDIDATE_WAL_INVALID);
    expect_encode("descriptor-height-zero",descriptor_node(4,5,0),3,CANDIDATE_WAL_INVALID);
    expect_encode("descriptor-height-over",descriptor_node(4,5,CANDIDATE_STORE_HEIGHT + 1),3,CANDIDATE_WAL_INVALID);
    expect_encode("descriptor-self-root",descriptor_node(3,5,1),3,CANDIDATE_WAL_INVALID);
}

static void encode_sample(uint8_t page[CANDIDATE_PAGE_BYTES])
{
    candidate_store_node_t *n = leaf_node();
    add_entry(n,4,vref(0),10); add_entry(n,6,(candidate_version_ref_t){0},0); add_entry(n,9,vref(2),12);
    candidate_page_location_t location = at(3,1);
    OK(candidate_store_encode(&location,n,page,CANDIDATE_PAGE_BYTES));
}

static void expect_decode(const char *label, const uint8_t *page, const uint8_t *uuid, candidate_wal_status_t expected)
{
    candidate_wal_status_t status = candidate_store_decode(uuid,page,CANDIDATE_PAGE_BYTES,&decoded_node);
    printf("DECODE %s status=%s\n",label,mf_status(status));
    EXPECT(status,expected);
}

static void check_decode_refusals(void)
{
    uint8_t page[CANDIDATE_PAGE_BYTES], other[16];
    encode_sample(page); expect_decode("valid",page,fx_uuid,CANDIDATE_WAL_OK);
    CHECK(decoded_node.kind == CANDIDATE_STORE_LEAF && decoded_node.count == 3 && decoded_node.entries[2].head.slot == 2);
    CHECK(decoded_node.entries[1].head.page.id == 0 && decoded_node.entries[2].head_transaction == 12);
    encode_sample(page); write_le(page + ITEMS + 8,9,8); write_le(page + ITEMS + 2 * 48 + 8,4,8); reseal(page);
    expect_decode("entries-swapped",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); write_le(page + ITEMS + 48 + 8,4,8); reseal(page);
    expect_decode("duplicate-key",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); write_le(page + BODY + 6,4,2); reseal(page); expect_decode("kind-unknown",page,fx_uuid,CANDIDATE_WAL_UNSUPPORTED);
    encode_sample(page); page[BODY] = 'X'; reseal(page); expect_decode("magic",page,fx_uuid,CANDIDATE_WAL_UNSUPPORTED);
    encode_sample(page); write_le(page + BODY + 4,2,2); reseal(page); expect_decode("version",page,fx_uuid,CANDIDATE_WAL_UNSUPPORTED);
    encode_sample(page); write_le(page + BODY + 8,4,4); reseal(page); expect_decode("count-body-mismatch",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); write_le(page + BODY + 12,1,4); reseal(page); expect_decode("leaf-level",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); write_le(page + ITEMS + 2 * 48 + 36,0,4); reseal(page);
    expect_decode("head-slot-generation",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); write_le(page + 22,1,2); reseal(page); expect_decode("page-type",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); page[ITEMS + 3] ^= 1; expect_decode("crc",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    encode_sample(page); memcpy(other,fx_uuid,16); other[0] ^= 0xff; expect_decode("uuid",page,other,CANDIDATE_WAL_UNSUPPORTED);
    candidate_page_location_t location = at(3,1);
    OK(candidate_store_encode(&location,descriptor_node(4,5,1),page,CANDIDATE_PAGE_BYTES));
    write_le(page + BODY + 12,1,4); reseal(page); expect_decode("descriptor-reserved",page,fx_uuid,CANDIDATE_WAL_UNSUPPORTED);
    OK(candidate_store_encode(&location,descriptor_node(4,5,1),page,CANDIDATE_PAGE_BYTES));
    write_le(page + BODY + 40,0,8); reseal(page); expect_decode("descriptor-append-generation",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    candidate_store_node_t *b = branch_node(1); add_child(b,0,4,1); add_child(b,9,5,1);
    OK(candidate_store_encode(&location,b,page,CANDIDATE_PAGE_BYTES));
    write_le(page + ITEMS + 32 + 16,4,8); reseal(page); expect_decode("branch-duplicate-child",page,fx_uuid,CANDIDATE_WAL_CORRUPT);
    CHECK(read_le(page + 22,2) == 3);
}

void case_store_codec(void)
{
    check_encode_refusals();
    check_decode_refusals();
    printf("STORE-CODEC refusals exercised\n");
}
