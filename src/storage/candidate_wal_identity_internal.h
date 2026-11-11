#ifndef STORAGE_CANDIDATE_WAL_IDENTITY_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_IDENTITY_INTERNAL_H

#include "storage/candidate_wal.h"

#define CANDIDATE_IDENTITY_INTERVALS 262144u
#define CANDIDATE_IDENTITY_TOMBSTONES 262144u

typedef struct candidate_wal_identity candidate_wal_identity_t;
typedef struct { uint64_t epoch; uint8_t nonce[16]; } candidate_identity_nonce_t;
typedef struct { uint64_t transactions, commits, aborts, groups, events; } candidate_identity_retired_t;
typedef struct {
    candidate_identity_retired_t retired;
    uint64_t max_started_id, max_owner_epoch;
    uint32_t intervals, tombstones;
    size_t resident_bytes;
} candidate_identity_info_t;
typedef struct {
    uint64_t *started;
    candidate_identity_nonce_t *nonces;
    uint32_t started_count, nonce_count;
    uint64_t retention_floor, max_owner_epoch;
    bool has_floor;
} candidate_identity_update_t;

/* source가 있으면 같은 이력의 사본이며 실패 시 *out은 NULL이다. */
candidate_wal_status_t candidate_identity_create(const candidate_wal_memory_t *memory,
    const candidate_wal_identity_t *source, candidate_wal_identity_t **out);
/* 시작 ID 구간과 floor 이상 nonce를 합치며 update 배열 순서를 바꾸고 실패 시 identity는 그대로다. */
candidate_wal_status_t candidate_identity_record(candidate_wal_identity_t *identity,
    candidate_identity_update_t *update);
void candidate_identity_retire(candidate_wal_identity_t *identity, const candidate_identity_retired_t *retired);
bool candidate_identity_has_started(const candidate_wal_identity_t *identity, uint64_t id);
bool candidate_identity_has_nonce(const candidate_wal_identity_t *identity, uint64_t epoch, const uint8_t nonce[16]);
candidate_identity_info_t candidate_identity_info(const candidate_wal_identity_t *identity);
void candidate_identity_close(candidate_wal_identity_t **identity);

#endif
