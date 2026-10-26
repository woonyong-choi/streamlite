#ifndef TEST_CANDIDATE_SEMANTIC_FIXTURE_H
#define TEST_CANDIDATE_SEMANTIC_FIXTURE_H

#include <stdio.h>
#include <stdlib.h>

#include "storage/candidate_semantic_seed.h"

#define CHECK(c) do { semantic_checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
#define FIXTURE_TRANSACTIONS 8u
#define FIXTURE_GROUPS 16u
#define FIXTURE_EVENTS 16u

extern unsigned semantic_checks;
extern const char *fixture_base;
extern const uint8_t fixture_uuid[16];

typedef struct { uint64_t id, first, last, count; uint32_t crc; } fixture_chain_t;
typedef struct {
    fixture_chain_t chain;
    uint64_t owner_epoch, epoch, limit, total, result_bytes, chunk_bytes, csn, terminal_end;
    uint32_t chunks, groups;
    uint8_t nonce[16], operation[32], result_digest[32], chunk_digest[32];
    candidate_version_ref_t ref;
    uint64_t group_ids[4], group_ends[4];
    candidate_witness_state_t state;
    bool has_admission, has_begin;
} fixture_transaction_t;
typedef struct { uint64_t transaction_highwater, page_highwater, epoch, floor; } fixture_system_t;
typedef struct {
    candidate_wal_t *wal;
    char path[256];
    uint64_t owner_epoch, highest_owner, end_lsn, records, next_group, read_bytes;
    unsigned read_records, transaction_count, group_count, event_count, admission_count, system_groups;
    bool has_system;
    fixture_system_t system;
    fixture_transaction_t transactions[FIXTURE_TRANSACTIONS];
    candidate_semantic_group_t groups[FIXTURE_GROUPS];
    candidate_witness_system_event_t events[FIXTURE_EVENTS];
    unsigned admitted[FIXTURE_TRANSACTIONS];
} fixture_t;
typedef struct {
    uint64_t floor_lsn, lowest_lsn, highest_end, bytes;
    unsigned reads, forbidden;
    bool is_armed;
} fixture_guard_t;
typedef struct { unsigned calls, fail_at; size_t live; } fixture_memory_t;

extern fixture_guard_t fixture_guard;
extern const candidate_wal_io_t fixture_guarded_io;

void fixture_create(fixture_t *f, const char *name, bool has_system);
void fixture_open(fixture_t *f, uint64_t owner_epoch);
void fixture_close(fixture_t *f);
candidate_wal_t *fixture_reader(const fixture_t *f, uint64_t owner_epoch);
void fixture_mark(fixture_t *f);
uint64_t fixture_append(fixture_t *f, fixture_chain_t *chain, unsigned type, const uint8_t *body, size_t bytes,
    uint32_t *group_crc);
fixture_transaction_t *fixture_transaction(fixture_t *f, uint64_t id);
void fixture_admit(fixture_t *f, fixture_transaction_t *t, uint8_t tag, uint64_t limit);
void fixture_begin(fixture_t *f, fixture_transaction_t *t);
uint64_t fixture_group_begin(fixture_t *f, fixture_transaction_t *t, uint64_t id, uint32_t *crc);
void fixture_group_finish(fixture_t *f, fixture_transaction_t *t, uint64_t id, uint32_t crc);
void fixture_delta_group(fixture_t *f, fixture_transaction_t *t, uint64_t id);
void fixture_result(fixture_transaction_t *t, uint64_t total, uint8_t tag, candidate_version_ref_t ref);
void fixture_chunk_body(const fixture_transaction_t *t, uint8_t out[112]);
void fixture_append_chunk(fixture_t *f, fixture_transaction_t *t, const uint8_t body[112]);
void fixture_chunk(fixture_t *f, fixture_transaction_t *t);
size_t fixture_commit_body(const fixture_transaction_t *t, uint64_t csn, uint8_t *out);
void fixture_append_commit(fixture_t *f, fixture_transaction_t *t, uint64_t csn, const uint8_t *body, size_t bytes);
void fixture_commit(fixture_t *f, fixture_transaction_t *t, uint64_t csn);
void fixture_abort(fixture_t *f, fixture_transaction_t *t);
uint64_t fixture_system_group(fixture_t *f, uint64_t id, unsigned kind, uint64_t generation);
void fixture_prefix(fixture_t *f);
void fixture_suffix_first(fixture_t *f);
void fixture_suffix_rest(fixture_t *f);

void fixture_expect(const fixture_t *f, const candidate_semantic_seed_t *seed);
void fixture_same_witness(const candidate_wal_witness_t *a, const candidate_wal_witness_t *b);
void fixture_same_seed(const candidate_semantic_seed_t *a, const candidate_semantic_seed_t *b);

candidate_witness_options_t fixture_options(bool has_system, const candidate_wal_memory_t *memory);
candidate_wal_memory_t fixture_memory(fixture_memory_t *memory);
void fixture_guard_arm(const char *path, uint64_t floor_lsn);
void fixture_guard_disarm(void);

#endif
