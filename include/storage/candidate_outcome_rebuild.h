#ifndef STORAGE_CANDIDATE_OUTCOME_REBUILD_H
#define STORAGE_CANDIDATE_OUTCOME_REBUILD_H

#include "storage/candidate_outcome_witness.h"

typedef struct candidate_outcome_rebuild candidate_outcome_rebuild_t;
typedef struct {
    candidate_outcome_witness_options_t source;
    uint64_t owner_epoch;
} candidate_outcome_rebuild_options_t;
typedef struct {
    uint64_t owner_epoch, current_epoch, retention_floor, transaction_highwater, page_highwater, visible_csn;
    uint64_t wal_end_lsn, result_bytes, minimum_retained_bytes;
    uint8_t wal_digest[32];
    uint32_t count, nodes;
    size_t resident_bytes;
} candidate_outcome_rebuild_info_t;

/* caller가 안정된 복원 target과 전체 forced WAL을 보유한다. checkpoint가 필수이며 파일은 변경하지 않는다. */
/* 결과는 미게시 재구성 사본이다. 완전한 allocation/물리 이력·APPLIED·복구 활성화를 증명하지 않는다. */
candidate_wal_status_t candidate_outcome_rebuild_load(candidate_wal_t *wal,
    const candidate_outcome_rebuild_options_t *options, candidate_outcome_rebuild_t **out);
candidate_outcome_rebuild_info_t candidate_outcome_rebuild_info(const candidate_outcome_rebuild_t *rebuild);
/* 기존 catalog 형식의 transaction ID 순서로 사본을 빌리며 close 뒤에는 무효다. */
const candidate_outcome_t *candidate_outcome_rebuild_at(const candidate_outcome_rebuild_t *rebuild, uint32_t index);
/* caller가 layout·실제 공간·WAL/data 순서와 pageLSN을 검증해야 한다. encoder 성공은 게시 권한이 아니다. */
candidate_wal_status_t candidate_outcome_rebuild_encode_node(const candidate_outcome_rebuild_t *rebuild,
    uint32_t index, const candidate_page_location_t *location, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_outcome_rebuild_encode_root(const candidate_outcome_rebuild_t *rebuild,
    const candidate_page_location_t *location, const candidate_page_ref_t *nodes, uint32_t count,
    uint64_t retained_bytes, uint8_t *out, size_t capacity);
void candidate_outcome_rebuild_close(candidate_outcome_rebuild_t **rebuild);

#endif
