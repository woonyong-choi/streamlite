#define CANDIDATE_RECOVERY_TEST_MAIN recovery_snapshot_regression_main
#include "test_candidate_recovery.c"

typedef struct {
    fixture_t files;
    candidate_wal_t *wal;
    int fd;
    size_t bytes;
    unsigned chunks, body_reads;
    uint8_t digest[32];
} aborted_t;

static candidate_wal_record_t append_record(chain_t *c, uint64_t transaction,
    unsigned type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t r = {.type = (uint16_t)type, .lsn = candidate_wal_info(c->wal).valid_end_lsn,
        .transaction_id = transaction, .previous_lsn = c->last, .owner_epoch = 8,
        .payload = body, .payload_bytes = size};
    uint64_t end; OK(candidate_wal_append(c->wal,&r,&end));
    if (c->records == 0) c->first = r.lsn;
    c->last = r.lsn; c->records++; c->crc = candidate_wal_witness_crc(c->crc,&r);
    return r;
}

static candidate_wal_status_t read_aborted(void *context, candidate_page_ref_t ref, uint8_t out[8192])
{
    aborted_t *a = context;
    if (ref.id < a->files.anchor.outcome_root.id) a->body_reads++;
    return pread(a->fd,out,8192,(off_t)(ref.id - 1) * 8192) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static candidate_outcome_witness_options_t aborted_options(aborted_t *a)
{
    candidate_outcome_witness_options_t o = {.catalog = {.root = a->files.anchor.outcome_root,
        .load = read_aborted, .context = a}, .checkpoint = &a->files.anchor};
    memcpy(o.catalog.uuid,uuid,16); return o;
}

static void publish_outcome(aborted_t *a, bool retain, uint64_t terminal_end)
{
    uint64_t id = a->files.anchor.outcome_root.id;
    uint8_t images[2][8192];
    CHECK(pread(a->fd,images[0],8192,(off_t)(id - 1) * 8192) == 8192);
    CHECK(pread(a->fd,images[1],8192,(off_t)id * 8192) == 8192);
    candidate_catalog_root_t root; candidate_catalog_node_t node;
    OK(candidate_catalog_decode_root(uuid,images[0],8192,&root));
    OK(candidate_catalog_decode_node(uuid,images[1],8192,&node));
    chain_t c = {.wal = a->wal}; uint8_t begin[16] = {0}; append_record(&c,10,2,begin,16);
    uint64_t end = candidate_wal_info(a->wal).valid_end_lsn + 112 + 2 * 8320 + 120;
    root.owner_epoch = 8;
    candidate_page_location_t location = {.ref = {id,1}, .lsn = end}; memcpy(location.uuid,uuid,16);
    OK(candidate_catalog_encode_root(&location,&root,images[0],8192));
    candidate_outcome_t *e = &node.entries[0];
    e->state = CANDIDATE_OUTCOME_ABORTED; e->terminal_end_lsn = terminal_end;
    e->result = retain ? (candidate_version_ref_t){{1,1},0,7} : (candidate_version_ref_t){0};
    e->result_bytes = retain ? a->bytes : 0;
    e->result_chunks = (uint32_t)((e->result_bytes + 65535) / 65536);
    if (retain) memcpy(e->result_digest,a->digest,32);
    else OK(candidate_digest_sha256(NULL,0,e->result_digest));
    location.ref.id++; OK(candidate_catalog_encode_node(&location,&node,images[1],8192));
    uint8_t group[16] = {0}; write_le(group,2,8); write_le(group + 8,2,4); write_le(group + 12,2,4);
    candidate_wal_record_t r = append_record(&c,10,3,group,16); uint32_t crc = candidate_wal_witness_crc(0,&r);
    for (unsigned i = 0; i < 2; i++) {
        uint8_t image[8224] = {0}; write_le(image,2,8); write_le(image + 8,id + i,8);
        write_le(image + 16,1,8); write_le(image + 24,a->files.anchor.checkpoint_begin,8);
        memcpy(image + 32,images[i],8192); r = append_record(&c,10,4,image,sizeof(image));
        crc = candidate_wal_witness_crc(crc,&r);
    }
    uint8_t finish[24] = {0}; write_le(finish,2,8); write_le(finish + 8,2,4); write_le(finish + 12,2,4); write_le(finish + 16,crc,4);
    append_record(&c,10,8,finish,24); CHECK(candidate_wal_info(a->wal).valid_end_lsn == end);
    uint8_t abort[32] = {0}; write_le(abort,c.first,8); write_le(abort + 8,c.last,8);
    write_le(abort + 16,c.records,8); write_le(abort + 24,c.crc,4); append_record(&c,10,11,abort,32);
    OK(candidate_wal_force(a->wal,candidate_wal_info(a->wal).valid_end_lsn));
    for (unsigned i = 0; i < 2; i++) CHECK(pwrite(a->fd,images[i],8192,(off_t)(id + i - 1) * 8192) == 8192);
    CHECK(fsync(a->fd) == 0);
}

static aborted_t aborted_fixture(size_t bytes, unsigned chunks, bool retain)
{
    aborted_t a = {.files = fixture(9,bytes), .bytes = bytes, .chunks = chunks};
    candidate_recovery_options_t o = options(&a.files); candidate_recovery_t *r = (void *)1;
    CHECK(candidate_recovery_open(&o,&r) == CANDIDATE_WAL_STALE && r == NULL);
    candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16);
    OK(candidate_wal_open(a.files.wal,&wo,&a.wal));
    candidate_witness_options_t opts = {.enable_system_groups = true}; memcpy(opts.uuid,uuid,16);
    candidate_wal_witness_t *history = NULL; OK(candidate_wal_witness_analyze_checkpoint(a.wal,&opts,&a.files.anchor,&history));
    const candidate_witness_transaction_t *t = candidate_wal_witness_find(history,9); CHECK(t != NULL);
    chain_t c = {.wal = a.wal, .first = t->first_lsn, .last = t->last_lsn, .records = t->records, .crc = t->chain_crc};
    candidate_wal_witness_close(&history);
    uint8_t payload[65537]; CHECK(bytes <= sizeof(payload));
    for (size_t i = 0; i < bytes; i++) payload[i] = (uint8_t)(i % 251);
    OK(candidate_digest_sha256(payload,bytes,a.digest));
    for (unsigned i = 0; i < chunks; i++) {
        uint8_t chunk[112] = {0}; write_le(chunk,1,8); write_le(chunk + 24,i,4);
        write_le(chunk + 28,(bytes + 65535) / 65536,4); write_le(chunk + 32,bytes,8);
        memcpy(chunk + 40,a.digest,32); write_le(chunk + 72,1,8); write_le(chunk + 80,1,8); write_le(chunk + 92,7,4);
        write_le(chunk + 96,(uint64_t)i * 65536,8); write_le(chunk + 104,bytes - i * 65536 < 65536 ? bytes - i * 65536 : 65536,4);
        emit(&c,9,chunk,112);
    }
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); OK(candidate_wal_close(&a.wal));
    o.owner_epoch = 8; candidate_recovery_terminal_info_t receipt;
    OK(candidate_recovery_resolve_terminals(&o,&receipt)); CHECK(receipt.appended_aborts == 1);
    OK(candidate_recovery_resolve_terminals(&o,&receipt)); CHECK(receipt.appended_aborts == 0 && receipt.existing_aborts == 1);
    wo.owner_epoch = 8; OK(candidate_wal_open(a.files.wal,&wo,&a.wal));
    a.fd = open(a.files.data,O_RDWR); CHECK(a.fd >= 0);
    publish_outcome(&a,retain,receipt.after_end_lsn);
    printf("ABORT_FIXTURE %s bytes=%zu chunks=%u retain=%u\n",a.files.wal,bytes,chunks,retain);
    return a;
}

static void close_aborted(aborted_t *a)
{
    CHECK(close(a->fd) == 0); OK(candidate_wal_close(&a->wal));
}

static void check_aborted(aborted_t *a, bool retain)
{
    candidate_outcome_witness_options_t o = aborted_options(a); candidate_outcome_witness_t *w = NULL;
    a->body_reads = 0; OK(candidate_outcome_witness_load(a->wal,&o,&w));
    candidate_catalog_query_t q = query(); const candidate_outcome_t *e = NULL;
    CHECK(candidate_outcome_witness_lookup(w,&q,&e) == CANDIDATE_CATALOG_MATCH && e != NULL);
    CHECK(e->state == CANDIDATE_OUTCOME_ABORTED && e->csn == 0);
    CHECK(e->result.page.id == (retain ? 1u : 0u) && e->result_bytes == (retain ? a->bytes : 0));
    CHECK(e->result_chunks == (retain ? (a->bytes + 65535) / 65536 : 0));
    CHECK(candidate_outcome_witness_info(w).aborted == 1 && (a->body_reads != 0) == retain);
    q.operation_digest[0] ^= 1; e = (void *)1;
    CHECK(candidate_outcome_witness_lookup(w,&q,&e) == CANDIDATE_CATALOG_CONTENT_MISMATCH && e == NULL);
    candidate_outcome_witness_close(&w);
}

static void final_descriptors(void)
{
    for (unsigned chunks = 0; chunks <= 2; chunks++) {
        aborted_t a = aborted_fixture(65537,chunks,false); check_aborted(&a,false);
        OK(candidate_wal_close(&a.wal)); candidate_wal_options_t wo = {.owner_epoch = 8}; memcpy(wo.uuid,uuid,16);
        OK(candidate_wal_open(a.files.wal,&wo,&a.wal));
        candidate_outcome_witness_options_t o = aborted_options(&a); candidate_outcome_witness_t *w = (void *)1;
        CHECK(candidate_outcome_witness_load(a.wal,&o,&w) == CANDIDATE_WAL_STALE && w == NULL);
        OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); check_aborted(&a,false); close_aborted(&a);
    }
    for (unsigned chunks = 0; chunks <= 2; chunks++) {
        aborted_t a = aborted_fixture(65537,chunks,true);
        if (chunks == 2) check_aborted(&a,true);
        else {
            candidate_outcome_witness_options_t o = aborted_options(&a); candidate_outcome_witness_t *w = (void *)1;
            CHECK(candidate_outcome_witness_load(a.wal,&o,&w) == CANDIDATE_WAL_CORRUPT && w == NULL);
        }
        close_aborted(&a);
    }
    for (unsigned retain = 0; retain <= 1; retain++) {
        aborted_t a = aborted_fixture(0,0,retain); check_aborted(&a,retain); close_aborted(&a);
    }
}

static void corrupt_descriptor(aborted_t *a, unsigned mode)
{
    uint64_t id = a->files.anchor.outcome_root.id + 1; uint8_t bytes[8192];
    CHECK(pread(a->fd,bytes,8192,(off_t)(id - 1) * 8192) == 8192);
    candidate_catalog_node_t node; candidate_page_info_t info;
    OK(candidate_catalog_decode_node(uuid,bytes,8192,&node)); OK(candidate_page_decode_header(uuid,bytes,8192,&info));
    candidate_outcome_t *e = &node.entries[0];
    switch (mode) {
    case 0: e->terminal_end_lsn += 8; break;
    case 1: e->admitting_owner_epoch++; break;
    case 2: e->operation_digest[0] ^= 1; break;
    case 3: e->result.slot_generation++; break;
    case 4: e->result_digest[0] ^= 1; break;
    case 5: e->result_bytes--; e->result_chunks--; break;
    case 6: e->state = CANDIDATE_OUTCOME_COMMITTED; e->csn = 1; break;
    default: CHECK(false);
    }
    OK(candidate_catalog_encode_node(&info.location,&node,bytes,8192));
    CHECK(pwrite(a->fd,bytes,8192,(off_t)(id - 1) * 8192) == 8192); CHECK(fsync(a->fd) == 0);
}

static void abort_corruption(void)
{
    for (unsigned mode = 0; mode < 8; mode++) {
        aborted_t a = aborted_fixture(65537,2,mode >= 3);
        if (mode < 7) corrupt_descriptor(&a,mode);
        else {
            uint8_t bytes[8192]; CHECK(pread(a.fd,bytes,8192,8192) == 8192);
            candidate_page_info_t info; OK(candidate_page_decode(uuid,bytes,8192,&info));
            uint8_t body[8104]; memset(body,42,sizeof(body));
            OK(candidate_page_encode_overflow(&info.location,(candidate_page_ref_t){3,1},body,sizeof(body),bytes,8192));
            CHECK(pwrite(a.fd,bytes,8192,8192) == 8192); CHECK(fsync(a.fd) == 0);
        }
        candidate_outcome_witness_options_t o = aborted_options(&a); candidate_catalog_t *raw = NULL;
        candidate_wal_status_t raw_status = candidate_catalog_load(&o.catalog,&raw);
        CHECK(raw_status == (mode == 6 ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK)); candidate_catalog_close(&raw);
        candidate_outcome_witness_t *w = (void *)1;
        CHECK(candidate_outcome_witness_load(a.wal,&o,&w) == CANDIDATE_WAL_CORRUPT && w == NULL);
        printf("ABORT_REJECT %s mode=%u raw_catalog=%u\n",a.files.wal,mode,raw_status); close_aborted(&a);
    }
}

static void abort_memory(void)
{
    for (unsigned retain = 0; retain <= 1; retain++) {
        aborted_t a = aborted_fixture(65537,retain ? 2 : 1,retain);
        candidate_outcome_witness_options_t o = aborted_options(&a); memory_t count = {0};
        candidate_wal_memory_t memory = {&count,allocate,release}; o.catalog.memory = &memory;
        candidate_outcome_witness_t *w = NULL; OK(candidate_outcome_witness_load(a.wal,&o,&w));
        unsigned calls = count.calls; size_t peak = count.peak;
        CHECK(count.live == candidate_outcome_witness_info(w).resident_bytes); candidate_outcome_witness_close(&w); CHECK(count.live == 0);
        for (unsigned fail = 1; fail <= calls; fail++) {
            count = (memory_t){.fail_at = fail}; w = (void *)1;
            CHECK(candidate_outcome_witness_load(a.wal,&o,&w) == CANDIDATE_WAL_NOMEM && w == NULL && count.live == 0);
        }
        count = (memory_t){.mutate_memory = &memory};
        OK(candidate_outcome_witness_load(a.wal,&o,&w)); candidate_outcome_witness_close(&w); CHECK(count.live == 0);
        printf("ABORT_MEMORY %s retain=%u calls=%u peak=%zu\n",a.files.wal,retain,calls,peak); close_aborted(&a);
    }
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90);
    final_descriptors(); abort_corruption(); abort_memory();
    printf("PASS aborted results checks=%u; final descriptor only, no product activation\n",checks); return 0;
}
