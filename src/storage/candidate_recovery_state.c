#include "candidate_recovery_internal.h"

#include <string.h>

#include "storage/candidate_allocation.h"
#include "candidate_outcome_witness_internal.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"

#define HISTORY_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)

typedef struct { candidate_page_ref_t ref; uint64_t lsn; } page_history_t;
typedef struct {
    candidate_catalog_load_t load;
    candidate_allocation_t *allocation;
    const candidate_wal_witness_t *witness;
    candidate_recovery_info_t info;
    candidate_wal_status_t status;
    uint32_t count, pending_count;
    bool allows_unfinished;
    candidate_page_ref_t pending[CANDIDATE_WITNESS_PAGES];
    page_history_t pages[HISTORY_SLOTS];
} state_t;
struct candidate_recovery_view {
    candidate_wal_memory_t memory;
    candidate_recovery_view_info_t info;
    state_t *graph;
    candidate_admissions_t *admissions;
    candidate_wal_witness_t *witness;
    candidate_catalog_t *catalog;
};

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

static bool same_page(candidate_page_ref_t a, candidate_page_ref_t b)
{
    return a.id == b.id && a.generation == b.generation;
}

static bool same_version(candidate_version_ref_t a, candidate_version_ref_t b)
{
    return same_page(a.page,b.page) && a.slot == b.slot && a.slot_generation == b.slot_generation;
}

static page_history_t *history_slot(state_t *s, uint64_t id)
{
    uint64_t hash = id * UINT64_C(11400714819323198485);
    unsigned slot = (unsigned)(hash ^ (hash >> 32)) & (HISTORY_SLOTS - 1);
    while (s->pages[slot].ref.id != 0 && s->pages[slot].ref.id != id) slot = (slot + 1) & (HISTORY_SLOTS - 1);
    return &s->pages[slot];
}

static bool observe_pages(void *context, const candidate_wal_record_t *r)
{
    state_t *s = context;
    if (r->type == 3) s->pending_count = 0;
    if (r->type == 4 || r->type == 5) {
        if (r->payload_bytes < 32 || s->pending_count == CANDIDATE_WITNESS_PAGES) {
            s->status = CANDIDATE_WAL_CORRUPT; return false;
        }
        s->pending[s->pending_count++] = (candidate_page_ref_t){read_le(r->payload + 8,8),read_le(r->payload + 16,8)};
    }
    if (r->type != 8) return true;
    for (unsigned i = 0; i < s->pending_count; i++) {
        candidate_page_ref_t ref = s->pending[i];
        if (ref.id == 0) { s->status = CANDIDATE_WAL_CORRUPT; return false; }
        page_history_t *p = history_slot(s,ref.id);
        if (p->ref.id == 0 && s->count++ == CANDIDATE_ALLOCATION_ENTRIES) {
            s->status = CANDIDATE_WAL_LIMIT; return false;
        }
        *p = (page_history_t){ref,r->lsn + 120};
    }
    s->pending_count = 0;
    return true;
}

static candidate_wal_status_t load_page(state_t *s, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    if (s->allocation != NULL) {
        candidate_allocation_entry_t entry;
        if (candidate_allocation_find(s->allocation,ref.id,&entry) != CANDIDATE_ALLOCATION_MATCH) return CANDIDATE_WAL_CORRUPT;
        if (entry.state != CANDIDATE_ALLOCATION_LIVE || !same_page(entry.ref,ref)) return CANDIDATE_WAL_CORRUPT;
    }
    candidate_wal_status_t status = s->load.load(s->load.context,ref,bytes);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info;
    status = candidate_page_decode_header(s->load.uuid,bytes,8192,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    const page_history_t *history = history_slot(s,ref.id);
    if (!same_page(info.location.ref,ref) || !same_page(history->ref,ref)) return CANDIDATE_WAL_CORRUPT;
    return history->lsn == info.location.lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t allocation_loader(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    return load_page(context,ref,bytes);
}

static candidate_wal_status_t check_transition(state_t *s, const candidate_allocation_entry_t *entry)
{
    candidate_witness_system_info_t info = candidate_wal_witness_system_info(s->witness);
    const candidate_witness_system_event_t *last = NULL;
    for (unsigned i = 0; i < info.events; i++) {
        const candidate_witness_system_event_t *event = candidate_wal_witness_system_at(s->witness,i);
        if (event->type != 6 && event->type != 7) continue;
        if (event->value.allocation.page_id == entry->ref.id) last = event;
    }
    if (last == NULL) {
        bool initial = entry->state == CANDIDATE_ALLOCATION_LIVE && entry->ref.generation == 1;
        return initial && entry->transition_lsn == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
    }
    if (entry->state != (last->type == 6 ? CANDIDATE_ALLOCATION_LIVE : CANDIDATE_ALLOCATION_FREE)) return CANDIDATE_WAL_CORRUPT;
    if (entry->ref.generation != last->value.allocation.next_generation) return CANDIDATE_WAL_CORRUPT;
    if (entry->release_horizon != last->value.allocation.release_horizon) return CANDIDATE_WAL_CORRUPT;
    return entry->transition_lsn == last->end_lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_reference(state_t *s, candidate_page_ref_t ref, unsigned type, unsigned kind)
{
    if (ref.id == 0) return CANDIDATE_WAL_OK;
    uint8_t bytes[8192]; candidate_wal_status_t status = load_page(s,ref,bytes);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info;
    status = candidate_page_decode_header(s->load.uuid,bytes,8192,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != type || read_le(bytes + 70,2) != kind) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t next_version(state_t *s, candidate_version_ref_t ref, candidate_version_ref_t *next)
{
    *next = (candidate_version_ref_t){0};
    if (ref.page.id == 0) return CANDIDATE_WAL_OK;
    uint8_t bytes[8192]; candidate_wal_status_t status = load_page(s,ref.page,bytes);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_version_t version;
    status = candidate_page_get_version(s->load.uuid,bytes,8192,ref,&version);
    if (status == CANDIDATE_WAL_OK) *next = version.previous;
    return status;
}

static candidate_wal_status_t check_version_chain(state_t *s, candidate_version_ref_t ref)
{
    candidate_version_ref_t slow = ref, fast = ref;
    do {
        candidate_wal_status_t status = next_version(s,slow,&slow);
        if (status == CANDIDATE_WAL_OK) status = next_version(s,fast,&fast);
        if (status == CANDIDATE_WAL_OK) status = next_version(s,fast,&fast);
        if (status != CANDIDATE_WAL_OK) return status;
        if (slow.page.id == 0 || fast.page.id == 0) return CANDIDATE_WAL_OK;
        if (same_version(slow,fast)) return CANDIDATE_WAL_CORRUPT;
    } while (true);
}

static candidate_wal_status_t next_overflow(state_t *s, candidate_page_ref_t ref, candidate_page_ref_t *next)
{
    *next = (candidate_page_ref_t){0};
    if (ref.id == 0) return CANDIDATE_WAL_OK;
    uint8_t bytes[8192]; candidate_wal_status_t status = load_page(s,ref,bytes);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_info_t info;
    status = candidate_page_decode(s->load.uuid,bytes,8192,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.type != 2) return CANDIDATE_WAL_CORRUPT;
    *next = (candidate_page_ref_t){read_le(bytes + 64,8),read_le(bytes + 72,8)};
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_overflow_chain(state_t *s, candidate_page_ref_t ref)
{
    candidate_page_ref_t slow = ref, fast = ref;
    do {
        candidate_wal_status_t status = next_overflow(s,slow,&slow);
        if (status == CANDIDATE_WAL_OK) status = next_overflow(s,fast,&fast);
        if (status == CANDIDATE_WAL_OK) status = next_overflow(s,fast,&fast);
        if (status != CANDIDATE_WAL_OK) return status;
        if (slow.id == 0 || fast.id == 0) return CANDIDATE_WAL_OK;
        if (same_page(slow,fast)) return CANDIDATE_WAL_CORRUPT;
    } while (true);
}

static candidate_wal_status_t check_payload(state_t *s, const candidate_page_version_t *version)
{
    candidate_page_ref_t ref = version->overflow;
    if (ref.id == 0) return CANDIDATE_WAL_OK;
    uint64_t remaining = version->payload_bytes;
    unsigned limit = candidate_allocation_info(s->allocation).count;
    for (unsigned i = 0; ref.id != 0 && i < limit; i++) {
        uint8_t bytes[8192]; candidate_wal_status_t status = load_page(s,ref,bytes);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_page_info_t info; status = candidate_page_decode(s->load.uuid,bytes,8192,&info);
        if (status != CANDIDATE_WAL_OK) return status;
        if (info.type != 2) return CANDIDATE_WAL_CORRUPT;
        uint64_t size = read_le(bytes + 80,4), expected = remaining < 8104 ? remaining : 8104;
        if (size == 0 || size != expected) return CANDIDATE_WAL_CORRUPT;
        remaining -= size; ref = (candidate_page_ref_t){read_le(bytes + 64,8),read_le(bytes + 72,8)};
        if (ref.id == 0) return remaining == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_versions(state_t *s, const uint8_t *bytes, const candidate_page_info_t *info)
{
    for (unsigned i = 0; i < info->slots; i++) {
        candidate_version_ref_t ref = {info->location.ref,i,(uint32_t)read_le(bytes + 76 + i * 8,4)};
        candidate_page_version_t v; candidate_wal_status_t status = candidate_page_get_version(s->load.uuid,bytes,8192,ref,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        const candidate_witness_transaction_t *t = candidate_wal_witness_find(s->witness,v.transaction_id);
        if (t == NULL) return CANDIDATE_WAL_CORRUPT;
        bool is_unfinished_preview = s->allows_unfinished && v.frozen_csn == 0;
        if (t->state == CANDIDATE_WITNESS_UNFINISHED && !is_unfinished_preview) return CANDIDATE_WAL_STALE;
        if (v.frozen_csn != 0 && (t->state != CANDIDATE_WITNESS_COMMIT || v.frozen_csn != t->csn)) return CANDIDATE_WAL_CORRUPT;
        status = check_version_chain(s,ref);
        if (status == CANDIDATE_WAL_OK) status = check_payload(s,&v);
        if (status != CANDIDATE_WAL_OK) return status;
        s->info.versions++;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_metadata(state_t *s, const uint8_t *bytes, unsigned type)
{
    unsigned kind = (unsigned)read_le(bytes + 70,2);
    if (type == 4 && kind == 1) {
        candidate_catalog_root_t root; candidate_wal_status_t status = candidate_catalog_decode_root(s->load.uuid,bytes,8192,&root);
        if (status != CANDIDATE_WAL_OK) return status;
        unsigned count = (root.count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
        for (unsigned i = 0; i < count; i++) { status = check_reference(s,root.pages[i],4,2); if (status != CANDIDATE_WAL_OK) return status; }
        return CANDIDATE_WAL_OK;
    }
    if (type == 4 && kind == 2) {
        candidate_catalog_node_t node; candidate_wal_status_t status = candidate_catalog_decode_node(s->load.uuid,bytes,8192,&node);
        if (status != CANDIDATE_WAL_OK) return status;
        for (unsigned i = 0; i < node.count; i++) {
            candidate_version_ref_t ref = node.entries[i].result;
            if (ref.page.id == 0) continue;
            status = check_version_chain(s,ref); if (status != CANDIDATE_WAL_OK) return status;
        }
        return CANDIDATE_WAL_OK;
    }
    if (type == 5 && kind == 1) {
        candidate_allocation_root_t root; candidate_wal_status_t status = candidate_allocation_decode_root(s->load.uuid,bytes,8192,&root);
        if (status != CANDIDATE_WAL_OK) return status;
        unsigned count = (root.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
        for (unsigned i = 0; i < count; i++) { status = check_reference(s,root.nodes[i],5,2); if (status != CANDIDATE_WAL_OK) return status; }
        return CANDIDATE_WAL_OK;
    }
    if (type == 5 && kind == 2) {
        candidate_allocation_node_t node; return candidate_allocation_decode_node(s->load.uuid,bytes,8192,&node);
    }
    return CANDIDATE_WAL_UNSUPPORTED;
}

static candidate_wal_status_t check_pages(state_t *s)
{
    candidate_allocation_info_t info = candidate_allocation_info(s->allocation);
    if (info.count != s->count) return CANDIDATE_WAL_CORRUPT;
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(s->witness);
    candidate_witness_info_t observed = candidate_wal_witness_info(s->witness);
    if (info.owner_epoch > observed.transport.highest_owner_epoch || info.gc_horizon > observed.highest_csn) return CANDIDATE_WAL_CORRUPT;
    if (system.has_reservation && info.page_highwater != system.page_highwater) return CANDIDATE_WAL_STALE;
    if (info.owner_epoch < s->info.anchor.owner_epoch || info.gc_horizon < s->info.anchor.gc_horizon) return CANDIDATE_WAL_STALE;
    for (unsigned i = 0; i < info.count; i++) {
        candidate_allocation_entry_t e; candidate_allocation_at(s->allocation,i,&e);
        candidate_wal_status_t status = check_transition(s,&e); if (status != CANDIDATE_WAL_OK) return status;
        if (!same_page(history_slot(s,e.ref.id)->ref,e.ref)) return CANDIDATE_WAL_CORRUPT;
        if (e.state == CANDIDATE_ALLOCATION_FREE) { s->info.free_pages++; continue; }
        uint8_t bytes[8192]; status = load_page(s,e.ref,bytes); if (status != CANDIDATE_WAL_OK) return status;
        candidate_page_info_t page; status = candidate_page_decode_header(s->load.uuid,bytes,8192,&page);
        if (status != CANDIDATE_WAL_OK) return status;
        if (page.type == 1 || page.type == 2) status = candidate_page_decode(s->load.uuid,bytes,8192,&page);
        else status = check_metadata(s,bytes,page.type);
        if (status != CANDIDATE_WAL_OK) return status;
        if (page.type == 1) status = check_versions(s,bytes,&page);
        if (page.type == 2) status = check_overflow_chain(s,e.ref);
        if (status != CANDIDATE_WAL_OK) return status;
        s->info.live_pages++;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t open_state(const candidate_wal_memory_t *memory, const candidate_recovery_info_t *info,
    bool allows_unfinished, state_t **out)
{
    state_t *s = memory->allocate(memory->context,sizeof(*s));
    if (s == NULL) return CANDIDATE_WAL_NOMEM;
    memset(s,0,sizeof(*s)); s->info = *info; s->allows_unfinished = allows_unfinished;
    *out = s;
    return CANDIDATE_WAL_OK;
}

static void close_state(state_t **out, const candidate_wal_memory_t *memory)
{
    if (*out == NULL) return;
    state_t *s = *out;
    *out = NULL;
    candidate_allocation_close(&s->allocation);
    memory->release(memory->context,s,sizeof(*s));
}

static candidate_wal_status_t check_graph(state_t *s, const candidate_control_state_t *anchor,
    const candidate_catalog_load_t *load, const candidate_wal_witness_t *witness)
{
    s->load = *load; s->witness = witness;
    candidate_witness_info_t observed = candidate_wal_witness_info(witness);
    bool is_resolved = observed.unfinished == 0 && observed.incomplete_group_lsn == 0;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (!is_resolved && !s->allows_unfinished) status = CANDIDATE_WAL_STALE;
    s->info.wal_end_lsn = observed.transport.valid_end_lsn;
    if (status == CANDIDATE_WAL_OK) {
        candidate_allocation_load_t request = {.root = anchor->allocation_root,.load = allocation_loader,.context = s,.memory = load->memory};
        memcpy(request.uuid,load->uuid,16); status = candidate_allocation_load(&request,&s->allocation);
    }
    if (status == CANDIDATE_WAL_OK) status = check_reference(s,anchor->outcome_root,4,1);
    if (status == CANDIDATE_WAL_OK) status = check_pages(s);
    s->load = (candidate_catalog_load_t){0}; s->witness = NULL;
    return status;
}

static candidate_wal_status_t validate(candidate_wal_t *wal, const candidate_control_state_t *anchor,
    const candidate_catalog_load_t *load, candidate_recovery_info_t *info, bool allows_unfinished)
{
    candidate_wal_memory_t memory = *load->memory;
    candidate_catalog_load_t saved = *load;
    saved.memory = &memory;
    state_t *s = NULL;
    candidate_wal_status_t status = open_state(&memory,info,allows_unfinished,&s);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_witness_options_t options = {.memory = &memory,.enable_system_groups = true}; memcpy(options.uuid,saved.uuid,16);
    candidate_wal_witness_t *witness = NULL;
    status = candidate_witness_analyze_observed(wal,&options,anchor,observe_pages,s,&witness);
    if (status == CANDIDATE_WAL_STOPPED && s->status != CANDIDATE_WAL_OK) status = s->status;
    if (status == CANDIDATE_WAL_OK) status = check_graph(s,anchor,&saved,witness);
    if (status == CANDIDATE_WAL_OK) *info = s->info;
    candidate_wal_witness_close(&witness); close_state(&s,&memory);
    return status;
}

candidate_wal_status_t candidate_recovery_validate(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info)
{
    return validate(wal,anchor,load,info,false);
}

candidate_wal_status_t candidate_recovery_validate_preview(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info)
{
    return validate(wal,anchor,load,info,true);
}

static bool observe_view(void *context, const candidate_wal_record_t *record)
{
    candidate_recovery_view_t *v = context;
    /* admission 실패는 graph 상태를 먼저 돌려주던 순서를 지키도록 기록만 하고 page 관측은 계속한다. */
    if (candidate_admissions_status(v->admissions) == CANDIDATE_WAL_OK) (void)candidate_admissions_observe(v->admissions,record);
    return observe_pages(v->graph,record);
}

static candidate_wal_status_t check_request(const candidate_recovery_view_request_t *r)
{
    const candidate_wal_memory_t *memory = r->load.memory;
    if (memory == NULL || r->load.load == NULL) return CANDIDATE_WAL_INVALID;
    if (memory->allocate == NULL || memory->release == NULL) return CANDIDATE_WAL_INVALID;
    bool is_preview = r->mode == CANDIDATE_RECOVERY_VIEW_PREVIEW;
    if (!is_preview && r->mode != CANDIDATE_RECOVERY_VIEW_RESOLVED) return CANDIDATE_WAL_INVALID;
    if (is_preview && r->owner_epoch == 0) return CANDIDATE_WAL_INVALID;
    return same_page(r->load.root,r->anchor.outcome_root) ? CANDIDATE_WAL_OK : CANDIDATE_WAL_INVALID;
}

static candidate_wal_status_t observe_anchor(candidate_recovery_view_t *v, candidate_wal_t *wal, const uint8_t uuid[16])
{
    candidate_recovery_info_t initial = {.anchor = v->info.anchor};
    bool is_preview = v->info.mode == CANDIDATE_RECOVERY_VIEW_PREVIEW;
    candidate_wal_status_t status = open_state(&v->memory,&initial,is_preview,&v->graph);
    if (status == CANDIDATE_WAL_OK) status = candidate_admissions_create(&v->memory,NULL,&v->admissions);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_witness_options_t options = {.memory = &v->memory,.enable_system_groups = true}; memcpy(options.uuid,uuid,16);
    status = candidate_witness_analyze_observed(wal,&options,&v->info.anchor,observe_view,v,&v->witness);
    if (status == CANDIDATE_WAL_STOPPED && v->graph->status != CANDIDATE_WAL_OK) status = v->graph->status;
    if (status == CANDIDATE_WAL_OK) v->info.cut = candidate_wal_witness_info(v->witness).transport;
    return status;
}

static candidate_wal_status_t verify_outcomes(candidate_recovery_view_t *v, candidate_wal_t *wal,
    const candidate_catalog_load_t *load)
{
    candidate_wal_status_t status = candidate_catalog_load(load,&v->catalog);
    if (status == CANDIDATE_WAL_OK) status = candidate_admissions_status(v->admissions);
    if (status != CANDIDATE_WAL_OK) return status;
    if (v->info.mode == CANDIDATE_RECOVERY_VIEW_PREVIEW)
        return candidate_outcome_check_preview(v->catalog,load,&v->info.anchor,v->witness,v->admissions,v->info.owner_epoch);
    if (v->info.cut.has_tail) return CANDIDATE_WAL_TAIL;
    if (candidate_wal_info(wal).durable_end_lsn < v->info.cut.valid_end_lsn) return CANDIDATE_WAL_STALE;
    return candidate_outcome_check_resolved(v->catalog,load,v->witness,v->admissions,&v->info.outcomes);
}

static bool is_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    if (a->records != b->records || a->valid_end_lsn != b->valid_end_lsn) return false;
    if (a->segments != b->segments || a->highest_owner_epoch != b->highest_owner_epoch) return false;
    return a->has_tail == b->has_tail && a->tail_bytes == b->tail_bytes;
}

static candidate_wal_status_t check_cut(const candidate_recovery_view_t *v, candidate_wal_t *wal, const uint8_t uuid[16])
{
    candidate_wal_status_t status = candidate_wal_check_uuid(wal,uuid);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_info_t current = candidate_wal_info(wal);
    return is_same_cut(&v->info.cut,&current) ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE;
}

static void settle_view(candidate_recovery_view_t *v)
{
    const candidate_recovery_info_t *graph = &v->graph->info;
    v->info.live_pages = graph->live_pages; v->info.free_pages = graph->free_pages; v->info.versions = graph->versions;
    v->info.outcomes.wal_end_lsn = v->info.cut.valid_end_lsn;
    size_t bytes = sizeof(*v) + sizeof(*v->graph) + candidate_allocation_info(v->graph->allocation).resident_bytes;
    bytes += candidate_admissions_resident_bytes(v->admissions) + candidate_wal_witness_info(v->witness).resident_bytes;
    v->info.resident_bytes = bytes + candidate_catalog_resident_bytes(v->catalog);
}

candidate_wal_status_t candidate_recovery_view_open(candidate_wal_t *wal,
    const candidate_recovery_view_request_t *request, candidate_recovery_view_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (wal == NULL || request == NULL) return CANDIDATE_WAL_INVALID;
    candidate_recovery_view_request_t saved = *request;
    candidate_wal_status_t status = check_request(&saved);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_memory_t memory = *saved.load.memory;
    status = candidate_wal_check_uuid(wal,saved.load.uuid);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_recovery_view_t *v = memory.allocate(memory.context,sizeof(*v));
    if (v == NULL) return CANDIDATE_WAL_NOMEM;
    memset(v,0,sizeof(*v)); v->memory = memory;
    v->info = (candidate_recovery_view_info_t){.anchor = saved.anchor,.mode = saved.mode,.owner_epoch = saved.owner_epoch};
    saved.load.memory = &v->memory;
    status = observe_anchor(v,wal,saved.load.uuid);
    if (status == CANDIDATE_WAL_OK) status = check_graph(v->graph,&v->info.anchor,&saved.load,v->witness);
    if (status == CANDIDATE_WAL_OK) status = verify_outcomes(v,wal,&saved.load);
    if (status == CANDIDATE_WAL_OK) status = check_cut(v,wal,saved.load.uuid);
    if (status != CANDIDATE_WAL_OK) { candidate_recovery_view_close(&v); return status; }
    settle_view(v);
    *out = v;
    return CANDIDATE_WAL_OK;
}

candidate_recovery_view_info_t candidate_recovery_view_info(const candidate_recovery_view_t *v)
{
    return v == NULL ? (candidate_recovery_view_info_t){0} : v->info;
}

void candidate_recovery_view_fill(const candidate_recovery_view_t *v, candidate_recovery_info_t *info)
{
    if (v == NULL || info == NULL) return;
    info->live_pages = v->info.live_pages; info->free_pages = v->info.free_pages;
    info->versions = v->info.versions; info->wal_end_lsn = v->info.cut.valid_end_lsn;
}

candidate_catalog_lookup_t candidate_recovery_view_lookup(const candidate_recovery_view_t *v,
    const candidate_catalog_query_t *query, const candidate_outcome_t **out)
{
    bool is_resolved = v != NULL && v->info.mode == CANDIDATE_RECOVERY_VIEW_RESOLVED;
    return candidate_catalog_lookup(is_resolved ? v->catalog : NULL,query,out);
}

void candidate_recovery_view_close(candidate_recovery_view_t **out)
{
    if (out == NULL || *out == NULL) return;
    candidate_recovery_view_t *v = *out;
    *out = NULL;
    candidate_wal_memory_t memory = v->memory;
    candidate_catalog_close(&v->catalog);
    candidate_wal_witness_close(&v->witness);
    candidate_admissions_close(&v->admissions);
    close_state(&v->graph,&memory);
    memory.release(memory.context,v,sizeof(*v));
}
