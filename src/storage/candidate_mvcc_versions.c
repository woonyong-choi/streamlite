#include "candidate_mvcc_internal.h"

#include <string.h>

#define RESPONSE_MAGIC "SLMR"
#define RESPONSE_FORMAT 1u
#define RESPONSE_AFFECTED_ROWS 1u

typedef struct {
    candidate_version_ref_t ref;
    candidate_page_version_t version;
    uint64_t csn;
} mvcc_hit_t;

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

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static const uint8_t *uuid_of(const candidate_mvcc_t *m) { return m->recovery->uuid; }

static candidate_wal_status_t copy_ref(candidate_mvcc_t *m, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
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

candidate_wal_status_t mvcc_page_copy(candidate_mvcc_t *m, uint32_t page, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    return copy_ref(m,m->pages[page],out);
}

candidate_wal_status_t mvcc_page_usage(const candidate_mvcc_t *m, const uint8_t *bytes, mvcc_usage_t *out)
{
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 1) return CANDIDATE_WAL_UNSUPPORTED;
    *out = (mvcc_usage_t){info.slots,info.body_bytes};
    return CANDIDATE_WAL_OK;
}

static bool is_key(const candidate_page_version_t *v, candidate_mvcc_key_t key)
{
    return v->table_id == key.table_id && v->row_id == key.row_id;
}

static bool is_visible(const candidate_mvcc_t *m, const candidate_page_version_t *v, mvcc_view_t view, uint64_t *csn)
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

static candidate_wal_status_t check_previous(const candidate_mvcc_t *m, const uint8_t *bytes, const mvcc_hit_t *hit)
{
    candidate_version_ref_t previous = hit->version.previous;
    if (previous.page.id == 0) return CANDIDATE_WAL_OK;
    if (!same_page(previous.page,hit->ref.page) || previous.slot >= hit->ref.slot) return CANDIDATE_WAL_CORRUPT;
    candidate_page_version_t older;
    candidate_wal_status_t status = candidate_page_get_version(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,previous,&older);
    if (status != CANDIDATE_WAL_OK) return status;
    return older.table_id == hit->version.table_id && older.row_id == hit->version.row_id ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t scan_key(const candidate_mvcc_t *m, const uint8_t *bytes, candidate_mvcc_key_t key,
    mvcc_view_t view, mvcc_hit_t *out)
{
    mvcc_usage_t usage;
    candidate_wal_status_t status = mvcc_page_usage(m,bytes,&usage);
    *out = (mvcc_hit_t){0};
    for (uint32_t i = usage.slots; status == CANDIDATE_WAL_OK && i > 0; i--) {
        mvcc_hit_t hit = {0};
        status = candidate_page_version_at(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,i - 1,&hit.ref,&hit.version);
        if (status != CANDIDATE_WAL_OK || !is_key(&hit.version,key)) continue;
        if (!is_visible(m,&hit.version,view,&hit.csn)) continue;
        status = check_previous(m,bytes,&hit);
        if (status == CANDIDATE_WAL_OK) *out = hit;
        return status;
    }
    return status;
}

candidate_wal_status_t mvcc_find_visible(const candidate_mvcc_t *m, const uint8_t *bytes, candidate_mvcc_key_t key,
    mvcc_view_t view, candidate_mvcc_value_t *out)
{
    mvcc_hit_t hit = {0};
    candidate_wal_status_t status = scan_key(m,bytes,key,view,&hit);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = (candidate_mvcc_value_t){0};
    if (hit.ref.page.id == 0 || hit.version.is_tombstone) return CANDIDATE_WAL_OK;
    if (hit.version.overflow.id != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (hit.version.payload_bytes > CANDIDATE_MVCC_PAYLOAD_BYTES) return CANDIDATE_WAL_LIMIT;
    *out = (candidate_mvcc_value_t){.is_found = true,.transaction_id = hit.version.transaction_id,.csn = hit.csn,
        .ref = hit.ref,.size = (uint32_t)hit.version.payload_bytes};
    if (out->size > 0) memcpy(out->bytes,hit.version.inline_bytes,out->size);
    return CANDIDATE_WAL_OK;
}

static bool is_pending_other(const candidate_mvcc_t *m, const candidate_page_version_t *v, uint64_t own_transaction)
{
    if (v->transaction_id == own_transaction) return false;
    const mvcc_outcome_t *o = mvcc_find_outcome(m->table,v->transaction_id);
    return o != NULL && (o->state == CANDIDATE_MVCC_ACTIVE || o->state == CANDIDATE_MVCC_UNKNOWN);
}

candidate_wal_status_t mvcc_find_head(const candidate_mvcc_t *m, const uint8_t *bytes, candidate_mvcc_key_t key,
    uint64_t own_transaction, candidate_version_ref_t *ref, uint64_t *csn)
{
    mvcc_usage_t usage;
    candidate_wal_status_t status = mvcc_page_usage(m,bytes,&usage);
    *ref = (candidate_version_ref_t){0}; *csn = 0;
    mvcc_view_t committed = {.snapshot_csn = UINT64_MAX};
    for (uint32_t i = usage.slots; status == CANDIDATE_WAL_OK && i > 0; i--) {
        mvcc_hit_t hit = {0};
        status = candidate_page_version_at(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,i - 1,&hit.ref,&hit.version);
        if (status != CANDIDATE_WAL_OK || !is_key(&hit.version,key)) continue;
        if (is_pending_other(m,&hit.version,own_transaction)) return CANDIDATE_WAL_BUSY;
        if (!is_visible(m,&hit.version,committed,&hit.csn)) continue;
        status = check_previous(m,bytes,&hit);
        if (status == CANDIDATE_WAL_OK) { *ref = hit.ref; *csn = hit.csn; }
        return status;
    }
    return status;
}

candidate_wal_status_t mvcc_check_page(const candidate_mvcc_t *m, const uint8_t *bytes)
{
    mvcc_usage_t usage;
    candidate_wal_status_t status = mvcc_page_usage(m,bytes,&usage);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < usage.slots; i++) {
        mvcc_hit_t hit = {0};
        status = candidate_page_version_at(uuid_of(m),bytes,CANDIDATE_PAGE_BYTES,i,&hit.ref,&hit.version);
        if (status != CANDIDATE_WAL_OK) return status;
        bool is_known = mvcc_find_outcome(m->table,hit.version.transaction_id) != NULL || hit.version.frozen_csn != 0;
        if (!is_known) return CANDIDATE_WAL_UNSUPPORTED;
    }
    return status;
}

void mvcc_encode_response(uint64_t transaction_id, uint32_t affected_rows, uint8_t out[CANDIDATE_MVCC_RESPONSE_BYTES])
{
    memcpy(out,RESPONSE_MAGIC,4);
    write_le(out + 4,RESPONSE_FORMAT,2); write_le(out + 6,RESPONSE_AFFECTED_ROWS,2);
    write_le(out + 8,affected_rows,8); write_le(out + 16,transaction_id,8);
}

static candidate_wal_status_t add_version(candidate_page_version_t *versions, uint32_t *count,
    const candidate_page_version_t *version, candidate_page_ref_t page, candidate_version_ref_t *ref)
{
    if (*count == CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_LIMIT;
    versions[*count] = *version;
    *ref = (candidate_version_ref_t){page,*count,version->slot_generation};
    (*count)++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_writes(const candidate_mvcc_t *m, const uint8_t *base, uint32_t page,
    mvcc_active_t *active, candidate_page_version_t *versions, uint32_t *count)
{
    uint64_t own = m->table->outcomes[active->outcome].outcome.transaction_id;
    for (uint32_t i = 0; i < active->plan.count; i++) {
        const mvcc_write_t *w = &active->plan.writes[i];
        if (w->page != page) continue;
        candidate_version_ref_t head; uint64_t csn;
        candidate_wal_status_t status = mvcc_find_head(m,base,w->key,own,&head,&csn);
        if (status != CANDIDATE_WAL_OK) return status;
        if (csn > active->snapshot_csn) return CANDIDATE_WAL_STALE;
        candidate_page_version_t v = {.transaction_id = own,.previous = head,.table_id = w->key.table_id,
            .row_id = w->key.row_id,.payload_bytes = w->size,.slot_generation = 1,.inline_bytes = w->bytes};
        status = add_version(versions,count,&v,m->pages[page],&active->versions[i]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t mvcc_merge_page(const candidate_mvcc_t *m, const uint8_t *base, uint32_t page,
    mvcc_active_t *active, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS];
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode(uuid_of(m),base,CANDIDATE_PAGE_BYTES,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 1 || !same_page(info.location.ref,m->pages[page])) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < info.slots; i++) {
        candidate_version_ref_t ref;
        status = candidate_page_version_at(uuid_of(m),base,CANDIDATE_PAGE_BYTES,i,&ref,&versions[i]);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    uint32_t count = info.slots;
    status = add_writes(m,base,page,active,versions,&count);
    uint8_t response[CANDIDATE_MVCC_RESPONSE_BYTES];
    if (status == CANDIDATE_WAL_OK && page == m->result_page) {
        uint64_t own = m->table->outcomes[active->outcome].outcome.transaction_id;
        mvcc_encode_response(own,active->plan.count,response);
        candidate_page_version_t v = {.transaction_id = own,.table_id = m->result_table_id,.row_id = own,
            .payload_bytes = sizeof(response),.slot_generation = 1,.inline_bytes = response};
        status = add_version(versions,&count,&v,m->pages[page],&active->result);
    }
    if (status != CANDIDATE_WAL_OK) return status;
    return candidate_page_encode_versions(&info.location,versions,count,out,CANDIDATE_PAGE_BYTES);
}

static candidate_wal_status_t load_result(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    return copy_ref(context,ref,out);
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
