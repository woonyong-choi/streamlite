#ifndef STORAGE_CANDIDATE_CATALOG_H
#define STORAGE_CANDIDATE_CATALOG_H

#include "storage/candidate_page.h"

#define CANDIDATE_CATALOG_ENTRIES 4096u
#define CANDIDATE_CATALOG_NODE_ENTRIES 46u
#define CANDIDATE_CATALOG_PAGES 90u
#define CANDIDATE_CATALOG_STORAGE_BYTES (64u * 1024u * 1024u)

typedef enum {
    CANDIDATE_OUTCOME_ADMITTED = 1,
    CANDIDATE_OUTCOME_COMMITTED,
    CANDIDATE_OUTCOME_ABORTED
} candidate_outcome_state_t;
typedef struct {
    uint64_t epoch;
    uint8_t nonce[16], operation_digest[32];
    uint64_t transaction_id;
    candidate_outcome_state_t state;
    uint64_t csn, terminal_end_lsn;
    candidate_version_ref_t result;
    uint64_t result_bytes;
    uint8_t result_digest[32];
    uint32_t result_chunks;
    uint64_t declared_result_limit, admitting_owner_epoch;
} candidate_outcome_t;
typedef struct {
    uint64_t owner_epoch, current_epoch, retention_floor;
    uint64_t transaction_highwater, page_highwater, visible_csn, retained_bytes;
    uint32_t count;
    candidate_page_ref_t pages[CANDIDATE_CATALOG_PAGES];
} candidate_catalog_root_t;
typedef struct {
    uint32_t ordinal, count;
    candidate_outcome_t entries[CANDIDATE_CATALOG_NODE_ENTRIES];
} candidate_catalog_node_t;

/* type4 metadata codec이며 성공·실패 모두 입력 page를 바꾸지 않는다. encoder 실패 시 out도 그대로다. */
candidate_wal_status_t candidate_catalog_encode_root(const candidate_page_location_t *location,
    const candidate_catalog_root_t *root, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_catalog_encode_node(const candidate_page_location_t *location,
    const candidate_catalog_node_t *node, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_catalog_decode_root(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_catalog_root_t *out);
candidate_wal_status_t candidate_catalog_decode_node(const uint8_t uuid[16], const uint8_t *bytes,
    size_t size, candidate_catalog_node_t *out);

typedef struct candidate_catalog candidate_catalog_t;
typedef struct {
    uint8_t uuid[16];
    candidate_page_ref_t root;
    candidate_page_loader_t load;
    void *context;
    const candidate_wal_memory_t *memory;
} candidate_catalog_load_t;
typedef enum {
    CANDIDATE_CATALOG_MATCH,
    CANDIDATE_CATALOG_NOT_FOUND,
    CANDIDATE_CATALOG_OUTSIDE_RETENTION,
    CANDIDATE_CATALOG_CONTENT_MISMATCH,
    CANDIDATE_CATALOG_INVALID_ID,
    CANDIDATE_CATALOG_UNAVAILABLE
} candidate_catalog_lookup_t;
typedef struct {
    uint8_t uuid[16];
    uint64_t epoch;
    uint8_t nonce[16], operation_digest[32];
} candidate_catalog_query_t;

/* 전체 metadata scan 뒤 반환하며 실패 시 *out은 NULL이다. 본문·WAL·가시성/복구 권한은 검증하지 않는다. */
candidate_wal_status_t candidate_catalog_load(const candidate_catalog_load_t *request, candidate_catalog_t **out);
/* MATCH는 원시 기록이며 APPLIED·NOT_APPLIED·현재 owner의 IN_PROGRESS 판정이 아니다. 반환 값은 catalog를 빌린다. */
candidate_catalog_lookup_t candidate_catalog_lookup(const candidate_catalog_t *catalog,
    const candidate_catalog_query_t *request, const candidate_outcome_t **out);
const candidate_catalog_root_t *candidate_catalog_root(const candidate_catalog_t *catalog);
/* 완전한 owning snapshot의 ordinal이며 범위 밖은 NULL이다; 포인터는 catalog를 빌린다. */
const candidate_outcome_t *candidate_catalog_at(const candidate_catalog_t *catalog, uint32_t ordinal);
size_t candidate_catalog_resident_bytes(const candidate_catalog_t *catalog);
void candidate_catalog_close(candidate_catalog_t **catalog);

#endif
