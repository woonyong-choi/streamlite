#include "storage/candidate_semantic_seed.h"

#include <stdlib.h>
#include <string.h>

#include "candidate_outcome_witness_internal.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"
#include "candidate_wal_witness_state_internal.h"

struct candidate_semantic_seed {
    candidate_wal_memory_t memory;
    candidate_semantic_cut_t cut;
    candidate_wal_witness_t *witness;
    candidate_admissions_t *admissions;
    bool has_system;
};

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static uint64_t frame_end(const candidate_wal_record_t *r)
{
    return r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7);
}

static candidate_wal_status_t read_options(const candidate_witness_options_t *options,
    candidate_witness_options_t *saved, candidate_wal_memory_t *memory)
{
    *saved = *options;
    *memory = (candidate_wal_memory_t){NULL, allocate_default, release_default};
    if (saved->memory != NULL) *memory = *saved->memory;
    if (is_zero(saved->uuid, 16) || memory->allocate == NULL || memory->release == NULL) return CANDIDATE_WAL_INVALID;
    saved->memory = memory;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t create_seed(const candidate_witness_options_t *options, candidate_semantic_seed_t **out)
{
    const candidate_wal_memory_t *memory = options->memory;
    candidate_semantic_seed_t *seed = memory->allocate(memory->context, sizeof(*seed));
    if (seed == NULL) return CANDIDATE_WAL_NOMEM;
    *seed = (candidate_semantic_seed_t){.memory = *memory, .has_system = options->enable_system_groups};
    memcpy(seed->cut.uuid, options->uuid, 16);
    *out = seed;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t seal(candidate_semantic_seed_t *seed)
{
    candidate_witness_info_t info = candidate_wal_witness_info(seed->witness);
    if (info.incomplete_group_lsn != 0) return CANDIDATE_WAL_STALE;
    seed->cut.end_lsn = info.transport.valid_end_lsn;
    seed->cut.records = info.transport.records;
    seed->cut.highest_owner_epoch = info.transport.highest_owner_epoch;
    seed->cut.segments = info.transport.segments;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_semantic_seed_capture(candidate_wal_t *wal,
    const candidate_witness_options_t *options, candidate_semantic_seed_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || options == NULL) return CANDIDATE_WAL_INVALID;
    candidate_witness_options_t saved;
    candidate_wal_memory_t memory;
    candidate_wal_status_t status = read_options(options, &saved, &memory);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_check_uuid(wal, saved.uuid);
    candidate_semantic_seed_t *seed = NULL;
    if (status == CANDIDATE_WAL_OK) status = create_seed(&saved, &seed);
    if (status == CANDIDATE_WAL_OK) status = candidate_admissions_create(&memory, NULL, &seed->admissions);
    if (status == CANDIDATE_WAL_OK) {
        status = candidate_witness_analyze_observed(wal, &saved, NULL, candidate_admissions_observe,
            seed->admissions, &seed->witness);
        candidate_wal_status_t observed = candidate_admissions_status(seed->admissions);
        if (status == CANDIDATE_WAL_STOPPED && observed != CANDIDATE_WAL_OK) status = observed;
    }
    if (status == CANDIDATE_WAL_OK) status = seal(seed);
    if (status != CANDIDATE_WAL_OK) { candidate_semantic_seed_close(&seed); return status; }
    *out = seed;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t locate_suffix(const candidate_semantic_cut_t *cut,
    const candidate_wal_info_t *transport, uint64_t *start)
{
    if (cut->end_lsn > transport->valid_end_lsn || cut->records > transport->records) return CANDIDATE_WAL_CORRUPT;
    *start = cut->end_lsn;
    if (*start == transport->valid_end_lsn) return CANDIDATE_WAL_OK;
    if (*start % CANDIDATE_WAL_SEGMENT_BYTES == 0) *start += CANDIDATE_WAL_HEADER_BYTES;
    if (*start / CANDIDATE_WAL_SEGMENT_BYTES + 1 != transport->segments) return CANDIDATE_WAL_UNSUPPORTED;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t apply(candidate_semantic_seed_t *seed, const candidate_wal_record_t *record)
{
    if (!candidate_admissions_observe(seed->admissions, record)) return candidate_admissions_status(seed->admissions);
    return candidate_witness_apply(seed->witness, record);
}

static candidate_wal_status_t consume(candidate_wal_t *wal, candidate_semantic_seed_t *next,
    const candidate_semantic_cut_t *cut, const candidate_wal_info_t *transport, uint64_t start)
{
    uint64_t at = start, records = cut->records, highest = cut->highest_owner_epoch;
    while (at < transport->valid_end_lsn) {
        if (transport->valid_end_lsn - at < 96) return CANDIDATE_WAL_CORRUPT;
        candidate_wal_record_t record;
        candidate_wal_status_t status = candidate_wal_read_at(wal, at, &record);
        if (status != CANDIDATE_WAL_OK) return status;
        if (record.owner_epoch < highest) return CANDIDATE_WAL_CORRUPT;
        status = apply(next, &record);
        if (status != CANDIDATE_WAL_OK) return status;
        highest = record.owner_epoch;
        records++;
        at = frame_end(&record);
    }
    bool is_complete = at == transport->valid_end_lsn && records == transport->records;
    return is_complete && highest == transport->highest_owner_epoch ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_semantic_seed_continue(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_semantic_seed_t *seed,
    candidate_semantic_seed_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || options == NULL || seed == NULL) return CANDIDATE_WAL_INVALID;
    candidate_witness_options_t saved;
    candidate_wal_memory_t memory;
    candidate_wal_status_t status = read_options(options, &saved, &memory);
    if (status != CANDIDATE_WAL_OK) return status;
    if (saved.enable_system_groups != seed->has_system) return CANDIDATE_WAL_INVALID;
    if (memcmp(saved.uuid, seed->cut.uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    status = candidate_wal_check_uuid(wal, saved.uuid);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_info_t transport = candidate_wal_info(wal);
    uint64_t start;
    status = locate_suffix(&seed->cut, &transport, &start);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_semantic_seed_t *next = NULL;
    status = create_seed(&saved, &next);
    if (status == CANDIDATE_WAL_OK) status = candidate_witness_clone(seed->witness, &memory, &next->witness);
    if (status == CANDIDATE_WAL_OK) status = candidate_admissions_create(&memory, seed->admissions, &next->admissions);
    if (status == CANDIDATE_WAL_OK) status = consume(wal, next, &seed->cut, &transport, start);
    if (status == CANDIDATE_WAL_OK) {
        candidate_witness_settle(next->witness, &transport);
        status = seal(next);
    }
    if (status != CANDIDATE_WAL_OK) { candidate_semantic_seed_close(&next); return status; }
    *out = next;
    return CANDIDATE_WAL_OK;
}

candidate_semantic_seed_info_t candidate_semantic_seed_info(const candidate_semantic_seed_t *seed)
{
    if (seed == NULL) return (candidate_semantic_seed_info_t){0};
    size_t bytes = sizeof(*seed) + candidate_wal_witness_info(seed->witness).resident_bytes +
        candidate_admissions_resident_bytes(seed->admissions);
    return (candidate_semantic_seed_info_t){seed->cut, candidate_admissions_count(seed->admissions), bytes,
        seed->has_system};
}

const candidate_wal_witness_t *candidate_semantic_seed_witness(const candidate_semantic_seed_t *seed)
{
    return seed == NULL ? NULL : seed->witness;
}

bool candidate_semantic_seed_transaction_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_transaction_t *out)
{
    return seed != NULL && candidate_witness_transaction_at(seed->witness, index, out);
}

bool candidate_semantic_seed_group_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_group_t *out)
{
    return seed != NULL && candidate_witness_group_at(seed->witness, index, out);
}

bool candidate_semantic_seed_admission_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_admission_t *out)
{
    return seed != NULL && candidate_admissions_at(seed->admissions, index, out);
}

void candidate_semantic_seed_close(candidate_semantic_seed_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_semantic_seed_t *seed = *out;
    *out = NULL;
    candidate_wal_witness_close(&seed->witness);
    candidate_admissions_close(&seed->admissions);
    candidate_wal_memory_t memory = seed->memory;
    memory.release(memory.context, seed, sizeof(*seed));
}
