#ifndef STORAGE_CANDIDATE_BUFFER_H
#define STORAGE_CANDIDATE_BUFFER_H

#include "storage/candidate_redo.h"

#define CANDIDATE_BUFFER_FRAME_BUDGET (64u * 1024u * 1024u)
#define CANDIDATE_BUFFER_STAGING_BUDGET (8u * 1024u * 1024u)
#define CANDIDATE_BUFFER_TOTAL_BUDGET (128u * 1024u * 1024u)
#define CANDIDATE_BUFFER_FRAMES 4096u
#define CANDIDATE_BUFFER_LEASES 64u
#define CANDIDATE_BUFFER_CONTRIBUTORS 128u
#define CANDIDATE_BUFFER_GROUP_PAGES 64u

typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    candidate_wal_t *wal;
    candidate_control_t *control;
    candidate_redo_target_t target;
    const candidate_wal_memory_t *memory;
    uint32_t frames, leases;
    size_t frame_budget_bytes, staging_budget_bytes, total_budget_bytes;
} candidate_buffer_options_t;

typedef struct {
    uint32_t slot;
    uint64_t frame_generation, pool_id, lease_id;
} candidate_buffer_lease_t;

typedef struct {
    candidate_page_ref_t ref;
    const uint8_t *page;
} candidate_buffer_page_t;

typedef struct {
    uint64_t transaction_id, previous_lsn;
    const candidate_buffer_page_t *pages;
    uint32_t count;
} candidate_buffer_group_t;

/* 앞의 count-allocated개는 기존 page, 뒤의 allocated개는 현재 page_highwater 다음 ID를 차례로 쓰는 세대1 새 page다. */
typedef struct {
    const candidate_buffer_page_t *pages;
    uint32_t count, allocated;
} candidate_buffer_structure_t;

typedef struct {
    uint64_t group_id, begin_lsn, end_lsn;
    uint64_t transaction_last_lsn, transaction_records;
    uint32_t transaction_chain_crc, pages, images, deltas;
} candidate_buffer_admission_t;

typedef struct {
    uint64_t group_id, end_lsn, previous_highwater, next_highwater;
} candidate_buffer_reservation_t;

typedef struct {
    uint64_t forced_lsn;
    uint32_t written, redirtied, unresolved, pinned, busy;
} candidate_buffer_flush_t;

typedef struct {
    candidate_control_state_t anchor;
    candidate_buffer_flush_t writeback;
    uint64_t begin_lsn, end_lsn;
    uint32_t dirty_entries, transactions, active, remaining_dirty;
} candidate_buffer_checkpoint_t;

typedef struct {
    candidate_control_state_t anchor;
    uint64_t pool_id, baseline_lsn, admitted_lsn, observed_end_lsn, fpi_floor_lsn, last_group_id, page_highwater;
    uint64_t groups_admitted, pages_written, data_syncs, evictions;
    uint32_t frames, resident, dirty, pinned, leases, known_pages, transactions;
    size_t frame_bytes, staging_bytes, requested_bytes, peak_requested_bytes, budget_bytes, borrowed_bytes;
    bool is_fenced;
} candidate_buffer_info_t;

typedef struct candidate_buffer candidate_buffer_t;

/* WAL/control/target은 닫힐 때까지 빌리며 다른 writer·파일 변경이 없는 배타 소유와 호출 직렬화는 caller 책임이다. */
/* 빌린 WAL은 처음 관측한 실제 record view에 pin되고 이후 그 handle의 append로만 늘어난다. pin은 close나 open 실패 뒤에도 남는다. */
/* 이미 pin된 WAL은 현재 파일을 그 view와 다시 대조하며 다른 정본 view는 증명·control verifier 중에도 영구 CORRUPT다. */
/* 선택 anchor와 전체 보존 WAL·data의 현재 page를 대조하며 미완료 거래/그룹·밀린 data는 UNSUPPORTED다. */
/* options와 allocator 값은 callback 전에 복사한다. 실패 시 *out은 NULL이고 파일을 바꾸지 않는다. */
candidate_wal_status_t candidate_buffer_open(const candidate_buffer_options_t *options, candidate_buffer_t **out);
/* 이미 append된 다음 완성 group을 끝 LSN으로 지정한다. LSN은 식별자이며 전체 WAL을 다시 대조한다. */
/* 한 group 전체를 받거나 아무것도 바꾸지 않는다. 받을 수 없는 이력은 pool을 차단하고 data를 쓰지 않는다. */
candidate_wal_status_t candidate_buffer_admit(candidate_buffer_t *pool, uint64_t end_lsn,
    candidate_buffer_admission_t *out);
/* 미완료 사용자 거래의 page 묶음을 checkpoint 뒤 첫 변경 image 규칙에 맞춰 append한 뒤 같은 경로로 받는다. */
/* 공간·의존성 검사는 append 전에 끝내며 append 뒤 실패는 pool 차단이다. 거래의 terminal은 caller가 append한다. */
/* page header LSN이 현재 page LSN과 다르면 STALE, 미등록·세대 변경 PageRef는 UNSUPPORTED이며 둘 다 append 전이다. */
/* 의도한 record는 I/O 전에 cut에 묶이고 빌린 WAL의 owner epoch는 pool과 같아야 한다. */
candidate_wal_status_t candidate_buffer_write_group(candidate_buffer_t *pool, const candidate_buffer_group_t *group,
    candidate_buffer_admission_t *out);
/* 현재 outcome·allocation root IMAGE와 ID_RESERVE를 한 system group으로 append·admit하고 끝까지 force한 뒤에만 receipt를 채운다. */
/* root 내용과 WAL system 상태 불일치는 append 전 UNSUPPORTED이고 append·force 뒤 실패는 pool 차단이다. */
candidate_wal_status_t candidate_buffer_reserve_transactions(candidate_buffer_t *pool, uint64_t count,
    candidate_buffer_reservation_t *out);
/* 기존·새 page IMAGE와 catalog/allocation root·allocation node IMAGE, page 예약과 새 page마다 ALLOC event를 */
/* 한 system group으로 append·admit하고 끝까지 force한 뒤에만 receipt를 채운다. 64 page·record 한도를 넘으면 append 전 LIMIT다. */
/* 새 page ID·세대 불일치는 append 전 INVALID, 미등록 기존 page는 UNSUPPORTED이며 append·force 뒤 실패는 pool 차단이다. */
candidate_wal_status_t candidate_buffer_write_structure(candidate_buffer_t *pool, const candidate_buffer_structure_t *structure,
    candidate_buffer_admission_t *out);
/* lease는 frame 세대와 pool에 묶이며 page bytes는 copy로만 읽는다. */
candidate_wal_status_t candidate_buffer_pin(candidate_buffer_t *pool, candidate_page_ref_t ref, candidate_buffer_lease_t *out);
candidate_wal_status_t candidate_buffer_copy(candidate_buffer_t *pool, const candidate_buffer_lease_t *lease,
    uint8_t out[CANDIDATE_PAGE_BYTES], uint64_t *page_lsn);
candidate_wal_status_t candidate_buffer_unpin(candidate_buffer_t *pool, candidate_buffer_lease_t *lease);
/* 의존 거래가 모두 WAL terminal인 dirty page만 최대 max_pages개 WAL force 뒤 쓰고 data sync한다. */
/* write callback 안에서는 admit만 허용한다. 쓰기·sync 실패는 dirty를 남기고 pool을 차단하며 out은0이다. */
candidate_wal_status_t candidate_buffer_flush(candidate_buffer_t *pool, uint32_t max_pages, candidate_buffer_flush_t *out);
/* BEGIN·WAL force·제한된 writeback/data sync·CHUNK/END·END force·반대 control slot 게시 순서로 fuzzy checkpoint를 만든다. */
/* root PageRef 이동이나 root 내용과 WAL system 상태의 불일치는 UNSUPPORTED이며 log 회수는 하지 않는다. */
/* 게시 전 END까지의 cut 대조와 비변경 redo preview 기반 canonical 검증을 pool 예산 안에서 수행한다. */
/* BEGIN 뒤 실패는 pool 차단이며 callback 안의 직접 WAL append는 게시 없는 BUSY다. */
candidate_wal_status_t candidate_buffer_checkpoint(candidate_buffer_t *pool, uint32_t max_pages,
    candidate_buffer_checkpoint_t *out);
/* 같은 checkpoint로 catalog_root가 비어 있는 anchor에 처음 store descriptor를 게시하며 게시 전 전체 type3 graph를 검증한다. */
/* 이미 다른 descriptor가 게시됐거나 descriptor가 등록된 type3 page가 아니면 효과 전 INVALID다. */
candidate_wal_status_t candidate_buffer_publish_store(candidate_buffer_t *pool, candidate_page_ref_t descriptor,
    uint32_t max_pages, candidate_buffer_checkpoint_t *out);
/* peak은 마지막 최상위 작업 시작부터의 pool 요청량이며 borrowed는 빌린 WAL/control의 별도 상주량이다. */
candidate_buffer_info_t candidate_buffer_info(const candidate_buffer_t *pool);
/* dirty frame을 쓰지 않고 닫으며 빌린 handle은 닫지 않는다. 작업 중 호출과 해제 callback 안의 재진입은 BUSY다. */
candidate_wal_status_t candidate_buffer_close(candidate_buffer_t **pool);

#endif
