#include "candidate_checkpoint_internal.h"

#include <stdlib.h>
#include <string.h>

struct candidate_checkpoint {
    candidate_checkpoint_info_t info;
    candidate_wal_memory_t memory;
    candidate_wal_status_t status;
    uint64_t last_lsn;
    uint32_t dirty_count, transaction_count, chunks;
    uint8_t digest[32];
    bool has_begin, has_end;
    candidate_checkpoint_dirty_t dirty[CANDIDATE_CHECKPOINT_DIRTY];
    candidate_checkpoint_transaction_t transactions[CANDIDATE_CHECKPOINT_TRANSACTIONS];
};

static candidate_wal_status_t read_begin(candidate_checkpoint_t *c, const candidate_wal_record_t *r)
{
    if (c->has_begin || r->payload_bytes != 160) return CANDIDATE_WAL_CORRUPT;
    if (r->lsn != c->info.summary.anchor.checkpoint_begin) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = r->payload;
    if (memcmp(p, "SLCP\1\0\0\0", 8) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (memcmp(p+8,c->info.summary.anchor.uuid,16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (candidate_checkpoint_read(p+156,4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    c->info.summary.dirty_count = (uint32_t)candidate_checkpoint_read(p+144,4);
    c->info.summary.transaction_count = (uint32_t)candidate_checkpoint_read(p+148,4);
    uint8_t expected[160]; size_t size;
    candidate_wal_status_t status = candidate_checkpoint_encode_begin(&c->info.summary,expected,sizeof(expected),&size);
    if (status != CANDIDATE_WAL_OK) return status;
    if (memcmp(expected,p,size) != 0) return CANDIDATE_WAL_CORRUPT;
    c->has_begin = true; return candidate_checkpoint_digest(c->digest,p,size,c->digest);
}

static candidate_wal_status_t add_dirty(candidate_checkpoint_t *c, const candidate_checkpoint_chunk_t *chunk)
{
    const candidate_control_state_t *a = &c->info.summary.anchor;
    for (unsigned i = 0; i < chunk->count; i++) {
        candidate_checkpoint_dirty_t d = chunk->entries.dirty[i];
        if (d.ref.id > a->page_highwater || d.rec_lsn > a->checkpoint_begin || d.rec_lsn < a->redo_begin)
            return CANDIDATE_WAL_CORRUPT;
        if (c->dirty_count > 0 && d.ref.id <= c->dirty[c->dirty_count-1].ref.id) return CANDIDATE_WAL_CORRUPT;
        c->dirty[c->dirty_count++] = d;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_transactions(candidate_checkpoint_t *c, const candidate_checkpoint_chunk_t *chunk)
{
    const candidate_control_state_t *a = &c->info.summary.anchor;
    for (unsigned i = 0; i < chunk->count; i++) {
        candidate_checkpoint_transaction_t t = chunk->entries.transactions[i];
        if (t.id > a->transaction_highwater || t.owner_epoch > a->owner_epoch) return CANDIDATE_WAL_CORRUPT;
        if (t.last_lsn >= a->checkpoint_begin || t.terminal_end_lsn > a->checkpoint_begin) return CANDIDATE_WAL_CORRUPT;
        if (t.csn > a->transaction_highwater) return CANDIDATE_WAL_CORRUPT;
        if (c->transaction_count > 0 && t.id <= c->transactions[c->transaction_count-1].id) return CANDIDATE_WAL_CORRUPT;
        if (t.state == CANDIDATE_CHECKPOINT_ACTIVE) {
            if (t.first_lsn < a->redo_begin) return CANDIDATE_WAL_CORRUPT;
            if (c->info.active_transactions == 64) return CANDIDATE_WAL_LIMIT;
            c->info.active_transactions++;
        }
        for (unsigned j = 0; j < c->transaction_count && t.csn > 0; j++)
            if (c->transactions[j].csn == t.csn) return CANDIDATE_WAL_CORRUPT;
        c->transactions[c->transaction_count++] = t;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t read_chunk(candidate_checkpoint_t *c, const candidate_wal_record_t *r)
{
    if (!c->has_begin) return CANDIDATE_WAL_CORRUPT;
    candidate_checkpoint_chunk_t chunk;
    candidate_wal_status_t status = candidate_checkpoint_decode_chunk(r->payload,r->payload_bytes,&chunk);
    if (status != CANDIDATE_WAL_OK) return status;
    if (chunk.checkpoint_id != c->info.summary.anchor.checkpoint_id || chunk.ordinal != c->chunks)
        return CANDIDATE_WAL_CORRUPT;
    bool is_dirty = c->dirty_count < c->info.summary.dirty_count;
    uint32_t remaining = is_dirty ? c->info.summary.dirty_count - c->dirty_count :
        c->info.summary.transaction_count - c->transaction_count;
    unsigned count = remaining > 128 ? 128 : remaining;
    if (chunk.kind != (is_dirty ? 1u : 2u) || chunk.count != count) return CANDIDATE_WAL_CORRUPT;
    status = is_dirty ? add_dirty(c,&chunk) : add_transactions(c,&chunk);
    if (status != CANDIDATE_WAL_OK) return status;
    c->chunks++; return candidate_checkpoint_digest(c->digest,r->payload,r->payload_bytes,c->digest);
}

static candidate_wal_status_t read_end(candidate_checkpoint_t *c, const candidate_wal_record_t *r)
{
    if (!c->has_begin || r->payload_bytes != 64) return CANDIDATE_WAL_CORRUPT;
    if (c->dirty_count != c->info.summary.dirty_count || c->transaction_count != c->info.summary.transaction_count)
        return CANDIDATE_WAL_CORRUPT;
    if (memcmp(r->payload,"SLCP\1\0\3\0",8) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (candidate_checkpoint_read(r->payload+28,4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_checkpoint_end_t end = {.checkpoint_id=c->info.summary.anchor.checkpoint_id,
        .begin_lsn=c->info.summary.anchor.checkpoint_begin,.chunks=c->chunks};
    memcpy(end.digest,c->digest,32); uint8_t expected[64]; size_t size;
    candidate_wal_status_t status = candidate_checkpoint_encode_end(&end,expected,sizeof(expected),&size);
    if (status != CANDIDATE_WAL_OK) return status;
    if (memcmp(expected,r->payload,size) != 0) return CANDIDATE_WAL_CORRUPT;
    uint64_t end_lsn = r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7);
    if (end_lsn != c->info.summary.anchor.checkpoint_end) return CANDIDATE_WAL_CORRUPT;
    c->has_end = true; return CANDIDATE_WAL_OK;
}

static bool visit(void *context, const candidate_wal_record_t *r)
{
    candidate_checkpoint_t *c = context;
    const candidate_control_state_t *a = &c->info.summary.anchor;
    if (r->lsn < a->checkpoint_begin || r->lsn >= a->checkpoint_end) return true;
    if (r->type < 12 || r->type > 14) return true;
    if (c->has_end || r->transaction_id != 0 || r->owner_epoch != a->owner_epoch) {
        c->status = CANDIDATE_WAL_CORRUPT; return false;
    }
    if (r->previous_lsn != c->last_lsn) { c->status = CANDIDATE_WAL_CORRUPT; return false; }
    switch (r->type) {
    case 12: c->status = read_begin(c,r); break;
    case 13: c->status = read_chunk(c,r); break;
    case 14: c->status = read_end(c,r); break;
    default: c->status = CANDIDATE_WAL_CORRUPT;
    }
    c->last_lsn = r->lsn; return c->status == CANDIDATE_WAL_OK;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *bytes, size_t size) { (void)context; (void)size; free(bytes); }

candidate_wal_status_t candidate_checkpoint_load(candidate_wal_t *wal, const candidate_control_state_t *anchor,
    const candidate_wal_memory_t *memory, candidate_checkpoint_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || anchor == NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_state_t saved = *anchor;
    candidate_wal_memory_t allocator = {NULL,allocate_default,release_default};
    if (memory != NULL) allocator = *memory;
    if (allocator.allocate == NULL || allocator.release == NULL) return CANDIDATE_WAL_INVALID;
    candidate_checkpoint_summary_t summary = {.anchor=saved}; uint8_t bytes[160]; size_t size;
    candidate_wal_status_t status = candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_checkpoint_t *c = allocator.allocate(allocator.context,sizeof(*c));
    if (c == NULL) return CANDIDATE_WAL_NOMEM;
    memset(c,0,sizeof(*c)); c->memory = allocator; c->info.summary = summary; c->info.resident_bytes = sizeof(*c);
    status = candidate_wal_scan(wal,visit,c,&c->info.transport);
    if (status == CANDIDATE_WAL_STOPPED) status = c->status;
    if (status == CANDIDATE_WAL_OK && !c->has_end) status = CANDIDATE_WAL_CORRUPT;
    if (status != CANDIDATE_WAL_OK) { candidate_checkpoint_close(&c); return status; }
    *out = c; return CANDIDATE_WAL_OK;
}

candidate_checkpoint_info_t candidate_checkpoint_info(const candidate_checkpoint_t *c)
{
    return c == NULL ? (candidate_checkpoint_info_t){0} : c->info;
}

const candidate_checkpoint_dirty_t *candidate_checkpoint_dirty_at(const candidate_checkpoint_t *c, uint32_t index)
{
    return c == NULL || index >= c->dirty_count ? NULL : &c->dirty[index];
}

const candidate_checkpoint_transaction_t *candidate_checkpoint_transaction_at(const candidate_checkpoint_t *c, uint32_t index)
{
    return c == NULL || index >= c->transaction_count ? NULL : &c->transactions[index];
}

void candidate_checkpoint_close(candidate_checkpoint_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_checkpoint_t *c = *out; *out = NULL;
    candidate_wal_memory_t memory = c->memory; memory.release(memory.context,c,sizeof(*c));
}
