#include "candidate_recovery_publish_internal.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_checkpoint.h"
#include "storage/candidate_digest.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"

#define GROUP_BEGIN_BYTES 16u
#define GROUP_EVENT_BYTES 40u
#define GROUP_END_BYTES 24u
#define CHECKPOINT_BEGIN_BYTES 160u
#define CHECKPOINT_END_BYTES 64u

typedef struct {
    uint64_t id, end;
    uint32_t images, events;
    uint64_t page_ids[CANDIDATE_WITNESS_PAGES], generations[CANDIDATE_WITNESS_PAGES], previous[CANDIDATE_WITNESS_PAGES];
    uint8_t event_bytes[CANDIDATE_WITNESS_PAGES][GROUP_EVENT_BYTES];
    uint16_t event_types[CANDIDATE_WITNESS_PAGES];
    uint64_t lsns[2 + 2 * CANDIDATE_WITNESS_PAGES];
} group_t;
typedef struct { uint32_t first, last; } slice_t;

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0; for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint8_t *add_event(group_t *g, uint16_t type)
{
    g->event_types[g->events] = type;
    uint8_t *e = g->event_bytes[g->events++];
    memset(e,0,GROUP_EVENT_BYTES); write_le(e,g->id,8);
    return e;
}

static uint64_t frame_bytes(size_t payload)
{
    return (96 + payload + 7) & ~(uint64_t)7;
}

static candidate_wal_status_t place_group(const candidate_wal_t *wal, candidate_wal_cursor_t *cursor,
    uint32_t images, uint32_t events, uint64_t *lsns, uint64_t *end)
{
    uint32_t n = 0;
    candidate_wal_status_t status = candidate_wal_place(wal,cursor,GROUP_BEGIN_BYTES,&lsns[n++]);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < images; i++) status = candidate_wal_place(wal,cursor,PUBLISH_IMAGE_BYTES,&lsns[n++]);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < events; i++) status = candidate_wal_place(wal,cursor,GROUP_EVENT_BYTES,&lsns[n++]);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_place(wal,cursor,GROUP_END_BYTES,&lsns[n]);
    if (status == CANDIDATE_WAL_OK) *end = cursor->position;
    return status;
}

candidate_wal_status_t candidate_publish_append(publish_t *p, candidate_wal_record_t *record, uint64_t lsn)
{
    uint64_t end;
    record->lsn = lsn; record->owner_epoch = p->owner;
    candidate_wal_status_t status = candidate_wal_append(p->recovery->wal,record,&end);
    if (status != CANDIDATE_WAL_OK) return status;
    if (end != lsn + frame_bytes(record->payload_bytes)) return CANDIDATE_WAL_CORRUPT;
    p->recovery->publication.wal_bytes_appended += end - lsn;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t append_exact(publish_t *p, candidate_wal_record_t *record, uint64_t lsn, uint32_t *crc)
{
    candidate_wal_status_t status = candidate_publish_append(p,record,lsn);
    if (status == CANDIDATE_WAL_OK && crc != NULL) *crc = candidate_wal_witness_crc(*crc,record);
    return status;
}

static candidate_wal_status_t write_images(publish_t *p, const group_t *g)
{
    candidate_redo_target_t target = candidate_recovery_target(p->recovery);
    for (uint32_t i = 0; i < g->images; i++) {
        candidate_wal_status_t status = target.write(target.context,g->page_ids[i],p->images[i] + 32);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    return target.sync(target.context);
}

static candidate_wal_status_t emit_group(publish_t *p, const group_t *g)
{
    uint8_t begin[GROUP_BEGIN_BYTES] = {0}, end[GROUP_END_BYTES] = {0}; uint32_t crc = 0, n = 0;
    write_le(begin,g->id,8); write_le(begin + 8,g->images,4); write_le(begin + 12,g->images + g->events,4);
    candidate_wal_record_t r = {.type = 3,.payload = begin,.payload_bytes = sizeof(begin)};
    candidate_wal_status_t status = append_exact(p,&r,g->lsns[n++],&crc);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < g->images; i++) {
        uint8_t *image = p->images[i];
        write_le(image,g->id,8); write_le(image + 8,g->page_ids[i],8);
        write_le(image + 16,g->generations[i],8); write_le(image + 24,g->previous[i],8);
        r = (candidate_wal_record_t){.type = 4,.previous_lsn = g->lsns[n - 1],.payload = image,.payload_bytes = PUBLISH_IMAGE_BYTES};
        status = append_exact(p,&r,g->lsns[n++],&crc);
    }
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < g->events; i++) {
        r = (candidate_wal_record_t){.type = g->event_types[i],.previous_lsn = g->lsns[n - 1],
            .payload = g->event_bytes[i],.payload_bytes = GROUP_EVENT_BYTES};
        status = append_exact(p,&r,g->lsns[n++],&crc);
    }
    if (status != CANDIDATE_WAL_OK) return status;
    write_le(end,g->id,8); write_le(end + 8,g->images,4); write_le(end + 12,g->images + g->events,4); write_le(end + 16,crc,4);
    r = (candidate_wal_record_t){.type = 8,.previous_lsn = g->lsns[n - 1],.payload = end,.payload_bytes = sizeof(end)};
    status = append_exact(p,&r,g->lsns[n],NULL);
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_force(p->recovery->wal,g->end);
    if (status == CANDIDATE_WAL_OK) status = write_images(p,g);
    if (status == CANDIDATE_WAL_OK) p->recovery->publication.system_groups++;
    return status;
}

static void add_image(group_t *g, candidate_page_ref_t ref, uint64_t previous)
{
    g->page_ids[g->images] = ref.id; g->generations[g->images] = ref.generation; g->previous[g->images++] = previous;
}

static candidate_page_location_t location_of(const publish_t *p, candidate_page_ref_t ref, uint64_t lsn)
{
    candidate_page_location_t location = {.ref = ref,.lsn = lsn}; memcpy(location.uuid,p->recovery->uuid,16);
    return location;
}

static slice_t slice_of(const publish_t *p, uint32_t group)
{
    uint32_t first = p->stage_base + group * PUBLISH_GROUP_PAGES;
    uint32_t last = p->stage_base + p->page_count;
    if (last - first > PUBLISH_GROUP_PAGES) last = first + PUBLISH_GROUP_PAGES;
    return (slice_t){first,last};
}

static int created_page(const publish_t *p, uint32_t node, slice_t s)
{
    for (uint32_t i = 0; i < p->page_count; i++) {
        const planned_page_t *page = &p->pages[i];
        if (page->role == ROLE_OUTCOME_NODE || page->ordinal != node) continue;
        uint32_t position = p->stage_base + i;
        return s.first <= position && position < s.last ? (int)i : -1;
    }
    return -1;
}

static void shape_group(const publish_t *p, uint32_t group, bool is_final, uint32_t *images, uint32_t *events)
{
    slice_t s = slice_of(p,group);
    uint32_t touched = (s.last - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES - s.first / CANDIDATE_ALLOCATION_NODE_ENTRIES + 1;
    uint32_t outcomes = 0;
    for (uint32_t at = s.first; at < s.last; at++) outcomes += p->pages[at - p->stage_base].role == ROLE_OUTCOME_NODE;
    *images = 1 + touched + outcomes + (is_final ? 1 : 0);
    *events = s.last - s.first;
}

static uint32_t group_count(const publish_t *p)
{
    return p->needs_rewrite ? (p->page_count + PUBLISH_GROUP_PAGES - 1) / PUBLISH_GROUP_PAGES : 0;
}

static candidate_wal_status_t build_node(const publish_t *p, uint32_t node, uint32_t last, candidate_allocation_node_t *out)
{
    uint32_t start = node * CANDIDATE_ALLOCATION_NODE_ENTRIES, stop = start + CANDIDATE_ALLOCATION_NODE_ENTRIES;
    if (stop > last) stop = last;
    *out = (candidate_allocation_node_t){.ordinal = node,.count = stop - start};
    for (uint32_t at = start; at < stop; at++) {
        candidate_allocation_entry_t *e = &out->entries[at - start];
        if (at < p->stage_base) {
            if (node + 1 != p->allocation_nodes || at - start >= p->tail.count) return CANDIDATE_WAL_CORRUPT;
            *e = p->tail.entries[at - start]; continue;
        }
        const planned_page_t *page = &p->pages[at - p->stage_base];
        *e = (candidate_allocation_entry_t){{page->id,1},CANDIDATE_ALLOCATION_LIVE,0,page->transition_lsn};
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_allocation_nodes(publish_t *p, group_t *g, slice_t s)
{
    uint32_t first = s.first / CANDIDATE_ALLOCATION_NODE_ENTRIES, last = (s.last - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    for (uint32_t node = first; node <= last; node++) {
        int created = created_page(p,node,s);
        if (created >= 0) p->allocation_root.nodes[node] = (candidate_page_ref_t){p->pages[created].id,1};
        else if (node >= p->allocation_nodes && p->node_pages[node].ref.id == 0) return CANDIDATE_WAL_CORRUPT;
        candidate_page_ref_t ref = p->allocation_root.nodes[node];
        candidate_allocation_node_t content;
        candidate_wal_status_t status = build_node(p,node,s.last,&content);
        candidate_page_location_t location = location_of(p,ref,g->end);
        if (status == CANDIDATE_WAL_OK)
            status = candidate_allocation_encode_node(&location,&content,p->images[g->images] + 32,CANDIDATE_PAGE_BYTES);
        if (status != CANDIDATE_WAL_OK) return status;
        add_image(g,ref,created >= 0 ? 0 : p->node_pages[node].lsn);
        p->node_pages[node] = (tracked_page_t){ref,g->end};
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_outcome_nodes(publish_t *p, group_t *g, slice_t s)
{
    for (uint32_t at = s.first; at < s.last; at++) {
        const planned_page_t *page = &p->pages[at - p->stage_base];
        if (page->role != ROLE_OUTCOME_NODE) continue;
        candidate_page_ref_t ref = {page->id,1}; candidate_page_location_t location = location_of(p,ref,g->end);
        candidate_wal_status_t status = candidate_outcome_rebuild_encode_node(p->rebuild,page->ordinal,&location,
            p->images[g->images] + 32,CANDIDATE_PAGE_BYTES);
        if (status != CANDIDATE_WAL_OK) return status;
        add_image(g,ref,0);
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_allocation_root(publish_t *p, group_t *g, uint32_t count)
{
    p->allocation_root.count = count; p->allocation_root.owner_epoch = p->owner;
    p->allocation_root.page_highwater = p->system_page_highwater;
    candidate_page_location_t location = location_of(p,p->allocation_page.ref,g->end);
    candidate_wal_status_t status = candidate_allocation_encode_root(&location,&p->allocation_root,
        p->images[g->images] + 32,CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    add_image(g,p->allocation_page.ref,p->allocation_page.lsn);
    p->allocation_page.lsn = g->end;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t add_final_root(publish_t *p, group_t *g)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    candidate_page_location_t location = location_of(p,p->catalog_page.ref,g->end);
    candidate_wal_status_t status = candidate_outcome_rebuild_encode_root(p->rebuild,&location,p->desired_nodes,info.nodes,
        p->retained_bytes,p->images[g->images] + 32,CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    add_image(g,p->catalog_page.ref,p->catalog_page.lsn);
    p->catalog_page.lsn = g->end;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t stage_group(publish_t *p, uint32_t group, bool is_final)
{
    slice_t s = slice_of(p,group); group_t g = {.id = ++p->max_group_id};
    uint32_t images, events; shape_group(p,group,is_final,&images,&events);
    candidate_wal_cursor_t cursor = candidate_wal_cursor(p->recovery->wal);
    candidate_wal_status_t status = place_group(p->recovery->wal,&cursor,images,events,g.lsns,&g.end);
    if (status != CANDIDATE_WAL_OK) return status;
    for (uint32_t at = s.first; at < s.last; at++) {
        planned_page_t *page = &p->pages[at - p->stage_base]; page->transition_lsn = g.end;
        uint8_t *e = add_event(&g,6); write_le(e + 8,page->id,8); write_le(e + 24,1,8);
    }
    status = add_allocation_nodes(p,&g,s);
    if (status == CANDIDATE_WAL_OK) status = add_allocation_root(p,&g,s.last);
    if (status == CANDIDATE_WAL_OK) status = add_outcome_nodes(p,&g,s);
    if (status == CANDIDATE_WAL_OK && is_final) status = add_final_root(p,&g);
    if (status == CANDIDATE_WAL_OK && (g.images != images || g.events != events)) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = emit_group(p,&g);
    if (status == CANDIDATE_WAL_OK) p->recovery->publication.events += g.events;
    return status;
}

candidate_wal_status_t candidate_publish_stage(publish_t *p)
{
    uint32_t groups = group_count(p);
    if (groups > 0 && p->stage_base != p->allocation_count) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < groups; i++) {
        candidate_wal_status_t status = stage_group(p,i,i + 1 == groups);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    p->recovery->publication.has_staging = groups > 0;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_reloaded(publish_t *p, uint64_t highwater)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    if (info.page_highwater != highwater || info.owner_epoch != p->owner) return CANDIDATE_WAL_CORRUPT;
    for (uint32_t i = 0; i < info.nodes; i++) {
        candidate_page_location_t location = location_of(p,(candidate_page_ref_t){1,1},0);
        uint8_t bytes[CANDIDATE_PAGE_BYTES], digest[32];
        candidate_wal_status_t status = candidate_outcome_rebuild_encode_node(p->rebuild,i,&location,bytes,sizeof(bytes));
        if (status == CANDIDATE_WAL_OK) status = candidate_digest_sha256(bytes + 64,(size_t)read_le(bytes + 48,4),digest);
        if (status != CANDIDATE_WAL_OK) return status;
        if (memcmp(digest,p->desired_digest[i],32) != 0) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static void reserve_shape(const publish_t *p, uint32_t *images, uint32_t *events)
{
    *images = 2 + (p->carrier_new != 0 ? 1 : 0);
    *events = 1 + p->carrier_new;
}

static candidate_wal_status_t add_carrier_allocation(publish_t *p, group_t *g)
{
    uint32_t first = p->allocation_count;
    candidate_allocation_node_t node;
    candidate_page_ref_t ref;
    uint64_t previous = 0;
    if (p->carrier_new == 1) {
        node = p->tail; ref = p->tail_page.ref; previous = p->node_pages[p->allocation_nodes - 1].lsn;
        if (node.count != first - node.ordinal * CANDIDATE_ALLOCATION_NODE_ENTRIES || node.count >= CANDIDATE_ALLOCATION_NODE_ENTRIES)
            return CANDIDATE_WAL_CORRUPT;
    } else {
        node = (candidate_allocation_node_t){.ordinal = first / CANDIDATE_ALLOCATION_NODE_ENTRIES};
        ref = (candidate_page_ref_t){p->carrier_ids[1],1};
    }
    for (uint32_t i = 0; i < p->carrier_new; i++) {
        node.entries[node.count++] = (candidate_allocation_entry_t){{p->carrier_ids[i],1},CANDIDATE_ALLOCATION_LIVE,0,g->end};
        uint8_t *e = add_event(g,6); write_le(e + 8,p->carrier_ids[i],8); write_le(e + 24,1,8);
    }
    candidate_page_location_t location = location_of(p,ref,g->end);
    candidate_wal_status_t status = candidate_allocation_encode_node(&location,&node,p->images[g->images] + 32,CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    add_image(g,ref,previous);
    if (p->carrier_new == 2) { p->allocation_root.nodes[node.ordinal] = ref; p->allocation_nodes++; }
    p->tail = node; p->tail_page = (tracked_page_t){ref,g->end}; p->node_pages[node.ordinal] = p->tail_page;
    p->allocation_count += p->carrier_new; p->max_allocated_id = p->carrier_ids[p->carrier_new - 1];
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_publish_reserve(publish_t *p)
{
    if (p->reserve_pages == 0) return CANDIDATE_WAL_OK;
    group_t g = {.id = ++p->max_group_id};
    uint32_t images, events; reserve_shape(p,&images,&events);
    candidate_wal_cursor_t cursor = candidate_wal_cursor(p->recovery->wal);
    candidate_wal_status_t status = place_group(p->recovery->wal,&cursor,images,events,g.lsns,&g.end);
    if (status != CANDIDATE_WAL_OK) return status;
    uint64_t highwater = p->system_page_highwater + p->reserve_pages;
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p->rebuild);
    candidate_catalog_root_t root = p->carrier_root;
    root.owner_epoch = p->owner; root.current_epoch = info.current_epoch; root.retention_floor = info.retention_floor;
    root.transaction_highwater = p->system_transaction_highwater; root.page_highwater = highwater;
    candidate_page_location_t location = location_of(p,p->carrier_page.ref,g.end);
    status = candidate_catalog_encode_root(&location,&root,p->images[0] + 32,CANDIDATE_PAGE_BYTES);
    if (status != CANDIDATE_WAL_OK) return status;
    add_image(&g,p->carrier_page.ref,p->carrier_new != 0 ? 0 : p->carrier_page.lsn);
    uint8_t *e = add_event(&g,16);
    write_le(e + 8,p->system_transaction_highwater,8); write_le(e + 16,p->system_transaction_highwater,8);
    write_le(e + 24,p->system_page_highwater,8); write_le(e + 32,highwater,8);
    p->system_page_highwater = highwater;
    if (p->carrier_new != 0) status = add_carrier_allocation(p,&g);
    if (status == CANDIDATE_WAL_OK) status = add_allocation_root(p,&g,p->allocation_count);
    if (status == CANDIDATE_WAL_OK && (g.images != images || g.events != events)) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK) status = emit_group(p,&g);
    if (status != CANDIDATE_WAL_OK) return status;
    if (p->carrier_is_selected) { p->catalog_root = root; p->catalog_page.lsn = g.end; }
    p->recovery->publication.events += g.events;
    candidate_outcome_rebuild_close(&p->rebuild);
    status = candidate_outcome_rebuild_load(p->recovery->wal,&p->rebuild_options,&p->rebuild);
    return status == CANDIDATE_WAL_OK ? check_reloaded(p,highwater) : status;
}

candidate_wal_status_t candidate_publish_preflight(publish_t *p, const candidate_control_info_t *control)
{
    candidate_wal_t *wal = p->recovery->wal;
    candidate_wal_cursor_t cursor = candidate_wal_cursor(wal);
    uint64_t lsns[2 + 2 * CANDIDATE_WITNESS_PAGES], end;
    uint32_t groups = 0, events = 0;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (p->reserve_pages > 0) {
        uint32_t images, reserve_events; reserve_shape(p,&images,&reserve_events);
        status = place_group(wal,&cursor,images,reserve_events,lsns,&end); groups++; events += reserve_events;
    }
    uint32_t count = group_count(p);
    for (uint32_t i = 0; status == CANDIDATE_WAL_OK && i < count; i++) {
        uint32_t images, group_events; shape_group(p,i,i + 1 == count,&images,&group_events);
        if (images > CANDIDATE_WITNESS_PAGES || images + group_events > CANDIDATE_WITNESS_RECORDS) return CANDIDATE_WAL_LIMIT;
        status = place_group(wal,&cursor,images,group_events,lsns,&end); groups++; events += group_events;
    }
    if (status != CANDIDATE_WAL_OK) return status;
    if (groups > UINT64_MAX - p->max_group_id) return CANDIDATE_WAL_LIMIT;
    if (p->existing_groups + groups > CANDIDATE_WITNESS_GROUPS || p->existing_events + events > CANDIDATE_SYSTEM_EVENTS)
        return CANDIDATE_WAL_LIMIT;
    uint64_t checkpoint[2 + CANDIDATE_CHECKPOINT_CHUNKS];
    status = candidate_publish_place_checkpoint(wal,&cursor,p->transactions,checkpoint,&end);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_control_state_t next;
    status = candidate_publish_next(p,control,p->system_page_highwater + p->reserve_pages,&next);
    if (status != CANDIDATE_WAL_OK) return status;
    next.checkpoint_begin = checkpoint[0]; next.checkpoint_end = end;
    return candidate_publish_check_control(p,&next);
}
