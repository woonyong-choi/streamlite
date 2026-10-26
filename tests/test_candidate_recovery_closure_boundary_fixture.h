#ifndef TEST_CANDIDATE_RECOVERY_CLOSURE_BOUNDARY_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_CLOSURE_BOUNDARY_FIXTURE_H

#include "test_candidate_recovery_closure_fixture.h"

typedef struct {
    unsigned calls;
    size_t live;
} bf_memory_t;
typedef struct {
    uint64_t records, images, terminals, checkpoints, pages;
} bf_clone_t;
typedef struct {
    char prefix[300], data[320];
    uint8_t uuid[16];
    uint64_t owner;
    candidate_control_state_t anchor;
    candidate_wal_t *wal;
    int data_fd;
    unsigned loads;
    bf_memory_t memory;
    candidate_wal_memory_t allocator;
    bf_clone_t cloned;
} bf_db_t;

extern const uint8_t bf_uuid_b[16];

void bf_clone(const cf_t *source, bf_db_t *b);
void bf_free(bf_db_t *b);
candidate_recovery_view_request_t bf_request(bf_db_t *b, const candidate_control_state_t *anchor,
    candidate_recovery_view_mode_t mode, uint64_t owner);
candidate_wal_status_t bf_load(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES]);
uint64_t bf_abort(bf_db_t *b, uint64_t id);
void bf_format_uuid(const uint8_t uuid[16], char out[33]);

#endif
