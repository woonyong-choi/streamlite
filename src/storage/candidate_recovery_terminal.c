#include "candidate_recovery_internal.h"

#include <string.h>

#include "candidate_wal_witness_internal.h"
#include "candidate_wal_tail_internal.h"

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

candidate_wal_status_t candidate_terminal_prepare(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **out)
{
    candidate_witness_options_t request = {.memory = options->memory,.enable_system_groups = true};
    memcpy(request.uuid,options->uuid,16);
    candidate_wal_status_t status = candidate_wal_witness_analyze_checkpoint(wal,&request,options->anchor,out);
    if (status != CANDIDATE_WAL_OK) return status;
    if (options->allow_group_tail || candidate_wal_witness_info(*out).incomplete_group_lsn == 0) return CANDIDATE_WAL_OK;
    candidate_wal_witness_close(out); return CANDIDATE_WAL_STALE;
}

static candidate_wal_status_t append_aborts(candidate_wal_t *wal, const candidate_wal_witness_t *history)
{
    candidate_witness_info_t info = candidate_wal_witness_info(history);
    for (unsigned i = 0; i < info.transactions; i++) {
        candidate_checkpoint_transaction_t position;
        if (!candidate_witness_checkpoint_at(history,i,&position)) return CANDIDATE_WAL_CORRUPT;
        const candidate_witness_transaction_t *transaction = candidate_wal_witness_find(history,position.id);
        if (transaction == NULL) return CANDIDATE_WAL_CORRUPT;
        if (transaction->state != CANDIDATE_WITNESS_UNFINISHED) continue;
        uint8_t body[32] = {0}; write_le(body,transaction->first_lsn,8); write_le(body + 8,transaction->last_lsn,8);
        write_le(body + 16,transaction->records,8); write_le(body + 24,transaction->chain_crc,4);
        candidate_wal_record_t record = {.type = 11,.transaction_id = transaction->transaction_id,
            .previous_lsn = transaction->last_lsn,.payload = body,.payload_bytes = sizeof(body)};
        uint64_t end; candidate_wal_status_t status = candidate_wal_append(wal,&record,&end);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

typedef struct { uint8_t digest[32]; candidate_wal_status_t status; } proof_hash_t;

static bool observe_hash(void *context, const candidate_wal_record_t *record)
{
    proof_hash_t *hash = context;
    hash->status = candidate_wal_tail_hash(hash->digest,record); return hash->status == CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_terminal_repair_wal(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_wal_info_t *out)
{
    candidate_wal_witness_close(history);
    candidate_witness_options_t request = {.memory = options->memory,.enable_system_groups = true};
    memcpy(request.uuid,options->uuid,16); proof_hash_t hash = {0};
    candidate_wal_status_t status = candidate_witness_analyze_observed(wal,&request,options->anchor,observe_hash,&hash,history);
    if (status == CANDIDATE_WAL_STOPPED && hash.status != CANDIDATE_WAL_OK) status = hash.status;
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_witness_info_t before = candidate_wal_witness_info(*history);
    candidate_recovery_wal_info_t info = {0};
    if (before.incomplete_group_lsn != 0) {
        if (options->owner_epoch <= options->anchor->owner_epoch) return CANDIDATE_WAL_STALE;
        if (before.incomplete_group_lsn < options->anchor->checkpoint_end) return CANDIDATE_WAL_CORRUPT;
        candidate_wal_tail_proof_t proof = {.begin_lsn = before.incomplete_group_lsn,
            .end_lsn = before.transport.valid_end_lsn,.tail_bytes = before.transport.tail_bytes,.has_tail = before.transport.has_tail};
        memcpy(proof.digest,hash.digest,32);
        status = candidate_wal_discard_group_tail(wal,&proof);
        if (status != CANDIDATE_WAL_OK) return status;
        info.discarded_group_lsn = proof.begin_lsn; info.discarded_valid_end_lsn = proof.end_lsn;
        info.discarded_tail_bytes = proof.tail_bytes;
        info.removed_segments = before.transport.segments - candidate_wal_info(wal).segments;
        candidate_wal_witness_close(history);
        candidate_terminal_options_t strict = *options; strict.allow_group_tail = false;
        status = candidate_terminal_prepare(wal,&strict,history);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_witness_info_t after = candidate_wal_witness_info(*history);
        if (after.commits != before.commits || after.aborts != before.aborts) return CANDIDATE_WAL_CORRUPT;
    }
    status = candidate_terminal_resolve(wal,options,history,&info.terminals);
    if (status == CANDIDATE_WAL_OK) *out = info;
    return status;
}

candidate_wal_status_t candidate_terminal_resolve(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_terminal_info_t *out)
{
    candidate_witness_info_t before = candidate_wal_witness_info(*history);
    if (before.unfinished != 0 || before.transport.has_tail)
        if (options->owner_epoch <= options->anchor->owner_epoch) return CANDIDATE_WAL_STALE;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (before.transport.has_tail) status = candidate_wal_repair_tail(wal);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn);
    if (status == CANDIDATE_WAL_OK) status = append_aborts(wal,*history);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_witness_close(history);
    status = candidate_terminal_prepare(wal,options,history);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_witness_info_t after = candidate_wal_witness_info(*history);
    if (after.unfinished != 0 || after.incomplete_group_lsn != 0 || after.transport.has_tail) return CANDIDATE_WAL_STALE;
    if (after.commits != before.commits || after.aborts != before.aborts + before.unfinished) return CANDIDATE_WAL_CORRUPT;
    if (candidate_wal_info(wal).durable_end_lsn < after.transport.valid_end_lsn) return CANDIDATE_WAL_STALE;
    *out = (candidate_recovery_terminal_info_t){.checkpoint_id = options->anchor->checkpoint_id,
        .before_end_lsn = before.transport.valid_end_lsn,.after_end_lsn = after.transport.valid_end_lsn,
        .repaired_tail_bytes = before.transport.tail_bytes,.appended_aborts = before.unfinished,
        .existing_commits = before.commits,.existing_aborts = before.aborts};
    return CANDIDATE_WAL_OK;
}
