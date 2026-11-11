#include "test_candidate_buffer_fixture.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "storage/checksum.h"

static uint32_t slot_generation(const uint8_t *page, uint32_t slot)
{
    const uint8_t *p = page + 76 + slot * 8;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool find_version(const uint8_t *page, uint64_t transaction, candidate_page_version_t *out)
{
    candidate_page_info_t info;
    if (candidate_page_decode(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info) != CANDIDATE_WAL_OK || info.type != 1) return false;
    for (uint32_t i = 0; i < info.slots; i++) {
        candidate_version_ref_t ref = {info.location.ref,i,slot_generation(page,i)};
        if (candidate_page_get_version(fx_uuid,page,CANDIDATE_PAGE_BYTES,ref,out) != CANDIDATE_WAL_OK) return false;
        if (out->transaction_id == transaction) return true;
    }
    return false;
}

void bf_read_data(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES], bool *present)
{
    int fd = b->data_fd >= 0 ? b->data_fd : open(b->fx->data,O_RDONLY); CHECK(fd >= 0);
    memset(out,0,CANDIDATE_PAGE_BYTES);
    ssize_t n = pread(fd,out,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)); CHECK(n >= 0);
    *present = n == CANDIDATE_PAGE_BYTES;
    if (fd != b->data_fd) CHECK(close(fd) == 0);
}

void bf_copy(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_buffer_lease_t lease; uint64_t lsn;
    OK(candidate_buffer_pin(b->pool,bf_ref(b,id),&lease));
    OK(candidate_buffer_copy(b->pool,&lease,out,&lsn));
    OK(candidate_buffer_unpin(b->pool,&lease));
    CHECK(lsn == bf_lsn(out));
}

uint64_t bf_generation(bf_t *b, uint64_t id)
{
    uint8_t page[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(b,id,page,&present); CHECK(present);
    uint64_t g = 0; for (unsigned i = 0; i < 8; i++) g |= (uint64_t)page[32 + i] << (8 * i);
    return g;
}

candidate_page_ref_t bf_ref(bf_t *b, uint64_t id) { return (candidate_page_ref_t){id,bf_generation(b,id)}; }

static void sync_group(bf_t *b)
{
    uint64_t seen = candidate_buffer_info(b->pool).last_group_id;
    if (seen > b->fx->group_id) b->fx->group_id = seen;
}

static void add_version(const uint8_t *page, const candidate_page_version_t *added, uint8_t out[CANDIDATE_PAGE_BYTES]);

void bf_with_version(const uint8_t *page, uint64_t transaction, uint64_t seed, size_t size, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    bf_with_frozen(page,transaction,0,seed,size,out);
}

void bf_with_previous(const uint8_t *page, uint64_t transaction, uint64_t seed, candidate_version_ref_t previous,
    uint8_t out[CANDIDATE_PAGE_BYTES])
{
    static uint8_t body[48]; fx_payload(seed,sizeof(body),body);
    candidate_page_version_t v = {.transaction_id = transaction,.table_id = 1,.row_id = 1000 + seed,.payload_bytes = sizeof(body),
        .slot_generation = 1,.inline_bytes = body,.previous = previous};
    add_version(page,&v,out);
}

void bf_with_frozen(const uint8_t *page, uint64_t transaction, uint64_t frozen, uint64_t seed, size_t size,
    uint8_t out[CANDIDATE_PAGE_BYTES])
{
    CHECK(size <= 256);
    static uint8_t body[256]; fx_payload(seed,size,body);
    candidate_page_version_t v = {.transaction_id = transaction,.frozen_csn = frozen,.table_id = 1,.row_id = 1000 + seed,
        .payload_bytes = size,.slot_generation = 1,.inline_bytes = body};
    add_version(page,&v,out);
}

static void add_version(const uint8_t *page, const candidate_page_version_t *added, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_page_info_t info; OK(candidate_page_decode(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info));
    CHECK(info.type == 1 && info.slots < CANDIDATE_PAGE_MAX_SLOTS);
    candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS];
    for (uint32_t i = 0; i < info.slots; i++)
        OK(candidate_page_get_version(fx_uuid,page,CANDIDATE_PAGE_BYTES,(candidate_version_ref_t){info.location.ref,i,slot_generation(page,i)},&versions[i]));
    versions[info.slots] = *added;
    OK(candidate_page_encode_versions(&info.location,versions,info.slots + 1,out,CANDIDATE_PAGE_BYTES));
}

void bf_rewrite_version(const uint8_t *page, uint64_t transaction, uint64_t seed, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_page_version_t v;
    CHECK(find_version(page,transaction,&v) && v.inline_bytes != NULL);
    memcpy(out,page,CANDIDATE_PAGE_BYTES);
    fx_payload(seed,(size_t)v.payload_bytes,out + (v.inline_bytes - page));
}

bool bf_has_version(const uint8_t *page, uint64_t transaction)
{
    candidate_page_version_t v;
    return find_version(page,transaction,&v);
}

size_t bf_body_offset(const uint8_t *page, uint64_t transaction)
{
    candidate_page_version_t v;
    CHECK(find_version(page,transaction,&v) && v.inline_bytes != NULL);
    return (size_t)(v.inline_bytes - page);
}

bool bf_has_body(const uint8_t *page, uint64_t transaction, uint64_t seed, size_t size)
{
    candidate_page_version_t v; uint8_t body[256];
    if (!find_version(page,transaction,&v) || v.inline_bytes == NULL || v.payload_bytes != size || size > sizeof(body)) return false;
    fx_payload(seed,size,body);
    return memcmp(v.inline_bytes,body,size) == 0;
}

static void current_snapshot(bf_t *b)
{
    if (b->has_snapshot && b->changes == b->snapshot_changes) return;
    char label[32]; CHECK(snprintf(label,sizeof(label),"expect%u",++b->checks) > 0);
    bf_snapshot_all(b,label);
}

void bf_expect_body(bf_t *b, uint64_t id, uint64_t transaction, uint64_t seed, size_t size)
{
    current_snapshot(b);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(b,id,page,&present);
    CHECK(present && bf_has_body(page,transaction,seed,size));
    printf("EXPECT %s page=%llu txn=%llu seed=%llu size=%zu\n",b->tag,(unsigned long long)id,(unsigned long long)transaction,
        (unsigned long long)seed,size);
}

void bf_expect_absent(bf_t *b, uint64_t id, uint64_t transaction)
{
    current_snapshot(b);
    uint8_t page[CANDIDATE_PAGE_BYTES]; bool present;
    bf_read_data(b,id,page,&present);
    CHECK(present && !bf_has_version(page,transaction));
    printf("ABSENT %s page=%llu txn=%llu\n",b->tag,(unsigned long long)id,(unsigned long long)transaction);
}

void bf_set_lsn(uint8_t page[CANDIDATE_PAGE_BYTES], uint64_t lsn)
{
    for (unsigned i = 0; i < 8; i++) page[40 + i] = (uint8_t)(lsn >> (8 * i));
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,page,52); crc = crc32c(crc,zero,4); crc = crc32c(crc,page + 56,CANDIDATE_PAGE_BYTES - 56);
    for (unsigned i = 0; i < 4; i++) page[52 + i] = (uint8_t)(crc >> (8 * i));
}

uint64_t bf_lsn(const uint8_t *page)
{
    uint64_t lsn = 0; for (unsigned i = 0; i < 8; i++) lsn |= (uint64_t)page[40 + i] << (8 * i);
    return lsn;
}

void bf_begin(bf_t *b, fx_txn_t *t)
{
    memset(t,0,sizeof(*t));
    fx_begin(b->fx,t,b->next_transaction++);
}

candidate_wal_status_t bf_write(bf_t *b, fx_txn_t *t, const uint64_t *ids, const uint8_t *const *pages, unsigned count,
    candidate_buffer_admission_t *out)
{
    candidate_buffer_page_t list[CANDIDATE_BUFFER_GROUP_PAGES];
    for (unsigned i = 0; i < count; i++) list[i] = (candidate_buffer_page_t){bf_ref(b,ids[i]),pages[i]};
    candidate_buffer_group_t group = {.transaction_id = t->id,.previous_lsn = t->last,.pages = list,.count = count};
    candidate_wal_status_t status = candidate_buffer_write_group(b->pool,&group,out);
    if (status != CANDIDATE_WAL_OK) return status;
    t->last = out->transaction_last_lsn; t->records = out->transaction_records; t->crc = out->transaction_chain_crc;
    t->group_ids[t->groups] = out->group_id; t->group_ends[t->groups] = out->end_lsn; t->groups++;
    b->fx->group_id = out->group_id;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t bf_write_version(bf_t *b, fx_txn_t *t, uint64_t id, uint64_t seed, candidate_buffer_admission_t *out)
{
    uint8_t before[CANDIDATE_PAGE_BYTES], after[CANDIDATE_PAGE_BYTES];
    bf_copy(b,id,before); bf_with_version(before,t->id,seed,48,after);
    const uint8_t *pages[1] = {after};
    return bf_write(b,t,&id,pages,1,out);
}

candidate_wal_status_t bf_write_update(bf_t *b, fx_txn_t *t, uint64_t id, uint64_t seed, candidate_buffer_admission_t *out)
{
    uint8_t before[CANDIDATE_PAGE_BYTES], after[CANDIDATE_PAGE_BYTES];
    bf_copy(b,id,before); bf_rewrite_version(before,t->id,seed,after);
    const uint8_t *pages[1] = {after};
    return bf_write(b,t,&id,pages,1,out);
}

void bf_current(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_buffer_lease_t lease; uint64_t lsn; bool present;
    candidate_wal_status_t status = candidate_buffer_pin(b->pool,bf_ref(b,id),&lease);
    if (status == CANDIDATE_WAL_BUSY || status == CANDIDATE_WAL_LIMIT) { bf_read_data(b,id,out,&present); CHECK(present); return; }
    OK(status); OK(candidate_buffer_copy(b->pool,&lease,out,&lsn)); OK(candidate_buffer_unpin(b->pool,&lease));
}

uint64_t bf_external_pages(bf_t *b, fx_txn_t *t, const uint64_t *ids, uint8_t (*pages)[CANDIDATE_PAGE_BYTES], unsigned count,
    bool complete)
{
    static fx_image_t images[8];
    CHECK(count <= 8);
    sync_group(b);
    uint64_t end = fx_group_end(b->fx,count,0);
    for (unsigned i = 0; i < count; i++) {
        uint8_t before[CANDIDATE_PAGE_BYTES];
        bf_current(b,ids[i],before);
        memcpy(images[i].page,pages[i],CANDIDATE_PAGE_BYTES); bf_set_lsn(images[i].page,end);
        images[i].id = ids[i]; images[i].generation = bf_generation(b,ids[i]); images[i].previous = bf_lsn(before);
    }
    uint64_t result = fx_group(b->fx,t,images,count,NULL,0,complete,false);
    return complete ? result : end;
}

uint64_t bf_external(bf_t *b, fx_txn_t *t, const uint64_t *ids, unsigned count, uint64_t seed, bool complete)
{
    static uint8_t pages[8][CANDIDATE_PAGE_BYTES];
    CHECK(count <= 8);
    for (unsigned i = 0; i < count; i++) {
        uint8_t before[CANDIDATE_PAGE_BYTES];
        bf_current(b,ids[i],before); bf_with_version(before,t->id,seed + i,48,pages[i]);
    }
    return bf_external_pages(b,t,ids,pages,count,complete);
}

uint64_t bf_external_delta(bf_t *b, fx_txn_t *t, uint64_t id, uint32_t offset, const uint8_t *bytes, uint32_t size)
{
    uint8_t before[CANDIDATE_PAGE_BYTES]; bf_copy(b,id,before);
    CHECK(id < b->fx->page_slots);
    b->fx->pages[id].generation = bf_generation(b,id); b->fx->pages[id].lsn = bf_lsn(before);
    sync_group(b);
    return fx_delta_group(b->fx,t,id,offset,bytes,size);
}

void bf_commit(bf_t *b, fx_txn_t *t) { fx_commit(b->fx,t); }

void bf_abort(bf_t *b, fx_txn_t *t) { fx_abort(b->fx,t); }

static void root_image(bf_t *b, uint64_t id, fx_image_t *image)
{
    bf_copy(b,id,image->page);
    image->id = id; image->generation = bf_generation(b,id); image->previous = bf_lsn(image->page);
}

static void encode_catalog(fx_image_t *image, const candidate_catalog_root_t *root, uint64_t end)
{
    candidate_page_location_t l = fx_location(image->id,image->generation,end);
    OK(candidate_catalog_encode_root(&l,root,image->page,CANDIDATE_PAGE_BYTES));
}

static void encode_allocation(fx_image_t *image, const candidate_allocation_root_t *root, uint64_t end)
{
    candidate_page_location_t l = fx_location(image->id,image->generation,end);
    OK(candidate_allocation_encode_root(&l,root,image->page,CANDIDATE_PAGE_BYTES));
}

static uint64_t system_group(bf_t *b, fx_image_t *images, unsigned count, const fx_event_t *event)
{
    sync_group(b);
    uint64_t end = fx_group(b->fx,NULL,images,count,event,1,true,false);
    printf("SYSTEM type=%u end=%llu\n",event->type,(unsigned long long)end);
    return end;
}

uint64_t bf_reserve(bf_t *b, uint64_t transactions, uint64_t pages)
{
    candidate_control_state_t anchor = candidate_buffer_info(b->pool).anchor;
    static fx_image_t images[2];
    root_image(b,anchor.outcome_root.id,&images[0]); root_image(b,anchor.allocation_root.id,&images[1]);
    candidate_catalog_root_t catalog; candidate_allocation_root_t allocation;
    OK(candidate_catalog_decode_root(fx_uuid,images[0].page,CANDIDATE_PAGE_BYTES,&catalog));
    OK(candidate_allocation_decode_root(fx_uuid,images[1].page,CANDIDATE_PAGE_BYTES,&allocation));
    fx_event_t event = {16,catalog.transaction_highwater,catalog.transaction_highwater + transactions,
        allocation.page_highwater,allocation.page_highwater + pages};
    uint64_t end = fx_group_end(b->fx,2,1);
    catalog.transaction_highwater += transactions; catalog.page_highwater = allocation.page_highwater + pages;
    allocation.page_highwater += pages;
    encode_catalog(&images[0],&catalog,end); encode_allocation(&images[1],&allocation,end);
    return system_group(b,images,2,&event);
}

uint64_t bf_epoch(bf_t *b, uint64_t epoch, uint64_t floor)
{
    candidate_control_state_t anchor = candidate_buffer_info(b->pool).anchor;
    static fx_image_t image;
    root_image(b,anchor.outcome_root.id,&image);
    candidate_catalog_root_t catalog;
    OK(candidate_catalog_decode_root(fx_uuid,image.page,CANDIDATE_PAGE_BYTES,&catalog));
    fx_event_t event = {15,catalog.current_epoch,epoch,catalog.retention_floor,floor};
    uint64_t end = fx_group_end(b->fx,1,1);
    catalog.current_epoch = epoch; catalog.retention_floor = floor;
    encode_catalog(&image,&catalog,end);
    return system_group(b,&image,1,&event);
}

uint64_t bf_alloc(bf_t *b, uint64_t id)
{
    candidate_control_state_t anchor = candidate_buffer_info(b->pool).anchor;
    static fx_image_t images[3];
    root_image(b,anchor.allocation_root.id,&images[0]);
    candidate_allocation_root_t allocation;
    OK(candidate_allocation_decode_root(fx_uuid,images[0].page,CANDIDATE_PAGE_BYTES,&allocation));
    uint32_t ordinal = (allocation.count - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    CHECK(allocation.count % CANDIDATE_ALLOCATION_NODE_ENTRIES != 0 && id <= allocation.page_highwater);
    root_image(b,allocation.nodes[ordinal].id,&images[1]);
    candidate_allocation_node_t node;
    OK(candidate_allocation_decode_node(fx_uuid,images[1].page,CANDIDATE_PAGE_BYTES,&node));
    CHECK(node.entries[node.count - 1].ref.id < id);
    uint64_t end = fx_group_end(b->fx,3,1);
    node.entries[node.count++] = (candidate_allocation_entry_t){{id,1},CANDIDATE_ALLOCATION_LIVE,0,end};
    allocation.count++;
    encode_allocation(&images[0],&allocation,end);
    candidate_page_location_t l = fx_location(images[1].id,images[1].generation,end);
    OK(candidate_allocation_encode_node(&l,&node,images[1].page,CANDIDATE_PAGE_BYTES));
    images[2].id = id; images[2].generation = 1; images[2].previous = 0;
    l = fx_location(id,1,end);
    OK(candidate_page_encode_versions(&l,NULL,0,images[2].page,CANDIDATE_PAGE_BYTES));
    fx_event_t event = {6,id,0,1,0};
    return system_group(b,images,3,&event);
}

uint64_t bf_external_new(bf_t *b, fx_txn_t *t, uint64_t id)
{
    static fx_image_t image;
    sync_group(b);
    uint64_t end = fx_group_end(b->fx,1,0);
    image.id = id; image.generation = 1; image.previous = 0;
    candidate_page_location_t l = fx_location(id,1,end);
    OK(candidate_page_encode_versions(&l,NULL,0,image.page,CANDIDATE_PAGE_BYTES));
    return fx_group(b->fx,t,&image,1,NULL,0,true,false);
}

uint64_t bf_alloc_node_ref(bf_t *b)
{
    candidate_control_state_t anchor = candidate_buffer_info(b->pool).anchor;
    static fx_image_t images[2];
    root_image(b,anchor.outcome_root.id,&images[0]); root_image(b,anchor.allocation_root.id,&images[1]);
    candidate_catalog_root_t catalog; candidate_allocation_root_t allocation;
    OK(candidate_catalog_decode_root(fx_uuid,images[0].page,CANDIDATE_PAGE_BYTES,&catalog));
    OK(candidate_allocation_decode_root(fx_uuid,images[1].page,CANDIDATE_PAGE_BYTES,&allocation));
    fx_event_t event = {16,catalog.transaction_highwater,catalog.transaction_highwater + 1,allocation.page_highwater,
        allocation.page_highwater + 3};
    uint64_t end = fx_group_end(b->fx,2,1);
    allocation.nodes[0] = (candidate_page_ref_t){allocation.page_highwater + 1,1};
    catalog.transaction_highwater++; allocation.page_highwater += 3; catalog.page_highwater = allocation.page_highwater;
    encode_catalog(&images[0],&catalog,end); encode_allocation(&images[1],&allocation,end);
    printf("DANGLING allocation node=%llu\n",(unsigned long long)allocation.nodes[0].id);
    return system_group(b,images,2,&event);
}
