#ifndef STORAGE_CANDIDATE_ALLOCATION_H
#define STORAGE_CANDIDATE_ALLOCATION_H

#include "storage/candidate_page.h"

#define CANDIDATE_ALLOCATION_ENTRIES 65536u
#define CANDIDATE_ALLOCATION_NODE_ENTRIES 202u
#define CANDIDATE_ALLOCATION_NODES 325u
#define CANDIDATE_ALLOCATION_BATCH 64u

typedef enum { CANDIDATE_ALLOCATION_LIVE = 1, CANDIDATE_ALLOCATION_FREE = 2 } candidate_allocation_state_t;
typedef struct {
    candidate_page_ref_t ref;
    candidate_allocation_state_t state;
    uint64_t release_horizon, transition_lsn;
} candidate_allocation_entry_t;
typedef struct {
    uint64_t owner_epoch, page_highwater, gc_horizon;
    uint32_t count;
    candidate_page_ref_t nodes[CANDIDATE_ALLOCATION_NODES];
} candidate_allocation_root_t;
typedef struct {
    uint32_t ordinal, count;
    candidate_allocation_entry_t entries[CANDIDATE_ALLOCATION_NODE_ENTRIES];
} candidate_allocation_node_t;

/* type5 공통 snapshot 형식이다. 실패 시 출력과 입력을 변경하지 않는다. */
candidate_wal_status_t candidate_allocation_encode_root(const candidate_page_location_t *location,
    const candidate_allocation_root_t *root, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_allocation_encode_node(const candidate_page_location_t *location,
    const candidate_allocation_node_t *node, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_allocation_decode_root(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_allocation_root_t *out);
candidate_wal_status_t candidate_allocation_decode_node(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_allocation_node_t *out);

typedef struct candidate_allocation candidate_allocation_t;
typedef enum { CANDIDATE_ALLOCATION_MATCH, CANDIDATE_ALLOCATION_NOT_FOUND,
    CANDIDATE_ALLOCATION_INVALID } candidate_allocation_lookup_t;
typedef struct {
    uint8_t uuid[16];
    candidate_page_ref_t root;
    candidate_page_loader_t load;
    void *context;
    const candidate_wal_memory_t *memory;
} candidate_allocation_load_t;
typedef struct {
    uint64_t owner_epoch, page_highwater, gc_horizon, lsn;
    uint32_t count;
    size_t resident_bytes;
} candidate_allocation_info_t;
typedef struct {
    uint64_t page_id, previous_generation;
    candidate_allocation_state_t previous_state; /* 새 ID의 이전 세대와 상태는 0이다. */
    candidate_allocation_state_t next_state;
    uint64_t release_horizon;
} candidate_allocation_change_t;
typedef struct {
    uint64_t lsn, page_highwater, gc_horizon;
    uint32_t count;
    const candidate_allocation_change_t *changes;
} candidate_allocation_batch_t;

/* 안정된 view는 caller 책임이다. 전체 scan 성공 뒤에만 *out을 반환하며 실패 시 NULL이다. */
candidate_wal_status_t candidate_allocation_load(const candidate_allocation_load_t *request,
    candidate_allocation_t **out);
candidate_allocation_info_t candidate_allocation_info(const candidate_allocation_t *allocation);
/* NOT_FOUND는 이 snapshot의 lifetime 표에 없다는 뜻이며 미적용·안전한 발급의 증거가 아니다. */
candidate_allocation_lookup_t candidate_allocation_find(const candidate_allocation_t *allocation,
    uint64_t page_id, candidate_allocation_entry_t *out);
candidate_allocation_lookup_t candidate_allocation_at(const candidate_allocation_t *allocation,
    uint32_t ordinal, candidate_allocation_entry_t *out);
/* 단일 caller의 메모리 전이만 검사한다. WAL group·내구성·참조 해제·안전 horizon은 증명하지 않는다. */
/* 실패 시 전체 상태 불변이다. 원 snapshot root/node의 FREE는 새 anchor를 load하기 전까지 거절한다. */
candidate_wal_status_t candidate_allocation_apply(candidate_allocation_t *allocation,
    const candidate_allocation_batch_t *batch);
void candidate_allocation_close(candidate_allocation_t **allocation);

#endif
