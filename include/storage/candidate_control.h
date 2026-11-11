#ifndef STORAGE_CANDIDATE_CONTROL_H
#define STORAGE_CANDIDATE_CONTROL_H

#include "storage/candidate_page.h"

#define CANDIDATE_CONTROL_BYTES 8192u
#define CANDIDATE_CONTROL_FILE_BYTES (2u * CANDIDATE_CONTROL_BYTES)
#define CANDIDATE_CONTROL_WAL 1u
#define CANDIDATE_CONTROL_COW 2u

typedef struct {
    uint8_t uuid[16];
    uint32_t backend, slot;
    uint64_t generation, owner_epoch, checkpoint_id;
    uint64_t checkpoint_begin, checkpoint_end, redo_begin;
    uint64_t transaction_highwater, page_highwater, request_epoch, retention_floor, visible_csn;
    candidate_page_ref_t outcome_root, allocation_root, catalog_root;
    uint64_t gc_horizon;
} candidate_control_state_t;

/* codec은 slot bytes만 검사하며 의존 checkpoint·WAL·data를 검증하지 않는다. 실패 시 out은 그대로다. */
candidate_wal_status_t candidate_control_encode(const candidate_control_state_t *state, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_control_decode(const uint8_t uuid[16], const uint8_t *bytes, size_t size,
    candidate_control_state_t *out);

typedef candidate_wal_status_t (*candidate_control_verify_t)(void *, const candidate_control_state_t *);
typedef struct {
    uint8_t uuid[16];
    uint32_t backend;
    const candidate_wal_io_t *io;
    const candidate_wal_memory_t *memory;
    candidate_control_verify_t verify;
    void *context;
} candidate_control_options_t;
/* callback 전에 options 값을 복사하며 allocator/verifier context는 닫힐 때까지 caller가 유지한다. */
typedef struct candidate_control candidate_control_t;
typedef struct {
    candidate_control_state_t slots[2];
    unsigned valid_mask, verified_mask;
    int selected;
    uint64_t redo_floor;
    size_t resident_bytes;
    bool is_poisoned;
} candidate_control_info_t;

/* 새16384B 파일만 만들며 초기 anchor는 없다. 실패 파일도 삭제하거나 덮지 않는다. */
candidate_wal_status_t candidate_control_create(const char *path, const candidate_control_options_t *options,
    candidate_control_t **out);
/* 높은 세대부터 필수 verifier로 검사한다. CORRUPT만 이전 anchor로 fallback하며 IO 등은 그대로 차단한다. */
/* verifier는 안정된 의존성 view에서 실제 checkpoint/WAL/data를 검사해야 한다. 정상 DB 활성화는 제공하지 않는다. */
candidate_wal_status_t candidate_control_open(const char *path, const candidate_control_options_t *options,
    candidate_control_t **out);
/* 단일 호출자 계약. generation/slot은 부여하며 검증된 기존 anchor 반대편에 write·fsync한다. */
/* verifier 성공은 caller 책임이다. 쓰기 시도 뒤 실패는 poison/불확정이며 NOT_APPLIED로 해석하지 않는다. */
candidate_wal_status_t candidate_control_publish(candidate_control_t *control, const candidate_control_state_t *next);
candidate_control_info_t candidate_control_info(const candidate_control_t *control);
/* force 없이 닫으며 fork 자식도 명시적으로 unlock하지 않는다. */
candidate_wal_status_t candidate_control_close(candidate_control_t **control);

#endif
