#include "test_candidate_recovery_closure_boundary_fixture.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_wal_witness.h"

#define BF_CHAINS 64u

const uint8_t bf_uuid_b[16] = {0xb2,0x6e,0x19,0xc4,0x07,0x3d,0x48,0xa1,0x95,0x2f,0x60,0xd8,0x1b,0x77,0xe3,0x5c};

static uint8_t bf_scratch[CANDIDATE_WAL_RECORD_BYTES];

typedef struct { uint64_t id; uint32_t crc; } bf_chain_t;
typedef struct {
    bf_db_t *b;
    bf_chain_t chains[BF_CHAINS];
    unsigned chain_count;
    uint32_t group_crc;
    uint8_t digest[32];
} bf_convert_t;

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero_page(const uint8_t *page)
{
    for (size_t i = 0; i < CANDIDATE_PAGE_BYTES; i++) if (page[i] != 0) return false;
    return true;
}

static void *bf_allocate(void *context, size_t size)
{
    bf_memory_t *m = context;
    m->calls++;
    void *p = malloc(size);
    if (p != NULL) m->live += size;
    return p;
}

static void bf_release(void *context, void *p, size_t size)
{
    bf_memory_t *m = context;
    CHECK(m->live >= size); m->live -= size; free(p);
}

static void reencode_page(const uint8_t uuid[16], const uint8_t *page, uint8_t *out)
{
    candidate_page_info_t info; OK(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,&info));
    candidate_page_location_t location = info.location; memcpy(location.uuid,uuid,16);
    OK(candidate_page_encode_body(&location,info.type,page + 64,info.body_bytes,out,CANDIDATE_PAGE_BYTES));
    CHECK(memcmp(out,uuid,16) == 0 && memcmp(out + 64,page + 64,CANDIDATE_PAGE_BYTES - 64) == 0);
    candidate_page_info_t check; OK(candidate_page_decode_header(uuid,out,CANDIDATE_PAGE_BYTES,&check));
    CHECK(check.type == info.type && check.location.ref.id == info.location.ref.id && check.location.lsn == info.location.lsn);
}

static bf_chain_t *chain_of(bf_convert_t *k, uint64_t id)
{
    for (unsigned i = 0; i < k->chain_count; i++) if (k->chains[i].id == id) return &k->chains[i];
    CHECK(k->chain_count < BF_CHAINS);
    k->chains[k->chain_count] = (bf_chain_t){id,0};
    return &k->chains[k->chain_count++];
}

static void rewrite_checkpoint(bf_convert_t *k, const candidate_wal_record_t *r)
{
    if (r->type == 12) {
        memcpy(bf_scratch + 8,k->b->uuid,16); memset(k->digest,0,sizeof(k->digest)); k->b->cloned.checkpoints++;
    }
    if (r->type == 14) { memcpy(bf_scratch + 32,k->digest,32); return; }
    OK(candidate_checkpoint_digest(k->digest,bf_scratch,r->payload_bytes,k->digest));
}

static void rewrite_payload(bf_convert_t *k, const candidate_wal_record_t *r)
{
    CHECK(r->payload_bytes <= sizeof(bf_scratch));
    memcpy(bf_scratch,r->payload,r->payload_bytes);
    if (r->type == 4) { reencode_page(k->b->uuid,r->payload + 32,bf_scratch + 32); k->b->cloned.images++; }
    if (r->type == 8) write_le(bf_scratch + 16,k->group_crc,4);
    if (r->type == 10 || r->type == 11) { write_le(bf_scratch + 24,chain_of(k,r->transaction_id)->crc,4); k->b->cloned.terminals++; }
    if (r->type >= 12 && r->type <= 14) rewrite_checkpoint(k,r);
}

static bool is_group_member(uint16_t type) { return (type >= 4 && type <= 7) || type == 15 || type == 16; }

static void fold_record(bf_convert_t *k, const candidate_wal_record_t *r)
{
    if (r->type == 3) k->group_crc = candidate_wal_witness_crc(0,r);
    else if (is_group_member(r->type)) k->group_crc = candidate_wal_witness_crc(k->group_crc,r);
    if (r->transaction_id == 0 || r->type >= 12) return;
    bf_chain_t *chain = chain_of(k,r->transaction_id);
    chain->crc = candidate_wal_witness_crc(chain->crc,r);
}

static bool convert_record(void *context, const candidate_wal_record_t *r)
{
    bf_convert_t *k = context;
    CHECK(r->owner_epoch == k->b->owner);
    rewrite_payload(k,r);
    candidate_wal_record_t next = *r; next.payload = bf_scratch;
    uint64_t end; OK(candidate_wal_append(k->b->wal,&next,&end));
    CHECK(end == r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7));
    fold_record(k,&next); k->b->cloned.records++;
    return true;
}

static void copy_pages(const cf_t *source, bf_db_t *b)
{
    static uint8_t page[CANDIDATE_PAGE_BYTES], out[CANDIDATE_PAGE_BYTES];
    uint64_t bytes = fx_file_bytes(source->f.built.fx->data);
    CHECK(bytes > 0 && bytes % CANDIDATE_PAGE_BYTES == 0);
    for (uint64_t at = 0; at < bytes; at += CANDIDATE_PAGE_BYTES) {
        CHECK(pread(source->f.data_fd,page,sizeof(page),(off_t)at) == (ssize_t)sizeof(page));
        if (is_zero_page(page)) memset(out,0,sizeof(out));
        else reencode_page(b->uuid,page,out);
        CHECK(pwrite(b->data_fd,out,sizeof(out),(off_t)at) == (ssize_t)sizeof(out));
        b->cloned.pages++;
    }
    CHECK(fsync(b->data_fd) == 0);
}

static void expect_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    CHECK(a->records == b->records && a->valid_end_lsn == b->valid_end_lsn && a->durable_end_lsn == b->durable_end_lsn);
    CHECK(a->segments == b->segments && a->highest_owner_epoch == b->highest_owner_epoch && !a->has_tail && !b->has_tail);
}

void bf_clone(const cf_t *source, bf_db_t *b)
{
    memset(b,0,sizeof(*b)); b->data_fd = -1; b->owner = source->f.owner;
    memcpy(b->uuid,bf_uuid_b,16); CHECK(memcmp(b->uuid,fx_uuid,16) != 0);
    CHECK(snprintf(b->prefix,sizeof(b->prefix),"%s-uuid-b",source->f.built.fx->wal) > 0);
    CHECK(snprintf(b->data,sizeof(b->data),"%s.pages",b->prefix) > 0);
    b->allocator = (candidate_wal_memory_t){&b->memory,bf_allocate,bf_release};
    b->anchor = source->f.anchor; memcpy(b->anchor.uuid,b->uuid,16);
    candidate_wal_options_t o = {.owner_epoch = b->owner}; memcpy(o.uuid,b->uuid,16);
    OK(candidate_wal_create(b->prefix,&o,&b->wal));
    b->data_fd = open(b->data,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(b->data_fd >= 0);
    static bf_convert_t k; memset(&k,0,sizeof(k)); k.b = b;
    candidate_wal_info_t scanned;
    OK(candidate_wal_scan(source->f.wal,convert_record,&k,&scanned));
    OK(candidate_wal_force(b->wal,candidate_wal_info(b->wal).valid_end_lsn));
    copy_pages(source,b);
    candidate_wal_info_t a = candidate_wal_info(source->f.wal), cloned = candidate_wal_info(b->wal);
    char ua[33], ub[33]; bf_format_uuid(fx_uuid,ua); bf_format_uuid(b->uuid,ub);
    printf("UUID_CLONE prefix=%s uuid_a=%s uuid_b=%s records=%llu/%llu end=%llu/%llu segments=%u/%u owner=%llu/%llu images=%llu "
        "terminals=%llu checkpoints=%llu pages=%llu\n",b->prefix,ua,ub,(unsigned long long)a.records,
        (unsigned long long)cloned.records,(unsigned long long)a.valid_end_lsn,(unsigned long long)cloned.valid_end_lsn,a.segments,
        cloned.segments,(unsigned long long)a.highest_owner_epoch,(unsigned long long)cloned.highest_owner_epoch,
        (unsigned long long)b->cloned.images,(unsigned long long)b->cloned.terminals,(unsigned long long)b->cloned.checkpoints,
        (unsigned long long)b->cloned.pages);
    expect_same_cut(&a,&cloned);
    CHECK(b->cloned.records == scanned.records && b->cloned.images > 0 && b->cloned.terminals > 0 && b->cloned.checkpoints == 1);
}

void bf_free(bf_db_t *b)
{
    if (b->wal != NULL) OK(candidate_wal_close(&b->wal));
    if (b->data_fd >= 0) { CHECK(close(b->data_fd) == 0); b->data_fd = -1; }
}

candidate_recovery_view_request_t bf_request(bf_db_t *b, const candidate_control_state_t *anchor,
    candidate_recovery_view_mode_t mode, uint64_t owner)
{
    candidate_recovery_view_request_t r = {.anchor = *anchor,.load = {.root = anchor->outcome_root,.load = bf_load,.context = b,
        .memory = &b->allocator},.mode = mode,.owner_epoch = owner};
    memcpy(r.load.uuid,b->uuid,16);
    return r;
}

candidate_wal_status_t bf_load(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    bf_db_t *b = context;
    CHECK(ref.id != 0);
    b->loads++;
    ssize_t n = pread(b->data_fd,out,CANDIDATE_PAGE_BYTES,(off_t)((ref.id - 1) * CANDIDATE_PAGE_BYTES));
    return n == (ssize_t)CANDIDATE_PAGE_BYTES ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

uint64_t bf_abort(bf_db_t *b, uint64_t id)
{
    static fx_txn_t t;
    fx_t fx = {.w = b->wal,.data_fd = -1,.owner = b->owner};
    memset(&t,0,sizeof(t));
    fx_begin(&fx,&t,id); fx_abort(&fx,&t);
    OK(candidate_wal_force(b->wal,candidate_wal_info(b->wal).valid_end_lsn));
    printf("UUID_B_ABORT transaction=%llu first=%llu last=%llu records=%llu terminal_end=%llu\n",(unsigned long long)t.id,
        (unsigned long long)t.first,(unsigned long long)t.last,(unsigned long long)t.records,(unsigned long long)t.terminal_end);
    return t.terminal_end;
}

void bf_format_uuid(const uint8_t uuid[16], char out[33])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; i++) { out[2 * i] = digits[uuid[i] >> 4]; out[2 * i + 1] = digits[uuid[i] & 15]; }
    out[32] = '\0';
}
