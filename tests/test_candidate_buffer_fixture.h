#ifndef TEST_CANDIDATE_BUFFER_FIXTURE_H
#define TEST_CANDIDATE_BUFFER_FIXTURE_H

#include <sys/types.h>

#include "test_candidate_recovery_publish_fixture.h"
#include "storage/candidate_buffer.h"

#define BF_EVENTS 8192u
#define BF_PAGES 16u
#define BF_SIZES 4096u
#define BF_HELD 64u
#define BF_WRITES 64u
#define EXPECT(call, expected) bf_expect_status((call),(expected),__func__,__LINE__,#call)
#undef OK
#define OK(call) EXPECT((call),CANDIDATE_WAL_OK)

typedef enum { EV_NONE, EV_WAL_WRITE, EV_WAL_SYNC, EV_DATA_WRITE, EV_DATA_SYNC, EV_CONTROL_WRITE, EV_CONTROL_SYNC, EV_OPEN,
    EV_DIRECTORY_SYNC, EV_DONE, EV_FAULT, EV_KINDS } bf_kind_t;
typedef enum { FAULT_ERROR, FAULT_SHORT, FAULT_CRASH_BEFORE, FAULT_CRASH_AFTER, FAULT_TORN } bf_fault_t;
typedef enum { VIEW_OFF, VIEW_ALTERED, VIEW_ERROR, VIEW_EOF } bf_view_mode_t;
typedef struct { bf_kind_t kind; uint64_t offset, size, id, inode; } bf_event_t;
typedef struct {
    bf_kind_t kind;
    unsigned at, matched;
    bf_fault_t mode;
    uint16_t type, chunk;
    bool has_fired;
} bf_trigger_t;
typedef struct bf bf_t;
typedef struct {
    unsigned calls, fail_at, size_count, releases, held_count;
    size_t live, sizes[BF_SIZES], held_sizes[BF_HELD];
    void *held[BF_HELD];
    bool is_quarantine;
    void (*on_release)(bf_t *b, size_t size);
} bf_memory_t;
typedef enum { MUTATE_OFF, MUTATE_IMAGE, MUTATE_CHUNK, MUTATE_END } bf_mutate_mode_t;
typedef struct {
    bf_mutate_mode_t mode;
    size_t offset;
    uint32_t group_crc;
    uint8_t digest[32];
    bool is_tracking, has_fired;
} bf_mutate_t;
typedef struct {
    bf_view_mode_t mode;
    unsigned from_sync, reads;
    ino_t inode;
    size_t size;
    uint8_t *bytes;
} bf_view_t;
typedef struct {
    uint64_t lsn;
    size_t offset;
    unsigned from_sync, at_scan, scans, verifies;
    const char *phase;
    bool is_armed, on_verify;
} bf_swap_t;
typedef void (*bf_hook_t)(bf_t *b, uint64_t id, unsigned index);
struct bf {
    built_t built;
    fx_t *fx;
    char tag[64];
    uint64_t owner;
    candidate_wal_t *wal;
    candidate_control_t *control;
    int data_fd, trace_fd;
    candidate_buffer_t *pool;
    bf_memory_t memory;
    candidate_wal_memory_t allocator;
    ino_t control_inode, data_inode;
    bf_event_t events[BF_EVENTS];
    unsigned event_count, counts[EV_KINDS], wal_syncs, data_writes;
    uint64_t trace_seq;
    bf_trigger_t trigger;
    bool is_tracing, is_injecting;
    bf_hook_t on_write;
    void *hook_context;
    uint8_t (*written)[CANDIDATE_PAGE_BYTES];
    uint64_t written_ids[BF_WRITES];
    bf_view_t view;
    bf_mutate_t mutate;
    bf_swap_t swap;
    unsigned verifies;
    bool is_weak_verifier;
    uint64_t pages[BF_PAGES];
    unsigned page_count;
    uint64_t next_transaction;
    unsigned recoveries, checks;
    uint64_t changes, snapshot_changes, post_changes;
    bool has_snapshot, is_post;
};

extern bf_t *bf_env;

void bf_boot(bf_t *b, const char *tag, unsigned requests);
void bf_open_handles(bf_t *b);
candidate_wal_status_t bf_open_pool(bf_t *b);
candidate_buffer_options_t bf_options(bf_t *b);
void bf_close(bf_t *b);
void bf_free(bf_t *b);
candidate_wal_status_t bf_recover(bf_t *b, uint64_t owner);
candidate_wal_status_t bf_prove(bf_t *b, const candidate_control_state_t *anchor, const char *label);
void bf_reattach(bf_t *b, uint64_t owner);
void bf_restart(bf_t *b, uint64_t owner);

void bf_read_data(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES], bool *present);
void bf_copy(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES]);
void bf_current(bf_t *b, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES]);
uint64_t bf_generation(bf_t *b, uint64_t id);
candidate_page_ref_t bf_ref(bf_t *b, uint64_t id);
void bf_with_version(const uint8_t *page, uint64_t transaction, uint64_t seed, size_t size, uint8_t out[CANDIDATE_PAGE_BYTES]);
void bf_with_frozen(const uint8_t *page, uint64_t transaction, uint64_t frozen, uint64_t seed, size_t size,
    uint8_t out[CANDIDATE_PAGE_BYTES]);
void bf_rewrite_version(const uint8_t *page, uint64_t transaction, uint64_t seed, uint8_t out[CANDIDATE_PAGE_BYTES]);
bool bf_has_version(const uint8_t *page, uint64_t transaction);
size_t bf_body_offset(const uint8_t *page, uint64_t transaction);
bool bf_has_body(const uint8_t *page, uint64_t transaction, uint64_t seed, size_t size);
void bf_expect_body(bf_t *b, uint64_t id, uint64_t transaction, uint64_t seed, size_t size);
void bf_expect_absent(bf_t *b, uint64_t id, uint64_t transaction);
void bf_set_lsn(uint8_t page[CANDIDATE_PAGE_BYTES], uint64_t lsn);
uint64_t bf_lsn(const uint8_t *page);

void bf_begin(bf_t *b, fx_txn_t *t);
candidate_wal_status_t bf_write(bf_t *b, fx_txn_t *t, const uint64_t *ids, const uint8_t *const *pages, unsigned count,
    candidate_buffer_admission_t *out);
candidate_wal_status_t bf_write_version(bf_t *b, fx_txn_t *t, uint64_t id, uint64_t seed, candidate_buffer_admission_t *out);
candidate_wal_status_t bf_write_update(bf_t *b, fx_txn_t *t, uint64_t id, uint64_t seed, candidate_buffer_admission_t *out);
uint64_t bf_external_pages(bf_t *b, fx_txn_t *t, const uint64_t *ids, uint8_t (*pages)[CANDIDATE_PAGE_BYTES], unsigned count,
    bool complete);
uint64_t bf_external(bf_t *b, fx_txn_t *t, const uint64_t *ids, unsigned count, uint64_t seed, bool complete);
uint64_t bf_external_delta(bf_t *b, fx_txn_t *t, uint64_t id, uint32_t offset, const uint8_t *bytes, uint32_t size);
void bf_commit(bf_t *b, fx_txn_t *t);
void bf_abort(bf_t *b, fx_txn_t *t);

uint64_t bf_reserve(bf_t *b, uint64_t transactions, uint64_t pages);
uint64_t bf_epoch(bf_t *b, uint64_t epoch, uint64_t floor);
uint64_t bf_alloc(bf_t *b, uint64_t id);

void bf_trace(bf_t *b, bool on);
void bf_dump(const bf_t *b, const char *label);
unsigned bf_find(const bf_t *b, unsigned from, bf_kind_t kind);
void bf_arm(bf_t *b, bf_kind_t kind, unsigned at, bf_fault_t mode);
void bf_arm_record(bf_t *b, uint16_t type, uint16_t chunk, unsigned at, bf_fault_t mode);
void bf_view_altered(bf_t *b, uint64_t lsn, size_t offset, bool keep_checksums);
void bf_view_set(bf_t *b, bf_view_mode_t mode, unsigned after_syncs);
void bf_swap_at_scan(bf_t *b, uint64_t lsn, size_t offset, unsigned after_syncs, unsigned at_scan);
void bf_swap_on_verify(bf_t *b, uint64_t lsn, size_t offset);
candidate_wal_status_t bf_recover_view(bf_t *b, const char *label, uint64_t owner, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES]);
void bf_snapshot(bf_t *b, const char *label);
void bf_snapshot_all(bf_t *b, const char *label);
void bf_release_held(bf_t *b);
void bf_mutate(bf_t *b, bf_mutate_mode_t mode, size_t offset);
uint64_t bf_external_new(bf_t *b, fx_txn_t *t, uint64_t id);
void bf_with_previous(const uint8_t *page, uint64_t transaction, uint64_t seed, candidate_version_ref_t previous,
    uint8_t out[CANDIDATE_PAGE_BYTES]);
uint64_t bf_alloc_node_ref(bf_t *b);
const char *bf_status(candidate_wal_status_t status);
void bf_expect_status(candidate_wal_status_t actual, candidate_wal_status_t expected, const char *function, int line,
    const char *call);
void bf_report(const bf_t *b, const char *label, candidate_wal_status_t status);

#endif
