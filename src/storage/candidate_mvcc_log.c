#include "candidate_mvcc_internal.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_wal_witness.h"
#include "candidate_buffer_internal.h"
#include "candidate_wal_internal.h"

#define ADMIT_BYTES 64u
#define BEGIN_BYTES 16u
#define CHUNK_BYTES 112u
#define COMMIT_BYTES 152u
#define DEPENDENCY_BYTES 16u
#define ABORT_BYTES 32u
#define GROUP_BEGIN_BYTES 16u
#define GROUP_EVENT_BYTES 40u
#define GROUP_END_BYTES 24u
#define IMAGE_BYTES (32u + CANDIDATE_PAGE_BYTES)
#define PLAN_MAGIC "SLMP"
#define PLAN_HEADER_BYTES 8u
#define PLAN_WRITE_BYTES 20u
#define SEGMENT_HEADER_BYTES 64u
#define BATCH_RECORDS 2u

typedef struct {
    candidate_wal_cursor_t cursor;
    buffer_cut_t intended;
    candidate_wal_record_t records[BATCH_RECORDS];
    uint32_t count;
    buffer_operation_t previous;
} mvcc_batch_t;

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t frame_bytes(size_t payload) { return (96 + payload + 7) & ~(uint64_t)7; }

static uint64_t padded(size_t payload) { return 2 * frame_bytes(payload) + SEGMENT_HEADER_BYTES; }

uint64_t mvcc_begin_bytes(bool needs_reserve)
{
    uint64_t bytes = padded(ADMIT_BYTES) + padded(BEGIN_BYTES);
    if (!needs_reserve) return bytes;
    return bytes + padded(GROUP_BEGIN_BYTES) + 2 * padded(IMAGE_BYTES) + padded(GROUP_EVENT_BYTES) + padded(GROUP_END_BYTES);
}

uint64_t mvcc_group_bytes(uint32_t pages)
{
    return padded(GROUP_BEGIN_BYTES) + (uint64_t)pages * padded(IMAGE_BYTES) + padded(GROUP_END_BYTES);
}

uint64_t mvcc_credit_bytes(uint32_t pages)
{
    uint64_t commit = padded(CHUNK_BYTES) + padded(COMMIT_BYTES + DEPENDENCY_BYTES);
    return mvcc_group_bytes(pages) + commit + padded(ABORT_BYTES);
}

static int compare_writes(const void *left, const void *right)
{
    const mvcc_write_t *a = left, *b = right;
    if (a->key.table_id != b->key.table_id) return a->key.table_id < b->key.table_id ? -1 : 1;
    if (a->key.row_id != b->key.row_id) return a->key.row_id < b->key.row_id ? -1 : 1;
    return 0;
}

static candidate_wal_status_t digest_plan(mvcc_plan_t *plan)
{
    uint8_t bytes[PLAN_HEADER_BYTES + CANDIDATE_MVCC_WRITES * (PLAN_WRITE_BYTES + CANDIDATE_MVCC_PAYLOAD_BYTES)];
    memcpy(bytes,PLAN_MAGIC,4); write_le(bytes + 4,plan->count,4);
    size_t at = PLAN_HEADER_BYTES;
    for (uint32_t i = 0; i < plan->count; i++) {
        const mvcc_write_t *w = &plan->writes[i];
        write_le(bytes + at,w->key.table_id,8); write_le(bytes + at + 8,w->key.row_id,8); write_le(bytes + at + 16,w->size,4);
        if (w->size > 0) memcpy(bytes + at + PLAN_WRITE_BYTES,w->bytes,w->size);
        at += PLAN_WRITE_BYTES + w->size;
    }
    return candidate_digest_sha256(bytes,at,plan->digest);
}

static candidate_wal_status_t copy_write(const candidate_mvcc_t *m, const candidate_mvcc_write_t *source, mvcc_write_t *out)
{
    candidate_mvcc_write_t w = *source;
    if (w.size > CANDIDATE_MVCC_PAYLOAD_BYTES) return CANDIDATE_WAL_LIMIT;
    if (w.size > 0 && w.bytes == NULL) return CANDIDATE_WAL_INVALID;
    const mvcc_row_t *row = mvcc_find_row(m,w.key);
    if (row == NULL) return CANDIDATE_WAL_UNSUPPORTED;
    *out = (mvcc_write_t){.key = w.key,.page = row->page,.size = w.size};
    if (w.size > 0) memcpy(out->bytes,w.bytes,w.size);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_plan_copy(const candidate_mvcc_t *m, const candidate_mvcc_request_t *request, mvcc_plan_t *out)
{
    candidate_mvcc_request_t saved = *request;
    if (saved.count == 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (saved.count > CANDIDATE_MVCC_WRITES) return CANDIDATE_WAL_LIMIT;
    if (saved.writes == NULL) return CANDIDATE_WAL_INVALID;
    memset(out,0,sizeof(*out));
    memcpy(out->nonce,saved.nonce,16); out->count = saved.count;
    for (uint32_t i = 0; i < saved.count; i++) {
        candidate_wal_status_t status = copy_write(m,&saved.writes[i],&out->writes[i]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    qsort(out->writes,out->count,sizeof(out->writes[0]),compare_writes);
    for (uint32_t i = 1; i < out->count; i++)
        if (compare_writes(&out->writes[i - 1],&out->writes[i]) == 0) return CANDIDATE_WAL_INVALID;
    return digest_plan(out);
}

uint32_t mvcc_plan_pages(const candidate_mvcc_t *m, const mvcc_plan_t *plan, uint32_t pages[MVCC_PLAN_PAGES])
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < plan->count; i++) {
        bool is_seen = false;
        for (uint32_t j = 0; j < count; j++) is_seen = is_seen || pages[j] == plan->writes[i].page;
        if (!is_seen) pages[count++] = plan->writes[i].page;
    }
    pages[count++] = m->result_page;
    return count;
}

static candidate_wal_status_t open_batch(candidate_mvcc_t *m, mvcc_batch_t *b)
{
    candidate_buffer_t *pool = m->pool;
    b->previous = pool->operation; b->count = 0;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_ADMIT);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_info_t info = candidate_wal_info(m->recovery->wal);
    if (pool->observed.cut.end_lsn != info.valid_end_lsn || info.highest_owner_epoch > m->owner_epoch) status = CANDIDATE_WAL_STALE;
    b->cursor = candidate_wal_cursor(m->recovery->wal); b->intended = pool->observed.cut;
    if (status != CANDIDATE_WAL_OK) candidate_buffer_leave(pool,b->previous);
    return status;
}

static candidate_wal_status_t close_batch(candidate_mvcc_t *m, const mvcc_batch_t *b, candidate_wal_status_t status)
{
    candidate_buffer_leave(m->pool,b->previous);
    return status;
}

static candidate_wal_status_t bind_chain(candidate_mvcc_t *m, mvcc_active_t *a, mvcc_batch_t *b, uint16_t type,
    const uint8_t *payload, size_t size)
{
    if (b->count == BATCH_RECORDS) return CANDIDATE_WAL_INVALID;
    candidate_wal_record_t *r = &b->records[b->count];
    *r = (candidate_wal_record_t){.type = type,.transaction_id = m->table->outcomes[a->outcome].outcome.transaction_id,
        .previous_lsn = a->chain.last_lsn,.owner_epoch = m->owner_epoch,.payload = payload,.payload_bytes = size};
    candidate_wal_status_t status = candidate_wal_place(m->recovery->wal,&b->cursor,size,&r->lsn);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_cut_add(m->pool,&b->intended,r);
    if (status != CANDIDATE_WAL_OK) return status;
    b->count++;
    if (a->chain.records == 0) a->chain.first_lsn = r->lsn;
    a->chain.last_lsn = r->lsn; a->chain.records++;
    a->chain.crc = candidate_wal_witness_crc(a->chain.crc,r);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t write_batch(candidate_mvcc_t *m, const mvcc_batch_t *b, bool is_terminal, uint64_t *end)
{
    candidate_buffer_t *pool = m->pool;
    candidate_wal_t *wal = m->recovery->wal;
    pool->observed.cut = b->intended;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < b->count; i++) {
        const candidate_wal_record_t *r = &b->records[i];
        status = candidate_wal_append(wal,r,end);
        if (status == CANDIDATE_WAL_OK) status = candidate_wal_check_uuid(wal,m->recovery->uuid);
        if (status == CANDIDATE_WAL_OK && *end != r->lsn + frame_bytes(r->payload_bytes)) status = CANDIDATE_WAL_CORRUPT;
        if (status == CANDIDATE_WAL_OK && candidate_wal_info(wal).highest_owner_epoch != m->owner_epoch) status = CANDIDATE_WAL_STALE;
    }
    if (status == CANDIDATE_WAL_OK && *end != b->intended.end_lsn) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK && is_terminal) status = candidate_wal_force(wal,*end);
    if (status == CANDIDATE_WAL_OK && is_terminal) status = candidate_buffer_verify_cut(pool,&b->intended);
    return status == CANDIDATE_WAL_OK ? status : candidate_buffer_fence(pool,status);
}

static void encode_terminal(const mvcc_chain_t *chain, uint8_t *p)
{
    write_le(p,chain->first_lsn,8); write_le(p + 8,chain->last_lsn,8);
    write_le(p + 16,chain->records,8); write_le(p + 24,chain->crc,4);
}

candidate_wal_status_t mvcc_log_reserve(candidate_mvcc_t *m)
{
    candidate_buffer_reservation_t receipt;
    candidate_wal_status_t status = candidate_buffer_reserve_transactions(m->pool,MVCC_ID_BATCH,&receipt);
    if (status != CANDIDATE_WAL_OK) return status;
    mvcc_state_lock(m);
    m->next_id = receipt.previous_highwater + 1; m->id_end = receipt.next_highwater;
    mvcc_state_unlock(m);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_log_begin(candidate_mvcc_t *m, mvcc_active_t *a, const candidate_outcome_t *outcome)
{
    uint8_t admit[ADMIT_BYTES] = {0}, begin[BEGIN_BYTES] = {0};
    write_le(admit,outcome->epoch,8); memcpy(admit + 8,outcome->nonce,16);
    memcpy(admit + 24,outcome->operation_digest,32); write_le(admit + 56,outcome->declared_result_limit,8);
    write_le(begin + 8,a->snapshot_csn,8);
    mvcc_batch_t b; uint64_t end = 0;
    candidate_wal_status_t status = open_batch(m,&b);
    if (status != CANDIDATE_WAL_OK) return status;
    status = bind_chain(m,a,&b,1,admit,sizeof(admit));
    if (status == CANDIDATE_WAL_OK) status = bind_chain(m,a,&b,2,begin,sizeof(begin));
    if (status == CANDIDATE_WAL_OK) status = write_batch(m,&b,false,&end);
    return close_batch(m,&b,status);
}

static void encode_chunk(const candidate_outcome_t *outcome, uint8_t *p)
{
    write_le(p,outcome->epoch,8); memcpy(p + 8,outcome->nonce,16);
    write_le(p + 24,0,4); write_le(p + 28,1,4); write_le(p + 32,outcome->result_bytes,8);
    memcpy(p + 40,outcome->result_digest,32);
    write_le(p + 72,outcome->result.page.id,8); write_le(p + 80,outcome->result.page.generation,8);
    write_le(p + 88,outcome->result.slot,4); write_le(p + 92,outcome->result.slot_generation,4);
    write_le(p + 96,0,8); write_le(p + 104,outcome->result_bytes,4);
}

static void encode_commit(const mvcc_active_t *a, const candidate_outcome_t *outcome, uint8_t *p)
{
    encode_terminal(&a->chain,p);
    write_le(p + 32,outcome->csn,8); write_le(p + 40,outcome->epoch,8); memcpy(p + 48,outcome->nonce,16);
    memcpy(p + 64,outcome->operation_digest,32); memcpy(p + 96,outcome->result_digest,32);
    write_le(p + 128,outcome->result_bytes,8); write_le(p + 136,outcome->result_chunks,4);
    write_le(p + 148,1,4); write_le(p + 152,a->group_id,8); write_le(p + 160,a->group_end,8);
}

candidate_wal_status_t mvcc_log_commit(candidate_mvcc_t *m, mvcc_active_t *a, candidate_outcome_t *outcome)
{
    uint8_t chunk[CHUNK_BYTES] = {0}, commit[COMMIT_BYTES + DEPENDENCY_BYTES] = {0};
    encode_chunk(outcome,chunk);
    mvcc_batch_t b; uint64_t end = 0;
    candidate_wal_status_t status = open_batch(m,&b);
    if (status != CANDIDATE_WAL_OK) return status;
    status = bind_chain(m,a,&b,9,chunk,sizeof(chunk));
    if (status == CANDIDATE_WAL_OK) {
        encode_commit(a,outcome,commit);
        status = bind_chain(m,a,&b,10,commit,sizeof(commit));
    }
    if (status == CANDIDATE_WAL_OK) status = write_batch(m,&b,true,&end);
    if (status == CANDIDATE_WAL_OK) outcome->terminal_end_lsn = end;
    return close_batch(m,&b,status);
}

candidate_wal_status_t mvcc_log_abort(candidate_mvcc_t *m, mvcc_active_t *a, candidate_outcome_t *outcome)
{
    uint8_t abort_payload[ABORT_BYTES] = {0};
    encode_terminal(&a->chain,abort_payload);
    mvcc_batch_t b; uint64_t end = 0;
    candidate_wal_status_t status = open_batch(m,&b);
    if (status != CANDIDATE_WAL_OK) return status;
    status = bind_chain(m,a,&b,11,abort_payload,sizeof(abort_payload));
    if (status == CANDIDATE_WAL_OK) status = write_batch(m,&b,true,&end);
    if (status == CANDIDATE_WAL_OK) outcome->terminal_end_lsn = end;
    return close_batch(m,&b,status);
}
