#ifndef STORAGE_CANDIDATE_REDO_INTERNAL_H
#define STORAGE_CANDIDATE_REDO_INTERNAL_H

#include "storage/candidate_redo.h"

#define CANDIDATE_REDO_BUDGET_BYTES (128u * 1024u * 1024u)

struct candidate_recovery_view;
typedef struct candidate_redo_preview candidate_redo_preview_t;
typedef struct { uint64_t generation, previous_lsn; bool is_image; } candidate_redo_link_t;
/* budget_bytes 0은 기본128MiB이며 retained_bytes는 caller가 함께 살려 둔 다른 view·control 등의 요청량이다. */
typedef struct {
    candidate_redo_target_t target;
    const candidate_wal_memory_t *memory;
    size_t budget_bytes, retained_bytes;
} candidate_redo_seed_t;

candidate_wal_status_t candidate_redo_preview_open(candidate_wal_t *wal,
    const candidate_redo_options_t *options, candidate_redo_preview_t **out);
candidate_wal_status_t candidate_redo_preview_load(void *preview, candidate_page_ref_t ref,
    uint8_t out[CANDIDATE_PAGE_BYTES]);
candidate_redo_info_t candidate_redo_preview_info(const candidate_redo_preview_t *preview);
void candidate_redo_preview_close(candidate_redo_preview_t **preview);
/* 같은 세대는 직전 group 끝을 정확히 이어야 하고 다른 세대는 previous0 IMAGE로 처음이거나 정확히 다음 세대여야 한다. */
candidate_wal_status_t candidate_redo_check_link(uint64_t generation, uint64_t last_lsn, const candidate_redo_link_t *next);
/* view의 물리 증명이 고정한 R..cut만 읽어 LIVE page를 IMAGE와 delta로 재구성하며 target은 읽지도 쓰지도 않는다. */
/* view는 open 동안만 빌리고 WAL·allocator는 preview보다 오래 살아야 하며 reader 등록이나 회수 허가가 아니다; 실패 시 *out은 NULL이다. */
candidate_wal_status_t candidate_redo_seeded_open(candidate_wal_t *wal, const struct candidate_recovery_view *view,
    const candidate_redo_seed_t *request, candidate_redo_preview_t **out);
/* RESOLVED view이고 handle cut이 view cut과 같을 때만 R..cut 검증과 force 뒤 전체8192B가 다른 LIVE page만 쓴다. */
/* 실패는 부분 page 쓰기를 남길 수 있으며 out은0이다; 같은 view로 재실행한다. */
candidate_wal_status_t candidate_redo_seeded_execute(candidate_wal_t *wal, const struct candidate_recovery_view *view,
    const candidate_redo_seed_t *request, candidate_redo_info_t *out);

#endif
