#include "candidate_recovery_publish_internal.h"

#include <string.h>
#include <sys/stat.h>

#include "storage/candidate_digest.h"

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

static unsigned set_slot(const uint64_t *slots, uint64_t id)
{
    uint64_t hash = id * UINT64_C(11400714819323198485);
    unsigned slot = (unsigned)(hash ^ (hash >> 32)) & (PUBLISH_SET_SLOTS - 1);
    while (slots[slot] != 0 && slots[slot] != id) slot = (slot + 1) & (PUBLISH_SET_SLOTS - 1);
    return slot;
}

bool candidate_publish_set_add(uint64_t *slots, uint64_t id, bool *added)
{
    unsigned slot = set_slot(slots,id);
    *added = slots[slot] == 0;
    if (*added) slots[slot] = id;
    return true;
}

bool candidate_publish_set_has(const uint64_t *slots, uint64_t id)
{
    return slots[set_slot(slots,id)] == id;
}

static candidate_wal_status_t add_protected(publish_t *p, uint64_t id, bool *added)
{
    *added = false;
    if (candidate_publish_set_has(p->protected_ids,id)) return CANDIDATE_WAL_OK;
    if (p->protected_count == CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
    candidate_publish_set_add(p->protected_ids,id,added);
    if (*added) p->protected_count++;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t load_page(publish_t *p, candidate_page_ref_t ref, uint8_t bytes[CANDIDATE_PAGE_BYTES],
    candidate_page_info_t *info)
{
    candidate_wal_status_t status = candidate_recovery_load(p->recovery,ref,bytes);
    if (status == CANDIDATE_WAL_OK) status = candidate_page_decode_header(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,info);
    if (status != CANDIDATE_WAL_OK) return status;
    bool is_same = info->location.ref.id == ref.id && info->location.ref.generation == ref.generation;
    return is_same ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

/* 엄격 검증이 LIVE·비순환을 확인한 VersionRef를 allocation 순번×slot bitmap으로 한 번만 방문한다. */
static candidate_wal_status_t version_bit(publish_t *p, candidate_version_ref_t ref, uint64_t *bit)
{
    candidate_allocation_info_t info = candidate_allocation_info(p->allocation);
    uint32_t lo = 0, hi = info.count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2; candidate_allocation_entry_t e; candidate_allocation_at(p->allocation,mid,&e);
        if (e.ref.id < ref.page.id) lo = mid + 1; else hi = mid;
    }
    candidate_allocation_entry_t e;
    if (lo == info.count || candidate_allocation_at(p->allocation,lo,&e) != CANDIDATE_ALLOCATION_MATCH) return CANDIDATE_WAL_CORRUPT;
    if (e.ref.id != ref.page.id || e.ref.generation != ref.page.generation || e.state != CANDIDATE_ALLOCATION_LIVE) return CANDIDATE_WAL_CORRUPT;
    if (ref.slot >= CANDIDATE_PAGE_MAX_SLOTS) return CANDIDATE_WAL_CORRUPT;
    *bit = (uint64_t)lo * CANDIDATE_PAGE_MAX_SLOTS + ref.slot;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t protect_overflow(publish_t *p, candidate_page_ref_t next)
{
    uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    for (unsigned i = 0; next.id != 0; i++) {
        if (i == CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_CORRUPT;
        bool added; candidate_wal_status_t status = add_protected(p,next.id,&added);
        if (status != CANDIDATE_WAL_OK) return status;
        /* 이미 센 overflow page의 다음 연결은 그 page를 처음 센 경로가 끝까지 셌다. */
        if (!added) return CANDIDATE_WAL_OK;
        status = load_page(p,next,bytes,&info);
        if (status == CANDIDATE_WAL_OK && info.type != 2) status = CANDIDATE_WAL_CORRUPT;
        if (status != CANDIDATE_WAL_OK) return status;
        next = (candidate_page_ref_t){read_le(bytes + 64,8),read_le(bytes + 72,8)};
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t protect_result(publish_t *p, candidate_version_ref_t ref)
{
    for (uint64_t steps = 0; ref.page.id != 0; steps++) {
        if (steps > PUBLISH_VERSION_BITS) return CANDIDATE_WAL_CORRUPT;
        uint64_t bit; candidate_wal_status_t status = version_bit(p,ref,&bit);
        if (status != CANDIDATE_WAL_OK) return status;
        if (p->visited[bit / 8] & (1u << (bit % 8))) return CANDIDATE_WAL_OK;
        p->visited[bit / 8] |= (uint8_t)(1u << (bit % 8));
        bool added; status = add_protected(p,ref.page.id,&added);
        uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info; candidate_page_version_t version = {0};
        if (status == CANDIDATE_WAL_OK) status = load_page(p,ref.page,bytes,&info);
        if (status == CANDIDATE_WAL_OK) status = candidate_page_get_version(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,ref,&version);
        if (status == CANDIDATE_WAL_OK) status = protect_overflow(p,version.overflow);
        if (status != CANDIDATE_WAL_OK) return status;
        ref = version.previous;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t pool_node(publish_t *p, candidate_page_ref_t ref, const uint8_t *bytes,
    const candidate_page_info_t *info)
{
    candidate_catalog_node_t node;
    candidate_wal_status_t status = candidate_catalog_decode_node(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,&node);
    if (status != CANDIDATE_WAL_OK) return status;
    if (p->pool_count == PUBLISH_POOL) return CANDIDATE_WAL_LIMIT;
    pooled_node_t *entry = &p->pool[p->pool_count++];
    *entry = (pooled_node_t){.ref = ref,.ordinal = node.ordinal,.count = node.count};
    status = candidate_digest_sha256(bytes + 64,info->body_bytes,entry->digest);
    for (unsigned i = 0; status == CANDIDATE_WAL_OK && i < node.count; i++) status = protect_result(p,node.entries[i].result);
    return status;
}

static candidate_wal_status_t inventory_live(publish_t *p)
{
    candidate_allocation_info_t info = candidate_allocation_info(p->allocation);
    for (unsigned i = 0; i < info.count; i++) {
        candidate_allocation_entry_t entry; candidate_allocation_at(p->allocation,i,&entry);
        p->max_allocated_id = entry.ref.id;
        if (entry.state != CANDIDATE_ALLOCATION_LIVE) continue;
        uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t page;
        candidate_wal_status_t status = load_page(p,entry.ref,bytes,&page);
        if (status != CANDIDATE_WAL_OK) return status;
        if (page.type == 5) { p->maintenance_pages++; continue; }
        if (page.type != 4) continue;
        bool added; status = add_protected(p,entry.ref.id,&added);
        if (status == CANDIDATE_WAL_OK && read_le(bytes + 70,2) == 2) status = pool_node(p,entry.ref,bytes,&page);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t load_roots(publish_t *p)
{
    uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    candidate_wal_status_t status = load_page(p,p->anchor.allocation_root,bytes,&info);
    if (status == CANDIDATE_WAL_OK) status = candidate_allocation_decode_root(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,&p->allocation_root);
    if (status != CANDIDATE_WAL_OK) return status;
    p->allocation_page = (tracked_page_t){p->anchor.allocation_root,info.location.lsn};
    p->allocation_count = p->allocation_root.count;
    p->allocation_nodes = (p->allocation_count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    if (p->allocation_nodes == 0) return CANDIDATE_WAL_CORRUPT;
    candidate_page_ref_t tail = p->allocation_root.nodes[p->allocation_nodes - 1];
    status = load_page(p,tail,bytes,&info);
    if (status == CANDIDATE_WAL_OK) status = candidate_allocation_decode_node(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,&p->tail);
    if (status != CANDIDATE_WAL_OK) return status;
    p->tail_page = (tracked_page_t){tail,info.location.lsn};
    p->node_pages[p->allocation_nodes - 1] = p->tail_page;
    status = load_page(p,p->anchor.outcome_root,bytes,&info);
    if (status == CANDIDATE_WAL_OK) status = candidate_catalog_decode_root(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,&p->catalog_root);
    if (status == CANDIDATE_WAL_OK) p->catalog_page = (tracked_page_t){p->anchor.outcome_root,info.location.lsn};
    return status;
}

static bool visit_history(void *context, const candidate_wal_record_t *r)
{
    publish_t *p = context; bool added;
    if (r->type == 3 && r->payload_bytes == 16 && read_le(r->payload,8) > p->max_group_id) p->max_group_id = read_le(r->payload,8);
    if ((r->type == 4 || r->type == 5) && r->payload_bytes >= 32) candidate_publish_set_add(p->history_ids,read_le(r->payload + 8,8),&added);
    return true;
}

static candidate_wal_status_t encode_desired(const publish_t *p, uint32_t ordinal, uint8_t bytes[CANDIDATE_PAGE_BYTES],
    uint8_t digest[32], uint32_t *body)
{
    candidate_page_location_t location = {.ref = {1,1}}; memcpy(location.uuid,p->recovery->uuid,16);
    candidate_wal_status_t status = candidate_outcome_rebuild_encode_node(p->rebuild,ordinal,&location,bytes,CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    *body = (uint32_t)read_le(bytes + 48,4);
    return candidate_digest_sha256(bytes + 64,*body,digest);
}

static candidate_wal_status_t is_identical(publish_t *p, candidate_page_ref_t ref, const uint8_t *desired,
    uint32_t body, bool *out)
{
    uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    candidate_wal_status_t status = load_page(p,ref,bytes,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = info.type == 4 && info.body_bytes == body && memcmp(bytes + 64,desired + 64,body) == 0;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t find_reusable(publish_t *p, uint32_t ordinal, const uint8_t *desired,
    const uint8_t digest[32], uint32_t body)
{
    uint32_t stored = (p->catalog_root.count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    bool found = false;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (ordinal < stored) status = is_identical(p,p->catalog_root.pages[ordinal],desired,body,&found);
    if (found) p->desired_nodes[ordinal] = p->catalog_root.pages[ordinal];
    for (unsigned i = 0; status == CANDIDATE_WAL_OK && !found && i < p->pool_count; i++) {
        const pooled_node_t *e = &p->pool[i];
        if (e->ordinal != ordinal || memcmp(e->digest,digest,32) != 0) continue;
        status = is_identical(p,e->ref,desired,body,&found);
        if (found) p->desired_nodes[ordinal] = e->ref;
    }
    p->reused_node[ordinal] = found;
    if (!found) p->new_nodes++;
    return status;
}

static candidate_wal_status_t set_retained(publish_t *p, uint32_t extra_pages)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    uint64_t pages = (uint64_t)p->protected_count + p->new_nodes + extra_pages;
    p->recovery->publication.protected_pages = pages;
    p->recovery->publication.protected_bytes = pages * CANDIDATE_PAGE_BYTES;
    if (pages * CANDIDATE_PAGE_BYTES > CANDIDATE_CATALOG_STORAGE_BYTES) return CANDIDATE_WAL_LIMIT;
    p->retained_bytes = pages * CANDIDATE_PAGE_BYTES;
    if (p->retained_bytes < info.minimum_retained_bytes) p->retained_bytes = info.minimum_retained_bytes;
    p->desired_root.retained_bytes = p->retained_bytes;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t plan_nodes(publish_t *p)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    for (uint32_t i = 0; i < info.nodes; i++) {
        uint8_t bytes[CANDIDATE_PAGE_BYTES]; uint32_t body;
        candidate_wal_status_t status = encode_desired(p,i,bytes,p->desired_digest[i],&body);
        if (status == CANDIDATE_WAL_OK) status = find_reusable(p,i,bytes,p->desired_digest[i],body);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    for (uint32_t i = 0; i < info.count; i++) {
        candidate_wal_status_t status = protect_result(p,candidate_outcome_rebuild_at(p->rebuild,i)->result);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return set_retained(p,0);
}

static bool same_root(const candidate_catalog_root_t *a, const candidate_catalog_root_t *b)
{
    if (a->owner_epoch != b->owner_epoch || a->current_epoch != b->current_epoch) return false;
    if (a->retention_floor != b->retention_floor || a->transaction_highwater != b->transaction_highwater) return false;
    if (a->page_highwater != b->page_highwater || a->visible_csn != b->visible_csn) return false;
    if (a->retained_bytes != b->retained_bytes || a->count != b->count) return false;
    uint32_t nodes = (a->count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    for (uint32_t i = 0; i < nodes; i++)
        if (a->pages[i].id != b->pages[i].id || a->pages[i].generation != b->pages[i].generation) return false;
    return true;
}

static void decide_rewrite(publish_t *p)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    p->desired_root = (candidate_catalog_root_t){.owner_epoch = p->owner,.current_epoch = info.current_epoch,
        .retention_floor = info.retention_floor,.transaction_highwater = info.transaction_highwater,
        .page_highwater = info.page_highwater,.visible_csn = info.visible_csn,.retained_bytes = p->retained_bytes,.count = info.count};
    memcpy(p->desired_root.pages,p->desired_nodes,sizeof(p->desired_nodes));
    p->needs_rewrite = p->new_nodes > 0 || p->allocation_root.owner_epoch != p->owner ||
        !same_root(&p->catalog_root,&p->desired_root);
}

candidate_wal_status_t candidate_publish_inventory(publish_t *p)
{
    candidate_wal_status_t status = load_roots(p);
    if (status == CANDIDATE_WAL_OK) {
        candidate_allocation_load_t request = {.root = p->anchor.allocation_root,.load = candidate_recovery_load,
            .context = p->recovery,.memory = &p->memory};
        memcpy(request.uuid,p->recovery->uuid,16);
        status = candidate_allocation_load(&request,&p->allocation);
    }
    if (status == CANDIDATE_WAL_OK) {
        p->visited = p->memory.allocate(p->memory.context,PUBLISH_VERSION_BITS / 8);
        if (p->visited == NULL) status = CANDIDATE_WAL_NOMEM;
        else memset(p->visited,0,PUBLISH_VERSION_BITS / 8);
    }
    if (status == CANDIDATE_WAL_OK) status = inventory_live(p);
    candidate_wal_info_t transport;
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_scan(p->recovery->wal,visit_history,p,&transport);
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(p->recovery->terminal_history);
    candidate_witness_info_t witness = candidate_wal_witness_info(p->recovery->terminal_history);
    p->existing_events = system.events; p->existing_groups = witness.complete_groups; p->transactions = witness.transactions;
    p->system_page_highwater = system.page_highwater; p->system_transaction_highwater = system.transaction_highwater;
    if (status == CANDIDATE_WAL_OK && (!system.has_reservation || !system.has_epoch)) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = plan_nodes(p);
    if (status == CANDIDATE_WAL_OK) decide_rewrite(p);
    if (p->visited != NULL) p->memory.release(p->memory.context,p->visited,PUBLISH_VERSION_BITS / 8);
    p->visited = NULL;
    p->recovery->publication.maintenance_pages = p->maintenance_pages;
    struct stat st;
    if (status == CANDIDATE_WAL_OK && fstat(p->recovery->fd,&st) != 0) status = CANDIDATE_WAL_IO;
    if (status == CANDIDATE_WAL_OK) p->recovery->publication.data_file_bytes = (uint64_t)st.st_size;
    return status;
}

static unsigned node_count(uint32_t entries)
{
    return (entries + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
}

static candidate_wal_status_t count_pages(const publish_t *p, uint32_t base, uint32_t *total)
{
    uint32_t added = 0;
    if (p->new_nodes == 0) { *total = 1; return CANDIDATE_WAL_OK; }
    for (unsigned round = 0; round < 8; round++) {
        uint32_t next = node_count(base + p->new_nodes + added) - node_count(base);
        if (next == added) { *total = p->new_nodes + added; return CANDIDATE_WAL_OK; }
        added = next;
    }
    return CANDIDATE_WAL_LIMIT;
}

static candidate_wal_status_t is_absent(publish_t *p, uint64_t id, bool *out)
{
    *out = false;
    if (candidate_publish_set_has(p->history_ids,id)) return CANDIDATE_WAL_OK;
    candidate_redo_target_t target = candidate_recovery_target(p->recovery);
    uint8_t bytes[CANDIDATE_PAGE_BYTES]; bool is_present = false;
    candidate_wal_status_t status = target.read(target.context,id,bytes,&is_present);
    if (status != CANDIDATE_WAL_OK) return status;
    *out = true;
    for (unsigned i = 0; is_present && i < CANDIDATE_PAGE_BYTES; i++) if (bytes[i] != 0) { *out = false; break; }
    return CANDIDATE_WAL_OK;
}

/* 같은 owner의 완성 예약 구간에서 allocation·WAL page 이력·data 어디에도 없는 ID만 오름차순으로 재사용한다. */
static candidate_wal_status_t find_reserved(publish_t *p, uint32_t need)
{
    candidate_witness_system_info_t system = candidate_wal_witness_system_info(p->recovery->terminal_history);
    p->reusable_count = 0;
    for (uint32_t i = 0; i < system.events && p->reusable_count < need; i++) {
        const candidate_witness_system_event_t *e = candidate_wal_witness_system_at(p->recovery->terminal_history,i);
        if (e->type != 16 || e->owner_epoch != p->owner || e->value.reserve.next_page <= p->max_allocated_id) continue;
        uint64_t id = e->value.reserve.previous_page > p->max_allocated_id ? e->value.reserve.previous_page : p->max_allocated_id;
        for (id++; id <= e->value.reserve.next_page && p->reusable_count < need; id++) {
            uint64_t offset; candidate_wal_status_t status = candidate_recovery_page_offset(id,&offset);
            if (status != CANDIDATE_WAL_OK) return status;
            bool is_unused; status = is_absent(p,id,&is_unused);
            if (status != CANDIDATE_WAL_OK) return status;
            if (is_unused) p->ids[p->reusable_count++] = id;
        }
    }
    return CANDIDATE_WAL_OK;
}

static void assign_roles(publish_t *p, uint32_t base, uint32_t base_nodes)
{
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < p->page_count; i++) {
        uint32_t position = base + i;
        planned_page_t *page = &p->pages[i];
        if (p->new_nodes == 0 && position % CANDIDATE_ALLOCATION_NODE_ENTRIES != 0) {
            page->role = ROLE_REPLACEMENT_NODE; page->ordinal = base_nodes - 1; continue;
        }
        if (position % CANDIDATE_ALLOCATION_NODE_ENTRIES == 0) {
            page->role = ROLE_ALLOCATION_NODE; page->ordinal = position / CANDIDATE_ALLOCATION_NODE_ENTRIES; continue;
        }
        while (p->reused_node[ordinal]) ordinal++;
        page->role = ROLE_OUTCOME_NODE; page->ordinal = ordinal;
        p->desired_nodes[ordinal++] = (candidate_page_ref_t){page->id,1};
    }
}

static candidate_wal_status_t encode_carrier(const publish_t *p, candidate_catalog_root_t *root, uint64_t lsn,
    uint64_t page_highwater, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    root->owner_epoch = p->owner; root->current_epoch = info.current_epoch; root->retention_floor = info.retention_floor;
    root->transaction_highwater = p->system_transaction_highwater; root->page_highwater = page_highwater;
    candidate_page_location_t location = {.ref = p->carrier_page.ref,.lsn = lsn}; memcpy(location.uuid,p->recovery->uuid,16);
    return candidate_catalog_encode_root(&location,root,out,CANDIDATE_PAGE_BYTES);
}

typedef struct { publish_t *p; uint64_t id; const uint8_t *bytes; } carrier_view_t;

static candidate_wal_status_t carrier_load(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    carrier_view_t *v = context;
    if (ref.id != v->id) return candidate_recovery_load(v->p->recovery,ref,out);
    memcpy(out,v->bytes,CANDIDATE_PAGE_BYTES); return CANDIDATE_WAL_OK;
}

/* 다른 LIVE root를 최종 epoch/floor·owner·HWM으로 바꾼 사본이 완전한 catalog로 적재될 때만 예약 image에 쓴다. */
static candidate_wal_status_t try_carrier(publish_t *p, tracked_page_t page, candidate_catalog_root_t root,
    uint8_t bytes[CANDIDATE_PAGE_BYTES], bool *eligible)
{
    *eligible = false;
    p->carrier_page = page;
    if (encode_carrier(p,&root,page.lsn,p->system_page_highwater,bytes) != CANDIDATE_WAL_OK) return CANDIDATE_WAL_OK;
    carrier_view_t view = {p,page.ref.id,bytes};
    candidate_catalog_load_t load = {.root = page.ref,.load = carrier_load,.context = &view,.memory = &p->memory};
    memcpy(load.uuid,p->recovery->uuid,16);
    candidate_catalog_t *catalog = NULL;
    candidate_wal_status_t status = candidate_catalog_load(&load,&catalog);
    candidate_catalog_close(&catalog);
    if (status == CANDIDATE_WAL_CORRUPT) return CANDIDATE_WAL_OK;
    if (status != CANDIDATE_WAL_OK) return status;
    p->carrier_root = root; *eligible = true;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t search_carrier(publish_t *p, bool empty, bool *found)
{
    candidate_allocation_info_t info = candidate_allocation_info(p->allocation);
    *found = false;
    for (uint32_t i = 0; i < info.count && !*found; i++) {
        candidate_allocation_entry_t entry; candidate_allocation_at(p->allocation,i,&entry);
        if (entry.state != CANDIDATE_ALLOCATION_LIVE || entry.ref.id == p->anchor.outcome_root.id) continue;
        uint8_t bytes[CANDIDATE_PAGE_BYTES]; candidate_page_info_t page; candidate_catalog_root_t root;
        candidate_wal_status_t status = load_page(p,entry.ref,bytes,&page);
        if (status != CANDIDATE_WAL_OK) return status;
        if (page.type != 4 || read_le(bytes + 70,2) != 1) continue;
        if (candidate_catalog_decode_root(p->recovery->uuid,bytes,CANDIDATE_PAGE_BYTES,&root) != CANDIDATE_WAL_OK) continue;
        if ((root.count == 0) != empty) continue;
        status = try_carrier(p,(tracked_page_t){entry.ref,page.location.lsn},root,bytes,found);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t choose_carrier(publish_t *p)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    p->carrier_new = 0; p->carrier_is_selected = false;
    if (p->catalog_root.current_epoch == info.current_epoch && p->catalog_root.retention_floor == info.retention_floor) {
        p->carrier_is_selected = true; p->carrier_page = p->catalog_page; p->carrier_root = p->catalog_root;
        return CANDIDATE_WAL_OK;
    }
    for (unsigned pass = 0; pass < 2; pass++) {
        bool found; candidate_wal_status_t status = search_carrier(p,pass == 0,&found);
        if (status != CANDIDATE_WAL_OK || found) return status;
    }
    p->carrier_page = (tracked_page_t){{0,0},0};
    p->carrier_root = (candidate_catalog_root_t){.visible_csn = info.visible_csn,.retained_bytes = CANDIDATE_PAGE_BYTES};
    p->carrier_new = p->allocation_count % CANDIDATE_ALLOCATION_NODE_ENTRIES == 0 ? 2 : 1;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_capacity(uint32_t base, uint32_t total)
{
    if (total > PUBLISH_NEW_PAGES || total > CANDIDATE_ALLOCATION_ENTRIES - base) return CANDIDATE_WAL_LIMIT;
    return node_count(base + total) > CANDIDATE_ALLOCATION_NODES ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_publish_plan_pages(publish_t *p)
{
    uint32_t total;
    candidate_wal_status_t status = count_pages(p,p->allocation_count,&total);
    if (status == CANDIDATE_WAL_OK) status = check_capacity(p->allocation_count,total);
    if (status == CANDIDATE_WAL_OK) status = find_reserved(p,total);
    if (status != CANDIDATE_WAL_OK) return status;
    p->carrier_new = 0; p->carrier_is_selected = false;
    if (p->reusable_count < total) {
        status = choose_carrier(p);
        uint32_t base = p->allocation_count + p->carrier_new;
        if (status == CANDIDATE_WAL_OK && p->carrier_new != 0) status = count_pages(p,base,&total);
        if (status == CANDIDATE_WAL_OK) status = check_capacity(base,total);
        if (status == CANDIDATE_WAL_OK && p->carrier_new != 0) status = find_reserved(p,p->carrier_new + total);
        if (status == CANDIDATE_WAL_OK && p->carrier_new != 0) status = set_retained(p,1);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    uint32_t need = p->carrier_new + total;
    p->reserve_pages = need > p->reusable_count ? need - p->reusable_count : 0;
    if (p->system_page_highwater > UINT64_MAX - p->reserve_pages) return CANDIDATE_WAL_LIMIT;
    for (uint32_t i = p->reusable_count; i < need; i++) p->ids[i] = p->system_page_highwater + (i - p->reusable_count) + 1;
    for (uint32_t i = 0; i < need; i++) {
        uint64_t offset; status = candidate_recovery_page_offset(p->ids[i],&offset);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    for (uint32_t i = 0; i < p->carrier_new; i++) p->carrier_ids[i] = p->ids[i];
    if (p->carrier_new != 0) p->carrier_page = (tracked_page_t){{p->carrier_ids[0],1},0};
    p->page_count = total;
    for (uint32_t i = 0; i < total; i++) p->pages[i] = (planned_page_t){.id = p->ids[p->carrier_new + i]};
    p->stage_base = p->allocation_count + p->carrier_new;
    assign_roles(p,p->stage_base,node_count(p->stage_base));
    return CANDIDATE_WAL_OK;
}
