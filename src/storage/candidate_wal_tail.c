#include "candidate_wal_tail_internal.h"

#include <string.h>

#include "storage/candidate_digest.h"

typedef struct {
    candidate_wal_tail_proof_t proof;
    uint8_t digest[32];
    uint64_t group_id, transaction_id;
    candidate_wal_status_t status;
    bool found;
} tail_t;

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t read_le(const uint8_t *p)
{
    uint64_t value = 0; for (unsigned i = 0; i < 8; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

candidate_wal_status_t candidate_wal_tail_hash(uint8_t digest[32], const candidate_wal_record_t *r)
{
    uint8_t bytes[112] = {0}; memcpy(bytes,digest,32);
    write_le(bytes + 32,r->type,2); write_le(bytes + 40,r->lsn,8);
    write_le(bytes + 48,r->transaction_id,8); write_le(bytes + 56,r->previous_lsn,8);
    write_le(bytes + 64,r->owner_epoch,8); write_le(bytes + 72,r->payload_bytes,8);
    candidate_wal_status_t status = candidate_digest_sha256(r->payload,r->payload_bytes,bytes + 80);
    if (status == CANDIDATE_WAL_OK) status = candidate_digest_sha256(bytes,sizeof(bytes),digest);
    return status;
}

candidate_wal_status_t candidate_wal_view_add(candidate_wal_view_t *v, const candidate_wal_record_t *r)
{
    if (r->type == 3) { memcpy(v->group_digest,v->digest,32); v->group_lsn = r->lsn; }
    return candidate_wal_tail_hash(v->digest,r);
}

bool candidate_wal_view_same(const candidate_wal_view_t *a, const candidate_wal_view_t *b)
{
    bool same = memcmp(a->digest,b->digest,32) == 0 && a->end_lsn == b->end_lsn && a->segments == b->segments;
    return same && a->has_tail == b->has_tail && a->tail_bytes == b->tail_bytes;
}

static bool visit_tail(void *context, const candidate_wal_record_t *r)
{
    tail_t *t = context;
    t->status = candidate_wal_tail_hash(t->digest,r);
    if (t->status != CANDIDATE_WAL_OK) return false;
    if (r->lsn < t->proof.begin_lsn) return true;
    if (r->lsn == t->proof.begin_lsn && r->type == 3 && r->payload_bytes == 16) {
        t->found = true; t->group_id = read_le(r->payload); t->transaction_id = r->transaction_id; return true;
    }
    bool is_member = (r->type >= 4 && r->type <= 7) || r->type == 15 || r->type == 16;
    if (!t->found || !is_member || r->payload_bytes < 8 ||
        r->transaction_id != t->transaction_id || read_le(r->payload) != t->group_id) {
        t->status = CANDIDATE_WAL_CORRUPT; return false;
    }
    return true;
}

candidate_wal_status_t candidate_wal_tail_check(candidate_wal_t *wal, const candidate_wal_tail_proof_t *proof)
{
    if (proof == NULL || proof->begin_lsn < 64 || proof->begin_lsn % 8 != 0 ||
        proof->begin_lsn >= proof->end_lsn) return CANDIDATE_WAL_INVALID;
    tail_t t = {.proof = *proof}; candidate_wal_info_t info;
    candidate_wal_status_t status = candidate_wal_scan(wal,visit_tail,&t,&info);
    if (status == CANDIDATE_WAL_STOPPED && t.status != CANDIDATE_WAL_OK) return t.status;
    if (status != CANDIDATE_WAL_OK) return status;
    if (!t.found || info.valid_end_lsn != proof->end_lsn || info.tail_bytes != proof->tail_bytes ||
        info.has_tail != proof->has_tail || memcmp(t.digest,proof->digest,32) != 0) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}
