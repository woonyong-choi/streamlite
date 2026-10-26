#ifndef STORAGE_CANDIDATE_RECOVERY_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_INTERNAL_H

#include <limits.h>
#include <sys/types.h>

#include "storage/candidate_recovery.h"
#include "storage/candidate_wal_witness.h"

typedef struct candidate_recovery_view candidate_recovery_view_t;
typedef enum {
    CANDIDATE_RECOVERY_VIEW_RESOLVED = 1,
    CANDIDATE_RECOVERY_VIEW_PREVIEW
} candidate_recovery_view_mode_t;
typedef struct {
    candidate_control_state_t anchor;
    candidate_catalog_load_t load;
    candidate_recovery_view_mode_t mode;
    uint64_t owner_epoch;
} candidate_recovery_view_request_t;
typedef struct {
    candidate_control_state_t anchor;
    candidate_recovery_view_mode_t mode;
    uint64_t owner_epoch;
    candidate_wal_info_t cut;
    uint32_t live_pages, free_pages;
    uint64_t versions;
    candidate_outcome_witness_info_t outcomes;
    size_t resident_bytes;
} candidate_recovery_view_info_t;

struct candidate_recovery {
    candidate_wal_memory_t memory;
    candidate_wal_io_t io;
    candidate_wal_t *wal;
    candidate_control_t *control;
    candidate_recovery_view_t *view;
    candidate_wal_witness_t *terminal_history;
    candidate_recovery_info_t info;
    candidate_recovery_publication_t publication;
    candidate_recovery_extent_t extent;
    uint8_t uuid[16];
    char name[NAME_MAX + 1];
    int fd, directory_fd;
    dev_t device;
    ino_t inode;
    pid_t process_id;
    uint64_t owner_epoch;
    bool is_busy, is_poisoned, is_terminal_repair, allow_group_tail, is_ready_verify;
};

typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    const candidate_control_state_t *anchor;
    const candidate_wal_memory_t *memory;
    bool allow_group_tail;
} candidate_terminal_options_t;

candidate_wal_status_t candidate_terminal_prepare(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_terminal_resolve(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_terminal_info_t *out);
candidate_wal_status_t candidate_terminal_repair_wal(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_wal_info_t *out);

candidate_wal_status_t candidate_recovery_validate(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);
candidate_wal_status_t candidate_recovery_validate_preview(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);

/* 직렬·배타·같은 prefix의 caller 전제에서 anchor의 한 의미 관측으로 graph와 outcome을 검증한 view를 소유한다. */
/* request는 사본으로 쓰며 load.root는 anchor.outcome_root여야 하고 PREVIEW는 실제 caller owner_epoch가 필요하다. */
/* loader는 open 동안만 빌리고 view에 남기지 않으며 load.memory의 allocator와 context는 view보다 오래 살아야 한다. */
/* 관측 cut과 callback 뒤 handle cut만 대조하며 prefix byte 불변·장기 reader 등록·회수 허가가 아니다. */
/* 실패 시 *out은 NULL이고 부분 소유물은 모두 해제한다. */
candidate_wal_status_t candidate_recovery_view_open(candidate_wal_t *wal,
    const candidate_recovery_view_request_t *request, candidate_recovery_view_t **out);
candidate_recovery_view_info_t candidate_recovery_view_info(const candidate_recovery_view_t *view);
/* info의 graph 계수와 WAL 끝만 채우며 anchor·redo·resident는 그대로 둔다. */
void candidate_recovery_view_fill(const candidate_recovery_view_t *view, candidate_recovery_info_t *info);
/* RESOLVED view만 조회하며 PREVIEW와 NULL은 UNAVAILABLE이다; 반환 값은 view를 빌린다. */
candidate_catalog_lookup_t candidate_recovery_view_lookup(const candidate_recovery_view_t *view,
    const candidate_catalog_query_t *query, const candidate_outcome_t **out);
void candidate_recovery_view_close(candidate_recovery_view_t **view);

candidate_wal_status_t candidate_recovery_own(const candidate_recovery_options_t *options,
    candidate_control_verify_t verify, candidate_recovery_t **out);
candidate_wal_status_t candidate_recovery_verify(void *recovery, const candidate_control_state_t *anchor);
candidate_wal_status_t candidate_recovery_check(candidate_recovery_t *recovery);
candidate_redo_target_t candidate_recovery_target(candidate_recovery_t *recovery);
candidate_wal_status_t candidate_recovery_load(void *recovery, candidate_page_ref_t ref, uint8_t bytes[CANDIDATE_PAGE_BYTES]);
size_t candidate_recovery_resident_bytes(const candidate_recovery_t *recovery);
candidate_wal_status_t candidate_recovery_page_offset(uint64_t id, uint64_t *offset);

#endif
