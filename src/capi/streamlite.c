/*
 * streamlite.c — 다른 언어에서 부르는 평탄한 C 인터페이스(#140). 설명은 include/streamlite.h.
 *
 * 불변 조건: 핸들을 쓰는 모든 호출은 streamlite_db_t의 mutex 안에서 세션을 쓴다. 결과(result_t)는 읽는 동안 database의
 * 상태를 참조하므로 실행부터 인코딩·해제까지 같은 잠금 구간에서 끝낸다. 다른 database의 트랜잭션은 잠그거나 실행하기
 * 전에 거절한다. db가 살아 있는 큐·트랜잭션 핸들을 세어 두고 하나라도 있으면 닫지 않는다.
 * 세션: 트랜잭션 밖의 호출은 연결의 자동 commit 세션을, 트랜잭션 안의 호출은 그 트랜잭션의 세션을 쓴다. 그래서 한
 * 스레드가 트랜잭션을 연 동안 다른 스레드의 자동 commit 문장이 그 트랜잭션에 섞이지 않는다. 지금 실행 중인 세션은
 * cancel_lock 아래 running에 둔다. 세션을 닫기 전에 running을 지우므로 streamlite_cancel이 닫힌 세션을 건드리지 않는다.
 * 핸들 수명(#273): 핸들을 끝내는 호출(close·queue_close·commit·rollback)은 그 핸들을 쓰는 다른 호출과 겹치지 않는다는
 * 것이 헤더의 계약이다. 그래서 잠그기 전에 읽는 것은 핸들이 살아 있는 동안 바뀌지 않는 필드(magic, db, owner)뿐이다.
 * 끝내는 호출은 mutex 안에서 magic을 지운 뒤 해제하고, 모든 진입점은 magic이 맞지 않는 포인터를 따라가기 전에 거절한다.
 */
#define _POSIX_C_SOURCE 200809L

#include "streamlite.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue/ack_queue.h"
#include "sql/result.h"
#include "sql/session.h"
#include "../benchmark.h"

/* 살아 있는 핸들의 표시. 닫으면서 0으로 지운다 */
#define DB_MAGIC 0x42444c53u      /* "SLDB" */
#define QUEUE_MAGIC 0x51444c53u   /* "SLDQ" */
#define TXN_MAGIC 0x54444c53u     /* "SLDT" */

struct streamlite_db {
    uint32_t magic;
    session_pool_t *pool;
    session_db_t *db;
    session_t *autocommit;        /* 트랜잭션 밖의 문장과 큐 연산 */
    pthread_mutex_t mutex;
    pthread_mutex_t cancel_lock;
    session_t *running;           /* cancel_lock 안에서만 읽고 쓴다 */
    size_t live_queues;           /* mutex 안에서만 바꾼다 */
    size_t live_txns;
    pid_t owner;                  /* 연 프로세스. fork한 자식은 mutex를 잡기 전에 거절한다 */
};

_Static_assert(STREAMLITE_INVALID == ACKQ_INVALID && STREAMLITE_NOMEM == ACKQ_NOMEM && STREAMLITE_LIMIT == ACKQ_LIMIT && STREAMLITE_BUSY == ACKQ_BUSY &&
                   STREAMLITE_NOT_INSTALLED == ACKQ_NOT_INSTALLED && STREAMLITE_NOT_FOUND == ACKQ_NOT_FOUND && STREAMLITE_STALE == ACKQ_STALE &&
                   STREAMLITE_CONFLICT == ACKQ_CONFLICT && STREAMLITE_UNSUPPORTED == ACKQ_UNSUPPORTED && STREAMLITE_IO == ACKQ_IO &&
                   STREAMLITE_NOT_APPLIED == ACKQ_NOT_APPLIED && STREAMLITE_UNCERTAIN == ACKQ_UNCERTAIN && STREAMLITE_POISONED == ACKQ_POISONED &&
                   STREAMLITE_TXN_FAILED == ACKQ_TXN_FAILED && STREAMLITE_INTERNAL == ACKQ_INTERNAL && STREAMLITE_CANCELLED == ACKQ_CANCELLED,
               "공개 상태 번호는 큐 모듈의 상태와 같다");

/* 자식은 fork 순간 다른 스레드가 쥔 mutex를 영원히 기다릴 수 있으므로 잠그기 전에 확인한다 */
static bool forked(const streamlite_db_t *db) { return db->owner != getpid(); }

struct streamlite_queue {
    uint32_t magic;
    streamlite_db_t *db;
    ackq_t *queue;
};

struct streamlite_txn {
    uint32_t magic;
    streamlite_db_t *db;
    session_t *session;           /* 이 트랜잭션만 쓰는 세션 */
};

/*
 * 표시가 맞는 핸들인지. 닫힌 핸들은 해제된 메모리라 이 검사도 보장이 아니고(메모리가 다시 쓰이기 전까지만 잡힌다),
 * 닫은 뒤의 사용과 핸들이 아닌 포인터를 잠그거나 따라가기 전에 걸러 내는 방어다.
 */
static bool live_db(const streamlite_db_t *db) { return db != NULL && db->magic == DB_MAGIC; }
static bool live_queue(const streamlite_queue_t *q) { return q != NULL && q->magic == QUEUE_MAGIC && live_db(q->db); }
static bool live_txn(const streamlite_txn_t *txn) { return txn != NULL && txn->magic == TXN_MAGIC && live_db(txn->db); }

static _Atomic size_t g_max_result_bytes = STREAMLITE_DEFAULT_MAX_RESULT_BYTES;

void streamlite_set_max_result_bytes(size_t limit)
{
    /* 버퍼 용량을 두 배로 키울 때 size_t가 넘치지 않도록 절반으로 제한한다 */
    atomic_store(&g_max_result_bytes, limit > SIZE_MAX / 2 ? SIZE_MAX / 2 : limit);
}

size_t streamlite_max_result_bytes(void)
{
    return atomic_load(&g_max_result_bytes);
}

const char *streamlite_version(void) { return STREAMLITE_VERSION; }

const char *streamlite_status_name(int status)
{
    switch (status) {
        case STREAMLITE_EMPTY: return "EMPTY";
        case STREAMLITE_DB_INVALID: return "DB_INVALID";
        case STREAMLITE_DB_IO: return "DB_IO";
        case STREAMLITE_DB_CORRUPT: return "DB_CORRUPT";
        case STREAMLITE_DB_LOCKED: return "DB_LOCKED";
        case STREAMLITE_DB_UNSUPPORTED: return "DB_UNSUPPORTED";
        case STREAMLITE_DB_NOMEM: return "DB_NOMEM";
        case STREAMLITE_DB_NOT_FOUND: return "DB_NOT_FOUND";
        case STREAMLITE_DB_FORKED: return "DB_FORKED";
        default: return ackq_status_name((ackq_status_t)status);
    }
}

void streamlite_free(void *pointer)
{
    free(pointer);
}

static int map_database_status(database_status_t status)
{
    switch (status) {
        case DATABASE_OK: return STREAMLITE_OK;
        case DATABASE_IO: return STREAMLITE_DB_IO;
        case DATABASE_CORRUPT: return STREAMLITE_DB_CORRUPT;
        case DATABASE_LOCKED: return STREAMLITE_DB_LOCKED;
        case DATABASE_UNSUPPORTED: return STREAMLITE_DB_UNSUPPORTED;
        case DATABASE_NOMEM: return STREAMLITE_DB_NOMEM;
        case DATABASE_FORKED: return STREAMLITE_DB_FORKED;
        default: return STREAMLITE_DB_INVALID;
    }
}

static int map_session_status(session_status_t status)
{
    switch (status) {
        case SESSION_OK: return STREAMLITE_OK;
        case SESSION_BUSY: return ACKQ_BUSY;
        case SESSION_NOMEM: return STREAMLITE_DB_NOMEM;
        default: return STREAMLITE_DB_INVALID;
    }
}

/* 연결의 세션 풀은 한도가 없다. 예산이 필요한 호출자는 session.h를 직접 쓴다. */
static const session_limits_t UNLIMITED = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, 0};

static void close_parts(streamlite_db_t *handle)
{
    if (handle->autocommit != NULL) session_close(handle->autocommit);
    if (handle->db != NULL) session_pool_close_database(handle->db, NULL);
    if (handle->pool != NULL) session_pool_close(handle->pool);
}

int streamlite_open(const char *path, int create, streamlite_db_t **out)
{
    if (out == NULL) return STREAMLITE_DB_INVALID;
    *out = NULL;
    if (path == NULL) return STREAMLITE_DB_INVALID;
    if (access(path, F_OK) != 0) {
        if (!create) return STREAMLITE_DB_NOT_FOUND;
        database_t *fresh = NULL;
        database_status_t created = database_create(path, &fresh);
        if (created != DATABASE_OK) return map_database_status(created);
        if (database_close(fresh) != DATABASE_OK) return STREAMLITE_DB_IO;
    }
    streamlite_db_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return STREAMLITE_DB_NOMEM;
    int status = map_session_status(session_pool_open(&UNLIMITED, &handle->pool));
    database_status_t opened = DATABASE_OK;
    if (status == STREAMLITE_OK) {
        session_status_t made = session_pool_open_transactional(handle->pool, path, NULL, &handle->db, &opened);
        status = opened != DATABASE_OK ? map_database_status(opened) : map_session_status(made);
    }
    if (status == STREAMLITE_OK) status = map_session_status(session_open(handle->db, &handle->autocommit));
    if (status != STREAMLITE_OK) {
        close_parts(handle);
        free(handle);
        return status;
    }
    pthread_mutex_init(&handle->mutex, NULL);
    pthread_mutex_init(&handle->cancel_lock, NULL);
    handle->owner = getpid();
    handle->magic = DB_MAGIC;
    *out = handle;
    return STREAMLITE_OK;
}

int streamlite_close(streamlite_db_t **db)
{
    if (db == NULL || *db == NULL) return STREAMLITE_OK;
    streamlite_db_t *handle = *db;
    if (!live_db(handle)) return STREAMLITE_DB_INVALID;   /* 이미 닫혔거나 핸들이 아니다. 건드리지 않는다 */
    if (forked(handle)) return STREAMLITE_DB_FORKED;
    pthread_mutex_lock(&handle->mutex);
    if (handle->live_queues > 0 || handle->live_txns > 0) {
        pthread_mutex_unlock(&handle->mutex);
        return ACKQ_BUSY;   /* 큐나 트랜잭션이 이 database를 쥐고 있다. 핸들은 그대로다 */
    }
    session_close(handle->autocommit);
    handle->autocommit = NULL;
    database_status_t closed = DATABASE_OK;
    session_status_t status = session_pool_close_database(handle->db, &closed);
    session_pool_close(handle->pool);
    handle->magic = 0;
    pthread_mutex_unlock(&handle->mutex);
    /* 이 순간 같은 핸들로 mutex를 기다리는 호출이 있으면 안 된다(헤더의 수명 계약). 여기서 막을 방법은 없다 */
    pthread_mutex_destroy(&handle->mutex);
    pthread_mutex_destroy(&handle->cancel_lock);
    free(handle);
    *db = NULL;
    return status != SESSION_OK ? map_session_status(status) : map_database_status(closed);
}

void streamlite_cancel(streamlite_db_t *db)
{
    if (!live_db(db) || forked(db)) return;
    pthread_mutex_lock(&db->cancel_lock);
    if (db->running != NULL) session_cancel(db->running);
    pthread_mutex_unlock(&db->cancel_lock);
}

/* mutex 안에서 부른다. 실행하는 동안 streamlite_cancel이 이 세션을 취소할 수 있다. */
static void set_running(streamlite_db_t *db, session_t *session)
{
    pthread_mutex_lock(&db->cancel_lock);
    db->running = session;
    pthread_mutex_unlock(&db->cancel_lock);
}

static session_t *session_for(streamlite_db_t *db, streamlite_txn_t *txn)
{
    return txn != NULL ? txn->session : db->autocommit;
}

/* ── 결과 인코딩: 상한을 넘거나 할당이 실패하면 error를 남기고 부분 결과를 주지 않는다 ── */

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    ackq_status_t error;   /* ACKQ_OK가 아니면 이후 쓰기는 무시한다 */
} buffer_t;

static void put_bytes(buffer_t *b, const void *bytes, size_t size)
{
    if (b->error != ACKQ_OK) return;
    size_t limit = streamlite_max_result_bytes();
    if (size > limit || b->size > limit - size) {
        b->error = ACKQ_LIMIT;
        return;
    }
    size_t need = b->size + size;   /* limit 이하라 넘치지 않는다 */
    if (need > b->capacity) {
        /* 선언한 상한보다 크게 할당하지 않는다. 작은 상한에서는 처음부터 상한 크기로 잡는다. */
        size_t capacity = b->capacity != 0 ? b->capacity : (limit < 256 ? limit : 256);
        while (capacity < need) {
            if (capacity > limit / 2) {
                capacity = limit;   /* need <= limit이므로 충분하다 */
                break;
            }
            capacity *= 2;
        }
        uint8_t *grown = realloc(b->data, capacity);
        if (grown == NULL) {
            b->error = ACKQ_NOMEM;
            return;
        }
        b->data = grown;
        b->capacity = capacity;
    }
    if (size > 0) memcpy(b->data + b->size, bytes, size);
    b->size = need;
}

static void put_u8(buffer_t *b, uint8_t v) { put_bytes(b, &v, 1); }

static void put_u32(buffer_t *b, uint32_t v)
{
    uint8_t raw[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put_bytes(b, raw, 4);
}

static void put_u64(buffer_t *b, uint64_t v)
{
    uint8_t raw[8];
    for (int i = 0; i < 8; i++) raw[i] = (uint8_t)(v >> (8 * i));
    put_bytes(b, raw, 8);
}

/* 길이는 u32로 인코딩한다. 넘으면 자르지 않고 거절한다. */
static void put_length_prefixed(buffer_t *b, const uint8_t *bytes, size_t size)
{
    if (size > UINT32_MAX) {
        if (b->error == ACKQ_OK) b->error = ACKQ_LIMIT;
        return;
    }
    put_u32(b, (uint32_t)size);
    put_bytes(b, bytes, size);
}

static void put_cell(buffer_t *b, const value_view_t *cell)
{
    put_u8(b, (uint8_t)cell->type);
    switch (cell->type) {
        case VALUE_INTEGER: put_u64(b, (uint64_t)cell->integer); break;
        case VALUE_REAL: {
            uint64_t bits;
            memcpy(&bits, &cell->real, sizeof(bits));
            put_u64(b, bits);
            break;
        }
        case VALUE_TEXT:
        case VALUE_BLOB: put_length_prefixed(b, cell->bytes, cell->size_bytes); break;
        default: break;
    }
}

static void encode_error(buffer_t *b, const result_error_t *error)
{
    put_u8(b, 2);
    put_u32(b, error != NULL ? (uint32_t)error->error_class : (uint32_t)RESULT_E_INTERNAL);
    put_u32(b, error != NULL ? (uint32_t)error->code : 0);
    put_length_prefixed(b, error != NULL ? (const uint8_t *)error->message : NULL, error != NULL ? error->message_len : 0);
}

static void encode_rows(buffer_t *b, result_t *result)
{
    size_t columns = result_column_count(result);
    put_u8(b, 0);
    if (columns > UINT32_MAX) {
        b->error = ACKQ_LIMIT;
        return;
    }
    put_u32(b, (uint32_t)columns);
    for (size_t i = 0; i < columns; i++) {
        const result_column_t *column = result_column(result, i);
        put_length_prefixed(b, column->name, column->name_len);
        put_u8(b, (uint8_t)column->type);
        put_u8(b, column->is_nullable ? 1 : 0);
    }
    result_row_t row;
    result_next_t next;
    while ((next = result_next(result, &row)) == RESULT_NEXT_ROW && b->error == ACKQ_OK) {
        put_u8(b, 1);
        for (size_t i = 0; i < row.cell_count; i++) put_cell(b, &row.cells[i]);
    }
    if (b->error != ACKQ_OK) return;   /* 상한이나 할당 실패면 부분 행을 주지 않는다 */
    if (next != RESULT_NEXT_DONE) {
        /* 읽는 도중 실패는 오류 결과로 바꿔 부분 행을 성공처럼 돌려주지 않는다 */
        b->size = 0;
        encode_error(b, result_outcome(result).error);
        return;
    }
    put_u8(b, 0);
}

static void encode_result(buffer_t *b, result_t *result)
{
    switch (result_kind(result)) {
        case RESULT_KIND_ROWS: encode_rows(b, result); break;
        case RESULT_KIND_COMMAND: {
            const result_command_t *command = result_command(result);
            put_u8(b, 1);
            put_length_prefixed(b, (const uint8_t *)command->tag, command->tag_len);
            put_u64(b, command->affected_rows);
            break;
        }
        default: encode_error(b, result_outcome(result).error);
    }
}

/* 입력 매개변수를 view로 바꾼다. 실패하면 상태를 돌려주고 *views는 NULL이다. */
static int build_views(const streamlite_param_t *params, size_t count, value_view_t **views)
{
    *views = NULL;
    if (count == 0) return STREAMLITE_OK;
    value_view_t *built = calloc(count, sizeof(*built));
    if (built == NULL) return ACKQ_NOMEM;
    for (size_t i = 0; i < count; i++) {
        const streamlite_param_t *p = &params[i];
        switch (p->type) {
            case STREAMLITE_PARAM_NULL: built[i] = (value_view_t){VALUE_NULL, 0, 0.0, NULL, 0}; break;
            case STREAMLITE_PARAM_INTEGER: built[i] = (value_view_t){VALUE_INTEGER, p->integer, 0.0, NULL, 0}; break;
            case STREAMLITE_PARAM_REAL: built[i] = (value_view_t){VALUE_REAL, 0, p->real, NULL, 0}; break;
            case STREAMLITE_PARAM_TEXT:
                built[i] = (value_view_t){VALUE_TEXT, 0, 0.0, p->size > 0 ? p->bytes : NULL, p->size};
                break;
            case STREAMLITE_PARAM_BLOB:
                built[i] = (value_view_t){VALUE_BLOB, 0, 0.0, p->size > 0 ? p->bytes : NULL, p->size};
                break;
            default:
                free(built);
                return ACKQ_INVALID;
        }
    }
    *views = built;
    return STREAMLITE_OK;
}

int streamlite_exec(streamlite_db_t *db, streamlite_txn_t *txn, const char *sql, const streamlite_param_t *params, size_t param_count,
               uint8_t **out, size_t *out_size)
{
    TB_REQUEST_SCOPE(bench_request);
    if (out != NULL) *out = NULL;
    if (out_size != NULL) *out_size = 0;
    if (!live_db(db) || sql == NULL || out == NULL || out_size == NULL || (param_count > 0 && params == NULL)) {
        return ACKQ_INVALID;
    }
    /* 끝난 트랜잭션과 다른 database의 트랜잭션은 실행 전에 거절 */
    if (txn != NULL && (!live_txn(txn) || txn->db != db)) return ACKQ_INVALID;
    if (forked(db)) return STREAMLITE_DB_FORKED;
    value_view_t *views;
    int status = build_views(params, param_count, &views);
    if (status != STREAMLITE_OK) return status;
    lexer_binding_t binding = {views, param_count};
    result_t *result = NULL;
    buffer_t buffer = {0};
    /* 실행, 인코딩, 해제를 한 잠금 구간에서 끝낸다. 행은 result_next가 database 상태를 읽어 만든다. */
    TB_BEGIN(bench_lock, TB_HANDLE_LOCK);
    pthread_mutex_lock(&db->mutex);
    TB_END(bench_lock);
    session_t *session = session_for(db, txn);
    set_running(db, session);
    database_exec_status_t exec_status = session_exec(session, NULL, (const uint8_t *)sql, strlen(sql),
                                                      param_count > 0 ? &binding : NULL, &result);
    set_running(db, NULL);
    if (exec_status == DATABASE_EXEC_OK && result != NULL) {
        encode_result(&buffer, result);
        result_release(&result);
    }
    pthread_mutex_unlock(&db->mutex);
    free(views);
    if (exec_status == DATABASE_EXEC_NOMEM) return ACKQ_NOMEM;
    if (exec_status != DATABASE_EXEC_OK) return ACKQ_INVALID;
    if (buffer.error != ACKQ_OK) {
        free(buffer.data);
        return buffer.error;
    }
    *out = buffer.data;
    *out_size = buffer.size;
    return STREAMLITE_OK;
}

/* ── 트랜잭션 ── */

static int map_txn(txn_status_t status)
{
    switch (status) {
        case TXN_OK: return STREAMLITE_OK;
        case TXN_BUSY:
        case TXN_TIMEOUT: return ACKQ_BUSY;
        case TXN_NOMEM: return ACKQ_NOMEM;
        case TXN_POISONED: return ACKQ_POISONED;
        case TXN_UNSUPPORTED: return ACKQ_UNSUPPORTED;
        case TXN_COMMIT_FAILED: return ACKQ_NOT_APPLIED;
        case TXN_COMMIT_UNCERTAIN: return ACKQ_UNCERTAIN;
        case TXN_INVALID: return ACKQ_TXN_FAILED;
        default: return ACKQ_IO;
    }
}

int streamlite_begin(streamlite_db_t *db, streamlite_txn_t **out)
{
    TB_REQUEST_SCOPE(bench_request);
    if (out == NULL) return ACKQ_INVALID;
    *out = NULL;
    if (!live_db(db)) return ACKQ_INVALID;
    if (forked(db)) return STREAMLITE_DB_FORKED;
    streamlite_txn_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return ACKQ_NOMEM;
    session_txn_options_t options = {TXN_GUARANTEE_DURABLE, 0};
    TB_BEGIN(bench_lock, TB_HANDLE_LOCK);
    pthread_mutex_lock(&db->mutex);
    TB_END(bench_lock);
    int status = session_open(db->db, &handle->session) == SESSION_OK ? STREAMLITE_OK : ACKQ_NOMEM;
    if (status == STREAMLITE_OK) {
        txn_status_t begun = session_txn_begin(handle->session, &options);
        status = map_txn(begun);
        if (begun != TXN_OK) session_close(handle->session);
    }
    if (status == STREAMLITE_OK) db->live_txns++;
    pthread_mutex_unlock(&db->mutex);
    if (status != STREAMLITE_OK) {
        free(handle);
        return status;
    }
    handle->db = db;
    handle->magic = TXN_MAGIC;
    *out = handle;
    return STREAMLITE_OK;
}

/* mutex 안에서 부른다. 트랜잭션 세션을 닫고 핸들을 해제한다. */
static void forget_txn(streamlite_txn_t **txn)
{
    streamlite_txn_t *handle = *txn;
    session_close(handle->session);
    handle->db->live_txns--;
    handle->magic = 0;
    free(handle);
    *txn = NULL;
}

int streamlite_commit(streamlite_txn_t **txn)
{
    TB_REQUEST_SCOPE(bench_request);
    if (txn == NULL || !live_txn(*txn)) return ACKQ_INVALID;
    streamlite_db_t *db = (*txn)->db;
    if (forked(db)) return STREAMLITE_DB_FORKED;
    TB_BEGIN(bench_lock, TB_HANDLE_LOCK);
    pthread_mutex_lock(&db->mutex);
    TB_END(bench_lock);
    TB_BEGIN(bench_commit, TB_PUBLISH);
    txn_status_t status = session_txn_commit((*txn)->session, NULL);
    TB_END(bench_commit);
    /* 실패 상태(INVALID)이면 트랜잭션이 남으므로 rollback으로 정리한다 */
    if (session_txn_active((*txn)->session)) session_txn_rollback((*txn)->session);
    forget_txn(txn);
    pthread_mutex_unlock(&db->mutex);
    return map_txn(status);
}

int streamlite_rollback(streamlite_txn_t **txn)
{
    TB_REQUEST_SCOPE(bench_request);
    if (txn == NULL || !live_txn(*txn)) return ACKQ_INVALID;
    streamlite_db_t *db = (*txn)->db;
    if (forked(db)) return STREAMLITE_DB_FORKED;
    TB_BEGIN(bench_lock, TB_HANDLE_LOCK);
    pthread_mutex_lock(&db->mutex);
    TB_END(bench_lock);
    txn_status_t status = session_txn_rollback((*txn)->session);
    forget_txn(txn);
    pthread_mutex_unlock(&db->mutex);
    return map_txn(status);
}

int streamlite_txn_failed(const streamlite_txn_t *txn)
{
    if (!live_txn(txn) || forked(txn->db)) return 0;
    /* 다른 스레드가 이 트랜잭션의 세션으로 실행 중일 수 있으므로 다른 호출처럼 mutex 안에서 읽는다 */
    pthread_mutex_lock(&txn->db->mutex);
    int failed = session_txn_failed(txn->session) ? 1 : 0;
    pthread_mutex_unlock(&txn->db->mutex);
    return failed;
}

/* ── 큐 ── */

int streamlite_queue_open(streamlite_db_t *db, const char *name, int install, const streamlite_queue_options_t *options,
                    streamlite_queue_t **out)
{
    if (out == NULL) return ACKQ_INVALID;
    *out = NULL;
    if (!live_db(db)) return ACKQ_INVALID;
    if (forked(db)) return STREAMLITE_DB_FORKED;
    static const streamlite_queue_options_t defaults = {0};
    const streamlite_queue_options_t *given = options != NULL ? options : &defaults;
    streamlite_queue_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return ACKQ_NOMEM;
    ackq_options_t queue_options = {0};
    queue_options.name = name;
    queue_options.max_attempts = given->max_attempts;
    queue_options.max_payload_bytes = given->max_payload_bytes;
    queue_options.idem_retention_ms = given->idem_retention_ms;
    queue_options.begin_timeout_ms = 0;
    queue_options.now_ms = given->clock;
    queue_options.clock_ctx = given->clock_ctx;
    pthread_mutex_lock(&db->mutex);
    int status = install ? ackq_install(db->autocommit, &queue_options, &handle->queue)
                         : ackq_open(db->autocommit, &queue_options, &handle->queue);
    if (status == ACKQ_OK) db->live_queues++;
    pthread_mutex_unlock(&db->mutex);
    if (status != ACKQ_OK) {
        free(handle);
        return status;
    }
    handle->db = db;
    handle->magic = QUEUE_MAGIC;
    *out = handle;
    return STREAMLITE_OK;
}

void streamlite_queue_close(streamlite_queue_t **queue)
{
    if (queue == NULL || !live_queue(*queue)) return;
    streamlite_db_t *db = (*queue)->db;
    if (forked(db)) return;
    pthread_mutex_lock(&db->mutex);
    ackq_close(&(*queue)->queue);
    (*queue)->magic = 0;
    db->live_queues--;
    pthread_mutex_unlock(&db->mutex);
    free(*queue);
    *queue = NULL;
}

/*
 * 큐 연산 공통: 닫힌 큐·끝난 트랜잭션이거나 txn이 이 큐의 database 것이 아니면 잠그기 전에 거절하고, 아니면 mutex 안에서
 * call을 실행한다. call 안의 session은 트랜잭션의 세션이거나 연결의 자동 commit 세션이다. 잠그기 전에 읽는 q->db는 큐
 * 핸들이 살아 있는 동안 바뀌지 않고, 살아 있는 큐가 있으면 streamlite_close가 BUSY라 그 db도 살아 있다.
 */
#define QUEUE_CALL(q, txn, call)                                              \
    do {                                                                      \
        if (!live_queue(q) || ((txn) != NULL && (!live_txn(txn) || (txn)->db != (q)->db))) return ACKQ_INVALID; \
        if (forked((q)->db)) return STREAMLITE_DB_FORKED;                           \
        TB_BEGIN(bench_lock, TB_HANDLE_LOCK);                                 \
        pthread_mutex_lock(&(q)->db->mutex);                                  \
        TB_END(bench_lock);                                                   \
        session_t *session = session_for((q)->db, (txn));                    \
        set_running((q)->db, session);                                        \
        status = (call);                                                      \
        set_running((q)->db, NULL);                                           \
        pthread_mutex_unlock(&(q)->db->mutex);                                \
    } while (0)

int streamlite_queue_put(streamlite_queue_t *q, streamlite_txn_t *txn, const uint8_t *payload, size_t size, const char *idem_key, uint64_t *id,
             int *duplicate)
{
    TB_REQUEST_SCOPE(bench_request);
    int status;
    bool dup = false;
    QUEUE_CALL(q, txn, ackq_enqueue(q->queue, session, payload, size, idem_key, id, &dup));
    if (duplicate != NULL) *duplicate = dup ? 1 : 0;
    return status;
}

int streamlite_queue_get(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t lease_ms, streamlite_queue_item_t *item)
{
    TB_REQUEST_SCOPE(bench_request);
    int status;
    ackq_claim_t claim = {0};
    size_t count = 0;
    if (item == NULL) return ACKQ_INVALID;
    *item = (streamlite_queue_item_t){0};
    if (id == 0) {
        QUEUE_CALL(q, txn, ackq_claim(q->queue, session, 1, lease_ms, &claim, &count));
        if (status == ACKQ_OK && count == 0) return STREAMLITE_EMPTY;
    } else {
        QUEUE_CALL(q, txn, ackq_claim_id(q->queue, session, id, lease_ms, &claim));
    }
    if (status != ACKQ_OK) return status;
    *item = (streamlite_queue_item_t){claim.id, claim.fence, claim.attempts, claim.lease_until_ms, claim.payload, claim.payload_size};
    return STREAMLITE_OK;
}

void streamlite_queue_item_free(streamlite_queue_item_t *item)
{
    if (item == NULL) return;
    free(item->payload);
    *item = (streamlite_queue_item_t){0};
}

int streamlite_queue_ack(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t fence)
{
    TB_REQUEST_SCOPE(bench_request);
    int status;
    QUEUE_CALL(q, txn, ackq_ack(q->queue, session, id, fence));
    return status;
}

int streamlite_queue_extend(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t fence, uint64_t lease_ms,
                      uint64_t *lease_until)
{
    int status;
    QUEUE_CALL(q, txn, ackq_extend(q->queue, session, id, fence, lease_ms, lease_until));
    return status;
}

int streamlite_queue_fail(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t fence, const char *reason)
{
    int status;
    QUEUE_CALL(q, txn, ackq_fail(q->queue, session, id, fence, reason));
    return status;
}

int streamlite_queue_nack(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t fence, uint64_t retry_delay_ms, const char *reason,
              int *terminal)
{
    int status;
    bool end = false;
    QUEUE_CALL(q, txn, ackq_nack(q->queue, session, id, fence, retry_delay_ms, reason, &end));
    if (terminal != NULL) *terminal = end ? 1 : 0;
    return status;
}

int streamlite_queue_update(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t id, uint64_t fence, const uint8_t *payload, size_t size)
{
    int status;
    QUEUE_CALL(q, txn, ackq_update(q->queue, session, id, fence, payload, size));
    return status;
}

int streamlite_queue_recover(streamlite_queue_t *q, streamlite_txn_t *txn, size_t limit, size_t *recovered)
{
    int status;
    QUEUE_CALL(q, txn, ackq_recover(q->queue, session, limit, recovered));
    return status;
}

int streamlite_queue_release_leases(streamlite_queue_t *q, streamlite_txn_t *txn, size_t limit, size_t *released)
{
    int status;
    QUEUE_CALL(q, txn, ackq_release_leases(q->queue, session, limit, released));
    return status;
}

int streamlite_queue_purge(streamlite_queue_t *q, streamlite_txn_t *txn, int include_failed, size_t limit, size_t *purged)
{
    int status;
    QUEUE_CALL(q, txn, ackq_purge(q->queue, session, include_failed != 0, limit, purged));
    return status;
}

int streamlite_queue_stats(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t counts[4])
{
    int status;
    ackq_stats_t stats = {0};
    if (counts == NULL) return ACKQ_INVALID;
    QUEUE_CALL(q, txn, ackq_stats(q->queue, session, &stats));
    counts[0] = stats.ready;
    counts[1] = stats.leased;
    counts[2] = stats.acked;
    counts[3] = stats.failed;
    return status;
}

/* QUEUE_CALL은 거절할 때 바로 return하므로, 호출자가 잡은 배열을 놓치지 않게 따로 감싼다 */
static int list_locked(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t after_id, size_t limit, int with_payload,
                       ackq_item_t *raw_items, size_t *n)
{
    int status;
    QUEUE_CALL(q, txn, ackq_list(q->queue, session, after_id, limit, with_payload != 0, raw_items, n));
    return status;
}

int streamlite_queue_list(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t after_id, size_t limit, int with_payload, streamlite_queue_list_item_t *items,
              size_t *count)
{
    int status;
    if (count != NULL) *count = 0;
    if (items == NULL || count == NULL || limit == 0 || limit > ACKQ_MAX_CLAIM_BATCH) return ACKQ_INVALID;
    ackq_item_t *raw_items = calloc(limit, sizeof(*raw_items));
    if (raw_items == NULL) return ACKQ_NOMEM;
    size_t n = 0;
    status = list_locked(q, txn, after_id, limit, with_payload, raw_items, &n);
    if (status == ACKQ_OK) {
        for (size_t i = 0; i < n; i++) {
            const ackq_item_t *src = &raw_items[i];
            items[i] = (streamlite_queue_list_item_t){.id = src->id,
                                         .state = (int32_t)src->state,
                                         .fence = src->fence,
                                         .attempts = src->attempts,
                                         .ready_at_ms = src->ready_at_ms,
                                         .lease_until_ms = src->lease_until_ms,
                                         .created_at_ms = src->created_at_ms,
                                         .payload = src->payload,
                                         .payload_size = src->payload_size};
            memcpy(items[i].note, src->note, sizeof(items[i].note));
            raw_items[i].payload = NULL;   /* 소유권을 넘겼다 */
        }
        *count = n;
    }
    free(raw_items);
    return status;
}

void streamlite_queue_list_free(streamlite_queue_list_item_t *items, size_t count)
{
    if (items == NULL) return;
    for (size_t i = 0; i < count; i++) {
        free(items[i].payload);
        items[i].payload = NULL;
    }
}
