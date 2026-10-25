#ifndef STORAGE_CANDIDATE_RECOVERY_PUBLISH_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_PUBLISH_INTERNAL_H

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_outcome_rebuild.h"
#include "candidate_recovery_internal.h"
#include "candidate_wal_internal.h"

#define PUBLISH_NEW_PAGES 128u
#define PUBLISH_GROUP_PAGES 60u
#define PUBLISH_POOL CANDIDATE_ALLOCATION_ENTRIES
#define PUBLISH_SET_SLOTS (2u * CANDIDATE_ALLOCATION_ENTRIES)
#define PUBLISH_IMAGE_BYTES (32u + CANDIDATE_PAGE_BYTES)

typedef enum { ROLE_OUTCOME_NODE = 1, ROLE_ALLOCATION_NODE, ROLE_REPLACEMENT_NODE } page_role_t;
typedef struct {
    uint64_t id, transition_lsn;
    page_role_t role;
    uint32_t ordinal;
} planned_page_t;
typedef struct {
    candidate_page_ref_t ref;
    uint32_t ordinal, count;
    uint8_t digest[32];
} pooled_node_t;
typedef struct {
    candidate_page_ref_t ref;
    uint64_t lsn;
} tracked_page_t;

#define PUBLISH_VERSION_BITS (CANDIDATE_ALLOCATION_ENTRIES * CANDIDATE_PAGE_MAX_SLOTS)

typedef struct {
    candidate_recovery_t *recovery;
    candidate_wal_memory_t memory;
    candidate_control_state_t anchor, next;
    candidate_outcome_rebuild_options_t rebuild_options;
    candidate_outcome_rebuild_t *rebuild;
    candidate_allocation_t *allocation;
    uint8_t *visited;
    uint64_t owner;
    candidate_allocation_root_t allocation_root;
    candidate_allocation_node_t tail;
    tracked_page_t allocation_page, tail_page, catalog_page, node_pages[CANDIDATE_ALLOCATION_NODES];
    candidate_catalog_root_t catalog_root, desired_root, carrier_root;
    tracked_page_t carrier_page;
    uint32_t carrier_new, stage_base, maintenance_pages;
    bool carrier_is_selected;
    uint64_t max_allocated_id, max_group_id, system_page_highwater, system_transaction_highwater;
    uint64_t retained_bytes;
    uint32_t allocation_count, allocation_nodes, existing_events, existing_groups, transactions;
    candidate_page_ref_t desired_nodes[CANDIDATE_CATALOG_PAGES];
    uint8_t desired_digest[CANDIDATE_CATALOG_PAGES][32];
    bool reused_node[CANDIDATE_CATALOG_PAGES];
    uint32_t pool_count, protected_count;
    pooled_node_t pool[PUBLISH_POOL];
    uint64_t protected_ids[PUBLISH_SET_SLOTS], history_ids[PUBLISH_SET_SLOTS];
    uint64_t ids[PUBLISH_NEW_PAGES + 2], carrier_ids[2];
    planned_page_t pages[PUBLISH_NEW_PAGES];
    uint32_t page_count, reusable_count, new_nodes;
    uint64_t reserve_pages;
    bool needs_rewrite;
    uint8_t images[CANDIDATE_WITNESS_PAGES][PUBLISH_IMAGE_BYTES];
} publish_t;

candidate_wal_status_t candidate_publish_inventory(publish_t *p);
candidate_wal_status_t candidate_publish_plan_pages(publish_t *p);
candidate_wal_status_t candidate_publish_preflight(publish_t *p, const candidate_control_info_t *control);
candidate_wal_status_t candidate_publish_reserve(publish_t *p);
candidate_wal_status_t candidate_publish_stage(publish_t *p);
candidate_wal_status_t candidate_publish_checkpoint(publish_t *p, const candidate_control_info_t *control);
bool candidate_publish_set_add(uint64_t *slots, uint64_t id, bool *added);
bool candidate_publish_set_has(const uint64_t *slots, uint64_t id);
candidate_wal_status_t candidate_publish_append(publish_t *p, candidate_wal_record_t *record, uint64_t lsn);
candidate_wal_status_t candidate_publish_place_checkpoint(const candidate_wal_t *wal, candidate_wal_cursor_t *cursor,
    uint32_t transactions, uint64_t *lsns, uint64_t *end);
candidate_wal_status_t candidate_publish_next(const publish_t *p, const candidate_control_info_t *control,
    uint64_t page_highwater, candidate_control_state_t *next);
candidate_wal_status_t candidate_publish_follows(const candidate_control_info_t *control, const candidate_control_state_t *next);

#endif
