/*
 * test_streamlite.c — Python 같은 다른 언어가 쓰는 평탄한 C 인터페이스(#140)의 경계 검사.
 *
 * streamlite.c를 직접 포함해 realloc 실패를 주입하고 static 인코더를 직접 검사한다. 보는 것:
 *   - 결과 인코딩 왕복과 출력 상한(상한 이하 통과, 상한 +1 거절, 부분 행 없음), 길이 u32 초과 거절
 *   - 모든 realloc 지점의 할당 실패가 NOMEM이고 출력이 없으며 다음 호출이 같은 결과를 준다
 *   - 다른 database의 트랜잭션을 streamlite_exec·streamlite_queue_*가 실행 전에 거절한다
 *   - 살아 있는 큐·트랜잭션 핸들이 있으면 streamlite_close가 BUSY이고 핸들은 그대로다
 *   - 여러 스레드가 실행·인코딩을 번갈아 불러도 결과가 깨지지 않는다(잠금 구간이 결과 소비까지 이어진다)
 * 합성 데이터만 쓴다.
 */
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

static long g_realloc_calls, g_realloc_fail_at;
static size_t g_realloc_largest;   /* 요청한 가장 큰 크기. 선언한 상한을 넘지 않는지 본다 */

static void *capi_test_realloc(void *pointer, size_t size)
{
    if (size > g_realloc_largest) g_realloc_largest = size;
    if (g_realloc_fail_at != 0 && ++g_realloc_calls == g_realloc_fail_at) return NULL;
    return realloc(pointer, size);
}

#define realloc capi_test_realloc
#include "../src/capi/streamlite.c"
#undef realloc

#include "ack_queue_test_util.h"

#define DIR "__test__streamlite"

static streamlite_db_t *open_capi_db(const char *name)
{
    char path[192];
    snprintf(path, sizeof(path), "%s/%s.streamlite", g_dir, name);
    streamlite_db_t *db = NULL;
    int st = streamlite_open(path, 1, &db);
    assert(st == STREAMLITE_OK);
    return db;
}

/* 인코딩 버퍼를 사람이 읽는 한 줄로 바꿔 기대 문자열과 비교한다 */
typedef struct {
    const uint8_t *p;
    size_t n, at;
} reader_t;

static uint64_t rd(reader_t *r, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes && r->at < r->n; i++) v |= (uint64_t)r->p[r->at++] << (8 * i);
    return v;
}

static void render(const uint8_t *buf, size_t n, char *out, size_t cap)
{
    reader_t r = {buf, n, 0};
    size_t len = 0;
#define EMIT(...) len += (size_t)snprintf(out + len, cap - len, __VA_ARGS__)
    out[0] = '\0';
    uint8_t kind = (uint8_t)rd(&r, 1);
    if (kind == 1) {
        uint32_t tag = (uint32_t)rd(&r, 4);
        EMIT("CMD %.*s ", (int)tag, (const char *)buf + r.at);
        r.at += tag;
        EMIT("%llu", (unsigned long long)rd(&r, 8));
    } else if (kind == 2) {
        uint32_t cls = (uint32_t)rd(&r, 4);
        EMIT("ERR %u", cls);
    } else {
        uint32_t cols = (uint32_t)rd(&r, 4);
        EMIT("ROWS[");
        for (uint32_t i = 0; i < cols; i++) {
            uint32_t l = (uint32_t)rd(&r, 4);
            EMIT("%s%.*s", i ? "," : "", (int)l, (const char *)buf + r.at);
            r.at += l;
            /* 열 형식 번호와 NULL 가능(?) */
            uint8_t type = (uint8_t)rd(&r, 1);
            EMIT(":%u%s", type, rd(&r, 1) ? "?" : "");
        }
        EMIT("]");
        while (r.at < r.n && rd(&r, 1) == 1) {
            EMIT(" (");
            for (uint32_t i = 0; i < cols; i++) {
                uint8_t t = (uint8_t)rd(&r, 1);
                EMIT("%s", i ? "|" : "");
                if (t == 0) EMIT("NULL");
                else if (t == 1) EMIT("%lld", (long long)rd(&r, 8));
                else if (t == 2) {
                    uint64_t bits = rd(&r, 8);
                    double d;
                    memcpy(&d, &bits, 8);
                    EMIT("%g", d);
                } else {
                    uint32_t l = (uint32_t)rd(&r, 4);
                    EMIT("%s%u", t == 3 ? "t" : "b", l);
                    r.at += l;
                }
            }
            EMIT(")");
        }
    }
#undef EMIT
}

static int exec_str(streamlite_db_t *db, streamlite_txn_t *txn, const char *sql, const streamlite_param_t *params, size_t n, char *out,
                    size_t cap)
{
    uint8_t *buf = NULL;
    size_t size = 0;
    int st = streamlite_exec(db, txn, sql, params, n, &buf, &size);
    if (st == STREAMLITE_OK) render(buf, size, out, cap);
    else out[0] = '\0';
    CHECK(st == STREAMLITE_OK ? buf != NULL : buf == NULL, "성공이 아니면 출력 버퍼가 없다");
    streamlite_free(buf);
    return st;
}

static streamlite_param_t p_int(int64_t v) { return (streamlite_param_t){STREAMLITE_PARAM_INTEGER, v, 0, NULL, 0}; }
static streamlite_param_t p_text(const char *s) { return (streamlite_param_t){STREAMLITE_PARAM_TEXT, 0, 0, (const uint8_t *)s, strlen(s)}; }
static streamlite_param_t p_blob(const void *b, size_t n) { return (streamlite_param_t){STREAMLITE_PARAM_BLOB, 0, 0, b, n}; }
static streamlite_param_t p_real(double d) { return (streamlite_param_t){STREAMLITE_PARAM_REAL, 0, d, NULL, 0}; }
static streamlite_param_t p_null(void) { return (streamlite_param_t){STREAMLITE_PARAM_NULL, 0, 0, NULL, 0}; }

/* ── 1. DB 단독 사용과 인코딩 왕복 ── */

static void test_db_only_and_encoding(void)
{
    printf("\n[DB 단독 사용과 결과 인코딩 왕복]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    char out[512];
    CHECK(exec_str(db, NULL, "CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, n INTEGER, s TEXT, b BLOB, r REAL)",
                   NULL, 0, out, sizeof(out)) == STREAMLITE_OK && strcmp(out, "CMD CREATE TABLE 0") == 0, "CREATE: %s", out);
    static const uint8_t blob[] = {0, 1, 2};
    streamlite_param_t row1[] = {p_int(7), p_text("abc"), p_blob(blob, 3), p_real(1.5)};
    CHECK(exec_str(db, NULL, "INSERT INTO t (n, s, b, r) VALUES (?, ?, ?, ?)", row1, 4, out, sizeof(out)) == STREAMLITE_OK &&
              strcmp(out, "CMD INSERT 1") == 0, "INSERT 매개변수: %s", out);
    streamlite_param_t row2[] = {p_null(), p_text(""), p_blob(NULL, 0), p_real(-0.5)};
    CHECK(exec_str(db, NULL, "INSERT INTO t (n, s, b, r) VALUES (?, ?, ?, ?)", row2, 4, out, sizeof(out)) == STREAMLITE_OK,
          "NULL·빈 값 INSERT");
    CHECK(exec_str(db, NULL, "SELECT id, n, s, b, r FROM t", NULL, 0, out, sizeof(out)) == STREAMLITE_OK &&
              strcmp(out, "ROWS[id:1,n:1?,s:3?,b:4?,r:2?] (1|7|t3|b3|1.5) (2|NULL|t0|b0|-0.5)") == 0, "SELECT 왕복: %s", out);
    CHECK(exec_str(db, NULL, "SELECT * FROM missing", NULL, 0, out, sizeof(out)) == STREAMLITE_OK && strncmp(out, "ERR ", 4) == 0,
          "문장 오류는 인코딩된 오류 결과: %s", out);
    streamlite_param_t bad = {99, 0, 0, NULL, 0};
    uint8_t *buf = NULL;
    size_t size = 0;
    CHECK(streamlite_exec(db, NULL, "SELECT 1", &bad, 1, &buf, &size) == ACKQ_INVALID && buf == NULL, "잘못된 매개변수 type");
    CHECK(streamlite_exec(db, NULL, NULL, NULL, 0, &buf, &size) == ACKQ_INVALID, "SQL 없음");
    CHECK(streamlite_close(&db) == STREAMLITE_OK && db == NULL, "close");
}

/* ── 2. 출력 상한과 길이 한도 ── */

static void test_output_cap(void)
{
    printf("\n[출력 상한: 이하 통과, +1 거절, 부분 행 없음]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    char out[2048];
    assert(exec_str(db, NULL, "CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, s TEXT)", NULL, 0, out, sizeof(out)) == STREAMLITE_OK);
    for (int i = 0; i < 20; i++) {
        streamlite_param_t p = p_text("0123456789012345678901234567890123456789");
        assert(exec_str(db, NULL, "INSERT INTO t (s) VALUES (?)", &p, 1, out, sizeof(out)) == STREAMLITE_OK);
    }
    uint8_t *buf = NULL;
    size_t full = 0;
    assert(streamlite_exec(db, NULL, "SELECT id, s FROM t", NULL, 0, &buf, &full) == STREAMLITE_OK);
    streamlite_free(buf);
    size_t original = streamlite_max_result_bytes();
    CHECK(original == STREAMLITE_DEFAULT_MAX_RESULT_BYTES, "기본 상한");
    streamlite_set_max_result_bytes(full);
    size_t got = 0;
    CHECK(streamlite_exec(db, NULL, "SELECT id, s FROM t", NULL, 0, &buf, &got) == STREAMLITE_OK && got == full, "상한과 같으면 통과");
    streamlite_free(buf);
    for (size_t cap = full - 1; cap + 60 > full; cap--) {
        streamlite_set_max_result_bytes(cap);
        buf = (uint8_t *)1;
        got = 99;
        CHECK(streamlite_exec(db, NULL, "SELECT id, s FROM t", NULL, 0, &buf, &got) == ACKQ_LIMIT && buf == NULL && got == 0,
              "상한 %zu(-%zu)는 부분 행 없이 LIMIT", cap, full - cap);
    }
    streamlite_set_max_result_bytes(32);
    CHECK(streamlite_exec(db, NULL, "SELECT id, s FROM t", NULL, 0, &buf, &got) == ACKQ_LIMIT && buf == NULL, "작은 상한");
    CHECK(streamlite_exec(db, NULL, "SELECT id FROM t WHERE id = 1", NULL, 0, &buf, &got) == STREAMLITE_OK, "상한 안의 결과는 통과");
    streamlite_free(buf);
    streamlite_set_max_result_bytes(original);
    CHECK(streamlite_close(&db) == STREAMLITE_OK, "close");

    /* u32 길이 초과는 자르지 않고 거절한다(실제 4GiB를 만들지 않고 인코더를 직접 본다) */
    buffer_t b = {0};
    put_length_prefixed(&b, NULL, (size_t)UINT32_MAX + 1);
    CHECK(b.error == ACKQ_LIMIT && b.size == 0 && b.data == NULL, "u32를 넘는 길이는 LIMIT이고 아무것도 쓰지 않는다");
    buffer_t c = {0};
    streamlite_set_max_result_bytes(10);
    put_bytes(&c, "0123456789", 10);
    CHECK(c.error == ACKQ_OK && c.size == 10, "상한과 같은 쓰기는 통과");
    put_bytes(&c, "x", 1);
    CHECK(c.error == ACKQ_LIMIT && c.size == 10, "상한을 넘는 쓰기는 거절하고 내용을 바꾸지 않는다");
    free(c.data);
    streamlite_set_max_result_bytes(original);
    /* 아주 작은 상한(1~255)에서도 선언한 상한보다 크게 할당하지 않는다 */
    for (size_t cap = 1; cap <= 300; cap += (cap < 20 ? 1 : 37)) {
        streamlite_set_max_result_bytes(cap);
        buffer_t e = {0};
        g_realloc_largest = 0;
        for (size_t i = 0; i < cap && e.error == ACKQ_OK; i++) put_bytes(&e, "z", 1);
        CHECK(e.error == ACKQ_OK && e.size == cap, "상한 %zu까지 채운다", cap);
        CHECK(g_realloc_largest <= cap, "상한 %zu: 요청한 최대 크기 %zu가 상한 이하", cap, g_realloc_largest);
        put_bytes(&e, "z", 1);
        CHECK(e.error == ACKQ_LIMIT && e.size == cap, "상한 %zu를 넘는 쓰기는 거절", cap);
        free(e.data);
    }
    streamlite_set_max_result_bytes(original);
    buffer_t d = {0};
    put_bytes(&d, "a", 1);
    put_bytes(&d, "b", SIZE_MAX);
    CHECK(d.error == ACKQ_LIMIT && d.size == 1, "size_t 덧셈이 넘치는 입력도 거절");
    free(d.data);
}

/* ── 3. 할당 실패 ── */

static void test_allocation_failure(void)
{
    printf("\n[인코딩 버퍼 할당 실패]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    char out[8192];
    assert(exec_str(db, NULL, "CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, s TEXT)", NULL, 0, out, sizeof(out)) == STREAMLITE_OK);
    for (int i = 0; i < 60; i++) {
        streamlite_param_t p = p_text("payload-payload-payload-payload");
        assert(exec_str(db, NULL, "INSERT INTO t (s) VALUES (?)", &p, 1, out, sizeof(out)) == STREAMLITE_OK);
    }
    char reference[8192];
    g_realloc_fail_at = 1000000;   /* 실패시키지 않고 realloc 호출 수만 센다 */
    g_realloc_calls = 0;
    assert(exec_str(db, NULL, "SELECT id, s FROM t", NULL, 0, reference, sizeof(reference)) == STREAMLITE_OK);
    long calls = g_realloc_calls;
    g_realloc_fail_at = 0;
    CHECK(calls >= 3, "버퍼가 여러 번 자란다 (realloc %ld회)", calls);
    for (long k = 1; k <= calls; k++) {
        g_realloc_calls = 0;
        g_realloc_fail_at = k;
        uint8_t *buf = (uint8_t *)1;
        size_t size = 5;
        int st = streamlite_exec(db, NULL, "SELECT id, s FROM t", NULL, 0, &buf, &size);
        g_realloc_fail_at = 0;
        CHECK(st == ACKQ_NOMEM && buf == NULL && size == 0, "k=%ld: realloc 실패는 NOMEM이고 출력이 없다 (st=%d)", k, st);
        CHECK(exec_str(db, NULL, "SELECT id, s FROM t", NULL, 0, out, sizeof(out)) == STREAMLITE_OK && strcmp(out, reference) == 0,
              "k=%ld: 다음 호출은 같은 결과를 준다", k);
    }
    CHECK(streamlite_close(&db) == STREAMLITE_OK, "close");
}

/* ── 4. 다른 database의 트랜잭션 ── */

static void test_wrong_database_transaction(void)
{
    printf("\n[다른 database의 트랜잭션 거절]\n");
    make_db();
    streamlite_db_t *a = open_capi_db("a");
    streamlite_db_t *b = open_capi_db("b");
    streamlite_queue_t *qa = NULL;
    assert(streamlite_queue_open(a, "jobs", 1, &(streamlite_queue_options_t){.clock = fake_clock}, &qa) == STREAMLITE_OK);
    g_now = 5;
    uint64_t id = 0;
    int dup = 0;
    assert(streamlite_queue_put(qa, NULL, (const uint8_t *)"x", 1, NULL, &id, &dup) == STREAMLITE_OK);
    streamlite_queue_item_t item;
    assert(streamlite_queue_get(qa, NULL, 0, 1000, &item) == STREAMLITE_OK);
    uint64_t fence = item.fence;
    streamlite_queue_item_free(&item);

    char out[256];
    assert(exec_str(b, NULL, "CREATE TABLE streamlite_queue_jobs (id INTEGER PRIMARY KEY, marker TEXT NOT NULL)", NULL, 0, out, sizeof(out)) == STREAMLITE_OK);
    streamlite_txn_t *foreign = NULL;
    assert(streamlite_begin(b, &foreign) == STREAMLITE_OK);
    uint8_t *buf = NULL;
    size_t size = 0;
    CHECK(streamlite_exec(a, foreign, "DELETE FROM streamlite_queue_jobs", NULL, 0, &buf, &size) == ACKQ_INVALID && buf == NULL,
          "streamlite_exec는 다른 database의 트랜잭션을 거절한다");
    uint64_t counts[4], other = 7;
    size_t n = 0;
    streamlite_queue_list_item_t list[1];
    CHECK(streamlite_queue_put(qa, foreign, (const uint8_t *)"x", 1, NULL, &other, NULL) == ACKQ_INVALID, "put");
    CHECK(streamlite_queue_get(qa, foreign, 0, 100, &item) == ACKQ_INVALID && item.payload == NULL, "get");
    CHECK(streamlite_queue_ack(qa, foreign, id, fence) == ACKQ_INVALID && streamlite_queue_fail(qa, foreign, id, fence, NULL) == ACKQ_INVALID, "ack·fail");
    CHECK(streamlite_queue_nack(qa, foreign, id, fence, 0, NULL, NULL) == ACKQ_INVALID, "nack");
    CHECK(streamlite_queue_update(qa, foreign, id, fence, (const uint8_t *)"y", 1) == ACKQ_INVALID, "update");
    CHECK(streamlite_queue_recover(qa, foreign, 1, NULL) == ACKQ_INVALID && streamlite_queue_release_leases(qa, foreign, 1, NULL) == ACKQ_INVALID &&
              streamlite_queue_purge(qa, foreign, 0, 1, NULL) == ACKQ_INVALID, "recover·release·purge");
    CHECK(streamlite_queue_stats(qa, foreign, counts) == ACKQ_INVALID && streamlite_queue_list(qa, foreign, 0, 1, 0, list, &n) == ACKQ_INVALID && n == 0,
          "stats·list");
    CHECK(streamlite_txn_failed(foreign) == 0, "거절은 남의 트랜잭션을 실패시키지 않는다");
    CHECK(streamlite_commit(&foreign) == STREAMLITE_OK, "남의 트랜잭션은 그대로 commit된다");
    CHECK(exec_str(b, NULL, "SELECT id FROM streamlite_queue_jobs", NULL, 0, out, sizeof(out)) == STREAMLITE_OK && strcmp(out, "ROWS[id:1]") == 0,
          "다른 database의 테이블이 바뀌지 않았다: %s", out);
    CHECK(streamlite_queue_stats(qa, NULL, counts) == STREAMLITE_OK && counts[1] == 1, "이 큐도 그대로다");
    CHECK(streamlite_queue_ack(qa, NULL, id, fence) == STREAMLITE_OK, "올바른 호출은 된다");
    streamlite_queue_close(&qa);
    CHECK(streamlite_close(&a) == STREAMLITE_OK && streamlite_close(&b) == STREAMLITE_OK, "close");
}

/* ── 5. 핸들 수명 ── */

static void test_handle_lifetime(void)
{
    printf("\n[살아 있는 큐·트랜잭션이 있으면 close는 BUSY]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    streamlite_queue_t *q = NULL;
    assert(streamlite_queue_open(db, "jobs", 1, &(streamlite_queue_options_t){.clock = fake_clock}, &q) == STREAMLITE_OK);
    streamlite_db_t *keep = db;
    CHECK(streamlite_close(&db) == ACKQ_BUSY && db == keep, "큐가 있으면 BUSY이고 핸들은 그대로다");
    uint64_t id;
    g_now = 1;
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"x", 1, NULL, &id, NULL) == STREAMLITE_OK, "BUSY 뒤에도 큐를 쓸 수 있다");
    streamlite_txn_t *txn = NULL;
    CHECK(streamlite_begin(db, &txn) == STREAMLITE_OK, "트랜잭션 시작");
    streamlite_queue_close(&q);
    CHECK(streamlite_close(&db) == ACKQ_BUSY && db == keep, "트랜잭션이 있으면 BUSY");
    CHECK(streamlite_rollback(&txn) == STREAMLITE_OK && txn == NULL, "rollback");
    CHECK(streamlite_close(&db) == STREAMLITE_OK && db == NULL, "모두 닫으면 close");
    streamlite_queue_close(&q);
    streamlite_close(&db);
}

/* ── 6. 여러 스레드가 실행과 인코딩을 번갈아 부른다 ── */

typedef struct {
    streamlite_db_t *db;
    int id;
    int errors;
} stress_t;

static void *stress_main(void *arg)
{
    stress_t *s = arg;
    for (int i = 0; i < 40; i++) {
        uint8_t *buf = NULL;
        size_t size = 0;
        streamlite_param_t p = p_int(s->id * 1000 + i);
        if (streamlite_exec(s->db, NULL, "INSERT INTO t (n) VALUES (?)", &p, 1, &buf, &size) != STREAMLITE_OK) s->errors++;
        streamlite_free(buf);
        buf = NULL;
        if (streamlite_exec(s->db, NULL, "SELECT id, n FROM t", NULL, 0, &buf, &size) != STREAMLITE_OK || size < 2) s->errors++;
        else {
            reader_t r = {buf, size, 0};
            if (rd(&r, 1) != 0) s->errors++;
        }
        streamlite_free(buf);
    }
    return NULL;
}

static void test_threads(void)
{
    printf("\n[여러 스레드의 실행·인코딩]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    char out[256];
    assert(exec_str(db, NULL, "CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, n INTEGER)", NULL, 0, out, sizeof(out)) == STREAMLITE_OK);
    stress_t workers[4];
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        workers[i] = (stress_t){db, i + 1, 0};
        pthread_create(&threads[i], NULL, stress_main, &workers[i]);
    }
    int errors = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
        errors += workers[i].errors;
    }
    CHECK(errors == 0, "스레드 오류 없음 (%d)", errors);
    uint8_t *buf = NULL;
    size_t size = 0;
    assert(streamlite_exec(db, NULL, "SELECT id FROM t", NULL, 0, &buf, &size) == STREAMLITE_OK);
    reader_t r = {buf, size, 0};
    rd(&r, 1);
    uint32_t cols = (uint32_t)rd(&r, 4);
    for (uint32_t i = 0; i < cols; i++) r.at += (size_t)rd(&r, 4) + 2;   /* 이름 뒤 형식·NULL 가능 */
    int rows = 0;
    while (r.at < r.n && rd(&r, 1) == 1) {
        rows++;
        rd(&r, 1);
        rd(&r, 8);
    }
    CHECK(rows == 160, "모든 INSERT가 반영됐다 (rows=%d)", rows);
    streamlite_free(buf);
    CHECK(streamlite_close(&db) == STREAMLITE_OK, "close");
}

/* ── 7. 큐 왕복 ── */

static void test_queue_round_trip(void)
{
    printf("\n[CAPI 큐 왕복: put·get·ack·nack·update]\n");
    make_db();
    streamlite_db_t *db = open_capi_db("main");
    streamlite_queue_t *q = NULL;
    CHECK(streamlite_queue_open(db, "jobs", 0, &(streamlite_queue_options_t){.clock = fake_clock}, &q) == ACKQ_NOT_INSTALLED && q == NULL, "설치 전 open");
    assert(streamlite_queue_open(db, "jobs", 1, &(streamlite_queue_options_t){.max_attempts = 2, .clock = fake_clock}, &q) == STREAMLITE_OK);
    g_now = 100;
    streamlite_queue_item_t item;
    CHECK(streamlite_queue_get(q, NULL, 0, 500, &item) == STREAMLITE_EMPTY, "비면 EMPTY");
    uint64_t id = 0, again = 0;
    int dup = 5;
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"hello", 5, "k", &id, &dup) == STREAMLITE_OK && dup == 0, "put");
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"hello", 5, "k", &again, &dup) == STREAMLITE_OK && dup == 1 && again == id, "중복 put");
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"other", 5, "k", &again, &dup) == ACKQ_CONFLICT && again == 0, "충돌");
    CHECK(streamlite_queue_get(q, NULL, 0, 500, &item) == STREAMLITE_OK && item.id == id && item.fence == 1 && item.payload_size == 5, "get");
    uint64_t fence = item.fence;
    streamlite_queue_item_free(&item);
    CHECK(streamlite_queue_update(q, NULL, id, fence, (const uint8_t *)"HELLO", 5) == STREAMLITE_OK, "update");
    int terminal = 9;
    CHECK(streamlite_queue_nack(q, NULL, id, fence, 0, "retry", &terminal) == STREAMLITE_OK && terminal == 0, "nack");
    CHECK(streamlite_queue_get(q, NULL, id, 500, &item) == STREAMLITE_OK && item.fence == 2 && memcmp(item.payload, "HELLO", 5) == 0, "id로 get");
    CHECK(streamlite_queue_get(q, NULL, id, 500, &(streamlite_queue_item_t){0}) == ACKQ_STALE, "claim 중인 id를 또 get하면 STALE");
    CHECK(streamlite_queue_ack(q, NULL, id, 1) == ACKQ_STALE, "옛 fence ack는 STALE");
    CHECK(streamlite_queue_ack(q, NULL, id, item.fence) == STREAMLITE_OK, "ack");
    streamlite_queue_item_free(&item);
    uint64_t counts[4];
    CHECK(streamlite_queue_stats(q, NULL, counts) == STREAMLITE_OK && counts[2] == 1 && counts[0] + counts[1] + counts[3] == 0, "집계");
    streamlite_queue_list_item_t list[4];
    size_t n = 0;
    CHECK(streamlite_queue_list(q, NULL, 0, 4, 1, list, &n) == STREAMLITE_OK && n == 1 && list[0].state == 2 && list[0].created_at_ms == 100 &&
              list[0].payload_size == 5 && list[0].note[0] == '\0', "list(ack가 이유를 비운다)");
    streamlite_queue_list_free(list, n);
    size_t purged = 0;
    CHECK(streamlite_queue_purge(q, NULL, 0, 10, &purged) == STREAMLITE_OK && purged == 1, "purge");
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"HELLO", 5, "k", &again, &dup) == STREAMLITE_OK && dup == 1 && again == id,
          "purge 뒤에도 기본 보존 기간 안의 같은 key는 중복(#196). 비교 대상은 마지막 payload");
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"hello", 5, "k", &again, &dup) == ACKQ_CONFLICT,
          "update 전 payload는 마지막 payload와 달라 CONFLICT");

    /* #195: 연장은 fence를 유지하고 다른 worker가 다시 가져간 뒤에는 STALE */
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"slow", 4, NULL, &id, NULL) == STREAMLITE_OK, "연장할 작업");
    CHECK(streamlite_queue_get(q, NULL, 0, 100, &item) == STREAMLITE_OK && item.id == id, "짧은 lease로 get");
    fence = item.fence;
    streamlite_queue_item_free(&item);
    uint64_t until = 0;
    g_now = 150;
    CHECK(streamlite_queue_extend(q, NULL, id, fence, 300, &until) == STREAMLITE_OK && until == 450, "연장");
    g_now = 300;
    CHECK(streamlite_queue_get(q, NULL, 0, 100, &item) == STREAMLITE_EMPTY, "연장한 동안 다른 get은 비어 있다");
    g_now = 450;
    CHECK(streamlite_queue_get(q, NULL, 0, 100, &item) == STREAMLITE_OK && item.fence == fence + 1, "연장이 끝나면 다시 전달");
    CHECK(streamlite_queue_extend(q, NULL, id, fence, 300, &until) == ACKQ_STALE, "옛 claim의 연장은 STALE");
    streamlite_queue_item_free(&item);
    streamlite_queue_close(&q);

    /* #197: payload 상한 옵션 */
    assert(streamlite_queue_open(db, "small", 1, &(streamlite_queue_options_t){.max_payload_bytes = 4, .clock = fake_clock}, &q) ==
           STREAMLITE_OK);
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"four", 4, NULL, &id, NULL) == STREAMLITE_OK, "상한과 같은 크기");
    CHECK(streamlite_queue_put(q, NULL, (const uint8_t *)"five!", 5, NULL, &again, NULL) == ACKQ_LIMIT, "상한+1");
    streamlite_queue_close(&q);
    CHECK(streamlite_close(&db) == STREAMLITE_OK, "close");
}

#ifdef STREAMLITE_BENCHMARK
static void test_benchmark_request_boundaries(void)
{
    fresh_dir();
    tb_bench_record records[TB_BENCH_RECORDS];
    uint8_t *wire = NULL;
    size_t size = 0;
    streamlite_txn_t *txn = NULL;
    streamlite_queue_item_t item;
    errno = EBUSY;
    CHECK(streamlite_exec(NULL, NULL, "", NULL, 0, &wire, &size) == ACKQ_INVALID, "exec validation");
    CHECK(streamlite_begin(NULL, &txn) == ACKQ_INVALID, "begin validation");
    CHECK(streamlite_commit(NULL) == ACKQ_INVALID, "commit validation");
    CHECK(streamlite_rollback(NULL) == ACKQ_INVALID, "rollback validation");
    CHECK(streamlite_queue_put(NULL, NULL, NULL, 0, NULL, NULL, NULL) == ACKQ_INVALID, "put validation");
    CHECK(streamlite_queue_get(NULL, NULL, 0, 1, &item) == ACKQ_INVALID, "get validation");
    CHECK(streamlite_queue_ack(NULL, NULL, 1, 1) == ACKQ_INVALID, "ack validation");
    CHECK(errno == EBUSY, "instrumentation preserves errno on early return");
    CHECK(tb_bench_drain(records, TB_BENCH_RECORDS) == 7, "all early returns produce requests");
    for (unsigned i = 0; i < 7; i++) CHECK(records[i].incomplete == 0, "early-return complete scope");
    CHECK(strcmp(records[0].operation, "streamlite_exec") == 0, "request operation");
    streamlite_db_t *db = open_capi_db("benchmark-empty.db");
    streamlite_queue_t *q = NULL;
    CHECK(streamlite_queue_open(db, "empty", 1, NULL, &q) == STREAMLITE_OK, "open benchmark queue");
    CHECK(streamlite_queue_get(q, NULL, 0, 1, &item) == STREAMLITE_EMPTY, "empty result");
    CHECK(tb_bench_drain(records, TB_BENCH_RECORDS) == 1, "empty return produces request");
    CHECK(strcmp(records[0].operation, "streamlite_queue_get") == 0 && records[0].incomplete == 0, "empty scope complete");
    streamlite_queue_close(&q);
    CHECK(streamlite_close(&db) == STREAMLITE_OK, "close benchmark queue database");
}
#endif

int main(void)
{
    set_dir(DIR);
#ifdef STREAMLITE_BENCHMARK
    test_benchmark_request_boundaries();
#endif
    test_db_only_and_encoding();
    test_output_cap();
    test_allocation_failure();
    test_wrong_database_transaction();
    test_handle_lifetime();
    test_threads();
    test_queue_round_trip();
    printf("\ntest_streamlite: %d checks, %d failed\n", g_run, g_fail);
    if (g_fail == 0) remove_dir();
    return g_fail == 0 ? 0 : 1;
}
