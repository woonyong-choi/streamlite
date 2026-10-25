#ifndef STORAGE_CANDIDATE_RECOVERY_H
#define STORAGE_CANDIDATE_RECOVERY_H

#include "storage/candidate_outcome_witness.h"
#include "storage/candidate_redo.h"

typedef struct candidate_recovery candidate_recovery_t;
typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    const char *wal_prefix, *control_path, *data_path;
    const candidate_wal_io_t *io;
    const candidate_wal_memory_t *memory;
} candidate_recovery_options_t;
typedef struct {
    candidate_control_state_t anchor;
    candidate_redo_info_t redo;
    uint32_t live_pages, free_pages;
    uint64_t versions, wal_end_lsn;
    size_t resident_bytes;
} candidate_recovery_info_t;
typedef struct {
    candidate_catalog_lookup_t lookup;
    candidate_outcome_t outcome;
    candidate_page_result_t *body;
} candidate_recovery_answer_t;
typedef struct {
    uint64_t checkpoint_id, before_end_lsn, after_end_lsn, repaired_tail_bytes;
    uint32_t appended_aborts, existing_commits, existing_aborts;
} candidate_recovery_terminal_info_t;
typedef struct {
    candidate_recovery_terminal_info_t terminals;
    uint64_t discarded_group_lsn, discarded_valid_end_lsn, discarded_tail_bytes;
    uint32_t removed_segments;
} candidate_recovery_wal_info_t;
/* maintenance_pages·data_file_bytes는 게시 전 재고 시점이고 allocation_pages는 carrier를 뺀 staging page다. */
/* reserved_pages는 새로 예약한 ID, reused_reserved_pages는 실제 재사용한 이전 예약 ID이며 둘 다 carrier 준비를 포함할 수 있다. */
typedef struct {
    candidate_recovery_wal_info_t repair;
    uint64_t checkpoint_id, page_highwater_before, page_highwater_after;
    uint64_t reserved_pages, reused_reserved_pages, wal_bytes_appended;
    uint64_t protected_pages, protected_bytes, maintenance_pages, data_file_bytes;
    uint32_t outcomes, nodes, new_nodes, reused_nodes, allocation_pages, system_groups, events;
    bool has_staging, has_publication;
} candidate_recovery_publication_t;
/* 게시 뒤 data file 크기, LIVE type5 page, 이번 호출의 새 ALLOC page, carrier 준비 page 전부(root와 필요한 allocation node)와 보호 outcome page 수다. */
/* allocated_pages는 reserved_pages+reused_reserved_pages이고 allocation_pages+carrier_pages와 같다. */
typedef struct {
    uint64_t data_file_bytes, maintenance_pages, allocated_pages, carrier_pages, protected_pages;
} candidate_recovery_extent_t;

/* 기존 후보 파일의 배타 잠금을 소유하고 resolved snapshot만 반환하며 SQL/ACK를 활성화하지 않는다. */
/* stale 표·미완료 거래/그룹은 STALE이고 tail 수리·ABORT/metadata 재작성은 제공하지 않는다. */
/* 실패는 부분 redo를 남길 수 있으나 *out은 NULL이며 같은 WAL로 재실행한다. */
/* data/WAL/control의 기존 생성 내구성과 handle 호출 직렬화는 caller의 계약이다. */
candidate_wal_status_t candidate_recovery_open(const candidate_recovery_options_t *options, candidate_recovery_t **out);
/* answer는 사본이며 body는 caller가 candidate_page_result_close로 해제한다. 기존 answer는 caller가 먼저 해제한다. */
candidate_wal_status_t candidate_recovery_query(candidate_recovery_t *recovery,
    const candidate_catalog_query_t *query, candidate_recovery_answer_t *answer);
candidate_recovery_info_t candidate_recovery_info(const candidate_recovery_t *recovery);
candidate_wal_status_t candidate_recovery_close(candidate_recovery_t **recovery);
/* WAL terminal만 내구 정리하며 data/control 검증·재작성이나 DB 활성화를 제공하지 않는다. */
/* 변경 시 owner는 선택 anchor보다 커야 하며 실패한 부분 append는 재열기로 확인한다; out은 실패 시0이다. */
candidate_wal_status_t candidate_recovery_resolve_terminals(const candidate_recovery_options_t *options,
    candidate_recovery_terminal_info_t *out);
/* 검증한 마지막 미완료 물리 그룹을 제거한 뒤 WAL terminal을 내구 정리한다. */
/* data/control은 보존하며 ready handle은 없다. 실패 receipt는0이지만 제거/ABORT 효과가 남을 수 있다. */
candidate_wal_status_t candidate_recovery_repair_wal(const candidate_recovery_options_t *options,
    candidate_recovery_wal_info_t *out);
/* 기존 후보 파일을 소유해 tail/terminal 수리, redo, 결과 재구성, metadata 게시와 checkpoint·control 게시를 수행한다. */
/* 성공 시에만 검증한 handle을 반환하며 SQL/ACK·writer 활성화는 아니다. 실패 시 *out은 NULL이고 효과와 control 게시는 불확실할 수 있다. */
/* 변경이 필요하면 owner는 선택 anchor보다 커야 하며 같은 owner로 이미 게시된 상태는 쓰기 없이 다시 연다. */
candidate_wal_status_t candidate_recovery_recover(const candidate_recovery_options_t *options, candidate_recovery_t **out);
candidate_recovery_publication_t candidate_recovery_publication(const candidate_recovery_t *recovery);
candidate_recovery_extent_t candidate_recovery_publication_extent(const candidate_recovery_t *recovery);

#endif
