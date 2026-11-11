#ifndef TEST_CANDIDATE_MVCC_FIXTURE_H
#define TEST_CANDIDATE_MVCC_FIXTURE_H

#include "storage/candidate_mvcc.h"
#include "test_candidate_recovery_publish_fixture.h"

#undef CHECK
#define CHECK(c) do { __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED); if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); \
    exit(1); } } while (0)

#define MF_ROWS 3u
#define MF_PAYLOAD 64u
#define MF_RECORDS 64u
#define MF_SLOTS CANDIDATE_PAGE_MAX_SLOTS
#define MF_RESULT_TABLE 900u
#define TEST_DB_PATH(f) ((f)->built.fx->data)

typedef struct {
    unsigned fsyncs, pwrites, allocations;
    unsigned fail_fsync_at, fail_pwrite_at, fail_allocation_at;
} mf_faults_t;
typedef struct {
    built_t built;
    char name[160];
    uint8_t initial[MF_ROWS][MF_PAYLOAD];
    uint64_t next_owner, wal_budget_bytes, seed_transaction;
    candidate_version_ref_t seed_versions[2];
    bool is_seeded;
} mf_t;
typedef struct {
    uint16_t types[MF_RECORDS];
    uint64_t lsns[MF_RECORDS];
    unsigned count;
    uint64_t csn, terminal_end;
} mf_chain_t;
/* structures와 allocations는 type3 IMAGE를 담은 txn0 group과 그 ALLOC event만 센다. */
typedef struct {
    unsigned records, reserves, commits, aborts, structures, allocations, group_allocations;
    uint64_t end_lsn, highwater;
    bool has_group_store;
} mf_wal_t;
typedef struct {
    uint64_t transaction_id, frozen_csn, table_id, row_id, size;
    candidate_version_ref_t previous;
    uint32_t generation;
    const uint8_t *bytes;
} mf_slot_t;
typedef struct {
    uint16_t type, page_type;
    uint64_t lsn, end, transaction_id, words[3];
} mf_record_t;
/* 다음 fsync 직전마다 불리며 true를 돌려준 첫 호출 뒤 해제된다. 실제 fsync는 그 뒤에 그대로 수행한다. */
typedef bool (*mf_fsync_hook_t)(void *context);
typedef bool (*mf_pwrite_hook_t)(void *context, const void *bytes, size_t size);

extern mf_faults_t mf_faults;

/* 새 배타 이름의 후보 파일을 만들고 runtime 거래로 ROW_A·ROW_B 초기값을 commit한다. */
void mf_boot(mf_t *f, const char *tag);
/* before_checkpoint가 없을 때만 초기값을 commit한다. */
void mf_boot_boundary(mf_t *f, const char *tag, void (*before_checkpoint)(void *, void *), void *context);
/* 거래 ID 소비를 대조하는 사례라 초기값을 commit하지 않는다. */
void mf_boot_highwater(mf_t *f, const char *tag, uint64_t txn_highwater);
void mf_arm_fsync_hook(mf_fsync_hook_t hook, void *context);
void mf_arm_pwrite_hook(mf_pwrite_hook_t hook, void *context);
/* 아래 둘은 실제 호출 직전 predicate가 true인 첫 호출만 EIO로 거절하고 해제된다. */
void mf_arm_pwrite_fault(mf_pwrite_hook_t predicate, void *context);
void mf_arm_fsync_fault(mf_fsync_hook_t predicate, void *context);
void mf_free(mf_t *f);
candidate_wal_status_t mf_open(mf_t *f, candidate_mvcc_t **out);
candidate_mvcc_key_t mf_key(unsigned row);
candidate_mvcc_request_t mf_request(uint64_t nonce, const candidate_mvcc_write_t *writes, uint32_t count);
candidate_mvcc_write_t mf_write(unsigned row, const uint8_t *bytes, uint32_t size);
void mf_fill(uint8_t seed, uint8_t out[MF_PAYLOAD]);
/* 초기값을 commit한 fixture는 그 bytes, 아니면 부재여야 한다. */
void mf_expect_initial(const mf_t *f, const candidate_mvcc_value_t *value, unsigned row);
void mf_arm_fsync(unsigned after);
void mf_arm_pwrite(unsigned after);
void mf_arm_allocation(unsigned after);
void mf_disarm(void);
void mf_scan_wal(const mf_t *f, uint64_t transaction_id, mf_chain_t *chain, mf_wal_t *wal);
/* from_lsn 이상 record를 같은 방식으로 읽고 payload 앞 세 8-byte 값을 words에 둔다. */
unsigned mf_scan_records(const mf_t *f, uint64_t from_lsn, mf_record_t *out, unsigned capacity);
void mf_read_page(const mf_t *f, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES]);
unsigned mf_slots(const uint8_t page[CANDIDATE_PAGE_BYTES], mf_slot_t *out, unsigned capacity);
/* data file의 실제 page에서 ref가 가리키는 slot을 page 버퍼와 함께 해석한다. */
void mf_read_version(const mf_t *f, candidate_version_ref_t ref, uint8_t page[CANDIDATE_PAGE_BYTES], mf_slot_t *out);
const char *mf_status(candidate_wal_status_t status);
void mf_expect(candidate_wal_status_t actual, candidate_wal_status_t expected, const char *function, int line, const char *call);

#define EXPECT(call, expected) mf_expect((call), (expected), __func__, __LINE__, #call)

#endif
