#ifndef STORAGE_CANDIDATE_RECOVERY_PHYSICAL_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_PHYSICAL_INTERNAL_H

#include "storage/candidate_allocation.h"
#include "candidate_recovery_internal.h"

typedef struct {
    uint64_t lsn, records, page_records, owner_epoch;
    uint8_t digest[32];
} candidate_physical_cut_t;
typedef struct {
    candidate_physical_cut_t range, end;
    candidate_physical_cut_t *groups;
    uint32_t group_count;
} candidate_recovery_physical_t;
typedef struct {
    candidate_page_ref_t ref;
    candidate_allocation_state_t state;
    uint64_t transition_lsn, end_lsn, image_lsn;
} candidate_physical_page_t;
typedef struct {
    uint8_t uuid[16];
    candidate_recovery_view_mode_t mode;
    candidate_wal_info_t before, after;
    uint8_t before_digest[32], after_digest[32];
    uint64_t page_records;
    size_t resident_bytes;
} candidate_physical_seed_t;

/* 한 관측 동안만 group 시작 cut 표를 소유하며 end는 0 또는 복사한 source cut의 hash 상태에서 이어진다. */
candidate_wal_status_t candidate_recovery_physical_begin(candidate_recovery_physical_t *physical,
    const candidate_wal_memory_t *memory);
candidate_wal_status_t candidate_recovery_physical_observe(candidate_recovery_physical_t *physical,
    const candidate_wal_record_t *record);
/* 관측 끝을 view cut과 대조하고 LIVE 최신 IMAGE group 중 가장 이른 시작을 R로 고정한 뒤 표를 해제한다. */
/* R의 hash 상태를 이번 관측에서 보지 못했으면 source R을 유지하며 그보다 앞서야 하면 CORRUPT다. */
candidate_wal_status_t candidate_recovery_physical_settle(candidate_recovery_view_t *view);
void candidate_recovery_physical_release(candidate_recovery_physical_t *physical, const candidate_wal_memory_t *memory);
/* before는 R 직전 transport cut, after는 view cut이며 반환 값은 view를 빌리지 않는다. */
candidate_wal_status_t candidate_recovery_physical_seed(const candidate_recovery_view_t *view,
    candidate_physical_seed_t *out);
/* allocation ordinal 순서의 LIVE·FREE 권위와 page 이력이며 범위 밖은 false다. */
bool candidate_recovery_physical_page_at(const candidate_recovery_view_t *view, uint32_t ordinal,
    candidate_physical_page_t *out);

#endif
