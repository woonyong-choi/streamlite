#ifndef STORAGE_CANDIDATE_MVCC_H
#define STORAGE_CANDIDATE_MVCC_H

#include "storage/candidate_recovery.h"

#define CANDIDATE_MVCC_ROWS 64u
#define CANDIDATE_MVCC_TRANSACTIONS 64u
#define CANDIDATE_MVCC_READERS 64u
#define CANDIDATE_MVCC_WRITES 4u
#define CANDIDATE_MVCC_PAYLOAD_BYTES 256u
#define CANDIDATE_MVCC_RESPONSE_BYTES 24u
#define CANDIDATE_MVCC_HISTORY CANDIDATE_CATALOG_ENTRIES

typedef enum {
    CANDIDATE_MVCC_ACTIVE = 1,
    CANDIDATE_MVCC_COMMITTED,
    CANDIDATE_MVCC_ABORTED,
    CANDIDATE_MVCC_UNKNOWN
} candidate_mvcc_state_t;

typedef struct { uint64_t table_id, row_id; } candidate_mvcc_key_t;
typedef struct {
    candidate_mvcc_key_t key;
    candidate_page_ref_t page;
} candidate_mvcc_row_t;
typedef struct {
    candidate_recovery_options_t recovery;
    const candidate_mvcc_row_t *rows;
    uint32_t row_count;
    candidate_page_ref_t result_page;
    uint64_t result_table_id, wal_budget_bytes;
} candidate_mvcc_options_t;

typedef struct { uint64_t owner_id, generation; uint32_t slot; } candidate_mvcc_txn_t;
typedef struct { uint64_t owner_id, generation; uint32_t slot; } candidate_mvcc_reader_t;
typedef struct {
    candidate_mvcc_key_t key;
    const uint8_t *bytes;
    uint32_t size;
} candidate_mvcc_write_t;
typedef struct {
    uint8_t nonce[16];
    const candidate_mvcc_write_t *writes;
    uint32_t count;
    const candidate_mvcc_reader_t *snapshot;
} candidate_mvcc_request_t;

typedef struct {
    candidate_mvcc_state_t state;
    uint64_t transaction_id, csn, terminal_end_lsn, affected_rows;
    candidate_version_ref_t result;
    uint32_t size;
    uint8_t bytes[CANDIDATE_MVCC_RESPONSE_BYTES];
} candidate_mvcc_response_t;
typedef struct {
    candidate_catalog_lookup_t lookup;
    candidate_mvcc_response_t response;
} candidate_mvcc_answer_t;
typedef struct {
    bool is_found;
    uint64_t transaction_id, csn;
    candidate_version_ref_t ref;
    uint32_t size;
    uint8_t bytes[CANDIDATE_MVCC_PAYLOAD_BYTES];
} candidate_mvcc_value_t;
typedef struct {
    candidate_mvcc_state_t state;
    uint64_t transaction_id, snapshot_csn, first_lsn, last_lsn, records, group_id, group_end_lsn;
    uint32_t chain_crc, write_count;
    candidate_version_ref_t versions[CANDIDATE_MVCC_WRITES], result;
    bool is_applied;
} candidate_mvcc_txn_info_t;
typedef struct {
    uint64_t owner_id, owner_epoch, request_epoch, visible_csn, last_csn;
    uint64_t next_transaction_id, transaction_end, wal_credit_bytes, gate_entries;
    uint32_t active, readers, outcomes;
    candidate_wal_status_t fence;
} candidate_mvcc_info_t;

typedef struct candidate_mvcc candidate_mvcc_t;

/* 기존 후보 파일에 candidate_recovery_recover의 전체 복구·게시를 실행하고 그 배타 handle을 닫을 때까지 보유한다. */
/* 선언 행·결과 page는 이미 LIVE인 type1 page여야 하며 실패 시 *out은 NULL이고 복구 게시 효과는 남을 수 있다. */
/* SQL/ACK·page 할당·split·GC·runtime checkpoint를 제공하지 않으며 WAL 전체 보존과 고정 한도에서 LIMIT로 거절한다. */
candidate_wal_status_t candidate_mvcc_open(const candidate_mvcc_options_t *options, candidate_mvcc_t **out);
/* 계획을 복사·정규화하고 ADMIT/BEGIN을 append한다. 같은 nonce는 EXISTS, 진행 중 같은 행은 BUSY, 이후 변경 snapshot은 STALE이다. */
/* 거절은 효과 전이며 append 뒤 실패는 owner fence와 UNKNOWN이다. */
candidate_wal_status_t candidate_mvcc_begin(candidate_mvcc_t *mvcc, const candidate_mvcc_request_t *request,
    candidate_mvcc_txn_t *out);
/* 최신 공유 page에 기존 slot을 모두 보존한 채 자기 version과 결과 version을 한 번만 추가한다. */
candidate_wal_status_t candidate_mvcc_apply(candidate_mvcc_t *mvcc, const candidate_mvcc_txn_t *txn);
/* RESULT_CHUNK·COMMIT을 terminal 순서 CSN으로 append하고 force가 끝난 뒤에만 COMMITTED와 응답 본문을 돌려준다. */
/* force·append 실패는 재시도 없이 UNKNOWN으로 남기고 owner를 fence한다. 끝난 handle은 0으로 지운다. */
candidate_wal_status_t candidate_mvcc_commit(candidate_mvcc_t *mvcc, candidate_mvcc_txn_t *txn,
    candidate_mvcc_response_t *out);
/* 이전 page로 되돌리지 않는 논리 ABORT이며 force 성공 뒤에만 ABORTED다. */
candidate_wal_status_t candidate_mvcc_abort(candidate_mvcc_t *mvcc, candidate_mvcc_txn_t *txn);
/* 거래 snapshot과 자기 쓰기를 읽고 결과 bytes는 out에 복사한다. */
candidate_wal_status_t candidate_mvcc_read(candidate_mvcc_t *mvcc, const candidate_mvcc_txn_t *txn,
    candidate_mvcc_key_t key, candidate_mvcc_value_t *out);
candidate_wal_status_t candidate_mvcc_reader_open(candidate_mvcc_t *mvcc, candidate_mvcc_reader_t *out);
/* 등록 CSN 이하 COMMITTED만 보이며 ACTIVE·ABORTED·UNKNOWN은 숨긴다. */
candidate_wal_status_t candidate_mvcc_reader_read(candidate_mvcc_t *mvcc, const candidate_mvcc_reader_t *reader,
    candidate_mvcc_key_t key, candidate_mvcc_value_t *out);
candidate_wal_status_t candidate_mvcc_reader_close(candidate_mvcc_t *mvcc, candidate_mvcc_reader_t *reader);
/* 같은 nonce·계획의 상태와 실제 저장 본문 사본을 돌려준다. 부재는 NOT_FOUND이며 NOT_APPLIED가 아니다. */
candidate_wal_status_t candidate_mvcc_query(candidate_mvcc_t *mvcc, const candidate_mvcc_request_t *request,
    candidate_mvcc_answer_t *out);
candidate_wal_status_t candidate_mvcc_inspect(candidate_mvcc_t *mvcc, const candidate_mvcc_txn_t *txn,
    candidate_mvcc_txn_info_t *out);
candidate_mvcc_info_t candidate_mvcc_info(candidate_mvcc_t *mvcc);
/* ACTIVE 거래나 열린 reader가 있으면 BUSY다. fence된 owner는 미해결 거래를 복구에 남기고 닫는다. */
/* 닫기와 겹치는 다른 호출을 막는 것은 caller 책임이며 fork 자식의 닫기는 FORKED다. */
candidate_wal_status_t candidate_mvcc_close(candidate_mvcc_t **mvcc);

#endif
