#include "test_candidate_semantic_fixture.h"

#include <string.h>
#include <unistd.h>

typedef struct {
    fixture_t fixture;
    candidate_semantic_seed_t *at_b, *at_b_copy, *at_b1;
    uint64_t b, b1, first_bytes, rest_bytes;
    unsigned first_records, rest_records;
} history_t;
typedef void (*suffix_t)(fixture_t *f);
typedef struct {
    const char *name;
    suffix_t append;
    uint64_t owner_epoch;
    candidate_wal_status_t expected, legacy;
} invalid_case_t;

static char sentinel;
#define SENTINEL ((candidate_semantic_seed_t *)(void *)&sentinel)

static void put(uint8_t *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t get(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static candidate_semantic_seed_t *capture_seed(candidate_wal_t *wal, bool has_system)
{
    candidate_witness_options_t o = fixture_options(has_system, NULL);
    candidate_semantic_seed_t *seed = SENTINEL;
    OK(candidate_semantic_seed_capture(wal, &o, &seed));
    CHECK(seed != NULL && seed != SENTINEL);
    return seed;
}

static candidate_semantic_seed_t *continue_seed(candidate_wal_t *wal, bool has_system,
    const candidate_semantic_seed_t *seed)
{
    candidate_witness_options_t o = fixture_options(has_system, NULL);
    candidate_semantic_seed_t *next = SENTINEL;
    OK(candidate_semantic_seed_continue(wal, &o, seed, &next));
    CHECK(next != NULL && next != SENTINEL);
    return next;
}

static candidate_semantic_seed_t *guarded_continue(candidate_wal_t *wal, const fixture_t *f,
    const candidate_semantic_seed_t *seed, uint64_t floor_lsn, unsigned records, uint64_t bytes)
{
    fixture_guard_arm(f->path, floor_lsn);
    candidate_witness_options_t o = fixture_options(f->has_system, NULL);
    candidate_semantic_seed_t *next = SENTINEL;
    candidate_wal_status_t status = candidate_semantic_seed_continue(wal, &o, seed, &next);
    fixture_guard_disarm();
    OK(status);
    CHECK(next != NULL && next != SENTINEL);
    CHECK(fixture_guard.forbidden == 0 && fixture_guard.reads == 2 * records && fixture_guard.bytes == bytes);
    if (records > 0) CHECK(fixture_guard.lowest_lsn == floor_lsn && fixture_guard.highest_end == f->end_lsn);
    printf("SUFFIX floor=%llu end=%llu records=%u reads=%u bytes=%llu forbidden=%u\n",
        (unsigned long long)floor_lsn, (unsigned long long)f->end_lsn, records, fixture_guard.reads,
        (unsigned long long)fixture_guard.bytes, fixture_guard.forbidden);
    return next;
}

static void build_history(history_t *h, const char *name, bool has_system)
{
    fixture_t *f = &h->fixture;
    fixture_create(f, name, has_system);
    fixture_prefix(f);
    h->at_b = capture_seed(f->wal, has_system);
    h->at_b_copy = capture_seed(f->wal, has_system);
    fixture_expect(f, h->at_b);
    h->b = f->end_lsn;
    fixture_close(f);
    fixture_expect(f, h->at_b);
    fixture_open(f, 7);
    fixture_mark(f);
    fixture_suffix_first(f);
    h->first_records = f->read_records;
    h->first_bytes = f->read_bytes;
    h->at_b1 = continue_seed(f->wal, has_system, h->at_b);
    fixture_expect(f, h->at_b1);
    h->b1 = f->end_lsn;
    fixture_mark(f);
    fixture_suffix_rest(f);
    h->rest_records = f->read_records;
    h->rest_bytes = f->read_bytes;
    fixture_close(f);
}

static void legacy_scan_reads_prefix(const history_t *h)
{
    candidate_wal_t *wal = fixture_reader(&h->fixture, 7);
    candidate_witness_options_t o = fixture_options(h->fixture.has_system, NULL);
    candidate_wal_witness_t *w = NULL;
    fixture_guard_arm(h->fixture.path, h->b);
    candidate_wal_status_t status = candidate_wal_witness_analyze(wal, &o, &w);
    fixture_guard_disarm();
    CHECK(status == CANDIDATE_WAL_IO && w == NULL);
    CHECK(fixture_guard.forbidden == 1 && fixture_guard.reads == 0);
    OK(candidate_wal_close(&wal));
    printf("PREFIX_GUARD positive floor=%llu legacy_status=%d forbidden=%u reads=%u\n",
        (unsigned long long)h->b, (int)status, fixture_guard.forbidden, fixture_guard.reads);
}

static void seeded_suffix_matches_full_history(bool has_system)
{
    history_t h;
    build_history(&h, has_system ? "system" : "raw", has_system);
    legacy_scan_reads_prefix(&h);
    const fixture_t *f = &h.fixture;
    candidate_wal_t *wal = fixture_reader(f, 7);
    candidate_semantic_seed_t *reference = capture_seed(wal, has_system);
    candidate_witness_options_t o = fixture_options(has_system, NULL);
    candidate_wal_witness_t *legacy = NULL;
    OK(candidate_wal_witness_analyze(wal, &o, &legacy));
    fixture_same_witness(candidate_semantic_seed_witness(reference), legacy);
    fixture_expect(f, reference);
    unsigned records = h.first_records + h.rest_records;
    uint64_t bytes = h.first_bytes + h.rest_bytes;
    candidate_semantic_seed_t *from_b = guarded_continue(wal, f, h.at_b, h.b, records, bytes);
    fixture_same_seed(from_b, reference);
    fixture_expect(f, from_b);
    candidate_semantic_seed_t *from_b1 = guarded_continue(wal, f, h.at_b1, h.b1, h.rest_records, h.rest_bytes);
    fixture_same_seed(from_b1, reference);
    candidate_semantic_seed_t *again = guarded_continue(wal, f, h.at_b, h.b, records, bytes);
    fixture_same_seed(again, from_b);
    candidate_semantic_seed_t *settled = guarded_continue(wal, f, from_b, f->end_lsn, 0, 0);
    fixture_same_seed(settled, reference);
    fixture_same_seed(h.at_b, h.at_b_copy);
    candidate_semantic_seed_close(&h.at_b);
    candidate_semantic_seed_close(&h.at_b_copy);
    candidate_semantic_seed_close(&h.at_b1);
    CHECK(h.at_b == NULL && h.at_b_copy == NULL && h.at_b1 == NULL);
    fixture_same_seed(from_b, reference);
    candidate_semantic_seed_close(&from_b);
    fixture_same_seed(again, reference);
    fixture_same_seed(settled, reference);
    fixture_same_seed(from_b1, reference);
    candidate_semantic_seed_close(&again);
    candidate_semantic_seed_close(&settled);
    candidate_semantic_seed_close(&from_b1);
    candidate_semantic_seed_close(&reference);
    candidate_wal_witness_close(&legacy);
    OK(candidate_wal_close(&wal));
    printf("DIFFERENTIAL mode=%s B=%llu B1=%llu E=%llu suffix_records=%u; prefix WAL open/scan before guard, "
        "cold open and old-file existence not solved\n", has_system ? "system" : "raw", (unsigned long long)h.b,
        (unsigned long long)h.b1, (unsigned long long)f->end_lsn, records);
}

static fixture_transaction_t *complete_result(fixture_t *f)
{
    fixture_transaction_t *t = fixture_transaction(f, 11);
    fixture_delta_group(f, t, ++f->next_group);
    fixture_chunk(f, t);
    return t;
}

static void commit_with(fixture_t *f, unsigned offset, uint64_t delta)
{
    fixture_transaction_t *t = complete_result(f);
    uint8_t body[152 + 4 * 16];
    size_t bytes = fixture_commit_body(t, 2, body);
    unsigned size = offset == 24 ? 4 : 8;
    put(body + offset, get(body + offset, size) + delta, size);
    fixture_append_commit(f, t, 2, body, bytes);
}

static void wrong_chain_crc(fixture_t *f) { commit_with(f, 24, 1); }
static void wrong_chain_count(fixture_t *f) { commit_with(f, 16, 1); }
static void wrong_dependency_end(fixture_t *f) { commit_with(f, 160, 8); }

static void missing_dependency(fixture_t *f)
{
    fixture_transaction_t *t = complete_result(f), shortened = *t;
    shortened.groups = 1;
    shortened.group_ids[0] = t->group_ids[1];
    shortened.group_ends[0] = t->group_ends[1];
    uint8_t body[152 + 4 * 16];
    size_t bytes = fixture_commit_body(&shortened, 2, body);
    fixture_append_commit(f, t, 2, body, bytes);
}

static void foreign_owner(fixture_t *f)
{
    fixture_delta_group(f, fixture_transaction(f, 11), ++f->next_group);
}

static void result_over_limit(fixture_t *f)
{
    fixture_transaction_t *t = fixture_transaction(f, 15);
    fixture_result(t, t->limit + 1, 0xb5, (candidate_version_ref_t){{23, 1}, 5, 1});
    fixture_chunk(f, t);
}

static void chunk_with(fixture_t *f, unsigned offset, uint64_t value)
{
    fixture_transaction_t *t = fixture_transaction(f, 11);
    fixture_delta_group(f, t, ++f->next_group);
    uint8_t body[112];
    fixture_chunk_body(t, body);
    put(body + offset, value, 8);
    fixture_append_chunk(f, t, body);
}

static void chunk_offset(fixture_t *f) { chunk_with(f, 96, 0); }
static void result_reference(fixture_t *f) { chunk_with(f, 72, 99); }
static void replayed_commit(fixture_t *f) { fixture_commit(f, fixture_transaction(f, 13), 2); }
static void replayed_abort(fixture_t *f) { fixture_abort(f, fixture_transaction(f, 13)); }
static void duplicate_admission(fixture_t *f) { fixture_admit(f, fixture_transaction(f, 11), 0x11, 70000); }
static void duplicate_nonce(fixture_t *f) { fixture_admit(f, fixture_transaction(f, 16), 0x13, 50); }

static void duplicate_group(fixture_t *f)
{
    f->system = (fixture_system_t){0};
    fixture_system_group(f, 1, 0, 1);
}

static void repeated_free(fixture_t *f) { fixture_system_group(f, ++f->next_group, 7, 1); }
static void floor_over_active(fixture_t *f) { fixture_system_group(f, ++f->next_group, 15, 0); }
static void above_reservation(fixture_t *f) { fixture_begin(f, fixture_transaction(f, 101)); }

static void incomplete_group(fixture_t *f)
{
    uint32_t crc = 0;
    fixture_group_begin(f, fixture_transaction(f, 11), ++f->next_group, &crc);
}

static void run_invalid_case(const invalid_case_t *c)
{
    fixture_t f;
    fixture_create(&f, c->name, true);
    fixture_prefix(&f);
    candidate_semantic_seed_t *seed = capture_seed(f.wal, true), *copy = capture_seed(f.wal, true);
    uint64_t b = f.end_lsn;
    fixture_close(&f);
    fixture_open(&f, c->owner_epoch);
    c->append(&f);
    fixture_close(&f);
    candidate_wal_t *wal = fixture_reader(&f, c->owner_epoch);
    candidate_witness_options_t o = fixture_options(true, NULL);
    candidate_semantic_seed_t *reference = SENTINEL, *seeded = SENTINEL;
    candidate_wal_witness_t *legacy = NULL;
    candidate_wal_status_t full = candidate_semantic_seed_capture(wal, &o, &reference);
    candidate_wal_status_t old = candidate_wal_witness_analyze(wal, &o, &legacy);
    fixture_guard_arm(f.path, b);
    candidate_wal_status_t actual = candidate_semantic_seed_continue(wal, &o, seed, &seeded);
    fixture_guard_disarm();
    printf("INVALID %s seeded=%d full=%d legacy=%d reads=%u forbidden=%u\n", c->name, (int)actual, (int)full,
        (int)old, fixture_guard.reads, fixture_guard.forbidden);
    CHECK(actual == c->expected && full == c->expected && old == c->legacy);
    CHECK(seeded == NULL && reference == NULL && (legacy != NULL) == (old == CANDIDATE_WAL_OK));
    CHECK(fixture_guard.forbidden == 0 && fixture_guard.reads > 0 && fixture_guard.lowest_lsn == b);
    fixture_same_seed(seed, copy);
    candidate_wal_witness_close(&legacy);
    candidate_semantic_seed_close(&seed);
    candidate_semantic_seed_close(&copy);
    OK(candidate_wal_close(&wal));
}

static void framed_invalid_suffix_matches_full_history(void)
{
    const candidate_wal_status_t corrupt = CANDIDATE_WAL_CORRUPT, ok = CANDIDATE_WAL_OK;
    const invalid_case_t cases[] = {
        {"chain-crc", wrong_chain_crc, 7, corrupt, corrupt},
        {"chain-count", wrong_chain_count, 7, corrupt, corrupt},
        {"owner", foreign_owner, 8, corrupt, corrupt},
        {"result-limit", result_over_limit, 7, corrupt, corrupt},
        {"chunk-offset", chunk_offset, 7, corrupt, corrupt},
        {"result-ref", result_reference, 7, corrupt, ok},
        {"dependency-end", wrong_dependency_end, 7, corrupt, corrupt},
        {"dependency-count", missing_dependency, 7, corrupt, corrupt},
        {"replayed-commit", replayed_commit, 7, corrupt, corrupt},
        {"replayed-abort", replayed_abort, 7, corrupt, corrupt},
        {"duplicate-admission", duplicate_admission, 7, corrupt, corrupt},
        {"duplicate-nonce", duplicate_nonce, 7, corrupt, corrupt},
        {"duplicate-group", duplicate_group, 7, corrupt, corrupt},
        {"repeated-free", repeated_free, 7, corrupt, corrupt},
        {"floor-over-active", floor_over_active, 7, corrupt, corrupt},
        {"above-reservation", above_reservation, 7, corrupt, corrupt},
        {"incomplete-group", incomplete_group, 7, CANDIDATE_WAL_STALE, ok},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) run_invalid_case(&cases[i]);
    printf("INVALID_SUFFIX count=%zu\n", sizeof(cases) / sizeof(cases[0]));
}

static void group_boundary_is_required(void)
{
    fixture_t f;
    fixture_create(&f, "boundary", true);
    fixture_prefix(&f);
    fixture_transaction_t *t = fixture_transaction(&f, 11);
    uint64_t id = ++f.next_group;
    uint32_t crc = 0;
    uint64_t pending = fixture_group_begin(&f, t, id, &crc);
    candidate_witness_options_t o = fixture_options(true, NULL);
    candidate_semantic_seed_t *seed = SENTINEL;
    CHECK(candidate_semantic_seed_capture(f.wal, &o, &seed) == CANDIDATE_WAL_STALE && seed == NULL);
    candidate_wal_witness_t *legacy = NULL;
    OK(candidate_wal_witness_analyze(f.wal, &o, &legacy));
    CHECK(candidate_wal_witness_info(legacy).incomplete_group_lsn == pending);
    candidate_wal_witness_close(&legacy);
    fixture_group_finish(&f, t, id, crc);
    seed = capture_seed(f.wal, true);
    fixture_expect(&f, seed);
    CHECK(candidate_wal_witness_find(candidate_semantic_seed_witness(seed), 11)->groups == 2);
    candidate_semantic_seed_close(&seed);
    fixture_close(&f);
    printf("GROUP_BOUNDARY incomplete=STALE complete=OK pending=%llu\n", (unsigned long long)pending);
}

static void refuse_continue(candidate_wal_t *wal, const candidate_witness_options_t *o,
    const candidate_semantic_seed_t *seed, candidate_wal_status_t expected)
{
    candidate_semantic_seed_t *out = SENTINEL;
    CHECK(candidate_semantic_seed_continue(wal, o, seed, &out) == expected && out == NULL);
}

static void invalid_arguments_are_refused(void)
{
    fixture_t f;
    fixture_create(&f, "arguments", true);
    fixture_prefix(&f);
    candidate_semantic_seed_t *seed = capture_seed(f.wal, true), *out = SENTINEL;
    candidate_witness_options_t o = fixture_options(true, NULL), zero = o, raw = fixture_options(false, NULL);
    candidate_witness_options_t other = o;
    memset(zero.uuid, 0, 16);
    other.uuid[0] ^= 0xff;
    candidate_wal_memory_t broken = {NULL, NULL, NULL};
    candidate_witness_options_t no_allocator = fixture_options(true, &broken);
    CHECK(candidate_semantic_seed_capture(NULL, &o, &out) == CANDIDATE_WAL_INVALID && out == NULL);
    out = SENTINEL;
    CHECK(candidate_semantic_seed_capture(f.wal, NULL, &out) == CANDIDATE_WAL_INVALID && out == NULL);
    out = SENTINEL;
    CHECK(candidate_semantic_seed_capture(f.wal, &zero, &out) == CANDIDATE_WAL_INVALID && out == NULL);
    out = SENTINEL;
    CHECK(candidate_semantic_seed_capture(f.wal, &no_allocator, &out) == CANDIDATE_WAL_INVALID && out == NULL);
    CHECK(candidate_semantic_seed_capture(f.wal, &o, NULL) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_semantic_seed_continue(f.wal, &o, seed, NULL) == CANDIDATE_WAL_INVALID);
    refuse_continue(NULL, &o, seed, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, NULL, seed, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, &o, NULL, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, &zero, seed, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, &no_allocator, seed, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, &raw, seed, CANDIDATE_WAL_INVALID);
    refuse_continue(f.wal, &other, seed, CANDIDATE_WAL_UNSUPPORTED);
    candidate_semantic_transaction_t t;
    candidate_semantic_group_t g;
    candidate_semantic_admission_t a;
    CHECK(candidate_semantic_seed_info(NULL).resident_bytes == 0 && candidate_semantic_seed_witness(NULL) == NULL);
    CHECK(!candidate_semantic_seed_transaction_at(NULL, 0, &t) && !candidate_semantic_seed_group_at(NULL, 0, &g));
    CHECK(!candidate_semantic_seed_admission_at(NULL, 0, &a) && !candidate_semantic_seed_transaction_at(seed, 0, NULL));
    CHECK(!candidate_semantic_seed_group_at(seed, 0, NULL) && !candidate_semantic_seed_admission_at(seed, 0, NULL));
    fixture_expect(&f, seed);
    candidate_semantic_seed_close(NULL);
    candidate_semantic_seed_close(&seed);
    CHECK(seed == NULL);
    candidate_semantic_seed_close(&seed);
    fixture_close(&f);
    printf("ARGUMENTS invalid/mode=INVALID uuid=UNSUPPORTED\n");
}

static void empty_seed_replays_whole_history(void)
{
    fixture_t f;
    fixture_create(&f, "empty", true);
    candidate_semantic_seed_t *empty = capture_seed(f.wal, true);
    fixture_expect(&f, empty);
    CHECK(candidate_semantic_seed_info(empty).cut.end_lsn == 64 && candidate_semantic_seed_info(empty).cut.records == 0);
    fixture_mark(&f);
    fixture_prefix(&f);
    fixture_suffix_first(&f);
    fixture_suffix_rest(&f);
    unsigned records = f.read_records;
    uint64_t bytes = f.read_bytes;
    fixture_close(&f);
    candidate_wal_t *wal = fixture_reader(&f, 7);
    candidate_semantic_seed_t *reference = capture_seed(wal, true);
    candidate_semantic_seed_t *replayed = guarded_continue(wal, &f, empty, 64, records, bytes);
    fixture_same_seed(replayed, reference);
    fixture_expect(&f, replayed);
    candidate_semantic_seed_close(&empty);
    candidate_semantic_seed_close(&replayed);
    candidate_semantic_seed_close(&reference);
    OK(candidate_wal_close(&wal));
    printf("EMPTY_SEED records=%u\n", records);
}

static void expect_refused_cut(const fixture_t *f, const candidate_semantic_seed_t *seed, uint64_t floor_lsn,
    candidate_wal_status_t expected)
{
    candidate_wal_t *wal = fixture_reader(f, 7);
    candidate_witness_options_t o = fixture_options(f->has_system, NULL);
    candidate_semantic_seed_t *out = SENTINEL;
    fixture_guard_arm(f->path, floor_lsn);
    candidate_wal_status_t status = candidate_semantic_seed_continue(wal, &o, seed, &out);
    fixture_guard_disarm();
    CHECK(status == expected && out == NULL && fixture_guard.reads == 0 && fixture_guard.forbidden == 0);
    OK(candidate_wal_close(&wal));
}

static void cut_outside_handle_is_refused(void)
{
    fixture_t longer, shorter, segmented;
    fixture_create(&longer, "longer", true);
    fixture_prefix(&longer);
    fixture_suffix_first(&longer);
    candidate_semantic_seed_t *ahead = capture_seed(longer.wal, true);
    fixture_close(&longer);
    fixture_create(&shorter, "shorter", true);
    fixture_prefix(&shorter);
    fixture_close(&shorter);
    expect_refused_cut(&shorter, ahead, 64, CANDIDATE_WAL_CORRUPT);
    fixture_create(&segmented, "segment", false);
    fixture_begin(&segmented, fixture_transaction(&segmented, 1));
    candidate_semantic_seed_t *seed = capture_seed(segmented.wal, false);
    static uint8_t payload[CANDIDATE_WAL_RECORD_BYTES - 96];
    candidate_wal_record_t r = {.type = 12, .payload = payload, .payload_bytes = sizeof(payload)};
    for (unsigned i = 0; i < 4; i++) {
        uint64_t end = 0;
        OK(candidate_wal_append(segmented.wal, &r, &end));
    }
    CHECK(candidate_wal_info(segmented.wal).segments == 2);
    fixture_close(&segmented);
    expect_refused_cut(&segmented, seed, candidate_semantic_seed_info(seed).cut.end_lsn, CANDIDATE_WAL_UNSUPPORTED);
    candidate_semantic_seed_close(&ahead);
    candidate_semantic_seed_close(&seed);
    printf("CUT_REFUSAL beyond-end=CORRUPT other-segment=UNSUPPORTED reads=0\n");
}

static void expect_allocation_failures(candidate_wal_t *wal, bool has_system, const candidate_semantic_seed_t *seed,
    fixture_memory_t *m, unsigned points)
{
    candidate_wal_memory_t memory = fixture_memory(m);
    candidate_witness_options_t o = fixture_options(has_system, &memory);
    for (unsigned fail = 1; fail <= points; fail++) {
        *m = (fixture_memory_t){.fail_at = fail};
        candidate_semantic_seed_t *out = SENTINEL;
        candidate_wal_status_t status = seed == NULL ? candidate_semantic_seed_capture(wal, &o, &out) :
            candidate_semantic_seed_continue(wal, &o, seed, &out);
        CHECK(status == CANDIDATE_WAL_NOMEM && out == NULL && m->live == 0 && m->calls == fail);
    }
    *m = (fixture_memory_t){0};
}

static candidate_semantic_seed_t *allocate_seed(candidate_wal_t *wal, bool has_system,
    const candidate_semantic_seed_t *seed, fixture_memory_t *m, unsigned points)
{
    candidate_wal_memory_t memory = fixture_memory(m);
    candidate_witness_options_t o = fixture_options(has_system, &memory);
    candidate_semantic_seed_t *out = SENTINEL;
    OK(seed == NULL ? candidate_semantic_seed_capture(wal, &o, &out) : candidate_semantic_seed_continue(wal, &o, seed, &out));
    CHECK(m->calls == points && m->live == candidate_semantic_seed_info(out).resident_bytes);
    return out;
}

static void allocation_failures_leave_no_output(bool has_system)
{
    fixture_t f;
    unsigned points = has_system ? 4 : 3;
    fixture_memory_t captured = {0}, continued = {0};
    fixture_create(&f, has_system ? "oom-system" : "oom-raw", has_system);
    fixture_prefix(&f);
    expect_allocation_failures(f.wal, has_system, NULL, &captured, points);
    candidate_semantic_seed_t *seed = allocate_seed(f.wal, has_system, NULL, &captured, points);
    fixture_expect(&f, seed);
    fixture_suffix_first(&f);
    expect_allocation_failures(f.wal, has_system, seed, &continued, points);
    candidate_semantic_seed_t *next = allocate_seed(f.wal, has_system, seed, &continued, points);
    fixture_expect(&f, next);
    candidate_semantic_seed_close(&seed);
    CHECK(captured.live == 0);
    fixture_expect(&f, next);
    candidate_semantic_seed_close(&next);
    CHECK(continued.live == 0);
    fixture_close(&f);
    printf("ALLOCATION mode=%s points=%u capture+continue NOMEM out=NULL live=0\n", has_system ? "system" : "raw",
        points);
}

static void run_all(void)
{
    seeded_suffix_matches_full_history(true);
    seeded_suffix_matches_full_history(false);
    framed_invalid_suffix_matches_full_history();
    group_boundary_is_required();
    invalid_arguments_are_refused();
    empty_seed_replays_whole_history();
    cut_outside_handle_is_refused();
    allocation_failures_leave_no_output(true);
    allocation_failures_leave_no_output(false);
}

static void run_system_differential(void) { seeded_suffix_matches_full_history(true); }
static void run_raw_differential(void) { seeded_suffix_matches_full_history(false); }
static void run_system_allocation(void) { allocation_failures_leave_no_output(true); }
static void run_raw_allocation(void) { allocation_failures_leave_no_output(false); }

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"all", run_all}, {"differential-system", run_system_differential},
        {"differential-raw", run_raw_differential}, {"invalid-suffix", framed_invalid_suffix_matches_full_history},
        {"group-boundary", group_boundary_is_required}, {"arguments", invalid_arguments_are_refused},
        {"empty-seed", empty_seed_replays_whole_history}, {"cut-refusal", cut_outside_handle_is_refused},
        {"allocation-system", run_system_allocation}, {"allocation-raw", run_raw_allocation},
    };
    const char *selected = argc == 2 ? argv[1] : "all";
    void (*run)(void) = NULL;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        if (strcmp(cases[i].name, selected) == 0) run = cases[i].run;
    if (argc > 2 || run == NULL) {
        fprintf(stderr, "usage: %s [all|differential-system|differential-raw|invalid-suffix|group-boundary|"
            "arguments|empty-seed|cut-refusal|allocation-system|allocation-raw]\n", argv[0]);
        return 2;
    }
    char base[128];
    snprintf(base, sizeof(base), "build/task24a1-semantic-%ld", (long)getpid());
    fixture_base = base;
    alarm(300);
    setvbuf(stdout, NULL, _IONBF, 0);
    run();
    printf("PASS semantic seed case=%s checks=%u; in-memory seed only: no durable codec, pruning, page/redo "
        "authority, cold open or log reuse\n", selected, semantic_checks);
    return 0;
}
