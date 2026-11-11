#include "candidate_buffer_internal.h"

#include <string.h>

#include "storage/candidate_catalog.h"
#include "storage/checksum.h"

#define IMAGE_BYTES (32u + CANDIDATE_PAGE_BYTES)
#define ALLOC_EVENT 6u
#define RESERVE_EVENT 16u
#define ROOT_PAGES 3u

typedef struct {
    candidate_witness_system_info_t system;
    candidate_catalog_root_t catalog;
    candidate_allocation_root_t allocation;
    candidate_allocation_node_t tail;
    uint32_t existing, allocated, added, tail_index;
    bool has_node;
} structure_t;

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t page_crc(const uint8_t *p)
{
    const uint8_t zero[4] = {0};
    uint32_t crc = crc32c(0,p,52);
    crc = crc32c(crc,zero,4);
    return crc32c(crc,p + 56,CANDIDATE_PAGE_BYTES - 56);
}

static candidate_page_location_t location_of(const candidate_buffer_t *pool, const buffer_planned_t *k)
{
    candidate_page_location_t location = {.ref = k->ref,.lsn = k->previous_lsn};
    memcpy(location.uuid,pool->options.uuid,16);
    return location;
}

static candidate_wal_status_t copy_pages(candidate_buffer_t *pool, const candidate_buffer_structure_t *request,
    buffer_production_t *p, structure_t *s)
{
    candidate_buffer_structure_t saved = *request;
    if (saved.pages == NULL || saved.allocated == 0 || saved.allocated > saved.count) return CANDIDATE_WAL_INVALID;
    if (saved.count > CANDIDATE_BUFFER_GROUP_PAGES - ROOT_PAGES) return CANDIDATE_WAL_LIMIT;
    s->existing = saved.count - saved.allocated; s->allocated = saved.allocated; p->count = saved.count;
    for (uint32_t i = 0; i < saved.count; i++) {
        candidate_buffer_page_t page = saved.pages[i];
        if (page.page == NULL || page.ref.id == 0 || page.ref.generation == 0) return CANDIDATE_WAL_INVALID;
        for (uint32_t j = 0; j < i; j++) if (p->pages[j].ref.id == page.ref.id) return CANDIDATE_WAL_INVALID;
        p->pages[i] = (buffer_planned_t){.ref = page.ref,.bytes = IMAGE_BYTES,.is_image = true};
        uint8_t *next = candidate_buffer_desired(pool,i);
        memcpy(next,page.page,CANDIDATE_PAGE_BYTES);
        write_le(next + 52,page_crc(next),4);
    }
    return CANDIDATE_WAL_OK;
}

static bool is_metadata(const candidate_buffer_t *pool, const structure_t *s, uint64_t id)
{
    if (id == pool->anchor.outcome_root.id || id == pool->anchor.allocation_root.id) return true;
    uint32_t nodes = (s->allocation.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    for (uint32_t i = 0; i < nodes; i++) if (s->allocation.nodes[i].id == id) return true;
    return false;
}

static candidate_wal_status_t check_header(candidate_buffer_t *pool, uint32_t index, candidate_page_ref_t ref, uint64_t lsn)
{
    candidate_page_info_t info;
    candidate_wal_status_t status = candidate_page_decode_header(pool->options.uuid,candidate_buffer_desired(pool,index),
        CANDIDATE_PAGE_BYTES,&info);
    if (status == CANDIDATE_WAL_CORRUPT) return CANDIDATE_WAL_INVALID;
    if (status != CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id != ref.id || info.location.ref.generation != ref.generation) return CANDIDATE_WAL_INVALID;
    return info.location.lsn == lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_STALE;
}

static candidate_wal_status_t plan_existing(candidate_buffer_t *pool, buffer_production_t *p, const structure_t *s, uint32_t index)
{
    buffer_planned_t *k = &p->pages[index];
    if (is_metadata(pool,s,k->ref.id)) return CANDIDATE_WAL_INVALID;
    const buffer_page_t *page = candidate_buffer_page(pool,k->ref.id);
    if (page == NULL || page->generation != k->ref.generation) return CANDIDATE_WAL_UNSUPPORTED;
    k->previous_lsn = page->last_lsn;
    return check_header(pool,index,k->ref,page->last_lsn);
}

static candidate_wal_status_t plan_new(candidate_buffer_t *pool, buffer_production_t *p, const structure_t *s, uint32_t index)
{
    buffer_planned_t *k = &p->pages[index];
    uint64_t expected = s->system.page_highwater + 1 + (index - s->existing);
    if (k->ref.id != expected || k->ref.generation != 1 || candidate_buffer_page(pool,k->ref.id) != NULL) return CANDIDATE_WAL_INVALID;
    k->previous_lsn = 0;
    return check_header(pool,index,k->ref,0);
}

static candidate_wal_status_t load_metadata(candidate_buffer_t *pool, buffer_production_t *p, structure_t *s)
{
    uint32_t at = p->count;
    const uint8_t *uuid = pool->options.uuid;
    candidate_wal_status_t status = candidate_buffer_load_root(pool,p,at,pool->anchor.outcome_root);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_load_root(pool,p,at + 1,pool->anchor.allocation_root);
    if (status == CANDIDATE_WAL_OK) status = candidate_catalog_decode_root(uuid,candidate_buffer_base(pool,at),CANDIDATE_PAGE_BYTES,&s->catalog);
    if (status == CANDIDATE_WAL_OK)
        status = candidate_allocation_decode_root(uuid,candidate_buffer_base(pool,at + 1),CANDIDATE_PAGE_BYTES,&s->allocation);
    if (status != CANDIDATE_WAL_OK) return status;
    bool is_current = s->catalog.transaction_highwater == s->system.transaction_highwater &&
        s->catalog.page_highwater == s->system.page_highwater && s->allocation.page_highwater == s->system.page_highwater;
    if (!is_current) return CANDIDATE_WAL_UNSUPPORTED;
    uint32_t nodes = (s->allocation.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    s->tail_index = at + 2;
    status = candidate_buffer_load_root(pool,p,s->tail_index,s->allocation.nodes[nodes - 1]);
    if (status == CANDIDATE_WAL_OK)
        status = candidate_allocation_decode_node(uuid,candidate_buffer_base(pool,s->tail_index),CANDIDATE_PAGE_BYTES,&s->tail);
    if (status != CANDIDATE_WAL_OK) return status;
    bool is_tail = s->tail.ordinal == nodes - 1 && s->tail.count == s->allocation.count - (nodes - 1) * CANDIDATE_ALLOCATION_NODE_ENTRIES;
    return is_tail ? CANDIDATE_WAL_OK : CANDIDATE_WAL_UNSUPPORTED;
}

static candidate_wal_status_t size_group(buffer_production_t *p, structure_t *s)
{
    uint32_t room = CANDIDATE_ALLOCATION_NODE_ENTRIES - s->tail.count;
    uint32_t nodes = (s->allocation.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    s->has_node = s->allocated > room;
    s->added = s->allocated + (s->has_node ? 1u : 0u);
    if (s->has_node && nodes == CANDIDATE_ALLOCATION_NODES) return CANDIDATE_WAL_LIMIT;
    if (s->added > CANDIDATE_ALLOCATION_ENTRIES - s->allocation.count) return CANDIDATE_WAL_LIMIT;
    if (s->system.page_highwater > UINT64_MAX - s->added) return CANDIDATE_WAL_LIMIT;
    uint32_t pages = p->count + ROOT_PAGES + (s->has_node ? 1u : 0u), events = 1 + s->added;
    if (pages > CANDIDATE_BUFFER_GROUP_PAGES || events > BUFFER_GROUP_EVENTS) return CANDIDATE_WAL_LIMIT;
    return pages + events + 2 > CANDIDATE_WITNESS_RECORDS ? CANDIDATE_WAL_LIMIT : CANDIDATE_WAL_OK;
}

static void plan_events(buffer_production_t *p, const structure_t *s)
{
    uint64_t first = s->system.page_highwater + 1;
    p->events = 1 + s->added;
    p->event_types[0] = RESERVE_EVENT;
    uint8_t *e = p->event[0];
    write_le(e + 8,s->system.transaction_highwater,8); write_le(e + 16,s->system.transaction_highwater,8);
    write_le(e + 24,s->system.page_highwater,8); write_le(e + 32,s->system.page_highwater + s->added,8);
    for (uint32_t i = 0; i < s->added; i++) {
        p->event_types[1 + i] = ALLOC_EVENT;
        e = p->event[1 + i];
        write_le(e + 8,first + i,8); write_le(e + 16,0,8); write_le(e + 24,1,8); write_le(e + 32,0,8);
    }
}

static candidate_wal_status_t encode_roots(candidate_buffer_t *pool, buffer_production_t *p, structure_t *s)
{
    uint32_t at = p->count;
    uint64_t highwater = s->system.page_highwater + s->added;
    uint32_t nodes = (s->allocation.count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    s->catalog.owner_epoch = pool->options.owner_epoch; s->catalog.page_highwater = highwater;
    s->allocation.owner_epoch = pool->options.owner_epoch; s->allocation.page_highwater = highwater;
    if (s->has_node) {
        candidate_page_ref_t node = {s->system.page_highwater + 1 + s->allocated,1};
        s->allocation.nodes[nodes] = node;
        p->pages[at + ROOT_PAGES] = (buffer_planned_t){.ref = node,.bytes = IMAGE_BYTES,.is_image = true};
    }
    s->allocation.count += s->added;
    candidate_page_location_t location = location_of(pool,&p->pages[at]);
    candidate_wal_status_t status = candidate_catalog_encode_root(&location,&s->catalog,candidate_buffer_desired(pool,at),
        CANDIDATE_PAGE_BYTES);
    location = location_of(pool,&p->pages[at + 1]);
    if (status == CANDIDATE_WAL_OK)
        status = candidate_allocation_encode_root(&location,&s->allocation,candidate_buffer_desired(pool,at + 1),CANDIDATE_PAGE_BYTES);
    p->count = at + ROOT_PAGES + (s->has_node ? 1u : 0u);
    return status == CANDIDATE_WAL_CORRUPT ? CANDIDATE_WAL_INVALID : status;
}

static candidate_wal_status_t encode_nodes(candidate_buffer_t *pool, buffer_production_t *p)
{
    structure_t *s = p->context;
    candidate_allocation_node_t node = {.ordinal = s->tail.ordinal + 1};
    uint64_t first = s->system.page_highwater + 1;
    for (uint32_t i = 0; i < s->added; i++) {
        candidate_allocation_entry_t entry = {.ref = {first + i,1},.state = CANDIDATE_ALLOCATION_LIVE,.transition_lsn = p->end_lsn};
        if (s->tail.count < CANDIDATE_ALLOCATION_NODE_ENTRIES) s->tail.entries[s->tail.count++] = entry;
        else node.entries[node.count++] = entry;
    }
    candidate_page_location_t location = location_of(pool,&p->pages[s->tail_index]);
    candidate_wal_status_t status = candidate_allocation_encode_node(&location,&s->tail,candidate_buffer_desired(pool,s->tail_index),
        CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK || !s->has_node) return status;
    location = location_of(pool,&p->pages[s->tail_index + 1]);
    return candidate_allocation_encode_node(&location,&node,candidate_buffer_desired(pool,s->tail_index + 1),CANDIDATE_PAGE_BYTES);
}

static candidate_wal_status_t plan(candidate_buffer_t *pool, buffer_production_t *p, structure_t *s, const buffer_scan_t *scan)
{
    s->system = scan->observed.system;
    candidate_wal_status_t status = candidate_buffer_check_system(pool,scan,0);
    if (status == CANDIDATE_WAL_OK) status = load_metadata(pool,p,s);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < s->existing; i++) status = plan_existing(pool,p,s,i);
    for (uint32_t i = s->existing; status == CANDIDATE_WAL_OK && i < s->existing + s->allocated; i++) status = plan_new(pool,p,s,i);
    if (status == CANDIDATE_WAL_OK) status = size_group(p,s);
    if (status != CANDIDATE_WAL_OK) return status;
    plan_events(p,s);
    return encode_roots(pool,p,s);
}

static candidate_wal_status_t write_structure(candidate_buffer_t *pool, const candidate_buffer_structure_t *request,
    candidate_buffer_admission_t *out)
{
    buffer_production_t production;
    structure_t s;
    memset(&production,0,sizeof(production)); memset(&s,0,sizeof(s));
    production.placed = encode_nodes; production.context = &s;
    candidate_wal_status_t status = copy_pages(pool,request,&production,&s);
    if (status != CANDIDATE_WAL_OK) return status;
    buffer_scan_t scan = {.target_end = 0};
    status = candidate_buffer_scan(pool,&scan);
    if (status == CANDIDATE_WAL_OK) status = plan(pool,&production,&s,&scan);
    if (status != CANDIDATE_WAL_OK) return candidate_buffer_refuse(pool,&scan,status);
    candidate_buffer_admission_t admission;
    status = candidate_buffer_emit(pool,&production,&scan,&admission);
    if (status == CANDIDATE_WAL_OK) status = candidate_buffer_force_group(pool,&admission);
    if (status == CANDIDATE_WAL_OK) *out = admission;
    return status;
}

candidate_wal_status_t candidate_buffer_write_structure(candidate_buffer_t *pool, const candidate_buffer_structure_t *structure,
    candidate_buffer_admission_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_buffer_admission_t){0};
    if (pool == NULL || structure == NULL) return CANDIDATE_WAL_INVALID;
    buffer_operation_t previous = pool->operation;
    candidate_wal_status_t status = candidate_buffer_enter(pool,BUFFER_ADMIT);
    if (status != CANDIDATE_WAL_OK) return status;
    status = write_structure(pool,structure,out);
    candidate_buffer_leave(pool,previous);
    return candidate_buffer_result(pool,status);
}
