#include "candidate_recovery_publish_internal.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_checkpoint.h"
#include "candidate_control_internal.h"
#include "candidate_wal_witness_internal.h"

#define CHECKPOINT_BEGIN_BYTES 160u
#define CHECKPOINT_END_BYTES 64u
#define CHUNK_HEADER_BYTES 32u
#define CHUNK_ENTRY_BYTES 56u

static uint32_t chunk_entries(uint32_t transactions, uint32_t chunk)
{
    uint32_t left = transactions - chunk * CANDIDATE_CHECKPOINT_ENTRIES;
    return left < CANDIDATE_CHECKPOINT_ENTRIES ? left : CANDIDATE_CHECKPOINT_ENTRIES;
}

candidate_wal_status_t candidate_publish_place_checkpoint(const candidate_wal_t *wal, candidate_wal_cursor_t *cursor,
    uint32_t transactions, uint64_t *lsns, uint64_t *end)
{
    if (transactions > CANDIDATE_CHECKPOINT_TRANSACTIONS) return CANDIDATE_WAL_LIMIT;
    uint32_t chunks = (transactions + CANDIDATE_CHECKPOINT_ENTRIES - 1) / CANDIDATE_CHECKPOINT_ENTRIES, n = 0;
    candidate_wal_status_t status = candidate_wal_place(wal,cursor,CHECKPOINT_BEGIN_BYTES,&lsns[n++]);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < chunks; i++)
        status = candidate_wal_place(wal,cursor,CHUNK_HEADER_BYTES + chunk_entries(transactions,i) * CHUNK_ENTRY_BYTES,&lsns[n++]);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_place(wal,cursor,CHECKPOINT_END_BYTES,&lsns[n]);
    if (status == CANDIDATE_WAL_OK) *end = cursor->position;
    return status;
}

candidate_wal_status_t candidate_publish_next(const publish_t *p, const candidate_control_info_t *control,
    uint64_t page_highwater, candidate_control_state_t *next)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    uint64_t checkpoint = 0, redo = p->anchor.redo_begin;
    for (unsigned i = 0; i < 2; i++) {
        if ((control->valid_mask & (1u << i)) == 0) continue;
        if (control->slots[i].checkpoint_id > checkpoint) checkpoint = control->slots[i].checkpoint_id;
        if (control->slots[i].redo_begin > redo) redo = control->slots[i].redo_begin;
    }
    if (checkpoint == UINT64_MAX) return CANDIDATE_WAL_LIMIT;
    *next = (candidate_control_state_t){.backend = CANDIDATE_CONTROL_WAL,.generation = 1,.owner_epoch = p->owner,
        .checkpoint_id = checkpoint + 1,.redo_begin = redo,.transaction_highwater = p->system_transaction_highwater,
        .page_highwater = page_highwater,.request_epoch = info.current_epoch,.retention_floor = info.retention_floor,
        .visible_csn = info.visible_csn,.outcome_root = p->anchor.outcome_root,.allocation_root = p->anchor.allocation_root,
        .catalog_root = p->anchor.catalog_root,.gc_horizon = p->allocation_root.gc_horizon};
    memcpy(next->uuid,p->recovery->uuid,16);
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_publish_check_control(const publish_t *p, const candidate_control_state_t *next)
{
    candidate_wal_status_t status = candidate_control_check_publication(p->recovery->control,next);
    return status == CANDIDATE_WAL_INVALID ? CANDIDATE_WAL_CORRUPT : status;
}

static int compare_transactions(const void *left, const void *right)
{
    const candidate_checkpoint_transaction_t *a = left, *b = right;
    if (a->id == b->id) return 0;
    return a->id < b->id ? -1 : 1;
}

static candidate_wal_status_t collect_transactions(const candidate_wal_witness_t *history,
    candidate_checkpoint_transaction_t *list, uint32_t count)
{
    if (count == 0) return CANDIDATE_WAL_OK;
    for (uint32_t i = 0; i < count; i++)
        if (!candidate_witness_checkpoint_at(history,i,&list[i])) return CANDIDATE_WAL_CORRUPT;
    qsort(list,count,sizeof(list[0]),compare_transactions);
    for (uint32_t i = 0; i < count; i++)
        if (list[i].state == CANDIDATE_CHECKPOINT_ACTIVE || (i > 0 && list[i].id == list[i - 1].id)) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t append_checkpoint(publish_t *p, const uint8_t *payload, size_t size, uint16_t type,
    uint64_t lsn, uint64_t *previous, uint8_t digest[32])
{
    candidate_wal_record_t r = {.type = type,.previous_lsn = *previous,.payload = payload,.payload_bytes = size};
    candidate_wal_status_t status = candidate_publish_append(p,&r,lsn);
    if (status == CANDIDATE_WAL_OK && type != 14) status = candidate_checkpoint_digest(digest,payload,size,digest);
    *previous = lsn;
    return status;
}

static candidate_wal_status_t write_records(publish_t *p, const candidate_checkpoint_transaction_t *list, uint32_t count,
    const uint64_t *lsns)
{
    candidate_checkpoint_summary_t summary = {.anchor = p->next,.transaction_count = count};
    summary.anchor.generation = 1; summary.anchor.slot = 0;
    uint8_t payload[CANDIDATE_CHECKPOINT_PAYLOAD], digest[32] = {0}; size_t size; uint64_t previous = 0; uint32_t n = 0;
    candidate_wal_status_t status = candidate_checkpoint_encode_begin(&summary,payload,sizeof(payload),&size);
    if (status == CANDIDATE_WAL_OK) status = append_checkpoint(p,payload,size,12,lsns[n++],&previous,digest);
    uint32_t chunks = (count + CANDIDATE_CHECKPOINT_ENTRIES - 1) / CANDIDATE_CHECKPOINT_ENTRIES;
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < chunks; i++) {
        candidate_checkpoint_chunk_t chunk = {.checkpoint_id = p->next.checkpoint_id,.kind = 2,.ordinal = i,.count = chunk_entries(count,i)};
        memcpy(chunk.entries.transactions,list + i * CANDIDATE_CHECKPOINT_ENTRIES,chunk.count * sizeof(list[0]));
        status = candidate_checkpoint_encode_chunk(&chunk,payload,sizeof(payload),&size);
        if (status == CANDIDATE_WAL_OK) status = append_checkpoint(p,payload,size,13,lsns[n++],&previous,digest);
    }
    candidate_checkpoint_end_t end = {.checkpoint_id = p->next.checkpoint_id,.begin_lsn = lsns[0],.chunks = chunks};
    memcpy(end.digest,digest,32);
    if (status == CANDIDATE_WAL_OK) status = candidate_checkpoint_encode_end(&end,payload,sizeof(payload),&size);
    if (status == CANDIDATE_WAL_OK) status = append_checkpoint(p,payload,size,14,lsns[n],&previous,digest);
    return status;
}

static candidate_wal_status_t bind_next(publish_t *p, const candidate_control_info_t *control, const candidate_wal_witness_t *history)
{
    candidate_witness_info_t info = candidate_wal_witness_info(history);
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(history);
    if (info.unfinished != 0 || info.incomplete_group_lsn != 0 || info.transport.has_tail) return CANDIDATE_WAL_STALE;
    if (!system.has_reservation || !system.has_epoch) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status = candidate_publish_next(p,control,system.page_highwater,&p->next);
    if (status != CANDIDATE_WAL_OK) return status;
    bool is_same = p->next.transaction_highwater == system.transaction_highwater && p->next.page_highwater == p->system_page_highwater;
    is_same = is_same && p->next.request_epoch == system.current_epoch && p->next.retention_floor == system.retention_floor;
    return is_same && p->next.visible_csn == info.highest_csn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_publish_checkpoint(publish_t *p, const candidate_control_info_t *control)
{
    candidate_witness_options_t request = {.memory = &p->memory,.enable_system_groups = true};
    memcpy(request.uuid,p->recovery->uuid,16);
    candidate_wal_witness_t *history = NULL;
    candidate_wal_status_t status = candidate_wal_witness_analyze_checkpoint(p->recovery->wal,&request,&p->anchor,&history);
    if (status == CANDIDATE_WAL_OK) status = bind_next(p,control,history);
    uint32_t count = candidate_wal_witness_info(history).transactions;
    candidate_checkpoint_transaction_t *list = NULL;
    if (status == CANDIDATE_WAL_OK && count > 0) {
        list = p->memory.allocate(p->memory.context,count * sizeof(*list));
        if (list == NULL) status = CANDIDATE_WAL_NOMEM;
    }
    if (status == CANDIDATE_WAL_OK) status = collect_transactions(history,list,count);
    candidate_wal_witness_close(&history);
    uint64_t lsns[2 + CANDIDATE_CHECKPOINT_CHUNKS], end = 0;
    candidate_wal_cursor_t cursor = candidate_wal_cursor(p->recovery->wal);
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_place_checkpoint(p->recovery->wal,&cursor,count,lsns,&end);
    if (status == CANDIDATE_WAL_OK) {
        p->next.checkpoint_begin = lsns[0]; p->next.checkpoint_end = end;
        status = candidate_publish_check_control(p,&p->next);
    }
    if (status == CANDIDATE_WAL_OK) status = write_records(p,list,count,lsns);
    if (list != NULL) p->memory.release(p->memory.context,list,count * sizeof(*list));
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(p->recovery->wal,end);
    if (status == CANDIDATE_WAL_OK) p->recovery->publication.checkpoint_id = p->next.checkpoint_id;
    return status;
}
