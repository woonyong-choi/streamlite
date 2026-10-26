#ifndef TEST_CANDIDATE_RECOVERY_CLOSURE_TRANSPORT_FIXTURE_H
#define TEST_CANDIDATE_RECOVERY_CLOSURE_TRANSPORT_FIXTURE_H

#include "test_candidate_recovery_closure_fixture.h"

#define TF_ROOM 104u
#define TF_SUFFIX_UNITS 20u
#define TF_PATCH_BYTES 128u

typedef struct {
    uint64_t fail_from, hook_from;
    cf_hook_t on_read;
    void *context;
} tf_fault_t;
typedef struct {
    candidate_recovery_view_t *view;
    candidate_wal_status_t status;
    size_t live_before, live_after;
    unsigned calls, loads;
    cf_io_t io;
    bool is_source_same;
} tf_attempt_t;
typedef struct {
    cf_t c;
    candidate_recovery_view_t *source;
    candidate_wal_info_t before, cut;
    candidate_recovery_closure_t closure;
    uint64_t next_segment, first_frame;
    cf_source_t held;
} tf_cross_t;
typedef enum { TF_PADDING, TF_HEADER, TF_FRAME } tf_region_t;
typedef void (*tf_mutate_t)(const tf_cross_t *x, uint64_t lsn, uint8_t *bytes);
typedef struct {
    const char *name;
    tf_region_t region;
    uint64_t offset;
    size_t size;
    tf_mutate_t mutate;
    candidate_wal_status_t expected;
} tf_variant_t;

bool tf_same_cut(const candidate_wal_info_t *a, const candidate_wal_info_t *b);
void tf_read_wal(const cf_t *c, uint64_t lsn, void *out, size_t size);
void tf_write_wal(const cf_t *c, uint64_t lsn, const void *bytes, size_t size);
void tf_segment_path(const cf_t *c, uint64_t id, char out[320]);
void tf_seal_segment(uint8_t header[CANDIDATE_WAL_HEADER_BYTES]);
void tf_image_abort(cf_t *c);
void tf_expect_delta_page(const cf_t *c);
cf_suffix_t tf_advance(cf_t *c, candidate_recovery_view_t **view, const char *tag);
void tf_cross_open(tf_cross_t *x, const char *tag, uint64_t room);
void tf_cross_close(tf_cross_t *x);
tf_attempt_t tf_attempt(tf_cross_t *x, const tf_fault_t *fault);
void tf_expect_refused(const char *tag, const tf_attempt_t *a, candidate_wal_status_t expected);
void tf_expect_poisoned(const char *tag, const tf_attempt_t *a);
void tf_expect_reuse(tf_cross_t *x, const char *tag, bool should_reopen);
void tf_run_variants(const char *tag, const tf_variant_t *rows, unsigned count);

#endif
