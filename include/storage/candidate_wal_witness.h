#ifndef STORAGE_CANDIDATE_WAL_WITNESS_H
#define STORAGE_CANDIDATE_WAL_WITNESS_H

#include "storage/candidate_wal.h"
#include "storage/candidate_wal_system.h"

#define CANDIDATE_WITNESS_TRANSACTIONS 4096u
#define CANDIDATE_WITNESS_GROUPS 4096u
#define CANDIDATE_WITNESS_PAGES 64u
#define CANDIDATE_WITNESS_RECORDS 128u
#define CANDIDATE_WITNESS_CHUNKS 16u
#define CANDIDATE_WITNESS_PAGE_BYTES 8192u

typedef enum {
    CANDIDATE_WITNESS_UNFINISHED,
    CANDIDATE_WITNESS_COMMIT,
    CANDIDATE_WITNESS_ABORT
} candidate_witness_state_t;

typedef struct {
    uint64_t transaction_id;
    uint64_t first_lsn;
    uint64_t last_lsn;
    uint64_t records;
    uint64_t csn;
    uint64_t terminal_end_lsn;
    uint64_t request_epoch;
    uint8_t request_nonce[16];
    uint8_t operation_digest[32];
    uint8_t result_digest[32];
    uint64_t result_bytes;
    uint32_t result_chunks;
    uint32_t groups;
    uint32_t chain_crc;
    candidate_witness_state_t state;
    bool has_admission;
    bool has_begin;
} candidate_witness_transaction_t;

typedef struct {
    uint64_t records;
    uint64_t highest_csn;
    uint64_t incomplete_group_lsn;
    uint32_t transactions;
    uint32_t commits;
    uint32_t aborts;
    uint32_t unfinished;
    uint32_t complete_groups;
    size_t resident_bytes;
    candidate_wal_info_t transport;
} candidate_witness_info_t;

typedef struct candidate_wal_witness candidate_wal_witness_t;
typedef struct {
    uint8_t uuid[16];
    const candidate_wal_memory_t *memory;
    bool enable_system_groups;
} candidate_witness_options_t;

/* 내부 chain/group checksum이며 operation/result의 SHA-256이나 인증을 대신하지 않는다. */
uint32_t candidate_wal_witness_crc(uint32_t crc, const candidate_wal_record_t *record);
/* 전체 scan 성공 뒤에만 분석 결과를 반환하며 파일·거래 상태를 변경하지 않는다. */
candidate_wal_status_t candidate_wal_witness_analyze(candidate_wal_t *wal,
    const candidate_witness_options_t *options, candidate_wal_witness_t **out);
candidate_witness_info_t candidate_wal_witness_info(const candidate_wal_witness_t *witness);
/* 관측한 prefix의 상태이며 최초 이전 값·checkpoint·안전한 번호 발급/회수를 증명하지 않는다. */
candidate_witness_system_info_t candidate_wal_witness_system_info(const candidate_wal_witness_t *witness);
/* 전체 scan 뒤 완성 group의 기록만 빌린다. 분석기를 닫으면 포인터도 무효다. */
const candidate_witness_system_event_t *candidate_wal_witness_system_at(const candidate_wal_witness_t *witness, uint32_t index);
/* COMMIT은 chain/group witness이며 본문 복구·APPLIED·내구 barrier 성공의 판정이 아니다. */
/* 반환 포인터는 분석기가 소유하며 분석기를 닫기 전까지만 유효하다. */
const candidate_witness_transaction_t *candidate_wal_witness_find(
    const candidate_wal_witness_t *witness, uint64_t transaction_id);
void candidate_wal_witness_close(candidate_wal_witness_t **witness);

#endif
