/*
 * ack_queue_sql.c — 큐 연산이 세션으로 문장을 실행하고 세션의 트랜잭션을 여닫는 공통 부분.
 */
#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <time.h>

#include "ack_queue_internal.h"

static uint64_t wall_clock_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec < 0) return UINT64_MAX;   /* 1970년 전의 시계는 범위 밖이다 */
    if ((uint64_t)ts.tv_sec > ACKQ_NOW_MAX / 1000u) return UINT64_MAX;       /* 곱셈이 넘치기 전에 범위 밖으로 표시한다 */
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* 벽시계와 따로 흐르는 단조 시각(밀리초). 절전 중에도 흐르는 시계를 고른다(macOS의 CLOCK_MONOTONIC은 절전 중에도 흐른다) */
static uint64_t mono_clock_ms(void)
{
    struct timespec ts;
#ifdef CLOCK_BOOTTIME
    const clockid_t id = CLOCK_BOOTTIME;
#else
    const clockid_t id = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(id, &ts) != 0 || ts.tv_sec < 0) return 0;   /* 읽지 못하면 경과 0으로 본다 */
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* at_mono에 at이던 시각이 mono에는 얼마인지. ACKQ_NOW_MAX에서 멈춘다 */
static uint64_t advance(uint64_t at, uint64_t at_mono, uint64_t mono)
{
    uint64_t elapsed = mono > at_mono ? mono - at_mono : 0;
    return elapsed > ACKQ_NOW_MAX - at ? ACKQ_NOW_MAX : at + elapsed;
}

/* clock_lock을 쥔 채 유효한 벽시계 값 raw(단조 시각 mono에 읽음)로 하한을 고친다 */
static void observe_clock(ackq_t *queue, uint64_t raw, uint64_t mono)
{
    if (!queue->clock_valid) {
        queue->clock_valid = true;
        queue->clock_floor = raw;
    } else {
        uint64_t expected = advance(queue->clock_seen, queue->clock_seen_mono, mono);
        if (expected < queue->clock_floor) expected = queue->clock_floor;
        if (raw > expected && raw - expected > ACKQ_CLOCK_JUMP_MS) {
            /* 앞으로 뛰었다. 실제 경과(절전 등)와 구분하지 못하므로 받아들이되 뛰기 직전의 예상 시각을 기억한다 */
            if (!queue->clock_jumped) {
                queue->clock_jumped = true;
                queue->clock_prior = expected;
                queue->clock_prior_mono = mono;
            }
        } else if (queue->clock_jumped && raw < queue->clock_floor && queue->clock_floor - raw > ACKQ_CLOCK_JUMP_MS) {
            /* 뛴 시계가 돌아왔다. 하한을 뛰기 직전의 예상 시각 + 단조 경과까지만 남기고 뛴 값에 머물지 않는다 */
            uint64_t prior = advance(queue->clock_prior, queue->clock_prior_mono, mono);
            if (prior < queue->clock_floor) queue->clock_floor = prior > raw ? prior : raw;
            queue->clock_jumped = false;
        }
        if (raw > queue->clock_floor) queue->clock_floor = raw;
    }
    queue->clock_seen = raw;
    queue->clock_seen_mono = mono;
}

uint64_t ackq_now(ackq_t *queue)
{
    uint64_t raw = queue->now_ms != NULL ? queue->now_ms(queue->clock_ctx) : wall_clock_ms();
    uint64_t mono = mono_clock_ms();
    bool unlocked = false;
    while (!atomic_compare_exchange_weak_explicit(&queue->clock_lock, &unlocked, true, memory_order_acquire,
                                                  memory_order_relaxed)) {
        unlocked = false;
    }
    /* 범위 밖 값을 하한으로 삼으면 되돌릴 수 없으므로 버린다 */
    if (raw <= ACKQ_NOW_MAX) observe_clock(queue, raw, mono);
    uint64_t now = queue->clock_valid ? queue->clock_floor : ACKQ_NOW_UNAVAILABLE;
    atomic_store_explicit(&queue->clock_lock, false, memory_order_release);
    return now;
}

value_view_t ackq_int(int64_t value)
{
    return (value_view_t){VALUE_INTEGER, value, 0.0, NULL, 0};
}

value_view_t ackq_text(const char *text)
{
    size_t size = strlen(text);
    return (value_view_t){VALUE_TEXT, 0, 0.0, size > 0 ? (const uint8_t *)text : NULL, size};
}

value_view_t ackq_null(void)
{
    return (value_view_t){VALUE_NULL, 0, 0.0, NULL, 0};
}

value_view_t ackq_blob(const uint8_t *bytes, size_t size)
{
    return (value_view_t){VALUE_BLOB, 0, 0.0, size > 0 ? bytes : NULL, size};
}

bool ackq_to_i64(uint64_t value, int64_t *out)
{
    if (value > (uint64_t)INT64_MAX) return false;
    *out = (int64_t)value;
    return true;
}

static ackq_status_t map_error_class(result_error_class_t error_class)
{
    switch (error_class) {
        case RESULT_E_NOMEM: return ACKQ_NOMEM;
        case RESULT_E_LIMIT: return ACKQ_LIMIT;
        case RESULT_E_IO: return ACKQ_IO;
        case RESULT_E_BUSY: return ACKQ_BUSY;
        case RESULT_E_CANCELLED: return ACKQ_CANCELLED;
        default: return ACKQ_INTERNAL;
    }
}

static ackq_status_t map_txn_status(txn_status_t status)
{
    switch (status) {
        case TXN_OK: return ACKQ_OK;
        case TXN_BUSY:
        case TXN_TIMEOUT: return ACKQ_BUSY;
        case TXN_NOMEM: return ACKQ_NOMEM;
        case TXN_LIMIT: return ACKQ_LIMIT;
        case TXN_POISONED: return ACKQ_POISONED;
        case TXN_UNSUPPORTED: return ACKQ_UNSUPPORTED;
        case TXN_IO: return ACKQ_IO;
        case TXN_COMMIT_FAILED: return ACKQ_NOT_APPLIED;
        case TXN_COMMIT_UNCERTAIN: return ACKQ_UNCERTAIN;
        default: return ACKQ_INTERNAL;
    }
}

ackq_status_t ackq_scope_open(ackq_t *queue, session_t *session, bool write, ackq_scope_t *scope)
{
    *scope = (ackq_scope_t){session, false, false};
    /* 다른 데이터베이스의 세션으로 이 큐의 SQL을 실행하지 않는다. 어떤 문장도 실행하기 전에 거절한다. */
    if (session == NULL || session_database(session) != queue->db) return ACKQ_INVALID;
    if (session_txn_active(session)) return session_txn_failed(session) ? ACKQ_TXN_FAILED : ACKQ_OK;
    if (database_is_poisoned(session_db_handle(queue->db))) return ACKQ_POISONED;
    if (!write) return ACKQ_OK;
    session_txn_options_t options = {TXN_GUARANTEE_DURABLE, queue->begin_timeout_ms};
    txn_status_t status = session_txn_begin(session, &options);
    if (status != TXN_OK) return map_txn_status(status);
    scope->own = true;
    return ACKQ_OK;
}

ackq_status_t ackq_scope_close(ackq_scope_t *scope, ackq_status_t result)
{
    if (!scope->own) return result;
    ackq_status_t final = result;
    if (result == ACKQ_OK && scope->dirty) {
        txn_status_t status = session_txn_commit(scope->session, NULL);
        final = map_txn_status(status);   /* TXN_OK면 승인된 commit이므로 그 결과를 그대로 돌려준다 */
        /* 실패 상태라 commit하지 못한 트랜잭션은 세션에 남기지 않는다 */
        if (session_txn_active(scope->session)) session_txn_rollback(scope->session);
    } else {
        session_txn_rollback(scope->session);
    }
    scope->own = false;
    return final;
}

ackq_status_t ackq_run(ackq_t *queue, ackq_scope_t *scope, bool write, const char *sql, const value_view_t *params,
                       size_t param_count, result_t **out, result_error_class_t *sql_class)
{
    lexer_binding_t binding = {params, param_count};
    const lexer_binding_t *bound = param_count > 0 ? &binding : NULL;
    result_t *result = NULL;

    *out = NULL;
    if (sql_class != NULL) *sql_class = RESULT_E_NONE;
    if (write) scope->dirty = true;
    database_exec_status_t status = session_exec(scope->session, NULL, (const uint8_t *)sql, strlen(sql), bound, &result);
    if (status == DATABASE_EXEC_NOMEM) return ACKQ_NOMEM;
    if (status != DATABASE_EXEC_OK || result == NULL) return ACKQ_INVALID;
    if (result_kind(result) == RESULT_KIND_ERROR) {
        const result_outcome_t outcome = result_outcome(result);
        result_error_class_t error_class = outcome.error != NULL ? outcome.error->error_class : RESULT_E_INTERNAL;
        if (sql_class != NULL) *sql_class = error_class;
        result_release(&result);
        if (database_is_poisoned(session_db_handle(queue->db))) return ACKQ_POISONED;
        return map_error_class(error_class);
    }
    *out = result;
    return ACKQ_OK;
}

ackq_status_t ackq_finish_rows(result_next_t last, result_t *result)
{
    if (last == RESULT_NEXT_DONE) return ACKQ_OK;
    const result_outcome_t outcome = result_outcome(result);
    return map_error_class(outcome.error != NULL ? outcome.error->error_class : RESULT_E_INTERNAL);
}

ackq_status_t ackq_expect_affected(const result_t *result, uint64_t expected)
{
    const result_command_t *command = result_command(result);
    return command != NULL && command->affected_rows == expected ? ACKQ_OK : ACKQ_INTERNAL;
}

const char *ackq_status_name(ackq_status_t status)
{
    static const char *const names[] = {"OK", "INVALID", "NOMEM", "LIMIT", "BUSY", "NOT_INSTALLED", "NOT_FOUND",
                                        "STALE", "CONFLICT", "UNSUPPORTED", "IO", "NOT_APPLIED", "UNCERTAIN",
                                        "POISONED", "TXN_FAILED", "INTERNAL", "CANCELLED"};
    return (size_t)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}
