#ifndef STORAGE_CANDIDATE_PAGE_H
#define STORAGE_CANDIDATE_PAGE_H

#include "storage/candidate_wal.h"

#define CANDIDATE_PAGE_BYTES 8192u
#define CANDIDATE_PAGE_OVERFLOW_BYTES 8104u
#define CANDIDATE_PAGE_MAX_SLOTS 101u
#define CANDIDATE_RESULT_MAX_BYTES 1048576u

typedef struct { uint64_t id, generation; } candidate_page_ref_t;
typedef struct {
    candidate_page_ref_t page;
    uint32_t slot, slot_generation;
} candidate_version_ref_t;
typedef struct {
    uint8_t uuid[16];
    candidate_page_ref_t ref;
    uint64_t lsn;
} candidate_page_location_t;
typedef struct {
    candidate_page_location_t location;
    uint16_t type;
    uint32_t body_bytes, slots;
} candidate_page_info_t;
typedef struct {
    uint64_t transaction_id, frozen_csn;
    candidate_version_ref_t previous;
    uint64_t table_id, row_id, payload_bytes;
    uint32_t slot_generation;
    bool is_tombstone;
    const uint8_t *inline_bytes;
    candidate_page_ref_t overflow;
} candidate_page_version_t;

/* encoder 실패 시 out은 그대로다. version의 bytes는 입력에서 빌린다. */
candidate_wal_status_t candidate_page_encode_versions(const candidate_page_location_t *location,
    const candidate_page_version_t *versions, size_t count, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_page_encode_overflow(const candidate_page_location_t *location,
    candidate_page_ref_t next, const uint8_t *bytes, size_t size, uint8_t *out, size_t capacity);
/* 정확한 한 page 전체와 모든 slot을 검사하며 실패 시 out은 그대로다. */
candidate_wal_status_t candidate_page_decode(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_page_info_t *out);
/* 성공한 inline_bytes는 입력 page를 빌리며 MVCC 가시성·commit 여부는 판정하지 않는다. */
candidate_wal_status_t candidate_page_get_version(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_version_ref_t ref, candidate_page_version_t *out);

typedef candidate_wal_status_t (*candidate_page_loader_t)(void *, candidate_page_ref_t,
    uint8_t out[CANDIDATE_PAGE_BYTES]);
typedef struct {
    uint8_t uuid[16];
    candidate_version_ref_t ref;
    uint64_t transaction_id, result_bytes;
    uint8_t digest[32];
    candidate_page_loader_t load;
    void *context;
    const candidate_wal_memory_t *memory;
} candidate_result_read_t;
typedef struct candidate_page_result candidate_page_result_t;

/* loader는 요청한 page 전체를 채우거나 오류를 반환한다. 실패 시 *out은 NULL이다. */
/* 본문·참조·digest만 검사하며 영속 ID 조회·APPLIED·복구 활성화를 제공하지 않는다. */
candidate_wal_status_t candidate_page_read_result(const candidate_result_read_t *request,
    candidate_page_result_t **out);
const uint8_t *candidate_page_result_bytes(const candidate_page_result_t *result);
size_t candidate_page_result_size(const candidate_page_result_t *result);
void candidate_page_result_close(candidate_page_result_t **result);

#endif
