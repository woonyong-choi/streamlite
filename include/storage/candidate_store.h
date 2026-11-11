#ifndef STORAGE_CANDIDATE_STORE_H
#define STORAGE_CANDIDATE_STORE_H

#include "storage/candidate_page.h"

#define CANDIDATE_STORE_LEAF_ENTRIES 169u
#define CANDIDATE_STORE_BRANCH_CHILDREN 253u
#define CANDIDATE_STORE_HEIGHT 8u

typedef enum {
    CANDIDATE_STORE_DESCRIPTOR = 1,
    CANDIDATE_STORE_LEAF,
    CANDIDATE_STORE_BRANCH
} candidate_store_kind_t;
typedef struct { uint64_t table_id, row_id; } candidate_store_key_t;
typedef struct {
    candidate_page_ref_t root, append;
    uint32_t height;
    uint64_t result_table_id;
} candidate_store_descriptor_t;
/* head가 비면 head_transaction도 0인 구조 자리이며 보이는 행이 아니다. */
typedef struct {
    candidate_store_key_t key;
    candidate_version_ref_t head;
    uint64_t head_transaction;
} candidate_store_entry_t;
/* 첫 child의 low는 (0,0)이고 이후 low는 그 child가 담는 key의 하한 separator다. */
typedef struct {
    candidate_store_key_t low;
    candidate_page_ref_t child;
} candidate_store_child_t;
typedef struct {
    candidate_store_kind_t kind;
    uint32_t count, level;
    union {
        candidate_store_descriptor_t descriptor;
        candidate_store_entry_t entries[CANDIDATE_STORE_LEAF_ENTRIES];
        candidate_store_child_t children[CANDIDATE_STORE_BRANCH_CHILDREN];
    };
} candidate_store_node_t;

typedef struct {
    uint8_t uuid[16];
    candidate_page_ref_t descriptor;
    candidate_page_loader_t load;
    void *context;
    const candidate_wal_memory_t *memory;
} candidate_store_check_t;
typedef struct {
    candidate_store_descriptor_t descriptor;
    uint32_t pages, leaves, branches, entries, heads;
    uint64_t versions;
} candidate_store_info_t;

/* type3 page 전체 본문 codec이며 실패 시 out은 그대로다. 구조 간 참조·가시성은 판정하지 않는다. */
candidate_wal_status_t candidate_store_encode(const candidate_page_location_t *location,
    const candidate_store_node_t *node, uint8_t *out, size_t capacity);
candidate_wal_status_t candidate_store_decode(const uint8_t uuid[16], const uint8_t *bytes, size_t size,
    candidate_store_node_t *out);
int candidate_store_compare(candidate_store_key_t left, candidate_store_key_t right);
/* descriptor에서 닿는 tree 전체의 종류·세대·높이·separator 범위·중복 child·순환과 head version 연쇄의 */
/* table/row 동일성·앞선 version 순서를 loader로 검사한다. 거래 commit 여부와 가시성은 판정하지 않는다. */
candidate_wal_status_t candidate_store_check(const candidate_store_check_t *request, candidate_store_info_t *out);

#endif
