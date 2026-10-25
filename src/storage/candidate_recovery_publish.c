#include "candidate_recovery_publish_internal.h"

#include <string.h>
#include <sys/stat.h>

#include "candidate_outcome_internal.h"
#include "candidate_redo_internal.h"

static candidate_catalog_load_t catalog_load(candidate_recovery_t *r, const candidate_control_state_t *anchor,
    candidate_page_loader_t loader, void *context)
{
    candidate_catalog_load_t load = {.root = anchor->outcome_root,.load = loader,.context = context,.memory = &r->memory};
    memcpy(load.uuid,r->uuid,16);
    return load;
}

static candidate_wal_status_t preview_anchor(candidate_recovery_t *r, const candidate_control_state_t *anchor)
{
    candidate_outcome_witness_close(&r->outcomes);
    r->info = (candidate_recovery_info_t){.anchor = *anchor};
    if (anchor->catalog_root.id != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_redo_options_t redo = {.checkpoint = anchor,.target = candidate_recovery_target(r),.memory = &r->memory};
    memcpy(redo.uuid,r->uuid,16);
    candidate_redo_preview_t *preview = NULL;
    candidate_wal_status_t status = candidate_redo_preview_open(r->wal,&redo,&preview);
    candidate_catalog_load_t load = catalog_load(r,anchor,candidate_redo_preview_load,preview);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_validate_preview(r->wal,anchor,&load,&r->info);
    if (status == CANDIDATE_WAL_OK) {
        candidate_outcome_witness_options_t options = {.catalog = load,.checkpoint = anchor};
        status = candidate_outcome_preview(r->wal,&options,r->owner_epoch);
    }
    if (status == CANDIDATE_WAL_OK) r->info.redo = candidate_redo_preview_info(preview);
    candidate_redo_preview_close(&preview);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_check(r);
    return status;
}

static candidate_wal_status_t verify_recovery(void *context, const candidate_control_state_t *anchor)
{
    candidate_recovery_t *r = context;
    return r->is_ready_verify ? candidate_recovery_verify(r,anchor) : preview_anchor(r,anchor);
}

static candidate_wal_status_t restore(publish_t *p)
{
    candidate_recovery_t *r = p->recovery;
    candidate_terminal_options_t terminal = {.owner_epoch = p->owner,.anchor = &p->anchor,.memory = &r->memory,
        .allow_group_tail = true};
    memcpy(terminal.uuid,r->uuid,16);
    candidate_wal_status_t status = candidate_terminal_repair_wal(r->wal,&terminal,&r->terminal_history,&r->publication.repair);
    candidate_redo_options_t redo = {.checkpoint = &p->anchor,.target = candidate_recovery_target(r),.memory = &r->memory};
    memcpy(redo.uuid,r->uuid,16);
    if (status == CANDIDATE_WAL_OK) status = candidate_redo_execute(r->wal,&redo,&r->info.redo);
    candidate_catalog_load_t load = catalog_load(r,&p->anchor,candidate_recovery_load,r);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_validate(r->wal,&p->anchor,&load,&r->info);
    p->rebuild_options = (candidate_outcome_rebuild_options_t){.source = {.catalog = load,.checkpoint = &p->anchor},
        .owner_epoch = p->owner};
    p->rebuild_options.source.catalog.memory = &p->memory;
    if (status == CANDIDATE_WAL_OK) status = candidate_outcome_rebuild_load(r->wal,&p->rebuild_options,&p->rebuild);
    return status;
}

static candidate_wal_status_t record_receipt(publish_t *p)
{
    candidate_recovery_publication_t *receipt = &p->recovery->publication;
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    receipt->outcomes = info.count; receipt->nodes = info.nodes;
    receipt->new_nodes = p->new_nodes; receipt->reused_nodes = info.nodes - p->new_nodes;
    receipt->allocation_pages = p->page_count; receipt->reserved_pages = p->reserve_pages;
    receipt->reused_reserved_pages = p->reusable_count; receipt->page_highwater_after = p->system_page_highwater;
    uint64_t maintenance = p->carrier_new == 2 ? 1 : 0;
    for (uint32_t i = 0; i < p->page_count; i++) maintenance += p->pages[i].role != ROLE_OUTCOME_NODE;
    struct stat st;
    if (fstat(p->recovery->fd,&st) != 0) return CANDIDATE_WAL_IO;
    p->recovery->extent = (candidate_recovery_extent_t){.data_file_bytes = (uint64_t)st.st_size,
        .maintenance_pages = p->maintenance_pages + maintenance,.allocated_pages = p->carrier_new + p->page_count,
        .carrier_pages = p->carrier_new,.protected_pages = receipt->protected_pages};
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t publish(publish_t *p)
{
    candidate_recovery_t *r = p->recovery;
    if (p->owner <= p->anchor.owner_epoch) return CANDIDATE_WAL_STALE;
    candidate_control_info_t control = candidate_control_info(r->control);
    candidate_wal_status_t status = p->needs_rewrite ? candidate_publish_plan_pages(p) : CANDIDATE_WAL_OK;
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_preflight(p,&control);
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_reserve(p);
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_stage(p);
    candidate_wal_witness_close(&r->terminal_history);
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_checkpoint(p,&control);
    if (status != CANDIDATE_WAL_OK) return status;
    r->is_ready_verify = true;
    status = candidate_control_publish(r->control,&p->next);
    if (status == CANDIDATE_WAL_OK) r->publication.has_publication = true;
    return status;
}

static candidate_wal_status_t recover_owned(candidate_recovery_t *r, uint64_t owner)
{
    if (owner < r->info.anchor.owner_epoch) return CANDIDATE_WAL_STALE;
    publish_t *p = r->memory.allocate(r->memory.context,sizeof(*p));
    if (p == NULL) return CANDIDATE_WAL_NOMEM;
    memset(p,0,sizeof(*p));
    p->recovery = r; p->memory = r->memory; p->anchor = r->info.anchor; p->owner = owner;
    r->publication = (candidate_recovery_publication_t){.page_highwater_before = p->anchor.page_highwater};
    r->extent = (candidate_recovery_extent_t){0};
    candidate_wal_status_t status = restore(p);
    if (status == CANDIDATE_WAL_OK) status = candidate_publish_inventory(p);
    if (status == CANDIDATE_WAL_OK) r->publication.page_highwater_before = p->system_page_highwater;
    bool is_published = status == CANDIDATE_WAL_OK && !p->needs_rewrite && p->anchor.owner_epoch == owner;
    if (is_published) {
        r->is_ready_verify = true;
        candidate_wal_witness_close(&r->terminal_history);
        status = candidate_recovery_verify(r,&p->anchor);
    } else if (status == CANDIDATE_WAL_OK) status = publish(p);
    if (status == CANDIDATE_WAL_OK) status = record_receipt(p);
    candidate_outcome_rebuild_close(&p->rebuild);
    candidate_allocation_close(&p->allocation);
    r->memory.release(r->memory.context,p,sizeof(*p));
    return status;
}

candidate_wal_status_t candidate_recovery_recover(const candidate_recovery_options_t *options, candidate_recovery_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (options == NULL) return CANDIDATE_WAL_INVALID;
    uint64_t owner = options->owner_epoch;
    candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_own(options,verify_recovery,&r);
    if (status != CANDIDATE_WAL_OK) return status;
    r->is_busy = true;
    status = recover_owned(r,owner);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_check(r);
    r->is_busy = false;
    if (status != CANDIDATE_WAL_OK) { candidate_recovery_close(&r); return status; }
    r->info.resident_bytes = candidate_recovery_resident_bytes(r);
    *out = r;
    return CANDIDATE_WAL_OK;
}

candidate_recovery_publication_t candidate_recovery_publication(const candidate_recovery_t *r)
{
    return r == NULL ? (candidate_recovery_publication_t){0} : r->publication;
}

candidate_recovery_extent_t candidate_recovery_publication_extent(const candidate_recovery_t *r)
{
    return r == NULL ? (candidate_recovery_extent_t){0} : r->extent;
}
