#include "candidate_mvcc_internal.h"

#include <string.h>

#define RESPONSE_MAGIC "SLMR"
#define RESPONSE_FORMAT 1u
#define RESPONSE_AFFECTED_ROWS 1u

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static const uint8_t *uuid_of(const candidate_mvcc_t *m) { return m->recovery->uuid; }

candidate_wal_status_t mvcc_copy_page(candidate_mvcc_t *m, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_buffer_lease_t lease;
    candidate_wal_status_t status = candidate_buffer_pin(m->pool,ref,&lease);
    if (status != CANDIDATE_WAL_OK) return status;
    uint64_t lsn = 0;
    status = candidate_buffer_copy(m->pool,&lease,out,&lsn);
    candidate_wal_status_t released = candidate_buffer_unpin(m->pool,&lease);
    if (status == CANDIDATE_WAL_OK) status = released;
    if (status == CANDIDATE_WAL_OK && read_le(out + 40,8) != lsn) status = CANDIDATE_WAL_CORRUPT;
    return status;
}

candidate_wal_status_t mvcc_page_usage(const candidate_mvcc_t *m, const uint8_t *bytes, mvcc_usage_t *out)
{
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 1) return CANDIDATE_WAL_CORRUPT;
    *out = (mvcc_usage_t){info.slots,info.body_bytes};
    return CANDIDATE_WAL_OK;
}

bool mvcc_is_visible(const candidate_mvcc_t *m, const candidate_page_version_t *v, mvcc_view_t view, uint64_t *csn)
{
    *csn = 0;
    if (view.transaction_id != 0 && v->transaction_id == view.transaction_id) return true;
    const mvcc_outcome_t *o = mvcc_find_outcome(m->table,v->transaction_id);
    if (o == NULL) {
        *csn = v->frozen_csn;
        return v->frozen_csn != 0 && v->frozen_csn <= view.snapshot_csn;
    }
    if (o->state != CANDIDATE_MVCC_COMMITTED) return false;
    *csn = o->outcome.csn;
    return o->outcome.csn <= view.snapshot_csn;
}

bool mvcc_is_pending_other(const candidate_mvcc_t *m, const candidate_page_version_t *v, uint64_t own_transaction)
{
    if (v->transaction_id == own_transaction) return false;
    const mvcc_outcome_t *o = mvcc_find_outcome(m->table,v->transaction_id);
    return o != NULL && (o->state == CANDIDATE_MVCC_ACTIVE || o->state == CANDIDATE_MVCC_UNKNOWN);
}

void mvcc_encode_response(uint64_t transaction_id, uint32_t affected_rows, uint8_t out[CANDIDATE_MVCC_RESPONSE_BYTES])
{
    memcpy(out,RESPONSE_MAGIC,4);
    write_le(out + 4,RESPONSE_FORMAT,2); write_le(out + 6,RESPONSE_AFFECTED_ROWS,2);
    write_le(out + 8,affected_rows,8); write_le(out + 16,transaction_id,8);
}

static candidate_wal_status_t load_result(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    return mvcc_copy_page(context,ref,out);
}

static void fill_response(const candidate_outcome_t *outcome, const uint8_t *bytes, size_t size,
    candidate_mvcc_response_t *out)
{
    out->result = outcome->result; out->size = (uint32_t)size;
    if (size > 0) memcpy(out->bytes,bytes,size);
    bool is_native = size == CANDIDATE_MVCC_RESPONSE_BYTES && memcmp(bytes,RESPONSE_MAGIC,4) == 0 &&
        read_le(bytes + 4,2) == RESPONSE_FORMAT && read_le(bytes + 6,2) == RESPONSE_AFFECTED_ROWS;
    if (is_native && read_le(bytes + 16,8) == outcome->transaction_id) out->affected_rows = read_le(bytes + 8,8);
}

candidate_wal_status_t mvcc_read_response(candidate_mvcc_t *m, const candidate_outcome_t *outcome,
    candidate_mvcc_response_t *out)
{
    if (outcome->result.page.id == 0) return CANDIDATE_WAL_OK;
    if (outcome->result_bytes > CANDIDATE_MVCC_RESPONSE_BYTES) return CANDIDATE_WAL_LIMIT;
    candidate_result_read_t request = {.ref = outcome->result,.transaction_id = outcome->transaction_id,
        .result_bytes = outcome->result_bytes,.load = load_result,.context = m,.memory = &m->memory};
    memcpy(request.uuid,uuid_of(m),16); memcpy(request.digest,outcome->result_digest,32);
    candidate_page_result_t *body = NULL;
    candidate_wal_status_t status = candidate_page_read_result(&request,&body);
    if (status == CANDIDATE_WAL_OK) fill_response(outcome,candidate_page_result_bytes(body),candidate_page_result_size(body),out);
    candidate_page_result_close(&body);
    return status;
}
