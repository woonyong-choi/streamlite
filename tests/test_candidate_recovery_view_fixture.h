#ifndef TEST_CANDIDATE_RECOVERY_VIEW_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_VIEW_FIXTURE_H

#include "test_candidate_recovery_publish_fixture.h"
#include "../src/storage/candidate_recovery_internal.h"

typedef struct vf vf_t;
typedef void (*vf_hook_t)(vf_t *f);
typedef void (*vf_edit_t)(candidate_page_location_t *location, candidate_page_version_t *versions, uint32_t count,
    void *context);
typedef struct {
    unsigned calls, fail_at;
    size_t live, peak;
    candidate_recovery_view_request_t *mutate_request;
    candidate_wal_memory_t *mutate_memory;
} vf_memory_t;
typedef struct {
    uint64_t read_bytes;
    unsigned reads, fsyncs, fsync_failures;
    vf_hook_t on_read;
    bool should_fail_fsync;
} vf_io_t;
struct vf {
    char name[128];
    built_t built;
    uint64_t owner;
    candidate_control_state_t anchor;
    candidate_wal_t *wal;
    int data_fd;
    vf_memory_t memory;
    candidate_wal_memory_t allocator;
    unsigned loads, hook_at;
    vf_hook_t on_load;
    void *context;
    bool is_loader_closed;
};

extern vf_io_t vf_io;
extern vf_t *vf_env;
extern const req_t vf_clean[3];
extern const req_t vf_expired[4];

void vf_boot(vf_t *f, const char *tag, const scen_t *spec, bool is_recovered);
void vf_open(vf_t *f);
void vf_close(vf_t *f);
void vf_free(vf_t *f);
void vf_reset_memory(vf_t *f);
candidate_recovery_view_request_t vf_request(vf_t *f, candidate_recovery_view_mode_t mode, uint64_t owner);
candidate_wal_status_t vf_load(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES]);
candidate_wal_status_t vf_legacy(vf_t *f, candidate_recovery_view_mode_t mode, uint64_t owner,
    candidate_recovery_info_t *graph, candidate_outcome_witness_info_t *outcomes);
candidate_wal_status_t vf_graph(vf_t *f, candidate_recovery_view_mode_t mode);
void vf_edit_versions(vf_t *f, uint64_t id, vf_edit_t edit, void *context);
candidate_wal_status_t vf_read_version(vf_t *f, candidate_version_ref_t ref, candidate_page_version_t *out);
void vf_expect_absent(vf_t *f, unsigned index);
void vf_abort_resultless(vf_t *f, uint64_t id);
void vf_flip_wal_byte(vf_t *f, uint64_t offset);
void vf_mark_admitted(vf_t *f, unsigned index);
void vf_expect_answer(const vf_t *f, unsigned index, candidate_catalog_lookup_t lookup, const candidate_outcome_t *outcome);
void vf_expect_body(const vf_t *f, unsigned index, const candidate_page_result_t *body);
candidate_recovery_options_t vf_options(vf_t *f, uint64_t owner);
const char *vf_status(candidate_wal_status_t status);

#endif
