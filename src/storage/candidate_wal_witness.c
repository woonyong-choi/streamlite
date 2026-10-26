#include "storage/candidate_wal_witness.h"

#include <stdlib.h>
#include <string.h>

#include "storage/checksum.h"
#include "candidate_wal_system_internal.h"
#include "candidate_wal_witness_data_internal.h"
#include "candidate_wal_witness_internal.h"
#include "candidate_wal_witness_state_internal.h"

static uint64_t read_le(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

uint32_t candidate_wal_witness_crc(uint32_t crc, const candidate_wal_record_t *r)
{
    uint8_t header[48] = {0};
    write_le(header, r->type, 2);
    write_le(header + 8, r->lsn, 8);
    write_le(header + 16, r->transaction_id, 8);
    write_le(header + 24, r->previous_lsn, 8);
    write_le(header + 32, r->owner_epoch, 8);
    write_le(header + 40, r->payload_bytes, 8);
    crc = crc32c(crc, header, sizeof(header));
    return crc32c(crc, r->payload, r->payload_bytes);
}

static transaction_t *find_transaction(candidate_wal_witness_t *w, uint64_t id)
{
    for (unsigned i = 0; i < w->info.transactions; i++)
        if (w->transactions[i].value.transaction_id == id) return &w->transactions[i];
    return NULL;
}

static candidate_wal_status_t add_transaction(candidate_wal_witness_t *w,
    const candidate_wal_record_t *r, transaction_t **out)
{
    if (r->transaction_id == 0 || r->previous_lsn != 0) return CANDIDATE_WAL_CORRUPT;
    if (r->type != 1 && r->type != 2) return CANDIDATE_WAL_CORRUPT;
    candidate_witness_system_info_t system = candidate_system_info(w->system);
    if (system.has_reservation && r->transaction_id > system.transaction_highwater) return CANDIDATE_WAL_CORRUPT;
    if (w->info.transactions == CANDIDATE_WITNESS_TRANSACTIONS || w->active == 64)
        return CANDIDATE_WAL_LIMIT;
    transaction_t *t = &w->transactions[w->info.transactions++];
    *t = (transaction_t){.value = {.transaction_id = r->transaction_id,.first_lsn = r->lsn},.owner_epoch = r->owner_epoch};
    w->active++;
    *out = t;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t admit(candidate_wal_witness_t *w, transaction_t *t,
    const candidate_wal_record_t *r)
{
    if (r->payload_bytes != 64 || t->value.records != 0) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload, 8) == 0) return CANDIDATE_WAL_CORRUPT;
    candidate_witness_system_info_t system = candidate_system_info(w->system);
    if (system.has_epoch && (read_le(r->payload, 8) < system.retention_floor || read_le(r->payload, 8) > system.current_epoch))
        return CANDIDATE_WAL_CORRUPT;
    uint64_t limit = read_le(r->payload + 56, 8);
    if (limit > 1048576) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < w->info.transactions; i++) {
        const candidate_witness_transaction_t *other = &w->transactions[i].value;
        if (!other->has_admission || other->request_epoch != read_le(r->payload, 8)) continue;
        if (memcmp(other->request_nonce, r->payload + 8, 16) == 0) return CANDIDATE_WAL_CORRUPT;
    }
    for (unsigned i = 0; i < 16; i++) t->value.request_nonce[i] = r->payload[8 + i];
    memcpy(t->value.operation_digest, r->payload + 24, 32);
    t->value.request_epoch = read_le(r->payload, 8);
    t->result_limit = limit;
    const uint8_t empty_digest[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    memcpy(t->value.result_digest, empty_digest, 32);
    t->value.has_admission = true;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t begin(transaction_t *t, const candidate_wal_record_t *r,
    uint64_t highest_csn)
{
    if (r->payload_bytes != 16 || t->value.has_begin) return CANDIDATE_WAL_CORRUPT;
    unsigned isolation = (unsigned)read_le(r->payload, 4);
    if (isolation > 1 || !is_zero(r->payload + 4, 4)) return CANDIDATE_WAL_UNSUPPORTED;
    if (read_le(r->payload + 8, 8) > highest_csn) return CANDIDATE_WAL_CORRUPT;
    if (isolation == 1 && read_le(r->payload + 8, 8) != 0) return CANDIDATE_WAL_CORRUPT;
    t->value.has_begin = true;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t start_group(candidate_wal_witness_t *w,
    const candidate_wal_record_t *r)
{
    if (r->payload_bytes != 16 || w->pending.id != 0) return CANDIDATE_WAL_CORRUPT;
    uint64_t id = read_le(r->payload, 8);
    if (id == 0) return CANDIDATE_WAL_CORRUPT;
    if (w->info.complete_groups == CANDIDATE_WITNESS_GROUPS) return CANDIDATE_WAL_LIMIT;
    if (w->info.complete_groups > 0 && id <= w->groups[w->info.complete_groups - 1].id)
        return CANDIDATE_WAL_CORRUPT;
    unsigned pages = (unsigned)read_le(r->payload + 8, 4);
    unsigned records = (unsigned)read_le(r->payload + 12, 4);
    if (pages == 0 || records < pages) return CANDIDATE_WAL_CORRUPT;
    if (pages > CANDIDATE_WITNESS_PAGES || records > CANDIDATE_WITNESS_RECORDS)
        return CANDIDATE_WAL_LIMIT;
    w->pending = (pending_group_t){.id = id, .transaction_id = r->transaction_id,
        .first_lsn = r->lsn, .owner_epoch = r->owner_epoch, .expected_pages = pages, .expected_records = records};
    w->pending.crc = candidate_wal_witness_crc(0, r);
    if (w->system != NULL) candidate_system_begin(w->system);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t validate_image(candidate_wal_witness_t *w,
    const candidate_wal_record_t *r, page_t *page)
{
    if (r->payload_bytes != 32 + CANDIDATE_WITNESS_PAGE_BYTES) return CANDIDATE_WAL_CORRUPT;
    const uint8_t *p = r->payload + 32;
    if (memcmp(p, w->uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (memcmp(p + 16, "SLPG", 4) != 0 || read_le(p + 20, 2) != 1)
        return CANDIDATE_WAL_UNSUPPORTED;
    if (read_le(p + 22, 2) < 1 || read_le(p + 22, 2) > 5) return CANDIDATE_WAL_UNSUPPORTED;
    if (read_le(p + 24, 8) != page->id || read_le(p + 32, 8) != page->generation)
        return CANDIDATE_WAL_CORRUPT;
    size_t body = (size_t)read_le(p + 48, 4);
    if (body > 8128 || !is_zero(p + 56, 8)) return CANDIDATE_WAL_CORRUPT;
    if (!is_zero(p + 64 + body, 8128 - body)) return CANDIDATE_WAL_CORRUPT;
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0, p, 52);
    crc = crc32c(crc, zero, 4);
    crc = crc32c(crc, p + 56, 8192 - 56);
    if (crc != read_le(p + 52, 4)) return CANDIDATE_WAL_CORRUPT;
    page->has_image = true;
    page->image_lsn = read_le(p + 40, 8);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t validate_delta(const candidate_wal_record_t *r)
{
    if (r->payload_bytes < 40 || !is_zero(r->payload + 36, 4)) return CANDIDATE_WAL_CORRUPT;
    unsigned count = (unsigned)read_le(r->payload + 32, 4);
    if (count == 0 || count > 128) return CANDIDATE_WAL_CORRUPT;
    size_t at = 40, end = 64;
    for (unsigned i = 0; i < count; i++) {
        if (r->payload_bytes - at < 8) return CANDIDATE_WAL_CORRUPT;
        uint64_t offset = read_le(r->payload + at, 4), size = read_le(r->payload + at + 4, 4);
        at += 8;
        if (offset < end || offset > 8192 || size == 0) return CANDIDATE_WAL_CORRUPT;
        if (size > 8192 - offset || size > r->payload_bytes - at) return CANDIDATE_WAL_CORRUPT;
        at += (size_t)size;
        end = (size_t)(offset + size);
    }
    return at == r->payload_bytes ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t event_record(candidate_wal_witness_t *w, const candidate_wal_record_t *r)
{
    pending_group_t *g = &w->pending;
    if (w->system == NULL) return CANDIDATE_WAL_UNSUPPORTED;
    if (g->id == 0 || r->payload_bytes < 8 || read_le(r->payload, 8) != g->id || g->records == g->expected_records)
        return CANDIDATE_WAL_CORRUPT;
    if (r->type == 16 && r->payload_bytes == 40) {
        uint64_t previous = read_le(r->payload + 8, 8);
        for (unsigned i = 0; i < w->info.transactions; i++)
            if (w->transactions[i].value.transaction_id > previous) return CANDIDATE_WAL_CORRUPT;
    }
    candidate_wal_status_t status = candidate_system_event(w->system, r);
    if (status != CANDIDATE_WAL_OK) return status;
    g->records++; g->crc = candidate_wal_witness_crc(g->crc, r); return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t page_record(candidate_wal_witness_t *w,
    const candidate_wal_record_t *r)
{
    pending_group_t *g = &w->pending;
    if (r->type == 6 || r->type == 7) return event_record(w, r);
    if (g->id == 0 || r->payload_bytes < 32) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload, 8) != g->id) return CANDIDATE_WAL_CORRUPT;
    page_t p = {.id = read_le(r->payload + 8, 8), .generation = read_le(r->payload + 16, 8)};
    if (p.id == 0 || p.generation == 0) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload + 24, 8) >= r->lsn) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status;
    if (r->type == 4) status = validate_image(w, r, &p);
    else if (r->type == 5) status = validate_delta(r);
    else return CANDIDATE_WAL_UNSUPPORTED;
    if (status != CANDIDATE_WAL_OK) return status;
    if (r->type == 4 && w->system != NULL) {
        status = candidate_system_image(w->system, r);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    for (unsigned i = 0; i < g->pages; i++)
        if (g->seen[i].id == p.id) return CANDIDATE_WAL_CORRUPT;
    if (g->pages == g->expected_pages || g->records == g->expected_records)
        return CANDIDATE_WAL_CORRUPT;
    g->seen[g->pages++] = p;
    g->records++;
    g->crc = candidate_wal_witness_crc(g->crc, r);
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t end_group(candidate_wal_witness_t *w,
    transaction_t *t, const candidate_wal_record_t *r)
{
    pending_group_t *g = &w->pending;
    if (g->id == 0 || r->payload_bytes != 24) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload, 8) != g->id || !is_zero(r->payload + 20, 4)) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload + 8, 4) != g->pages || g->pages != g->expected_pages)
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload + 12, 4) != g->records || g->records != g->expected_records)
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload + 16, 4) != g->crc) return CANDIDATE_WAL_CORRUPT;
    uint64_t end = r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7);
    for (unsigned i = 0; i < g->pages; i++)
        if (g->seen[i].has_image && g->seen[i].image_lsn != end) return CANDIDATE_WAL_CORRUPT;
    if (w->system != NULL) {
        uint64_t minimum = 0;
        for (unsigned i = 0; i < w->info.transactions; i++) {
            const candidate_witness_transaction_t *v = &w->transactions[i].value;
            if (!v->has_admission || v->state != CANDIDATE_WITNESS_UNFINISHED) continue;
            if (minimum == 0 || v->request_epoch < minimum) minimum = v->request_epoch;
        }
        candidate_wal_status_t status = candidate_system_finish(w->system, g->id, end, g->owner_epoch, minimum, g->transaction_id == 0);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    w->groups[w->info.complete_groups++] = (group_t){g->id, g->transaction_id, end};
    if (t != NULL) t->value.groups++;
    memset(g, 0, sizeof(*g));
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t result_chunk(transaction_t *t, const candidate_wal_record_t *r)
{
    const uint8_t *p = r->payload;
    if (!t->value.has_admission || r->payload_bytes != 112) return CANDIDATE_WAL_CORRUPT;
    if (read_le(p, 8) != t->value.request_epoch || memcmp(p + 8, t->value.request_nonce, 16) != 0)
        return CANDIDATE_WAL_CORRUPT;
    unsigned index = (unsigned)read_le(p + 24, 4), count = (unsigned)read_le(p + 28, 4);
    uint64_t total = read_le(p + 32, 8), offset = read_le(p + 96, 8);
    uint64_t length = read_le(p + 104, 4);
    if (total == 0 || total > t->result_limit || count != (total + 65535) / 65536)
        return CANDIDATE_WAL_CORRUPT;
    if (index != t->value.result_chunks || index >= count || count > CANDIDATE_WITNESS_CHUNKS)
        return CANDIDATE_WAL_CORRUPT;
    if (offset != t->chunk_bytes || offset >= total) return CANDIDATE_WAL_CORRUPT;
    if (length != (total - offset < 65536 ? total - offset : 65536))
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(p + 72, 8) == 0 || read_le(p + 80, 8) == 0 || read_le(p + 92, 4) == 0)
        return CANDIDATE_WAL_CORRUPT;
    if (!is_zero(p + 108, 4)) return CANDIDATE_WAL_CORRUPT;
    if (index > 0 && (total != t->value.result_bytes || memcmp(p + 40, t->value.result_digest, 32) != 0))
        return CANDIDATE_WAL_CORRUPT;
    t->value.result_bytes = total;
    memcpy(t->value.result_digest, p + 40, 32);
    t->value.result_chunks++;
    t->chunk_bytes += length;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t validate_terminal(const transaction_t *t,
    const candidate_wal_record_t *r)
{
    if (r->payload_bytes < 32 || !is_zero(r->payload + 28, 4)) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload, 8) != t->value.first_lsn || read_le(r->payload + 8, 8) != t->value.last_lsn)
        return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload + 16, 8) != t->value.records || read_le(r->payload + 24, 4) != t->value.chain_crc)
        return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t commit(candidate_wal_witness_t *w, transaction_t *t,
    const candidate_wal_record_t *r)
{
    if (!t->value.has_begin || r->payload_bytes < 152) return CANDIDATE_WAL_CORRUPT;
    uint64_t csn = read_le(r->payload + 32, 8);
    unsigned count = (unsigned)read_le(r->payload + 148, 4);
    if (count != t->value.groups || r->payload_bytes != 152 + (size_t)count * 16)
        return CANDIDATE_WAL_CORRUPT;
    if (csn == 0 || csn <= w->info.highest_csn) return CANDIDATE_WAL_CORRUPT;
    if (t->value.has_admission) {
        if (read_le(r->payload + 40, 8) != t->value.request_epoch) return CANDIDATE_WAL_CORRUPT;
        if (memcmp(r->payload + 48, t->value.request_nonce, 16) != 0) return CANDIDATE_WAL_CORRUPT;
        if (memcmp(r->payload + 64, t->value.operation_digest, 32) != 0) return CANDIDATE_WAL_CORRUPT;
        if (memcmp(r->payload + 96, t->value.result_digest, 32) != 0) return CANDIDATE_WAL_CORRUPT;
        if (read_le(r->payload + 128, 8) != t->value.result_bytes || t->chunk_bytes != t->value.result_bytes)
            return CANDIDATE_WAL_CORRUPT;
        if (read_le(r->payload + 136, 4) != t->value.result_chunks) return CANDIDATE_WAL_CORRUPT;
    } else if (!is_zero(r->payload + 40, 100)) return CANDIDATE_WAL_CORRUPT;
    if (!is_zero(r->payload + 140, 8)) return CANDIDATE_WAL_CORRUPT;
    unsigned dependency = 0;
    for (unsigned i = 0; i < w->info.complete_groups; i++) {
        group_t *g = &w->groups[i];
        if (g->transaction_id != r->transaction_id) continue;
        const uint8_t *p = r->payload + 152 + dependency++ * 16;
        if (read_le(p, 8) != g->id || read_le(p + 8, 8) != g->end_lsn) return CANDIDATE_WAL_CORRUPT;
    }
    t->value.state = CANDIDATE_WITNESS_COMMIT;
    t->value.csn = csn;
    w->info.highest_csn = csn;
    w->info.commits++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t dispatch(candidate_wal_witness_t *w, transaction_t *t,
    const candidate_wal_record_t *r)
{
    if (w->pending.id != 0) {
        if (r->transaction_id != w->pending.transaction_id || r->type < 4 || r->type > 8)
            return CANDIDATE_WAL_CORRUPT;
    }
    if (r->type != 1 && r->type != 2 && r->type != 11 && !t->value.has_begin)
        return CANDIDATE_WAL_CORRUPT;
    switch (r->type) {
    case 1: return admit(w, t, r);
    case 2: return begin(t, r, w->info.highest_csn);
    case 3: return start_group(w, r);
    case 4: case 5: case 6: case 7: return page_record(w, r);
    case 8: return end_group(w, t, r);
    case 9: return result_chunk(t, r);
    case 10: return commit(w, t, r);
    case 11:
        if (r->payload_bytes != 32) return CANDIDATE_WAL_CORRUPT;
        t->value.state = CANDIDATE_WITNESS_ABORT;
        w->info.aborts++;
        return CANDIDATE_WAL_OK;
    default: return CANDIDATE_WAL_UNSUPPORTED;
    }
}

static candidate_wal_status_t system_record(candidate_wal_witness_t *w, const candidate_wal_record_t *r)
{
    if (w->system == NULL) return CANDIDATE_WAL_UNSUPPORTED;
    pending_group_t *g = &w->pending;
    candidate_wal_status_t status;
    if (g->id == 0) {
        if (r->type != 3 || r->previous_lsn != 0) return CANDIDATE_WAL_CORRUPT;
        status = start_group(w, r);
    } else {
        if (g->transaction_id != 0 || r->previous_lsn != g->last_lsn || r->owner_epoch != g->owner_epoch)
            return CANDIDATE_WAL_CORRUPT;
        switch (r->type) {
        case 4: status = page_record(w, r); break;
        case 6: case 7: case 15: case 16: status = event_record(w, r); break;
        case 8: status = end_group(w, NULL, r); break;
        case 5: return CANDIDATE_WAL_UNSUPPORTED;
        default: return CANDIDATE_WAL_CORRUPT;
        }
    }
    if (status == CANDIDATE_WAL_OK) {
        if (w->pending.id != 0) w->pending.last_lsn = r->lsn;
        w->info.records++;
    }
    return status;
}

static bool visit_record(void *context, const candidate_wal_record_t *r)
{
    candidate_wal_witness_t *w = context;
    if (r->type >= 12 && r->type <= 14) { w->status = CANDIDATE_WAL_UNSUPPORTED; return false; }
    if (r->transaction_id == 0) { w->status = system_record(w, r); return w->status == CANDIDATE_WAL_OK; }
    if (r->type >= 12) { w->status = CANDIDATE_WAL_UNSUPPORTED; return false; }
    transaction_t *t = find_transaction(w, r->transaction_id);
    if (t == NULL) w->status = add_transaction(w, r, &t);
    if (w->status != CANDIDATE_WAL_OK) return false;
    if (t->value.state != CANDIDATE_WITNESS_UNFINISHED || r->previous_lsn != t->value.last_lsn) {
        w->status = CANDIDATE_WAL_CORRUPT;
        return false;
    }
    if (r->type != 11 && r->owner_epoch != t->owner_epoch) w->status = CANDIDATE_WAL_CORRUPT;
    bool terminal = r->type == 10 || r->type == 11;
    if (w->status == CANDIDATE_WAL_OK && terminal) w->status = validate_terminal(t, r);
    if (w->status == CANDIDATE_WAL_OK) w->status = dispatch(w, t, r);
    if (w->status != CANDIDATE_WAL_OK) return false;
    t->value.records++;
    t->value.last_lsn = r->lsn;
    t->value.chain_crc = candidate_wal_witness_crc(t->value.chain_crc, r);
    if (terminal) {
        t->value.terminal_end_lsn = r->lsn + ((96 + r->payload_bytes + 7) & ~(uint64_t)7);
        w->active--;
    }
    w->info.records++;
    return true;
}

static void settle(candidate_wal_witness_t *w)
{
    w->info.unfinished = w->active;
    w->info.incomplete_group_lsn = w->pending.first_lsn;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

typedef struct {
    candidate_wal_witness_t *witness;
    candidate_wal_visitor_t observer;
    void *context;
    bool has_observer_stop;
} observed_t;

static bool observe_forward(void *context, const candidate_wal_record_t *record)
{
    observed_t *o=context;
    if (o->observer==NULL || o->observer(o->context,record)) return true;
    o->has_observer_stop=true; return false;
}

static bool visit_observed(void *context, const candidate_wal_record_t *record)
{
    observed_t *o=context;
    return observe_forward(o,record) && visit_record(o->witness,record);
}

static candidate_wal_status_t analyze(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_checkpoint_t *checkpoint,
    candidate_wal_visitor_t observer, void *context, candidate_wal_witness_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || options == NULL) return CANDIDATE_WAL_INVALID;
    candidate_witness_options_t saved = *options;
    if (is_zero(saved.uuid, 16)) return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t allocator = {NULL, allocate_default, release_default};
    if (saved.memory != NULL) allocator = *saved.memory;
    if (allocator.allocate == NULL || allocator.release == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_witness_t *w = allocator.allocate(allocator.context, sizeof(*w));
    if (w == NULL) return CANDIDATE_WAL_NOMEM;
    memset(w, 0, sizeof(*w));
    w->memory = allocator;
    memcpy(w->uuid, saved.uuid, 16);
    w->info.resident_bytes = sizeof(*w);
    if (saved.enable_system_groups) {
        candidate_wal_status_t status = candidate_system_create(saved.uuid, &allocator, &w->system);
        if (status != CANDIDATE_WAL_OK) { allocator.release(allocator.context, w, sizeof(*w)); return status; }
        w->info.resident_bytes += candidate_system_resident_bytes(w->system);
    }
    observed_t observed={.witness=w,.observer=observer,.context=context};
    candidate_wal_status_t status = checkpoint == NULL ? candidate_wal_scan(wal, visit_observed, &observed, &w->info.transport) :
        candidate_witness_checkpoint_scan(wal, w, checkpoint, visit_record, observe_forward, &observed, &w->info.transport);
    if (status == CANDIDATE_WAL_STOPPED && !observed.has_observer_stop) status = w->status;
    if (status != CANDIDATE_WAL_OK) { candidate_wal_witness_close(&w); return status; }
    settle(w);
    *out = w;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_witness_analyze(candidate_wal_t *wal,
    const candidate_witness_options_t *options, candidate_wal_witness_t **out)
{
    return analyze(wal, options, NULL, NULL, NULL, out);
}

static candidate_wal_status_t analyze_checkpoint(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_control_state_t *anchor,
    candidate_wal_visitor_t observer, void *context, candidate_wal_witness_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || options == NULL || anchor == NULL) return CANDIDATE_WAL_INVALID;
    candidate_witness_options_t saved = *options;
    candidate_control_state_t state = *anchor;
    if (!saved.enable_system_groups) return CANDIDATE_WAL_INVALID;
    if (memcmp(saved.uuid, state.uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (saved.memory != NULL) memory = *saved.memory;
    saved.memory = &memory;
    candidate_checkpoint_t *checkpoint = NULL;
    candidate_wal_witness_t *witness = NULL;
    candidate_wal_status_t status = candidate_checkpoint_load(wal, &state, &memory, &checkpoint);
    if (status == CANDIDATE_WAL_OK) status = analyze(wal, &saved, checkpoint, observer, context, &witness);
    candidate_checkpoint_close(&checkpoint);
    if (status == CANDIDATE_WAL_OK) *out = witness;
    return status;
}

candidate_wal_status_t candidate_wal_witness_analyze_checkpoint(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_control_state_t *anchor, candidate_wal_witness_t **out)
{
    return analyze_checkpoint(wal,options,anchor,NULL,NULL,out);
}

candidate_wal_status_t candidate_witness_analyze_observed(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_control_state_t *anchor,
    candidate_wal_visitor_t observer, void *context, candidate_wal_witness_t **out)
{
    if (anchor==NULL) return analyze(wal,options,NULL,observer,context,out);
    return analyze_checkpoint(wal,options,anchor,observer,context,out);
}

candidate_wal_status_t candidate_witness_bind_checkpoint(candidate_wal_witness_t *w,
    const candidate_control_state_t *anchor)
{
    if (w == NULL || anchor == NULL || w->system == NULL) return CANDIDATE_WAL_INVALID;
    for (unsigned i = 0; i < w->info.transactions; i++) {
        const candidate_witness_transaction_t *v = &w->transactions[i].value;
        if (!v->has_admission) continue;
        if (v->request_epoch > anchor->request_epoch) return CANDIDATE_WAL_CORRUPT;
        if (v->state == CANDIDATE_WITNESS_UNFINISHED && v->request_epoch < anchor->retention_floor)
            return CANDIDATE_WAL_CORRUPT;
    }
    return candidate_system_bind_checkpoint(w->system,anchor);
}

bool candidate_witness_checkpoint_at(const candidate_wal_witness_t *w, uint32_t index,
    candidate_checkpoint_transaction_t *out)
{
    if (w == NULL || out == NULL || index >= w->info.transactions) return false;
    const transaction_t *t = &w->transactions[index];
    const candidate_witness_transaction_t *v = &t->value;
    *out = (candidate_checkpoint_transaction_t){v->transaction_id,v->first_lsn,v->last_lsn,
        v->terminal_end_lsn,v->csn,t->owner_epoch,(candidate_checkpoint_state_t)(v->state + 1)};
    return true;
}

candidate_wal_status_t candidate_witness_clone(const candidate_wal_witness_t *source,
    const candidate_wal_memory_t *memory, candidate_wal_witness_t **out)
{
    *out = NULL;
    candidate_wal_memory_t allocator = *memory;
    candidate_wal_witness_t *w = allocator.allocate(allocator.context, sizeof(*w));
    if (w == NULL) return CANDIDATE_WAL_NOMEM;
    memcpy(w, source, sizeof(*w));
    w->memory = allocator;
    w->system = NULL;
    if (source->system != NULL) {
        candidate_wal_status_t status = candidate_system_clone(source->system, &allocator, &w->system);
        if (status != CANDIDATE_WAL_OK) { allocator.release(allocator.context, w, sizeof(*w)); return status; }
    }
    *out = w;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_witness_apply(candidate_wal_witness_t *w, const candidate_wal_record_t *record)
{
    return visit_record(w, record) ? CANDIDATE_WAL_OK : w->status;
}

void candidate_witness_settle(candidate_wal_witness_t *w, const candidate_wal_info_t *transport)
{
    w->info.transport = *transport;
    settle(w);
}

bool candidate_witness_transaction_at(const candidate_wal_witness_t *w, uint32_t index,
    candidate_semantic_transaction_t *out)
{
    if (w == NULL || out == NULL || index >= w->info.transactions) return false;
    const transaction_t *t = &w->transactions[index];
    *out = (candidate_semantic_transaction_t){t->value, t->owner_epoch, t->result_limit, t->chunk_bytes};
    return true;
}

bool candidate_witness_group_at(const candidate_wal_witness_t *w, uint32_t index, candidate_semantic_group_t *out)
{
    if (w == NULL || out == NULL || index >= w->info.complete_groups) return false;
    const group_t *g = &w->groups[index];
    *out = (candidate_semantic_group_t){g->id, g->transaction_id, g->end_lsn};
    return true;
}

candidate_witness_info_t candidate_wal_witness_info(const candidate_wal_witness_t *w)
{
    return w == NULL ? (candidate_witness_info_t){0} : w->info;
}

candidate_witness_system_info_t candidate_wal_witness_system_info(const candidate_wal_witness_t *w)
{
    return candidate_system_info(w == NULL ? NULL : w->system);
}

const candidate_witness_system_event_t *candidate_wal_witness_system_at(const candidate_wal_witness_t *w, uint32_t index)
{
    return candidate_system_at(w == NULL ? NULL : w->system, index);
}

const candidate_witness_transaction_t *candidate_wal_witness_find(
    const candidate_wal_witness_t *w, uint64_t id)
{
    if (w == NULL) return NULL;
    for (unsigned i = 0; i < w->info.transactions; i++)
        if (w->transactions[i].value.transaction_id == id) return &w->transactions[i].value;
    return NULL;
}

void candidate_wal_witness_close(candidate_wal_witness_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_wal_witness_t *w = *out;
    *out = NULL;
    candidate_system_close(&w->system);
    candidate_wal_memory_t memory = w->memory;
    memory.release(memory.context, w, sizeof(*w));
}
