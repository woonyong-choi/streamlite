#ifndef TEST_CANDIDATE_RECOVERY_PHYSICAL_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_PHYSICAL_FIXTURE_H

#include "test_candidate_recovery_closure_fixture.h"
#include "../src/storage/candidate_recovery_physical_internal.h"
#include "../src/storage/candidate_redo_internal.h"

#define PF_PAGES 64u
#define PF_FIRST_NONCE 0x11u
#define PF_SECOND_NONCE 0x22u
#define PF_LATE_NONCE 0x55u

typedef struct { uint64_t begin, record, end_record, end; } pf_group_t;
typedef struct {
    uint64_t ids[PF_PAGES];
    uint8_t (*bytes)[CANDIDATE_PAGE_BYTES];
    uint32_t count;
} pf_snapshot_t;
typedef struct {
    unsigned reads, writes, syncs, fail_write_at, patch_after;
    uint64_t absent_id, durable_at_write;
    pf_group_t patched;
    bool should_fail_sync;
} pf_target_t;
typedef struct {
    cf_t c;
    candidate_recovery_view_t *source, *next;
    uint64_t refresh_lsn, refresh_end, free_id, free_end, realloc_id, realloc_begin, realloc_end;
    pf_group_t first_patch, second_patch;
    uint8_t delta_base[CANDIDATE_PAGE_BYTES];
    candidate_wal_info_t b, t;
    pf_snapshot_t at_b, at_t;
    pf_target_t target;
} pf_t;

void pf_build(pf_t *p, const char *tag, candidate_recovery_view_mode_t mode);
void pf_extend(pf_t *p);
void pf_free(pf_t *p);
void pf_snapshot(pf_t *p, pf_snapshot_t *out);
const uint8_t *pf_snapshot_page(const pf_snapshot_t *s, uint64_t id);
void pf_read_data(const pf_t *p, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES]);
uint64_t pf_patch(pf_t *p, uint8_t seed, pf_group_t *out);
void pf_image(pf_t *p, uint64_t previous);
void pf_plain(pf_t *p);
void pf_expected_patch(const pf_t *p, uint8_t seed, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES]);
void pf_expected_empty(uint64_t id, uint64_t generation, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES]);
void pf_tear(pf_t *p, uint64_t id);
void pf_forge_body(pf_t *p, uint64_t id);
void pf_rewrite_payload(pf_t *p, const pf_group_t *group);
void pf_rewrite_records(pf_t *p, const pf_group_t *group, int32_t change);
void pf_replace_segment(pf_t *p);
candidate_physical_seed_t pf_seed_of(const candidate_recovery_view_t *view);
candidate_redo_seed_t pf_seed(pf_t *p, size_t retained_bytes);
candidate_redo_target_t pf_target(pf_t *p);
uint32_t pf_live_pages(const candidate_recovery_view_t *view);
uint32_t pf_expect_loads(candidate_redo_preview_t *preview, const candidate_recovery_view_t *view, const pf_snapshot_t *expected);
uint32_t pf_expect_data(const pf_t *p, const pf_snapshot_t *expected);

#endif
