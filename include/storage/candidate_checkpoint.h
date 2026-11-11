#ifndef STORAGE_CANDIDATE_CHECKPOINT_H
#define STORAGE_CANDIDATE_CHECKPOINT_H

#include "storage/candidate_control.h"

#define CANDIDATE_CHECKPOINT_DIRTY 8192u
#define CANDIDATE_CHECKPOINT_TRANSACTIONS 4096u
#define CANDIDATE_CHECKPOINT_ENTRIES 128u
#define CANDIDATE_CHECKPOINT_CHUNKS 96u
#define CANDIDATE_CHECKPOINT_PAYLOAD 7200u

typedef struct {
    candidate_page_ref_t ref;
    uint64_t rec_lsn;
} candidate_checkpoint_dirty_t;

typedef enum {
    CANDIDATE_CHECKPOINT_ACTIVE = 1,
    CANDIDATE_CHECKPOINT_COMMIT = 2,
    CANDIDATE_CHECKPOINT_ABORT = 3
} candidate_checkpoint_state_t;

typedef struct {
    uint64_t id, first_lsn, last_lsn, terminal_end_lsn, csn, owner_epoch;
    candidate_checkpoint_state_t state;
} candidate_checkpoint_transaction_t;

typedef struct {
    candidate_control_state_t anchor;
    uint32_t dirty_count, transaction_count;
} candidate_checkpoint_summary_t;

typedef struct {
    uint64_t checkpoint_id;
    uint32_t kind, ordinal, count;
    union {
        candidate_checkpoint_dirty_t dirty[CANDIDATE_CHECKPOINT_ENTRIES];
        candidate_checkpoint_transaction_t transactions[CANDIDATE_CHECKPOINT_ENTRIES];
    } entries;
} candidate_checkpoint_chunk_t;

typedef struct {
    uint64_t checkpoint_id, begin_lsn;
    uint32_t chunks;
    uint8_t digest[32];
} candidate_checkpoint_end_t;

/* 내부 WAL checkpoint 형식이며 기존 DB 형식과 연결하지 않는다. 실패 시 output은 그대로다. */
candidate_wal_status_t candidate_checkpoint_encode_begin(const candidate_checkpoint_summary_t *summary,
    uint8_t *out, size_t capacity, size_t *written);
candidate_wal_status_t candidate_checkpoint_encode_chunk(const candidate_checkpoint_chunk_t *chunk,
    uint8_t *out, size_t capacity, size_t *written);
candidate_wal_status_t candidate_checkpoint_encode_end(const candidate_checkpoint_end_t *end,
    uint8_t *out, size_t capacity, size_t *written);
/* initial previous는0이며 BEGIN/CHUNK payload 순서의 SHA256(previous32B || payload)를 계산한다. */
candidate_wal_status_t candidate_checkpoint_digest(const uint8_t previous[32], const uint8_t *payload,
    size_t size, uint8_t out[32]);

typedef struct candidate_checkpoint candidate_checkpoint_t;
typedef struct {
    candidate_checkpoint_summary_t summary;
    uint32_t active_transactions;
    size_t resident_bytes;
    candidate_wal_info_t transport;
} candidate_checkpoint_info_t;

/* 전체 WAL scan 뒤 anchor와 일치하는 완성 snapshot만 소유한다. 파일·내구 상태를 바꾸지 않는다. */
/* snapshot의 선언 완전성은 data/terminal 이력의 완전성·안전한 회수·DB 활성화 증명이 아니다. */
candidate_wal_status_t candidate_checkpoint_load(candidate_wal_t *wal, const candidate_control_state_t *anchor,
    const candidate_wal_memory_t *memory, candidate_checkpoint_t **out);
candidate_checkpoint_info_t candidate_checkpoint_info(const candidate_checkpoint_t *checkpoint);
/* 반환 값은 checkpoint 수명 동안 빌리며 범위 밖에서는 NULL이다. */
const candidate_checkpoint_dirty_t *candidate_checkpoint_dirty_at(const candidate_checkpoint_t *checkpoint, uint32_t index);
const candidate_checkpoint_transaction_t *candidate_checkpoint_transaction_at(const candidate_checkpoint_t *checkpoint, uint32_t index);
void candidate_checkpoint_close(candidate_checkpoint_t **checkpoint);

#endif
