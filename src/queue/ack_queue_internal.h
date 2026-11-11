#ifndef ACK_QUEUE_INTERNAL_H
#define ACK_QUEUE_INTERNAL_H

#include <stdatomic.h>

#include "queue/ack_queue.h"
#include "sql/session.h"
#include "sql/result.h"
#include "storage/value.h"

/* 문장 하나의 최대 SQL 길이. 이름은 ACKQ_NAME_MAX 이하라 가장 긴 문장도 들어간다. */
#define ACKQ_SQL_MAX 512u

/* 큐 테이블 이름은 ACKQ_TABLE_PREFIX + 이름, key 테이블 이름은 그 뒤에 ACKQ_KEYS_SUFFIX를 붙인다 */
#define ACKQ_TABLE_PREFIX "streamlite_queue_"
#define ACKQ_KEYS_SUFFIX "_keys"

/* 시계 값의 상한. 이보다 작은 시각에 lease·지연·보존 기간을 더해도 INT64_MAX를 넘지 않는다 */
#define ACKQ_NOW_MAX ((uint64_t)INT64_MAX - ACKQ_MAX_RETENTION_MS)

#ifndef STREAMLITE_ACKQ_CONDITIONAL
#define STREAMLITE_ACKQ_CONDITIONAL 0
#endif

struct ackq {
    session_db_t *db;               /* 설치한 데이터베이스. 연산의 세션은 이 데이터베이스의 것이어야 한다 */
    char name[ACKQ_NAME_MAX + 1];
    char table[sizeof(ACKQ_TABLE_PREFIX) + ACKQ_NAME_MAX];                        /* 접두어 + 이름 + NUL */
    char keys[sizeof(ACKQ_TABLE_PREFIX) + ACKQ_NAME_MAX + sizeof(ACKQ_KEYS_SUFFIX) - 1]; /* table + 접미어 */
    uint32_t max_attempts;
    size_t max_payload;
    uint64_t retention_ms;           /* 0이면 key를 남기지 않는다 */
    uint32_t begin_timeout_ms;
    uint64_t (*now_ms)(void *ctx);
    void *clock_ctx;
    bool conditional_enabled;
    bool due_column;                 /* due 열과 색인이 있는 형식(이 판이 만든 큐). 아니면 이전 형식 */
    bool lease_index;                /* 이전 형식에 lease_until 색인이 있다(열 때 만들었거나 이미 있었다) */
    _Atomic bool last_claim_empty;   /* 이 핸들의 마지막 claim이 빈 결과였다. 다음 claim은 먼저 읽기로 확인한다 */
    _Atomic uint64_t clock_floor;    /* 이 핸들이 읽은 가장 늦은 시각. ackq_now만 바꾼다 */
};

/*
 * due 열(이 판이 만드는 큐). claim·recover·purge가 쓰는 정수 색인 열이며 상태마다 겹치지 않는 구간에 둔다.
 *   READY: ready_at, 시도가 남은 LEASED: lease_until (0 이상, claim 가능해지는 시각)
 *   마지막 시도의 LEASED: ACKQ_DUE_LAST + id, ACKED: ACKQ_DUE_ACKED + id, FAILED: ACKQ_DUE_FAILED + id
 * id는 ACKQ_DUE_BAND보다 작아야 한다(넘으면 enqueue가 ACKQ_LIMIT).
 */
#define ACKQ_DUE_BAND (INT64_C(1) << 60)
#define ACKQ_DUE_ACKED (-(INT64_C(1) << 62))
#define ACKQ_DUE_FAILED (ACKQ_DUE_ACKED + ACKQ_DUE_BAND)
#define ACKQ_DUE_LAST (-(INT64_C(1) << 61))

/* 한 연산이 문장을 실행하는 세션. 세션에 열려 있던 트랜잭션(호출자의 것)이면 own이 false이고 commit하지 않는다. */
typedef struct {
    session_t *session;
    bool own;
    bool dirty;   /* 쓰기 문장을 실행했다. 자기 트랜잭션은 dirty일 때만 commit한다 */
} ackq_scope_t;

typedef struct {
    uint64_t id;
    int64_t now;
    int64_t lease_until;
} ackq_claim_request_t;

typedef struct {
    uint64_t id;
    uint64_t fence;
    ackq_state_t state;
    const char *note;
} ackq_finish_request_t;

ackq_status_t ackq_conditional_claim(ackq_t *queue, ackq_scope_t *scope, const ackq_claim_request_t *request,
                                      ackq_claim_t *item, bool *matched);
ackq_status_t ackq_conditional_finish(ackq_t *queue, ackq_scope_t *scope, const ackq_finish_request_t *request,
                                       bool *matched);

/*
 * 지금 시각(밀리초). 시계가 준 값이 ACKQ_NOW_MAX를 넘으면(음수가 uint64로 감긴 값 포함) 읽지 못한 것으로 보고, 이 핸들이
 * 읽은 가장 늦은 시각보다 과거이면 그 시각을 돌려준다. 그래서 돌려주는 값은 핸들 안에서 줄지 않고 INT64 범위 안이다.
 */
uint64_t ackq_now(ackq_t *queue);

/*
 * 세션에 트랜잭션이 열려 있으면 그 안에서, 없으면 write일 때 세션에 자기 트랜잭션을 연다. write가 false이고 열린
 * 트랜잭션이 없으면 트랜잭션 없이(문장마다 자동 commit) 읽는다.
 */
ackq_status_t ackq_scope_open(ackq_t *queue, session_t *session, bool write, ackq_scope_t *scope);
/* result가 ACKQ_OK이고 dirty이면 commit해 그 결과를 돌려준다. 아니면 rollback하고 result를 그대로 돌려준다. */
ackq_status_t ackq_scope_close(ackq_scope_t *scope, ackq_status_t result);

/*
 * 문장을 실행한다. 성공(ROWS·COMMAND)이면 *out에 결과를 주고 ACKQ_OK다. 오류 결과이면 결과를 해제하고 매핑한 상태를
 * 돌려주며 *sql_class에 RESULT 오류 분류를 준다(NULL 가능).
 */
ackq_status_t ackq_run(ackq_t *queue, ackq_scope_t *scope, bool write, const char *sql, const value_view_t *params,
                       size_t param_count, result_t **out, result_error_class_t *sql_class);

/* 결과의 행을 다 읽었는지(DONE) 확인한다. 중간 실패는 매핑한 상태로 돌려준다. */
ackq_status_t ackq_finish_rows(result_next_t last, result_t *result);
/* COMMAND 결과의 영향 행 수가 expected와 같은지 확인한다. 다르면 ACKQ_INTERNAL */
ackq_status_t ackq_expect_affected(const result_t *result, uint64_t expected);

value_view_t ackq_int(int64_t value);
value_view_t ackq_text(const char *text);
value_view_t ackq_blob(const uint8_t *bytes, size_t size);
value_view_t ackq_null(void);
bool ackq_to_i64(uint64_t value, int64_t *out);

#endif
