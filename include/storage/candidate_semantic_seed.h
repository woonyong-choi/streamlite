#ifndef STORAGE_CANDIDATE_SEMANTIC_SEED_H
#define STORAGE_CANDIDATE_SEMANTIC_SEED_H

#include "storage/candidate_wal_witness.h"

typedef struct {
    candidate_witness_transaction_t value;
    uint64_t owner_epoch, result_limit, chunk_bytes;
} candidate_semantic_transaction_t;
typedef struct { uint64_t id, transaction_id, end_lsn; } candidate_semantic_group_t;
typedef struct {
    uint64_t transaction_id, owner_epoch, result_limit;
    candidate_version_ref_t result;
    bool has_result;
} candidate_semantic_admission_t;
typedef struct {
    uint8_t uuid[16];
    uint64_t end_lsn, records, highest_owner_epoch;
    unsigned segments;
} candidate_semantic_cut_t;
typedef struct {
    candidate_semantic_cut_t cut;
    uint32_t admissions;
    size_t resident_bytes;
    bool has_system;
} candidate_semantic_seed_info_t;
typedef struct candidate_semantic_seed candidate_semantic_seed_t;

/* 보존 WAL 전체를 기존 reducer·admission 관측기로 검증한 끝 B의 상태를 메모리에 소유한다. */
/* B가 완성 group 경계가 아니면 STALE이다. */
/* options의 UUID가 열린 WAL의 UUID와 다르면 UNSUPPORTED다. */
/* checkpoint anchor 모드와 내구 seed 형식은 제공하지 않는다. */
candidate_wal_status_t candidate_semantic_seed_capture(candidate_wal_t *wal,
    const candidate_witness_options_t *options, candidate_semantic_seed_t **out);
/* seed를 한 번 복사한 뒤 B 이후 기록만 read_at으로 읽어 같은 reducer에 적용하며 B 이전 byte는 읽지 않는다. */
/* B 이후 기록이 없어도 handle 접근 거부와 열린 WAL의 UUID 불일치는 그대로 실패한다. */
/* seed와 WAL prefix의 동일성은 caller 책임이며 UUID·cut·기록 수·owner epoch만 handle 상태와 대조한다. */
/* suffix가 B의 segment 밖에 있으면 UNSUPPORTED다. */
/* 실패 시 *out은 NULL이고 입력 seed는 바뀌지 않는다. */
candidate_wal_status_t candidate_semantic_seed_continue(candidate_wal_t *wal,
    const candidate_witness_options_t *options, const candidate_semantic_seed_t *seed,
    candidate_semantic_seed_t **out);
candidate_semantic_seed_info_t candidate_semantic_seed_info(const candidate_semantic_seed_t *seed);
/* 반환 witness는 seed가 소유하며 seed를 닫기 전까지만 유효하다. */
const candidate_wal_witness_t *candidate_semantic_seed_witness(const candidate_semantic_seed_t *seed);
bool candidate_semantic_seed_transaction_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_transaction_t *out);
bool candidate_semantic_seed_group_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_group_t *out);
bool candidate_semantic_seed_admission_at(const candidate_semantic_seed_t *seed, uint32_t index,
    candidate_semantic_admission_t *out);
void candidate_semantic_seed_close(candidate_semantic_seed_t **seed);

#endif
