#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_page.h"
#include "storage/candidate_wal_witness.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)

static unsigned checks;
static const uint8_t uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
typedef struct {
    candidate_wal_t *wal;
    uint64_t first, last, records;
    uint32_t crc;
} chain_t;

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (i * 8); return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static candidate_wal_record_t emit(chain_t *c, unsigned type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t r = {.type = (uint16_t)type, .lsn = candidate_wal_info(c->wal).valid_end_lsn,
        .transaction_id = 9, .previous_lsn = c->last, .owner_epoch = 7, .payload = body, .payload_bytes = size};
    uint64_t end; OK(candidate_wal_append(c->wal, &r, &end)); CHECK(end == r.lsn + ((size + 103) & ~(uint64_t)7));
    if (c->records == 0) c->first = r.lsn;
    c->last = r.lsn; c->records++; c->crc = candidate_wal_witness_crc(c->crc, &r);
    return r;
}

static uint64_t emit_group(chain_t *c, unsigned group, unsigned first, unsigned count,
    unsigned total, const uint8_t *body)
{
    uint64_t end = candidate_wal_info(c->wal).valid_end_lsn + 112 + (uint64_t)count * 8320 + 120;
    uint8_t begin[16] = {0}; write_le(begin, group, 8); write_le(begin + 8, count, 4); write_le(begin + 12, count, 4);
    candidate_wal_record_t r = emit(c, 3, begin, sizeof(begin)); uint32_t crc = candidate_wal_witness_crc(0, &r);
    for (unsigned i = first; i < first + count; i++) {
        candidate_page_location_t location = {.ref = {i,1}, .lsn = end}; memcpy(location.uuid, uuid, 16);
        uint8_t image[32 + 8192] = {0};
        write_le(image, group, 8); write_le(image + 8, i, 8); write_le(image + 16, 1, 8);
        if (i == 1) {
            candidate_page_version_t v = {.transaction_id = 9, .table_id = 1, .row_id = 42,
                .payload_bytes = 1048576, .slot_generation = 7, .overflow = {2,1}};
            OK(candidate_page_encode_versions(&location, &v, 1, image + 32, 8192));
        } else {
            size_t offset = (size_t)(i - 2) * 8104;
            size_t size = 1048576 - offset < 8104 ? 1048576 - offset : 8104;
            candidate_page_ref_t next = {i == total ? 0 : i + 1, i == total ? 0 : 1};
            OK(candidate_page_encode_overflow(&location, next, body + offset, size, image + 32, 8192));
        }
        r = emit(c, 4, image, sizeof(image)); crc = candidate_wal_witness_crc(crc, &r);
    }
    uint8_t finish[24] = {0}; write_le(finish, group, 8); write_le(finish + 8, count, 4);
    write_le(finish + 12, count, 4); write_le(finish + 16, crc, 4); emit(c, 8, finish, sizeof(finish));
    CHECK(candidate_wal_info(c->wal).valid_end_lsn == end); return end;
}

static uint64_t emit_metadata(chain_t *c, const uint8_t digest[32])
{
    uint64_t end = candidate_wal_info(c->wal).valid_end_lsn + 112 + 2 * 8320 + 120;
    candidate_catalog_root_t root = {.owner_epoch = 7, .current_epoch = 1, .retention_floor = 1,
        .transaction_highwater = 9, .page_highwater = 133, .visible_csn = 1,
        .retained_bytes = 133u * 8192u, .count = 1, .pages = {{133,1}}};
    candidate_catalog_node_t node = {.ordinal = 0, .count = 1};
    node.entries[0] = (candidate_outcome_t){.epoch = 1, .transaction_id = 9, .state = CANDIDATE_OUTCOME_COMMITTED,
        .csn = 1, .terminal_end_lsn = end + 16 * 208 + 312, .result = {{1,1},0,7}, .result_bytes = 1048576,
        .result_chunks = 16, .declared_result_limit = 1048576, .admitting_owner_epoch = 7};
    memset(node.entries[0].operation_digest, 0x5c, 32); memcpy(node.entries[0].result_digest, digest, 32);
    uint8_t begin[16] = {0}; write_le(begin, 4, 8); write_le(begin + 8, 2, 4); write_le(begin + 12, 2, 4);
    candidate_wal_record_t r = emit(c, 3, begin, sizeof(begin)); uint32_t crc = candidate_wal_witness_crc(0, &r);
    for (unsigned id = 132; id <= 133; id++) {
        candidate_page_location_t l = {.ref = {id,1}, .lsn = end}; memcpy(l.uuid, uuid, 16);
        uint8_t image[32 + 8192] = {0}; write_le(image, 4, 8); write_le(image + 8, id, 8); write_le(image + 16, 1, 8);
        if (id == 132) OK(candidate_catalog_encode_root(&l, &root, image + 32, 8192));
        else OK(candidate_catalog_encode_node(&l, &node, image + 32, 8192));
        r = emit(c, 4, image, sizeof(image)); crc = candidate_wal_witness_crc(crc, &r);
    }
    uint8_t finish[24] = {0}; write_le(finish, 4, 8); write_le(finish + 8, 2, 4);
    write_le(finish + 12, 2, 4); write_le(finish + 16, crc, 4); emit(c, 8, finish, sizeof(finish));
    CHECK(candidate_wal_info(c->wal).valid_end_lsn == end); return end;
}

static uint64_t emit_result(chain_t *c, const uint8_t *body, const uint8_t digest[32])
{
    uint8_t admit[64] = {0}; write_le(admit, 1, 8); memset(admit + 24, 0x5c, 32); write_le(admit + 56, 1048576, 8);
    emit(c, 1, admit, sizeof(admit)); const uint8_t begin[16] = {0}; emit(c, 2, begin, sizeof(begin));
    unsigned pages = 1 + (1048576 + 8103) / 8104;
    uint64_t ends[4]; unsigned first = 1;
    for (unsigned group = 1; group <= 3; group++) {
        unsigned count = pages - first + 1 < 64 ? pages - first + 1 : 64;
        ends[group - 1] = emit_group(c, group, first, count, pages, body); first += count;
    }
    CHECK(first == pages + 1);
    ends[3] = emit_metadata(c, digest);
    for (unsigned i = 0; i < 16; i++) {
        uint8_t chunk[112] = {0}; write_le(chunk, 1, 8); write_le(chunk + 24, i, 4);
        write_le(chunk + 28, 16, 4); write_le(chunk + 32, 1048576, 8); memcpy(chunk + 40, digest, 32);
        write_le(chunk + 72, 1, 8); write_le(chunk + 80, 1, 8); write_le(chunk + 92, 7, 4);
        write_le(chunk + 96, (uint64_t)i * 65536, 8); write_le(chunk + 104, 65536, 4);
        emit(c, 9, chunk, sizeof(chunk));
    }
    uint8_t commit[152 + 4 * 16] = {0};
    write_le(commit, c->first, 8); write_le(commit + 8, c->last, 8); write_le(commit + 16, c->records, 8);
    write_le(commit + 24, c->crc, 4); write_le(commit + 32, 1, 8); write_le(commit + 40, 1, 8);
    memset(commit + 64, 0x5c, 32); memcpy(commit + 96, digest, 32);
    write_le(commit + 128, 1048576, 8); write_le(commit + 136, 16, 4); write_le(commit + 148, 4, 4);
    for (unsigned i = 0; i < 4; i++) { write_le(commit + 152 + i * 16, i + 1, 8); write_le(commit + 160 + i * 16, ends[i], 8); }
    emit(c, 10, commit, sizeof(commit));
    return candidate_wal_info(c->wal).valid_end_lsn;
}

typedef struct { candidate_wal_t *wal; int fd; unsigned pages; } replay_t;
static bool replay_image(void *context, const candidate_wal_record_t *record)
{
    replay_t *target = context;
    if (record->type != 4) return true;
    uint64_t id = read_le(record->payload + 8, 8);
    CHECK(id == ++target->pages && id <= 133);
    CHECK(candidate_wal_info(target->wal).durable_end_lsn >= read_le(record->payload + 72, 8));
    CHECK(pwrite(target->fd, record->payload + 32, 8192, (off_t)((id - 1) * 8192)) == 8192);
    return true;
}

static candidate_wal_status_t load_file(void *context, candidate_page_ref_t ref, uint8_t page[8192])
{
    int fd = *(int *)context;
    if (ref.id == 0 || ref.id > 133) return CANDIDATE_WAL_IO;
    return pread(fd, page, 8192, (off_t)((ref.id - 1) * 8192)) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); alarm(90);
    char prefix[PATH_MAX], data[PATH_MAX];
    CHECK(snprintf(prefix, sizeof(prefix), "build/candidate-result-wal-%ld", (long)getpid()) < (int)sizeof(prefix));
    CHECK(snprintf(data, sizeof(data), "%s.pages", prefix) < (int)sizeof(data));
    uint8_t *body = malloc(1048576); CHECK(body != NULL);
    for (unsigned i = 0; i < 1048576; i++) body[i] = (uint8_t)(i % 251);
    uint8_t digest[32]; OK(candidate_digest_sha256(body, 1048576, digest));
    candidate_wal_options_t options = {.owner_epoch = 7}; memcpy(options.uuid, uuid, 16);
    candidate_wal_t *wal = NULL; OK(candidate_wal_create(prefix, &options, &wal));
    chain_t chain = {.wal = wal}; uint64_t end = emit_result(&chain, body, digest);
    OK(candidate_wal_force(wal, end)); OK(candidate_wal_close(&wal));
    OK(candidate_wal_open(prefix, &options, &wal));
    candidate_witness_options_t wo = {0}; memcpy(wo.uuid, uuid, 16);
    candidate_wal_witness_t *witness = NULL; OK(candidate_wal_witness_analyze(wal, &wo, &witness));
    candidate_witness_info_t info = candidate_wal_witness_info(witness);
    CHECK(info.commits == 1 && info.complete_groups == 4 && info.unfinished == 0);
    const candidate_witness_transaction_t *transaction = candidate_wal_witness_find(witness, 9);
    CHECK(transaction != NULL && transaction->result_bytes == 1048576 && transaction->result_chunks == 16);
    CHECK(memcmp(transaction->result_digest, digest, 32) == 0);
    CHECK(candidate_wal_info(wal).durable_end_lsn == 0); OK(candidate_wal_force(wal, transaction->terminal_end_lsn));
    int fd = open(data, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600); CHECK(fd >= 0);
    replay_t replay = {wal, fd, 0}; candidate_wal_info_t scanned;
    OK(candidate_wal_scan(wal, replay_image, &replay, &scanned)); CHECK(replay.pages == 133);
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); fd = open(data, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); CHECK(fd >= 0);
    candidate_catalog_load_t catalog_request = {.root = {132,1}, .load = load_file, .context = &fd}; memcpy(catalog_request.uuid, uuid, 16);
    candidate_catalog_t *catalog = NULL; OK(candidate_catalog_load(&catalog_request, &catalog));
    candidate_catalog_query_t query = {.epoch = 1}; memcpy(query.uuid, uuid, 16); memset(query.operation_digest, 0x5c, 32);
    const candidate_outcome_t *found = NULL; CHECK(candidate_catalog_lookup(catalog, &query, &found) == CANDIDATE_CATALOG_MATCH);
    CHECK(found->state == CANDIDATE_OUTCOME_COMMITTED && found->transaction_id == 9 && found->csn == 1);
    CHECK(found->terminal_end_lsn == transaction->terminal_end_lsn && found->terminal_end_lsn == end);
    CHECK(found->result_chunks == transaction->result_chunks && found->result_bytes == transaction->result_bytes);
    CHECK(memcmp(found->result_digest, transaction->result_digest, 32) == 0);
    candidate_result_read_t request = {.ref = found->result, .transaction_id = found->transaction_id, .result_bytes = found->result_bytes,
        .load = load_file, .context = &fd}; memcpy(request.uuid, uuid, 16); memcpy(request.digest, found->result_digest, 32);
    candidate_page_result_t *result = NULL; OK(candidate_page_read_result(&request, &result));
    CHECK(candidate_page_result_size(result) == 1048576);
    CHECK(memcmp(candidate_page_result_bytes(result), body, 1048576) == 0);
    memset(body, 0, 1048576); CHECK(candidate_page_result_bytes(result)[1] == 1);
    query.operation_digest[0] ^= 1; CHECK(candidate_catalog_lookup(catalog, &query, &found) == CANDIDATE_CATALOG_CONTENT_MISMATCH && found == NULL);
    query.nonce[0] = 1; CHECK(candidate_catalog_lookup(catalog, &query, &found) == CANDIDATE_CATALOG_NOT_FOUND);
    query.epoch = 2; CHECK(candidate_catalog_lookup(catalog, &query, &found) == CANDIDATE_CATALOG_INVALID_ID);
    candidate_catalog_close(&catalog); candidate_page_result_close(&result); candidate_wal_witness_close(&witness);
    CHECK(close(fd) == 0); OK(candidate_wal_close(&wal)); free(body);
    printf("FIXTURE wal %s pages=133 groups=4 chunks=16 end=%llu\n", prefix, (unsigned long long)end);
    printf("FIXTURE data %s bytes=%u\n", data, 133u * 8192u);
    printf("PASS result WAL/page roundtrip checks=%u; fixture replay only, no general recovery activation\n", checks);
    return 0;
}
