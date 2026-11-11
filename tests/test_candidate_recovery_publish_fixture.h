#ifndef TEST_CANDIDATE_RECOVERY_PUBLISH_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_PUBLISH_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_checkpoint.h"
#include "storage/candidate_recovery.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
#define FX_GROUP_DEPENDENCIES 256u
#ifndef TEST_DIR_PATH
#define TEST_DIR_PATH "build"
#endif

extern unsigned checks;
extern const uint8_t fx_uuid[16];

typedef struct {
    uint64_t id, first, last, records, csn, terminal_end, epoch, limit, result_bytes;
    uint32_t crc, chunks, groups;
    uint8_t nonce[16], operation[32], result_digest[32];
    uint64_t group_ids[FX_GROUP_DEPENDENCIES], group_ends[FX_GROUP_DEPENDENCIES];
    candidate_version_ref_t result;
    bool has_admission, has_begin;
} fx_txn_t;
typedef struct { uint64_t id, generation, previous; uint8_t page[CANDIDATE_PAGE_BYTES]; } fx_image_t;
typedef struct { uint16_t type; uint64_t a, b, c, d; } fx_event_t;
typedef struct { uint64_t generation, lsn; bool has_data; } fx_page_t;
typedef struct {
    char wal[256], control[280], data[280];
    candidate_wal_t *w;
    int data_fd;
    uint64_t owner, group_id, highest_csn, page_slots;
    fx_page_t *pages;
    candidate_allocation_entry_t *alloc;
    uint32_t alloc_count, alloc_capacity;
} fx_t;

fx_t *fx_create(const char *name, uint64_t owner);
uint64_t fx_position(const fx_t *fx);
uint64_t fx_place(uint64_t position, size_t payload_bytes, uint64_t *start);
uint64_t fx_group_end(const fx_t *fx, unsigned images, unsigned events);
void fx_admit(fx_t *fx, fx_txn_t *t, uint64_t id, uint64_t epoch, uint64_t limit);
void fx_begin(fx_t *fx, fx_txn_t *t, uint64_t id);
void fx_chunks(fx_t *fx, fx_txn_t *t, candidate_version_ref_t ref, const uint8_t *body, size_t size);
void fx_commit(fx_t *fx, fx_txn_t *t);
void fx_abort(fx_t *fx, fx_txn_t *t);
uint64_t fx_group(fx_t *fx, fx_txn_t *t, fx_image_t *images, unsigned image_count,
    const fx_event_t *events, unsigned event_count, bool complete, bool write_data);
uint64_t fx_delta_group(fx_t *fx, fx_txn_t *t, uint64_t id, uint32_t offset, const uint8_t *bytes, uint32_t size);
candidate_page_location_t fx_location(uint64_t id, uint64_t generation, uint64_t lsn);
void fx_payload(uint64_t seed, size_t size, uint8_t *out);
void fx_alloc_add(fx_t *fx, uint64_t id, uint64_t generation, candidate_allocation_state_t state,
    uint64_t horizon, uint64_t transition_lsn);
candidate_allocation_entry_t *fx_alloc_find(fx_t *fx, uint64_t id);
void fx_encode_alloc_node(const fx_t *fx, uint32_t ordinal, uint64_t id, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES]);
void fx_encode_alloc_root(const fx_t *fx, uint64_t id, uint64_t lsn, uint64_t owner, uint64_t highwater,
    uint64_t gc, const candidate_page_ref_t *nodes, uint8_t out[CANDIDATE_PAGE_BYTES]);
void fx_checkpoint(fx_t *fx, candidate_control_state_t *anchor, const fx_txn_t *const *active, unsigned active_count);
void fx_control(fx_t *fx, const candidate_control_state_t *anchors, unsigned count);
void fx_tail_garbage(fx_t *fx, size_t bytes);
void fx_close(fx_t *fx);
void fx_free(fx_t **fx);
void fx_copy_file(const char *source, const char *target);
void fx_copy_set(const fx_t *fx, const char *tag, fx_t *out);
uint64_t fx_file_bytes(const char *path);
void fx_sha256_file(const char *path, uint8_t out[32]);
candidate_recovery_options_t fx_options(const fx_t *fx, uint64_t owner);

typedef enum { REQ_COMMIT = 'C', REQ_ABORT = 'A', REQ_UNFINISHED = 'U' } req_state_t;
typedef enum { STORE_NONE = 'N', STORE_ADMITTED = 'D', STORE_FINAL = 'F' } req_store_t;
typedef struct {
    char state, store;
    uint64_t epoch;
    uint32_t bytes;
    bool retain, has_version;
} req_t;
typedef struct {
    const char *name;
    unsigned requests;
    const req_t *req;
    uint64_t root_epoch, root_floor, epoch_to, floor_to;
    bool free_epoch_root, epoch_root_entries, invalid_extra_root, good_extra_root;
    unsigned expired_roots;
    unsigned ballast, extra_empty_roots, pad_alloc_to;
    uint64_t page_highwater, txn_highwater;
    unsigned chain_b;
    bool unapplied_last, system_bootstrap, stored_visible_ahead;
    uint64_t retained_extra, gc_horizon;
    void (*before_checkpoint)(void *built, void *context);
    void *context;
} scen_t;
typedef struct {
    fx_t *fx;
    scen_t spec;
    candidate_control_state_t anchor;
    fx_txn_t *txn;
    uint64_t root_id, alloc_root_id, epoch_root_id, carrier_txn;
    uint32_t alloc_count, version_pages, stored_nodes;
    uint64_t last_group_end;
} built_t;

built_t scen_build(const scen_t *spec);
void scen_finish(built_t *b);
candidate_catalog_query_t scen_query(const built_t *b, unsigned index);
void scen_free(built_t *b);

#endif
