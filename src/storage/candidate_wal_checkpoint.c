#include "candidate_wal_witness_internal.h"

#include <string.h>

#include "candidate_checkpoint_internal.h"

typedef struct {
    candidate_wal_witness_t *witness;
    const candidate_checkpoint_t *checkpoint;
    candidate_wal_visitor_t visitor;
    candidate_wal_visitor_t observer;
    void *observer_context;
    candidate_checkpoint_info_t info;
    candidate_wal_status_t status;
    uint64_t group_begin, last_checkpoint, highest_owner;
    candidate_page_ref_t pages[CANDIDATE_WITNESS_PAGES];
    unsigned pages_count, chunks;
    bool dirty[CANDIDATE_CHECKPOINT_DIRTY], has_begin, has_end;
    uint8_t digest[32];
} scan_t;

static const candidate_checkpoint_transaction_t *find_transaction(const scan_t *s, uint64_t id)
{
    unsigned lo = 0, hi = s->info.summary.transaction_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const candidate_checkpoint_transaction_t *t = candidate_checkpoint_transaction_at(s->checkpoint, mid);
        if (t->id < id) lo = mid + 1; else hi = mid;
    }
    const candidate_checkpoint_transaction_t *t = candidate_checkpoint_transaction_at(s->checkpoint, lo);
    return t != NULL && t->id == id ? t : NULL;
}

static bool is_same_transaction(const candidate_checkpoint_transaction_t *a, const candidate_checkpoint_transaction_t *b)
{
    if (a->id != b->id || a->first_lsn != b->first_lsn || a->last_lsn != b->last_lsn) return false;
    if (a->terminal_end_lsn != b->terminal_end_lsn || a->csn != b->csn) return false;
    return a->state == b->state && a->owner_epoch == b->owner_epoch;
}

static candidate_wal_status_t check_cut(scan_t *s)
{
    const candidate_control_state_t *a = &s->info.summary.anchor;
    candidate_witness_info_t observed = candidate_wal_witness_info(s->witness);
    if (a->visible_csn > observed.highest_csn || s->highest_owner > a->owner_epoch) return CANDIDATE_WAL_CORRUPT;
    unsigned matched = 0;
    for (unsigned i = 0; i < observed.transactions; i++) {
        candidate_checkpoint_transaction_t actual;
        if (!candidate_witness_checkpoint_at(s->witness, i, &actual)) return CANDIDATE_WAL_CORRUPT;
        if (actual.id > a->transaction_highwater) return CANDIDATE_WAL_CORRUPT;
        const candidate_checkpoint_transaction_t *declared = find_transaction(s, actual.id);
        if (declared == NULL) {
            if (actual.state == CANDIDATE_CHECKPOINT_ACTIVE) return CANDIDATE_WAL_CORRUPT;
        } else {
            if (!is_same_transaction(&actual, declared)) return CANDIDATE_WAL_CORRUPT;
            matched++;
        }
    }
    if (matched != s->info.summary.transaction_count) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < s->info.summary.dirty_count; i++) if (!s->dirty[i]) return CANDIDATE_WAL_CORRUPT;
    return candidate_witness_bind_checkpoint(s->witness,a);
}

static void mark_dirty(scan_t *s)
{
    if (s->has_begin) return;
    for (unsigned i = 0; i < s->pages_count; i++) {
        candidate_page_ref_t ref = s->pages[i];
        unsigned lo = 0, hi = s->info.summary.dirty_count;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            const candidate_checkpoint_dirty_t *d = candidate_checkpoint_dirty_at(s->checkpoint, mid);
            if (d->ref.id < ref.id) lo = mid + 1; else hi = mid;
        }
        const candidate_checkpoint_dirty_t *d = candidate_checkpoint_dirty_at(s->checkpoint, lo);
        if (d == NULL || d->ref.id != ref.id || d->ref.generation != ref.generation) continue;
        if (d->rec_lsn == s->group_begin) s->dirty[lo] = true;
    }
}

static candidate_wal_status_t encode_chunk(const scan_t *s, uint8_t *out, size_t *size)
{
    unsigned dirty_chunks = (s->info.summary.dirty_count + 127) / 128;
    candidate_checkpoint_chunk_t chunk = {.checkpoint_id=s->info.summary.anchor.checkpoint_id,.ordinal=s->chunks};
    unsigned start, remaining;
    if (s->chunks < dirty_chunks) {
        chunk.kind = 1; start = s->chunks * 128; remaining = s->info.summary.dirty_count - start;
        chunk.count = remaining < 128 ? remaining : 128;
        for (unsigned i = 0; i < chunk.count; i++) chunk.entries.dirty[i] = *candidate_checkpoint_dirty_at(s->checkpoint,start+i);
    } else {
        chunk.kind = 2; start = (s->chunks - dirty_chunks) * 128;
        if (start >= s->info.summary.transaction_count) return CANDIDATE_WAL_CORRUPT;
        remaining = s->info.summary.transaction_count - start; chunk.count = remaining < 128 ? remaining : 128;
        for (unsigned i = 0; i < chunk.count; i++) chunk.entries.transactions[i] = *candidate_checkpoint_transaction_at(s->checkpoint,start+i);
    }
    return candidate_checkpoint_encode_chunk(&chunk,out,CANDIDATE_CHECKPOINT_PAYLOAD,size);
}

static candidate_wal_status_t checkpoint_record(scan_t *s, const candidate_wal_record_t *r)
{
    const candidate_control_state_t *a = &s->info.summary.anchor;
    if (r->transaction_id != 0 || s->group_begin != 0) return CANDIDATE_WAL_CORRUPT;
    if (r->lsn < a->checkpoint_begin || r->lsn >= a->checkpoint_end) return CANDIDATE_WAL_OK;
    if (s->has_end || r->owner_epoch != a->owner_epoch || r->previous_lsn != s->last_checkpoint)
        return CANDIDATE_WAL_CORRUPT;
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD]; size_t size;
    candidate_wal_status_t status;
    switch (r->type) {
    case 12:
        if (s->has_begin || r->lsn != a->checkpoint_begin) return CANDIDATE_WAL_CORRUPT;
        status = candidate_checkpoint_encode_begin(&s->info.summary,bytes,sizeof(bytes),&size);
        if (status == CANDIDATE_WAL_OK) status = check_cut(s);
        s->has_begin = true; break;
    case 13:
        if (!s->has_begin) return CANDIDATE_WAL_CORRUPT;
        status = encode_chunk(s,bytes,&size); s->chunks++; break;
    case 14: {
        if (!s->has_begin || s->chunks != candidate_checkpoint_chunks(&s->info.summary)) return CANDIDATE_WAL_CORRUPT;
        if (r->lsn + 160 != a->checkpoint_end) return CANDIDATE_WAL_CORRUPT;
        candidate_checkpoint_end_t end = {.checkpoint_id=a->checkpoint_id,.begin_lsn=a->checkpoint_begin,.chunks=s->chunks};
        memcpy(end.digest,s->digest,32); status = candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size);
        s->has_end = true; break;
    }
    default: return CANDIDATE_WAL_CORRUPT;
    }
    if (status != CANDIDATE_WAL_OK) return status;
    if (r->payload_bytes != size || memcmp(r->payload,bytes,size) != 0) return CANDIDATE_WAL_CORRUPT;
    s->last_checkpoint = r->lsn;
    if (!s->has_end) return candidate_checkpoint_digest(s->digest,r->payload,size,s->digest);
    return CANDIDATE_WAL_OK;
}

static bool visit(void *context, const candidate_wal_record_t *r)
{
    scan_t *s = context;
    if (s->observer != NULL && !s->observer(s->observer_context,r)) return false;
    if (r->owner_epoch > s->highest_owner) s->highest_owner = r->owner_epoch;
    if (r->type >= 12 && r->type <= 14) {
        s->status = checkpoint_record(s,r); return s->status == CANDIDATE_WAL_OK;
    }
    if (!s->visitor(s->witness,r)) return false;
    if (r->type == 3) { s->group_begin = r->lsn; s->pages_count = 0; }
    if (r->type == 4 || r->type == 5) {
        if (s->pages_count == CANDIDATE_WITNESS_PAGES) { s->status=CANDIDATE_WAL_LIMIT; return false; }
        s->pages[s->pages_count++] = (candidate_page_ref_t){candidate_checkpoint_read(r->payload+8,8),candidate_checkpoint_read(r->payload+16,8)};
    }
    if (r->type == 8) { mark_dirty(s); s->group_begin = 0; s->pages_count = 0; }
    return true;
}

candidate_wal_status_t candidate_witness_checkpoint_scan(candidate_wal_t *wal,
    candidate_wal_witness_t *witness, const candidate_checkpoint_t *checkpoint,
    candidate_wal_visitor_t visitor, candidate_wal_visitor_t observer, void *context, candidate_wal_info_t *transport)
{
    scan_t scan = {.witness=witness,.checkpoint=checkpoint,.visitor=visitor,.observer=observer,
        .observer_context=context,.info=candidate_checkpoint_info(checkpoint)};
    candidate_wal_status_t status = candidate_wal_scan(wal,visit,&scan,transport);
    if (status == CANDIDATE_WAL_STOPPED && scan.status != CANDIDATE_WAL_OK) return scan.status;
    if (status == CANDIDATE_WAL_OK && !scan.has_end) return CANDIDATE_WAL_CORRUPT;
    return status;
}
