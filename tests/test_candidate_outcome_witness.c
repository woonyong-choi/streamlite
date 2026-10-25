#define main original_result_test_main
#include "test_candidate_result_wal.c"
#undef main

#include "storage/candidate_checkpoint.h"
#include "storage/candidate_outcome_witness.h"
#include "storage/candidate_redo.h"

typedef struct { int fd; unsigned reads, fail_read; } data_t;
typedef struct {
    unsigned calls, fail_at;
    size_t live, peak;
    candidate_outcome_witness_options_t *mutate;
    candidate_wal_memory_t *mutate_memory;
} memory_t;
static unsigned serial;
static bool fail_wal_read;

static ssize_t fault_read(int fd, void *bytes, size_t size, off_t offset)
{
    if (fail_wal_read) return -1;
    return pread(fd, bytes, size, offset);
}

static void *allocate(void *context, size_t size)
{
    memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    if (m->mutate != NULL) { memset(m->mutate, 0, sizeof(*m->mutate)); m->mutate = NULL; }
    if (m->mutate_memory != NULL) { memset(m->mutate_memory, 0, sizeof(*m->mutate_memory)); m->mutate_memory = NULL; }
    void *p = malloc(size);
    if (p != NULL) { m->live += size; if (m->live > m->peak) m->peak = m->live; }
    return p;
}

static void release(void *context, void *p, size_t size)
{
    memory_t *m = context; CHECK(m->live >= size); m->live -= size; free(p);
}

static candidate_wal_status_t read_data(void *context, uint64_t id, uint8_t bytes[8192], bool *present)
{
    data_t *data = context;
    ssize_t n = pread(data->fd, bytes, 8192, (off_t)((id - 1) * 8192));
    if (n < 0) return CANDIDATE_WAL_IO;
    *present = n != 0;
    if (n < 8192) memset(bytes + n, 0, 8192 - (size_t)n);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t write_data(void *context, uint64_t id, const uint8_t bytes[8192])
{
    data_t *data = context;
    return pwrite(data->fd, bytes, 8192, (off_t)((id - 1) * 8192)) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static candidate_wal_status_t sync_data(void *context)
{
    data_t *data = context;
    return fsync(data->fd) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static candidate_wal_status_t load_data(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    data_t *data = context;
    if (++data->reads == data->fail_read) return CANDIDATE_WAL_IO;
    return pread(data->fd, bytes, 8192, (off_t)((ref.id - 1) * 8192)) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static void restore(candidate_wal_t *wal, data_t *data)
{
    candidate_redo_options_t options = {.target = {data, read_data, write_data, sync_data}};
    memcpy(options.uuid, uuid, 16);
    candidate_redo_info_t info;
    OK(candidate_redo_execute(wal, &options, &info));
    CHECK(info.pages_written == 3 || info.pages_written == 133);
}

static uint64_t small_result(chain_t *c, unsigned state, bool empty)
{
    uint8_t admit[64] = {0}; write_le(admit, 1, 8); memset(admit + 24, 0x5c, 32); write_le(admit + 56, 1, 8);
    emit(c, 1, admit, 64); uint8_t begin[16] = {0}; emit(c, 2, begin, 16);
    uint64_t end = candidate_wal_info(c->wal).valid_end_lsn + 112 + 3 * 8320 + 120;
    uint64_t terminal = state == 1 ? 0 : end + (state == 3 ? 128 : (empty ? 264 : 472));
    uint8_t value = 'h', digest[32]; OK(candidate_digest_sha256(&value, empty ? 0 : 1, digest));
    candidate_catalog_root_t root = {.owner_epoch = 7, .current_epoch = 1, .retention_floor = 1,
        .transaction_highwater = 9, .page_highwater = 3, .visible_csn = state == 2 ? 1 : 0,
        .retained_bytes = 3 * 8192, .count = 1, .pages = {{3,1}}};
    candidate_catalog_node_t node = {.count = 1};
    candidate_outcome_t *e = &node.entries[0];
    *e = (candidate_outcome_t){.epoch = 1, .transaction_id = 9, .state = (candidate_outcome_state_t)state,
        .csn = state == 2 ? 1 : 0, .terminal_end_lsn = terminal, .declared_result_limit = 1, .admitting_owner_epoch = 7};
    memset(e->operation_digest, 0x5c, 32);
    if (state != 1) OK(candidate_digest_sha256(NULL, 0, e->result_digest));
    if (state == 2) {
        e->result = (candidate_version_ref_t){{1,1},0,7}; e->result_bytes = empty ? 0 : 1;
        e->result_chunks = empty ? 0 : 1; memcpy(e->result_digest, digest, 32);
    }
    uint8_t start[16] = {0}; write_le(start, 1, 8); write_le(start + 8, 3, 4); write_le(start + 12, 3, 4);
    candidate_wal_record_t r = emit(c, 3, start, 16); uint32_t crc = candidate_wal_witness_crc(0, &r);
    for (unsigned id = 1; id <= 3; id++) {
        candidate_page_location_t l = {.ref = {id,1}, .lsn = end}; memcpy(l.uuid, uuid, 16);
        uint8_t image[8224] = {0}; write_le(image, 1, 8); write_le(image + 8, id, 8); write_le(image + 16, 1, 8);
        if (id == 1) {
            candidate_page_version_t v = {.transaction_id = 9, .table_id = 1, .row_id = 42,
                .payload_bytes = empty ? 0 : 1, .slot_generation = 7, .inline_bytes = &value};
            OK(candidate_page_encode_versions(&l, &v, 1, image + 32, 8192));
        } else if (id == 2) OK(candidate_catalog_encode_root(&l, &root, image + 32, 8192));
        else OK(candidate_catalog_encode_node(&l, &node, image + 32, 8192));
        r = emit(c, 4, image, sizeof(image)); crc = candidate_wal_witness_crc(crc, &r);
    }
    uint8_t finish[24] = {0}; write_le(finish, 1, 8); write_le(finish + 8, 3, 4); write_le(finish + 12, 3, 4); write_le(finish + 16, crc, 4);
    emit(c, 8, finish, 24); CHECK(candidate_wal_info(c->wal).valid_end_lsn == end);
    if (state == 1) return end;
    if (state == 2 && !empty) {
        uint8_t chunk[112] = {0}; write_le(chunk, 1, 8); write_le(chunk + 28, 1, 4); write_le(chunk + 32, 1, 8);
        memcpy(chunk + 40, digest, 32); write_le(chunk + 72, 1, 8); write_le(chunk + 80, 1, 8);
        write_le(chunk + 92, 7, 4); write_le(chunk + 104, 1, 4); emit(c, 9, chunk, 112);
    }
    uint8_t final[168] = {0}; write_le(final, c->first, 8); write_le(final + 8, c->last, 8);
    write_le(final + 16, c->records, 8); write_le(final + 24, c->crc, 4);
    if (state == 3) emit(c, 11, final, 32);
    else {
        write_le(final + 32, 1, 8); write_le(final + 40, 1, 8); memset(final + 64, 0x5c, 32);
        memcpy(final + 96, digest, 32); write_le(final + 128, empty ? 0 : 1, 8); write_le(final + 136, empty ? 0 : 1, 4);
        write_le(final + 148, 1, 4); write_le(final + 152, 1, 8); write_le(final + 160, end, 8); emit(c, 10, final, sizeof(final));
    }
    CHECK(candidate_wal_info(c->wal).valid_end_lsn == terminal); return terminal;
}

static candidate_wal_t *fixture(data_t *data, unsigned state, bool empty, bool large, char prefix[256])
{
    CHECK(snprintf(prefix, 256, "build/candidate-outcome-%ld-%u", (long)getpid(), ++serial) > 0);
    candidate_wal_options_t options = {.owner_epoch = 7}; memcpy(options.uuid, uuid, 16);
    candidate_wal_io_t io = {fault_read,pwrite,fsync,ftruncate}; options.io = &io;
    candidate_wal_t *wal = NULL; OK(candidate_wal_create(prefix, &options, &wal)); chain_t chain = {.wal = wal};
    if (large) {
        uint8_t *body = malloc(1048576), digest[32]; CHECK(body != NULL);
        for (unsigned i = 0; i < 1048576; i++) body[i] = (uint8_t)(i % 251);
        OK(candidate_digest_sha256(body, 1048576, digest)); emit_result(&chain, body, digest); free(body);
    } else small_result(&chain, state, empty);
    char path[300]; CHECK(snprintf(path, sizeof(path), "%s.pages", prefix) > 0);
    data->fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600); CHECK(data->fd >= 0);
    restore(wal, data); printf("FIXTURE %s state=%u empty=%u large=%u end=%llu\n", prefix, state, empty, large,
        (unsigned long long)candidate_wal_info(wal).valid_end_lsn);
    return wal;
}

static candidate_outcome_witness_options_t request(data_t *data, bool large)
{
    candidate_outcome_witness_options_t options = {.catalog = {.root = {large ? 132 : 2,1}, .load = load_data, .context = data}};
    memcpy(options.catalog.uuid, uuid, 16); return options;
}

static void check_lookup(candidate_outcome_witness_t *w, unsigned state)
{
    candidate_catalog_query_t query = {.epoch = 1}; memcpy(query.uuid, uuid, 16); memset(query.operation_digest, 0x5c, 32);
    const candidate_outcome_t *found = NULL;
    CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_MATCH);
    CHECK(found != NULL && found->state == state);
    query.operation_digest[0] ^= 1;
    CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_CONTENT_MISMATCH && found == NULL);
    query.nonce[0] = 1;
    CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_NOT_FOUND && found == NULL);
    query.epoch = 2; CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_INVALID_ID);
    query.uuid[0] ^= 1; CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_INVALID_ID);
}

static void normal_states(void)
{
    for (unsigned state = 1; state <= 3; state++) {
        data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, state, true, false, prefix);
        candidate_outcome_witness_options_t options = request(&data, false); candidate_outcome_witness_t *w = NULL;
        OK(candidate_outcome_witness_load(wal, &options, &w)); check_lookup(w, state);
        candidate_outcome_witness_info_t info = candidate_outcome_witness_info(w);
        CHECK(info.admitted == (state == 1) && info.committed == (state == 2) && info.aborted == (state == 3));
        CHECK(info.result_bytes == 0 && info.wal_end_lsn == candidate_wal_info(wal).valid_end_lsn);
        candidate_outcome_witness_close(&w); CHECK(w == NULL); CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
    }
}

static void mutate_metadata(data_t *data, unsigned mode)
{
    uint8_t page[8192]; OK(load_file(&data->fd, (candidate_page_ref_t){3,1}, page));
    candidate_catalog_node_t node; candidate_page_info_t info;
    OK(candidate_catalog_decode_node(uuid, page, 8192, &node)); OK(candidate_page_decode_header(uuid, page, 8192, &info));
    candidate_outcome_t *e = &node.entries[0];
    switch (mode) {
    case 1: e->state = CANDIDATE_OUTCOME_ABORTED; e->csn = 0; break;
    case 2: e->terminal_end_lsn += 8; break;
    case 3: e->result_digest[0] ^= 1; break;
    case 4: e->result.slot_generation++; break;
    case 5: e->admitting_owner_epoch = 6; break;
    case 6: e->declared_result_limit = 2; break;
    case 7: e->nonce[0] = 1; break;
    case 8: e->operation_digest[0] ^= 1; break;
    case 9:
        e->state = CANDIDATE_OUTCOME_ADMITTED; e->csn = 0; e->terminal_end_lsn = 0;
        e->result = (candidate_version_ref_t){0}; e->result_bytes = 0; e->result_chunks = 0;
        memset(e->result_digest, 0, 32); break;
    default: CHECK(false);
    }
    OK(candidate_catalog_encode_node(&info.location, &node, page, sizeof(page)));
    CHECK(pwrite(data->fd, page, 8192, 2 * 8192) == 8192); CHECK(fsync(data->fd) == 0);
}

static void metadata_mismatch(void)
{
    for (unsigned mode = 1; mode <= 9; mode++) {
        data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, false, false, prefix);
        mutate_metadata(&data, mode); candidate_outcome_witness_options_t options = request(&data, false);
        candidate_catalog_t *raw = NULL; OK(candidate_catalog_load(&options.catalog, &raw)); candidate_catalog_close(&raw);
        candidate_outcome_witness_t *w = (void *)1;
        candidate_wal_status_t status = candidate_outcome_witness_load(wal, &options, &w);
        CHECK(status == (mode == 9 ? CANDIDATE_WAL_STALE : CANDIDATE_WAL_CORRUPT)); CHECK(w == NULL);
        printf("REJECT metadata mode=%u status=%u raw=OK\n", mode, status);
        CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
    }
}

static void large_and_failures(void)
{
    data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, false, true, prefix);
    candidate_outcome_witness_options_t options = request(&data, true);
    memory_t count = {0}; candidate_wal_memory_t memory = {&count,allocate,release}; options.catalog.memory = &memory;
    candidate_outcome_witness_t *w = NULL; OK(candidate_outcome_witness_load(wal, &options, &w));
    unsigned allocations = count.calls; size_t peak = count.peak;
    CHECK(candidate_outcome_witness_info(w).result_bytes == 1048576); check_lookup(w, 2);
    CHECK(count.live == candidate_outcome_witness_info(w).resident_bytes);
    candidate_outcome_witness_close(&w); CHECK(count.live == 0);
    for (unsigned fail = 1; fail <= allocations; fail++) {
        count = (memory_t){.fail_at = fail}; data.reads = 0; w = (void *)1;
        CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_NOMEM); CHECK(w == NULL && count.live == 0);
    }
    count = (memory_t){.mutate = &options, .mutate_memory = &memory};
    OK(candidate_outcome_witness_load(wal, &options, &w)); candidate_outcome_witness_close(&w); CHECK(count.live == 0);
    options = request(&data, true);
    const unsigned failures[] = {1,2,3,133};
    for (unsigned i = 0; i < sizeof(failures)/sizeof(failures[0]); i++) {
        data.reads = 0; data.fail_read = failures[i]; w = (void *)1;
        CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_IO); CHECK(w == NULL);
    }
    data.fail_read = 0; fail_wal_read = true; w = (void *)1;
    CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_IO && w == NULL); fail_wal_read = false;
    OK(candidate_wal_close(&wal));
    candidate_wal_options_t reopen = {.owner_epoch = 7}; memcpy(reopen.uuid, uuid, 16);
    OK(candidate_wal_open(prefix, &reopen, &wal)); OK(candidate_wal_force(wal, candidate_wal_info(wal).valid_end_lsn));
    uint8_t broken[8192]; CHECK(pread(data.fd, broken, 8192, 8192) == 8192); broken[100] ^= 1;
    CHECK(pwrite(data.fd, broken, 8192, 8192) == 8192); w = (void *)1;
    CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_CORRUPT && w == NULL);
    printf("RESOURCE allocations=%u peak=%zu; result max=1048576\n", allocations, peak);
    CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
}

static void advance_epoch(candidate_wal_t *wal, candidate_catalog_root_t *root, candidate_page_location_t *location)
{
    uint64_t first = candidate_wal_info(wal).valid_end_lsn, end = first + 8688;
    uint8_t begin[16] = {0}, epoch[40] = {0}, image[8224] = {0}, finish[24] = {0};
    write_le(begin, 2, 8); write_le(begin + 8, 1, 4); write_le(begin + 12, 2, 4);
    write_le(epoch, 2, 8); write_le(epoch + 8, 1, 8); write_le(epoch + 16, 2, 8);
    write_le(epoch + 24, 1, 8); write_le(epoch + 32, 2, 8);
    write_le(image, 2, 8); write_le(image + 8, 2, 8); write_le(image + 16, 1, 8); write_le(image + 24, location->lsn, 8);
    location->lsn = end; OK(candidate_catalog_encode_root(location, root, image + 32, 8192));
    write_le(finish, 2, 8); write_le(finish + 8, 1, 4); write_le(finish + 12, 2, 4);
    const unsigned kinds[] = {3,15,4,8};
    const size_t sizes[] = {16,40,8224,24}; uint8_t *bodies[] = {begin,epoch,image,finish};
    uint64_t previous = 0; uint32_t crc = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (i == 3) write_le(finish + 16, crc, 4);
        candidate_wal_record_t r = {.type = (uint16_t)kinds[i], .lsn = candidate_wal_info(wal).valid_end_lsn,
            .previous_lsn = previous, .owner_epoch = 7, .payload = bodies[i], .payload_bytes = sizes[i]};
        uint64_t actual; OK(candidate_wal_append(wal, &r, &actual)); previous = r.lsn;
        crc = candidate_wal_witness_crc(crc, &r);
    }
    CHECK(candidate_wal_info(wal).valid_end_lsn == end); OK(candidate_wal_force(wal, end));
}

static void root_coverage(void)
{
    for (unsigned expired = 0; expired <= 1; expired++) {
        data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, true, false, prefix);
        uint8_t page[8192]; OK(load_file(&data.fd, (candidate_page_ref_t){2,1}, page));
        candidate_catalog_root_t root; candidate_page_info_t info;
        OK(candidate_catalog_decode_root(uuid, page, 8192, &root)); OK(candidate_page_decode_header(uuid, page, 8192, &info));
        root.count = 0; root.current_epoch = root.retention_floor = expired ? 2 : 1;
        if (expired) advance_epoch(wal, &root, &info.location);
        OK(candidate_catalog_encode_root(&info.location, &root, page, sizeof(page)));
        CHECK(pwrite(data.fd, page, 8192, 8192) == 8192); CHECK(fsync(data.fd) == 0);
        candidate_outcome_witness_options_t options = request(&data, false); candidate_outcome_witness_t *w = NULL;
        candidate_wal_status_t status = candidate_outcome_witness_load(wal, &options, &w);
        CHECK(status == (expired ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE));
        if (expired) {
            candidate_catalog_query_t query = {.epoch = 1}; memcpy(query.uuid, uuid, 16);
            const candidate_outcome_t *found = (void *)1;
            CHECK(candidate_outcome_witness_lookup(w, &query, &found) == CANDIDATE_CATALOG_OUTSIDE_RETENTION && found == NULL);
        } else CHECK(w == NULL);
        printf("COVERAGE %s expired=%u status=%u; retention authority unverified\n", prefix, expired, status);
        candidate_outcome_witness_close(&w); CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
    }
}

static void restart_boundaries(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, true, false, prefix);
        uint64_t end = candidate_wal_info(wal).valid_end_lsn; OK(candidate_wal_close(&wal));
        char path[320]; CHECK(snprintf(path, sizeof(path), "%s.wal.0000000000000000", prefix) > 0);
        int fd = open(path, O_RDWR); CHECK(fd >= 0);
        if (mode == 1) CHECK(ftruncate(fd, (off_t)(end - 264)) == 0);
        if (mode == 2) CHECK(pwrite(fd, "S", 1, (off_t)end) == 1);
        CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
        candidate_wal_options_t reopen = {.owner_epoch = 7}; memcpy(reopen.uuid, uuid, 16);
        OK(candidate_wal_open(prefix, &reopen, &wal));
        candidate_outcome_witness_options_t options = request(&data, false); candidate_outcome_witness_t *w = (void *)1;
        CHECK(candidate_outcome_witness_load(wal, &options, &w) == (mode == 2 ? CANDIDATE_WAL_TAIL : CANDIDATE_WAL_STALE));
        CHECK(w == NULL);
        if (mode != 2) {
            OK(candidate_wal_force(wal, candidate_wal_info(wal).valid_end_lsn));
            candidate_wal_status_t status = candidate_outcome_witness_load(wal, &options, &w);
            CHECK(status == (mode == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE));
            candidate_outcome_witness_close(&w);
        }
        printf("RESTART %s mode=%u\n", prefix, mode);
        CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
    }
}

static void change_chunk_ref(const char *prefix, uint64_t end)
{
    char path[320]; CHECK(snprintf(path, sizeof(path), "%s.wal.0000000000000000", prefix) > 0);
    int fd = open(path, O_RDWR); CHECK(fd >= 0);
    uint8_t *frame = malloc(1048576), *encoded = malloc(1048576); CHECK(frame != NULL && encoded != NULL);
    uint32_t chain = 0; unsigned chunks = 0;
    for (uint64_t at = 64; at < end;) {
        CHECK(pread(fd, frame, 80, (off_t)at) == 80); size_t size = (size_t)read_le(frame + 8, 4);
        CHECK(size <= 1048576); CHECK(pread(fd, frame, size, (off_t)at) == (ssize_t)size);
        candidate_wal_record_t r; OK(candidate_wal_decode(uuid, frame, size, &r));
        if (r.type == 9 && ++chunks == 2) write_le(frame + 80 + 72, 2, 8);
        if (r.type == 10) write_le(frame + 80 + 24, chain, 4);
        chain = candidate_wal_witness_crc(chain, &r); size_t written;
        OK(candidate_wal_encode(uuid, &r, encoded, 1048576, &written)); CHECK(written == size);
        CHECK(pwrite(fd, encoded, size, (off_t)at) == (ssize_t)size); at += size;
    }
    CHECK(chunks == 16); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); free(frame); free(encoded);
}

static void chunk_reference_mismatch(void)
{
    data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, false, true, prefix);
    uint64_t end = candidate_wal_info(wal).valid_end_lsn; OK(candidate_wal_close(&wal)); change_chunk_ref(prefix, end);
    candidate_wal_options_t reopen = {.owner_epoch = 7}; memcpy(reopen.uuid, uuid, 16); OK(candidate_wal_open(prefix, &reopen, &wal));
    candidate_witness_options_t wo = {0}; memcpy(wo.uuid, uuid, 16); candidate_wal_witness_t *raw = NULL;
    OK(candidate_wal_witness_analyze(wal, &wo, &raw)); candidate_wal_witness_close(&raw); OK(candidate_wal_force(wal, end));
    candidate_outcome_witness_options_t options = request(&data, true); candidate_outcome_witness_t *w = (void *)1;
    CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_CORRUPT && w == NULL);
    printf("CHUNK_REF %s legacy=OK outcome=CORRUPT\n", prefix);
    CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
}

static void checkpoint_boundary(void)
{
    data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, true, false, prefix);
    uint64_t begin = candidate_wal_info(wal).valid_end_lsn;
    candidate_control_state_t anchor = {.backend = 1, .generation = 1, .owner_epoch = 7, .checkpoint_id = 1,
        .checkpoint_begin = begin, .checkpoint_end = begin + 416, .redo_begin = 64, .transaction_highwater = 9,
        .page_highwater = 3, .request_epoch = 1, .retention_floor = 1, .visible_csn = 1, .outcome_root = {2,1}, .allocation_root = {3,1}};
    memcpy(anchor.uuid, uuid, 16); candidate_checkpoint_summary_t summary = {.anchor = anchor};
    uint8_t body[160], digest[32] = {0}; size_t size;
    OK(candidate_checkpoint_encode_begin(&summary, body, sizeof(body), &size));
    candidate_wal_record_t record = {.type = 12, .payload = body, .payload_bytes = size}; uint64_t end;
    OK(candidate_wal_append(wal, &record, &end)); OK(candidate_checkpoint_digest(digest, body, size, digest));
    candidate_checkpoint_end_t final = {.checkpoint_id = 1, .begin_lsn = begin}; memcpy(final.digest, digest, 32);
    OK(candidate_checkpoint_encode_end(&final, body, sizeof(body), &size));
    record = (candidate_wal_record_t){.type = 14, .previous_lsn = begin, .payload = body, .payload_bytes = size};
    OK(candidate_wal_append(wal, &record, &end)); CHECK(end == anchor.checkpoint_end); OK(candidate_wal_force(wal, end));
    candidate_outcome_witness_options_t options = request(&data, false); options.checkpoint = &anchor;
    candidate_outcome_witness_t *w = NULL; OK(candidate_outcome_witness_load(wal, &options, &w));
    check_lookup(w, 2); candidate_outcome_witness_close(&w);
    anchor.visible_csn = 2; w = (void *)1;
    CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_CORRUPT && w == NULL);
    printf("CHECKPOINT %s begin=%llu end=%llu; allocation root placeholder\n", prefix,
        (unsigned long long)begin, (unsigned long long)end);
    CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
}

static void signed_body_mismatch(void)
{
    data_t data = {0}; char prefix[256]; candidate_wal_t *wal = fixture(&data, 2, false, false, prefix);
    uint8_t page[8192]; OK(load_file(&data.fd, (candidate_page_ref_t){1,1}, page));
    candidate_page_version_t version; candidate_page_info_t info;
    OK(candidate_page_get_version(uuid, page, 8192, (candidate_version_ref_t){{1,1},0,7}, &version));
    OK(candidate_page_decode(uuid, page, 8192, &info)); uint8_t value = 'i'; version.inline_bytes = &value;
    OK(candidate_page_encode_versions(&info.location, &version, 1, page, sizeof(page)));
    CHECK(pwrite(data.fd, page, 8192, 0) == 8192); CHECK(fsync(data.fd) == 0);
    OK(candidate_page_decode(uuid, page, 8192, &info));
    candidate_outcome_witness_options_t options = request(&data, false); candidate_outcome_witness_t *w = (void *)1;
    CHECK(candidate_outcome_witness_load(wal, &options, &w) == CANDIDATE_WAL_CORRUPT && w == NULL);
    printf("BODY_DIGEST %s typed_page=OK outcome=CORRUPT\n", prefix);
    CHECK(close(data.fd) == 0); OK(candidate_wal_close(&wal));
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90);
    normal_states(); metadata_mismatch(); large_and_failures();
    root_coverage(); restart_boundaries(); chunk_reference_mismatch(); checkpoint_boundary();
    signed_body_mismatch();
    const candidate_outcome_t *found = (void *)1;
    CHECK(candidate_outcome_witness_lookup(NULL, NULL, &found) == CANDIDATE_CATALOG_UNAVAILABLE && found == NULL);
    candidate_outcome_witness_t *w = (void *)1;
    CHECK(candidate_outcome_witness_load(NULL, NULL, &w) == CANDIDATE_WAL_INVALID && w == NULL);
    CHECK(candidate_catalog_at(NULL, 0) == NULL);
    printf("PASS outcome corroboration checks=%u; no general recovery activation\n", checks);
    return 0;
}
