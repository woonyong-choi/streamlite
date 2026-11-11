#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sql/database.h"
#include "sql/result.h"
#include "sql/sql_statement.h"

/*
 * SQL 실행 경계 검사(#272). 재귀 한도 안의 가장 깊은 입력이 작은 스택에서 끝나는지, 기본 키에 NULL을 넣는 UPDATE,
 * 기본 키를 옮기는 UPDATE의 판정과 적용, 식 중간값의 메모리 계상, GROUP BY의 서브쿼리 대응, 유일 색인 오류 문구를 본다.
 * 합성 데이터만 쓰며 DB 파일은 build/ 아래 전용 디렉터리에 만든다.
 */

#define DIR_NAME "build/test_database_bounds_dir"
#define DB_PATH DIR_NAME "/db.streamlite"

/*
 * 깊은 입력을 돌릴 스레드의 스택. 최적화한 빌드는 macOS 보조 스레드 기본값인 512KiB에서 끝나야 한다. 최적화하지 않거나
 * sanitizer를 켠 빌드는 같은 입력에 스택을 몇 배 더 쓰므로(프레임마다 지역 변수 자리와 검사 코드) 주 스레드 기본값인
 * 8MiB로 본다.
 */
#if defined(__SANITIZE_ADDRESS__)
#define STACK_BYTES ((size_t)8 << 20)
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define STACK_BYTES ((size_t)8 << 20)
#endif
#endif
#ifndef STACK_BYTES
#ifdef __OPTIMIZE__
#define STACK_BYTES ((size_t)512 << 10)
#else
#define STACK_BYTES ((size_t)8 << 20)
#endif
#endif

static int g_run = 0;
static int g_fail = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        g_run++;                                          \
        if (!(cond)) {                                    \
            g_fail++;                                     \
            printf("  FAIL (line %d): ", __LINE__);       \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/* ── 파일 보조 ── */
static void clean_dir(void)
{
    DIR *dir = opendir(DIR_NAME);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", DIR_NAME, entry->d_name);
            unlink(path);
        }
        closedir(dir);
    }
    rmdir(DIR_NAME);
}

static database_t *fresh_db(void)
{
    clean_dir();
    mkdir("build", 0700);
    assert(mkdir(DIR_NAME, 0700) == 0);
    database_t *db;
    assert(database_create(DB_PATH, &db) == DATABASE_OK);
    return db;
}

/* ── 결과 표현 ── */
typedef struct {
    char *text;
    size_t len;
    size_t cap;
} buffer_t;

static void put(buffer_t *b, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void put(buffer_t *b, const char *format, ...)
{
    char piece[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(piece, sizeof(piece), format, args);
    va_end(args);
    assert(n >= 0);
    if ((size_t)n >= sizeof(piece)) n = (int)sizeof(piece) - 1;
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->cap + (size_t)n + 1) * 2;
        b->text = realloc(b->text, b->cap);
        assert(b->text != NULL);
    }
    memcpy(b->text + b->len, piece, (size_t)n + 1);
    b->len += (size_t)n;
}

/* ROWS는 "[n] 행/ 행", COMMAND는 "CMD tag n", ERROR는 "ERR 분류 코드 메시지". 호출자가 free한다 */
static char *dump(result_t *result)
{
    buffer_t out = {NULL, 0, 0};
    put(&out, "%s", "");
    result_row_t row;
    switch (result_kind(result)) {
        case RESULT_KIND_COMMAND: {
            const result_command_t *command = result_command(result);
            put(&out, "CMD %.*s %llu", (int)command->tag_len, command->tag, (unsigned long long)command->affected_rows);
            break;
        }
        case RESULT_KIND_ERROR: {
            assert(result_next(result, &row) == RESULT_NEXT_FAILED);
            const result_error_t *error = result_outcome(result).error;
            put(&out, "ERR %d %d %.*s", (int)error->error_class, error->code, (int)error->message_len, error->message);
            break;
        }
        default: {
            buffer_t rows = {NULL, 0, 0};
            put(&rows, "%s", "");
            size_t count = 0;
            result_next_t step;
            while ((step = result_next(result, &row)) == RESULT_NEXT_ROW) {
                put(&rows, "%s", count++ > 0 ? "/ " : " ");
                for (size_t i = 0; i < row.cell_count; i++) {
                    const value_view_t *cell = &row.cells[i];
                    if (i > 0) put(&rows, "|");
                    if (cell->type == VALUE_NULL) put(&rows, "NULL");
                    else if (cell->type == VALUE_INTEGER) put(&rows, "%lld", (long long)cell->integer);
                    else if (cell->type == VALUE_REAL) put(&rows, "%g", cell->real);
                    else if (cell->size_bytes <= 40) put(&rows, "'%.*s'", (int)cell->size_bytes, cell->bytes);
                    else put(&rows, "'%zu바이트'", cell->size_bytes);
                }
            }
            assert(step == RESULT_NEXT_DONE);
            put(&out, "[%zu]%s", count, rows.text);
            free(rows.text);
        }
    }
    return out.text;
}

static char *query_ex(database_t *db, const database_exec_options_t *options, const char *sql)
{
    result_t *result = NULL;
    database_exec_status_t status = database_exec_ex(db, options, (const uint8_t *)sql, strlen(sql), NULL, &result);
    assert(status == DATABASE_EXEC_OK && result != NULL);
    char *text = dump(result);
    result_release(&result);
    return text;
}

static void expect_text(database_t *db, const char *sql, const char *expected)
{
    char *got = query_ex(db, NULL, sql);
    CHECK(strcmp(got, expected) == 0, "%.200s\n      기대: %.300s\n      실제: %.300s", sql, expected, got);
    free(got);
}

/* 오류 결과의 분류·코드가 맞고 메시지에 part가 들어 있다(part가 NULL이면 보지 않는다) */
static void expect_error(database_t *db, const char *sql, result_error_class_t error_class, int code, const char *part)
{
    char *got = query_ex(db, NULL, sql);
    char head[32];
    snprintf(head, sizeof(head), "ERR %d %d ", (int)error_class, code);
    CHECK(strncmp(got, head, strlen(head)) == 0 && (part == NULL || strstr(got, part) != NULL),
          "%.200s\n      기대: %s...%s\n      실제: %.300s", sql, head, part != NULL ? part : "", got);
    free(got);
}

static void exec_ok(database_t *db, const char *sql)
{
    char *got = query_ex(db, NULL, sql);
    CHECK(strncmp(got, "CMD ", 4) == 0, "%.200s -> %.300s", sql, got);
    free(got);
}

/* head + piece×count + middle + tail_piece×count + tail. 호출자가 free한다 */
static char *repeated(const char *head, const char *piece, size_t count, const char *middle, const char *tail_piece,
                      const char *tail)
{
    size_t size = strlen(head) + strlen(middle) + strlen(tail) + count * (strlen(piece) + strlen(tail_piece)) + 1;
    char *sql = malloc(size);
    assert(sql != NULL);
    char *at = sql + sprintf(sql, "%s", head);
    for (size_t i = 0; i < count; i++) at += sprintf(at, "%s", piece);
    at += sprintf(at, "%s", middle);
    for (size_t i = 0; i < count; i++) at += sprintf(at, "%s", tail_piece);
    sprintf(at, "%s", tail);
    return sql;
}

/* sql을 실행하고 해제한다 */
static void expect_text_owned(database_t *db, char *sql, const char *expected)
{
    expect_text(db, sql, expected);
    free(sql);
}

static void expect_limit_owned(database_t *db, char *sql)
{
    expect_error(db, sql, RESULT_E_LIMIT, 15, NULL);
    free(sql);
}

/* ── 재귀 한도와 스택 ── */

/*
 * 질의 levels단계 중첩, 단계마다 원본 sources개 결합, 단계마다 길이 chain의 + 사슬 맨 왼쪽 잎에 다음 단계의
 * 서브쿼리. 서브쿼리는 바깥 결합의 마지막 원본을 보므로 가장 안쪽 결합 단계에서 다시 실행된다.
 */
static void put_nested(buffer_t *sql, size_t level, size_t levels, size_t sources, size_t chain)
{
    put(sql, "SELECT t%zu_%zu.a FROM ", level, sources - 1);
    for (size_t i = 0; i < sources; i++) put(sql, "%st t%zu_%zu", i > 0 ? ", " : "", level, i);
    put(sql, " WHERE 1 = 1");
    for (size_t i = 0; i + 1 < sources; i++) put(sql, " AND t%zu_%zu.a = t%zu_%zu.a", level, i, level, i + 1);
    if (level > 0) put(sql, " AND t%zu_%zu.a = t%zu_%zu.a", level, sources - 1, level - 1, sources - 1);
    if (level < levels) {
        put(sql, " AND (");
        put_nested(sql, level + 1, levels, sources, chain);
        put(sql, ")");
    } else {
        put(sql, " AND t%zu_%zu.a", level, sources - 1);
    }
    for (size_t i = 0; i < chain; i++) put(sql, "+1");
    put(sql, " = %zu", chain + 1);
}

static void *deep_statements(void *arg)
{
    (void)arg;
    database_t *db = fresh_db();
    size_t h = SQLSTMT_MAX_EXPR_HEIGHT, d = SQLSTMT_MAX_EXPR_NESTING, q = SQLSTMT_MAX_QUERY_NESTING;
    size_t m = SQLSTMT_MAX_SOURCES;
    char expected[64];
    exec_ok(db, "CREATE TABLE t (a INTEGER PRIMARY KEY, b TEXT)");
    exec_ok(db, "INSERT INTO t VALUES (1, 'x')");

    /* 한도 안의 가장 깊은 입력은 끝까지 계산된다 */
    snprintf(expected, sizeof(expected), "[1] %zu", h);
    expect_text_owned(db, repeated("SELECT 1", "+1", h - 1, "", "", ""), expected);
    expect_text_owned(db, repeated("SELECT a", "+1", h - 1, " FROM t", "", ""), expected);
    expect_text_owned(db, repeated("SELECT 0", " OR 0", h - 2, " OR a = 1 FROM t", "", ""), "[1] 1");
    expect_text_owned(db, repeated("SELECT 1", " AND 1", h - 2, " AND a = 2 FROM t", "", ""), "[1] 0");
    expect_text_owned(db, repeated("SELECT LENGTH('a'", "||'a'", h - 2, ")", "", ""), "[1] 511");
    expect_text_owned(db, repeated("SELECT a FROM t WHERE a = 1", " AND a = 1", h - 2, "", "", ""), "[1] 1");
    expect_text_owned(db, repeated("SELECT x.a FROM t x JOIN t y ON x.a = y.a", " AND x.a = y.a", h - 2, "", "", ""), "[1] 1");
    {
        char *chain = repeated("a", "+1", h - 3, "", "", "");
        buffer_t sql = {NULL, 0, 0};
        put(&sql, "SELECT ");
        for (int part = 0; part < 3; part++) {
            static const char *const JOINTS[] = {", COUNT(*) FROM t GROUP BY ", " HAVING SUM(", ") > 0"};
            size_t size = strlen(chain);
            sql.text = realloc(sql.text, sql.len + size + 64);
            assert(sql.text != NULL);
            sql.cap = sql.len + size + 64;
            memcpy(sql.text + sql.len, chain, size + 1);
            sql.len += size;
            put(&sql, "%s", JOINTS[part]);
        }
        snprintf(expected, sizeof(expected), "[1] %zu|1", h - 2);
        expect_text(db, sql.text, expected);
        free(sql.text);
        free(chain);
    }
    expect_text_owned(db, repeated("SELECT ", "(", d - 1, "a", ")", " FROM t"), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "ABS(", d - 1, "a", ")", " FROM t"), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "REPLACE(", d - 1, "b", ", 'x', 'x')", " FROM t"), "[1] 'x'");
    expect_text_owned(db, repeated("SELECT ", "COALESCE(", d - 1, "b", ", 'n')", " FROM t"), "[1] 'x'");
    expect_text_owned(db, repeated("SELECT ", "CASE b WHEN 'x' THEN ", d - 1, "b", " ELSE 'n' END", " FROM t"), "[1] 'x'");
    expect_text_owned(db, repeated("SELECT ", "CASE WHEN a = 1 THEN ", d - 1, "b", " END", " FROM t"), "[1] 'x'");
    expect_text_owned(db, repeated("SELECT ", "(a BETWEEN 0 AND ", d / 2 - 1, "1", ")", " FROM t"), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "(b LIKE 'x') = ", 1, "1", "", " FROM t"), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "CAST(", d - 1, "a", " AS INTEGER)", " FROM t"), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "NOT ", d - 1, "a", "", " FROM t"), "[1] 0");
    expect_text_owned(db, repeated("SELECT ", "-(", d / 2 - 1, "a", ")", " FROM t"), "[1] -1");
    expect_text_owned(db, repeated("SELECT ", "(SELECT ", q, "a FROM t", ")", ""), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "EXISTS (SELECT ", q, "a FROM t", ")", ""), "[1] 1");
    expect_text_owned(db, repeated("SELECT ", "1 IN (SELECT ", q, "a FROM t", ")", ""), "[1] 1");
    expect_text_owned(db, repeated("SELECT * FROM ", "(SELECT * FROM ", q, "t", ") s", ""), "[1] 1|'x'");
    expect_text_owned(db, repeated("", "WITH c AS (", q, "SELECT a FROM t", ") SELECT * FROM c", ""), "[1] 1");
    {
        buffer_t sql = {NULL, 0, 0};
        put(&sql, "SELECT COUNT(*) FROM t s0");
        for (size_t i = 1; i < m; i++) put(&sql, " JOIN t s%zu ON s%zu.a = s%zu.a", i, i, i - 1);
        expect_text(db, sql.text, "[1] 1");
        free(sql.text);
    }
    /* 세 한도를 함께 채운다: 질의 q단계, 결합 단계 합 m, 식 높이 h */
    {
        size_t levels = q - 1, sources = m / q;
        size_t chain = h / q - 4;
        buffer_t sql = {NULL, 0, 0};
        put_nested(&sql, 0, levels, sources, chain);
        expect_text(db, sql.text, "[1] 1");
        free(sql.text);
        /* 한 단계 더 깊거나 사슬이 조금만 길어도 실행 전에 거절한다 */
        sql = (buffer_t){NULL, 0, 0};
        put_nested(&sql, 0, levels + 1, sources, 1);
        expect_error(db, sql.text, RESULT_E_LIMIT, 15, NULL);
        free(sql.text);
        sql = (buffer_t){NULL, 0, 0};
        put_nested(&sql, 0, levels, sources, h / q + 1);
        expect_error(db, sql.text, RESULT_E_LIMIT, 15, NULL);
        free(sql.text);
        sql = (buffer_t){NULL, 0, 0};
        put_nested(&sql, 0, levels, sources + 1, 1);
        expect_error(db, sql.text, RESULT_E_LIMIT, 15, NULL);
        free(sql.text);
    }
    /* 변경 문장도 같은 깊이를 계산한다 */
    {
        char *sql = repeated("UPDATE t SET b = 'y' WHERE a", "+1", h - 3, " > 0", "", "");
        expect_text(db, sql, "CMD UPDATE 1");
        free(sql);
        sql = repeated("INSERT INTO t VALUES (2", "+1", h - 1, ", 'z')", "", "");
        expect_text(db, sql, "CMD INSERT 1");
        free(sql);
        sql = repeated("DELETE FROM t WHERE a", "+1", h - 3, " > 5000", "", "");
        expect_text(db, sql, "CMD DELETE 0");
        free(sql);
        sql = repeated("DELETE FROM t WHERE a", "+1", h - 3, " > 5000", "", " RETURNING a");
        expect_text(db, sql, "[0]");
        free(sql);
        snprintf(expected, sizeof(expected), "[2] 1|'y'/ %zu|'z'", h + 1);
        expect_text(db, "SELECT a, b FROM t ORDER BY a", expected);
    }

    /* 한도를 넘는 입력은 스택을 쓰기 전에 LIMIT 오류다. 감사 재현: 괄호 60단계 × 단계마다 +1 400개(48,128바이트) */
    {
        char *level = repeated(")", "+1", 400, "", "", "");
        char *sql = repeated("SELECT ", "(", 60, "1", level, "");
        CHECK(strlen(sql) == 48128, "재현 입력 크기 %zu", strlen(sql));
        expect_limit_owned(db, sql);
        free(level);
    }
    expect_limit_owned(db, repeated("SELECT 1", "+1", h, "", "", ""));
    expect_limit_owned(db, repeated("SELECT 0", " OR 0", h, "", "", ""));
    expect_limit_owned(db, repeated("SELECT a FROM t WHERE a = 1", " AND a = 1", h, "", "", ""));
    expect_limit_owned(db, repeated("SELECT ", "(", d, "1", ")", ""));
    expect_limit_owned(db, repeated("SELECT ", "ABS(", d, "1", ")", ""));
    expect_limit_owned(db, repeated("SELECT ", "(SELECT ", q + 1, "1", ")", ""));
    expect_limit_owned(db, repeated("SELECT * FROM ", "(SELECT * FROM ", q + 1, "t", ") s", ""));
    expect_limit_owned(db, repeated("UPDATE t SET b = 'q' WHERE a", "+1", h, " > 0", "", ""));
    expect_limit_owned(db, repeated("DELETE FROM t WHERE a", "+1", h, " > 0", "", ""));
    /* 큰 입력: 한도의 100배 사슬과 중첩(스택이 아니라 parser의 한도에서 끝난다) */
    expect_limit_owned(db, repeated("SELECT 1", "+1", 100 * h, "", "", ""));
    expect_limit_owned(db, repeated("SELECT ", "(", 100 * d, "1", ")", ""));
    expect_limit_owned(db, repeated("SELECT ", "(SELECT ", 100 * q, "1", ")", ""));
    expect_text(db, "SELECT COUNT(*) FROM t", "[1] 2");
    assert(database_close(db) == DATABASE_OK);
    return NULL;
}

static void test_deep_statements_fit_small_stack(void)
{
    printf("\n[test_deep_statements_fit_small_stack] 스택 %zuKiB\n", STACK_BYTES >> 10);
    pthread_attr_t attr;
    pthread_t thread;
    assert(pthread_attr_init(&attr) == 0);
    assert(pthread_attr_setstacksize(&attr, STACK_BYTES) == 0);
    assert(pthread_create(&thread, &attr, deep_statements, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    pthread_attr_destroy(&attr);
}

/* ── 기본 키에 NULL을 넣는 UPDATE ── */

static void test_update_cannot_null_primary_key(void)
{
    printf("\n[test_update_cannot_null_primary_key]\n");
    database_t *db = fresh_db();
    exec_ok(db, "CREATE TABLE z (id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT)");
    exec_ok(db, "INSERT INTO z (v) VALUES ('a')");
    exec_ok(db, "INSERT INTO z (v) VALUES ('b')");
    expect_error(db, "UPDATE z SET id = NULL WHERE id = 2", RESULT_E_CONSTRAINT, 8, "id");
    expect_text(db, "SELECT * FROM z ORDER BY id", "[2] 1|'a'/ 2|'b'");
    expect_error(db, "UPDATE z SET id = NULL", RESULT_E_CONSTRAINT, 8, "id");
    expect_error(db, "UPDATE z SET v = 'c', id = NULL WHERE v = 'a'", RESULT_E_CONSTRAINT, 8, "id");
    expect_error(db, "UPDATE z SET id = CASE WHEN v = 'a' THEN NULL ELSE id END", RESULT_E_CONSTRAINT, 8, "id");
    expect_error(db, "UPDATE z SET id = NULL RETURNING id", RESULT_E_CONSTRAINT, 8, "id");
    expect_text(db, "SELECT * FROM z ORDER BY id", "[2] 1|'a'/ 2|'b'");
    expect_text(db, "SELECT id FROM z WHERE id IS NULL", "[0]");
    /* INSERT의 NULL은 그대로 자동 번호를 받는다 */
    exec_ok(db, "INSERT INTO z (id, v) VALUES (NULL, 'c')");
    exec_ok(db, "INSERT INTO z VALUES (NULL, 'd'), (NULL, 'e')");
    expect_text(db, "SELECT * FROM z ORDER BY id", "[5] 1|'a'/ 2|'b'/ 3|'c'/ 4|'d'/ 5|'e'");
    /* 기본 키가 아닌 열과 값이 그대로인 기본 키 대입은 막지 않는다 */
    expect_text(db, "UPDATE z SET v = NULL WHERE id = 5", "CMD UPDATE 1");
    expect_text(db, "UPDATE z SET id = id WHERE id = 5", "CMD UPDATE 1");
    expect_text(db, "UPDATE z SET id = NULL WHERE id = 99", "CMD UPDATE 0");
    expect_text(db, "SELECT * FROM z WHERE id = 5", "[1] 5|NULL");

    exec_ok(db, "CREATE TABLE names (k TEXT PRIMARY KEY, v INTEGER)");
    exec_ok(db, "INSERT INTO names VALUES ('a', 1), ('b', 2)");
    expect_error(db, "UPDATE names SET k = NULL WHERE k = 'a'", RESULT_E_CONSTRAINT, 8, "k");
    expect_text(db, "SELECT * FROM names ORDER BY k", "[2] 'a'|1/ 'b'|2");
    assert(database_close(db) == DATABASE_OK);
    assert(database_open(DB_PATH, true, &db) == DATABASE_OK);
    expect_text(db, "SELECT * FROM z ORDER BY id", "[5] 1|'a'/ 2|'b'/ 3|'c'/ 4|'d'/ 5|NULL");
    assert(database_close(db) == DATABASE_OK);
}

/* ── CREATE INDEX 뒤의 토큰 ── */

static void test_create_index_rejects_trailing_tokens(void)
{
    printf("\n[test_create_index_rejects_trailing_tokens]\n");
    database_t *db = fresh_db();
    exec_ok(db, "CREATE TABLE t (n INTEGER, v TEXT)");
    exec_ok(db, "INSERT INTO t VALUES (1, 'a'), (1, 'b')");
    expect_error(db, "CREATE INDEX ix ON t (n) WHERE garbage", RESULT_E_SYNTAX, SQLSTMT_REASON_TRAILING_TOKENS, NULL);
    expect_error(db, "CREATE UNIQUE INDEX ux ON t (v) DROP TABLE t", RESULT_E_SYNTAX, SQLSTMT_REASON_TRAILING_TOKENS, NULL);
    expect_error(db, "CREATE INDEX ix ON t (n) (v)", RESULT_E_SYNTAX, SQLSTMT_REASON_TRAILING_TOKENS, NULL);
    expect_text(db, "SELECT COUNT(*) FROM t", "[1] 2");
    /* 거절한 문장은 색인을 만들지 않았다: 같은 이름으로 다시 만들 수 있다 */
    exec_ok(db, "CREATE INDEX ix ON t (n)");
    exec_ok(db, "CREATE UNIQUE INDEX ux ON t (v)");
    expect_error(db, "CREATE INDEX ix ON t (n)", RESULT_E_SEMANTIC, 4, NULL);
    assert(database_close(db) == DATABASE_OK);
}

/* ── 식 중간값의 메모리 계상 ── */

typedef struct {
    size_t used;
    size_t peak;
    size_t limit;
} budget_t;

static bool budget_reserve(void *ctx, mem_scope_t scope, size_t bytes)
{
    (void)scope;
    budget_t *budget = ctx;
    if (budget->limit != 0 && bytes > budget->limit - budget->used) return false;
    budget->used += bytes;
    if (budget->used > budget->peak) budget->peak = budget->used;
    return true;
}

static void budget_release(void *ctx, mem_scope_t scope, size_t bytes)
{
    (void)scope;
    budget_t *budget = ctx;
    assert(bytes <= budget->used);
    budget->used -= bytes;
}

static const mem_ledger_ops_t BUDGET_OPS = {budget_reserve, budget_release, NULL, NULL};

/* limit 바이트 예산으로 실행한 결과 표현과 최고 계상량. 결과를 놓은 뒤 남은 계상은 0이어야 한다 */
static char *run_budget(database_t *db, const char *sql, size_t limit, size_t *peak)
{
    budget_t budget = {0, 0, limit};
    mem_ledger_t ledger = {&BUDGET_OPS, &budget};
    database_exec_options_t options = {&ledger, NULL, 0};
    char *text = query_ex(db, &options, sql);
    CHECK(budget.used == 0, "%.80s: 문장이 끝난 뒤 계상 %zu바이트가 남음", sql, budget.used);
    if (peak != NULL) *peak = budget.peak;
    return text;
}

static void expect_charged(database_t *db, const char *sql, const char *expected, size_t at_least, size_t tight_limit)
{
    size_t peak = 0;
    char *got = run_budget(db, sql, 0, &peak);
    CHECK(strcmp(got, expected) == 0, "%.120s\n      기대: %s\n      실제: %.200s", sql, expected, got);
    CHECK(peak >= at_least, "%.120s: 최고 계상 %zu바이트, 중간값 %zu바이트 이상을 계상해야 함", sql, peak, at_least);
    free(got);
    /* 중간값보다 작은 예산에서는 예산 오류로 끝난다(값을 만들고 나서가 아니라 만들기 전에 거절한다) */
    got = run_budget(db, sql, tight_limit, &peak);
    char head[32];
    snprintf(head, sizeof(head), "ERR %d %d ", (int)RESULT_E_NOMEM, DATABASE_CODE_BUDGET);
    CHECK(strncmp(got, head, strlen(head)) == 0, "%.120s: 예산 %zu바이트에서 %.200s", sql, tight_limit, got);
    CHECK(peak <= tight_limit, "%.120s: 예산 %zu바이트를 넘겨 %zu바이트를 계상", sql, tight_limit, peak);
    free(got);
}

static void test_expression_intermediates_are_charged(void)
{
    printf("\n[test_expression_intermediates_are_charged]\n");
    database_t *db = fresh_db();
    exec_ok(db, "CREATE TABLE t (a INTEGER PRIMARY KEY, b TEXT)");
    char *seed = repeated("INSERT INTO t VALUES (1, '", "a", 1000, "')", "", "");
    exec_ok(db, seed);
    free(seed);
    size_t mb = (size_t)1 << 20, tight = (size_t)256 << 10;
    /* 1000글자를 10배씩 세 번 늘리면 1,000,000바이트다. 결과는 정수 하나라 결과 계상으로는 드러나지 않는다 */
    expect_charged(db, "SELECT LENGTH(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa')) FROM t",
                   "[1] 1000000", 1000000, tight);
    expect_charged(db, "SELECT LENGTH(LOWER(REPLACE(REPLACE(REPLACE(b, 'a', 'AAAAAAAAAA'), 'A', 'AAAAAAAAAA'), 'A', 'AAAAAAAAAA'))) FROM t",
                   "[1] 1000000", 1000000, tight);
    expect_charged(db, "SELECT LENGTH(UPPER(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'))) FROM t",
                   "[1] 1000000", 1000000, tight);
    expect_charged(db, "SELECT LENGTH(TRIM(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'))) FROM t",
                   "[1] 1000000", 1000000, tight);
    expect_charged(db, "SELECT LENGTH(SUBSTR(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 2)) FROM t",
                   "[1] 999999", 1000000, tight);
    expect_charged(db, "SELECT LENGTH(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa') || b) FROM t",
                   "[1] 1001000", 1001000, tight);
    expect_charged(db, "SELECT LENGTH(CAST(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa') AS BLOB)) FROM t",
                   "[1] 1000000", 1000000, tight);
    expect_charged(db, "SELECT COUNT(*) FROM t WHERE REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa') LIKE 'a%'",
                   "[1] 1", 1000000, tight);
    /* 넉넉한 예산에서는 그대로 계산되고 예산을 넘겨 계상하지 않는다 */
    size_t peak = 0;
    char *got = run_budget(db, "SELECT LENGTH(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa')) FROM t",
                           8 * mb, &peak);
    CHECK(strcmp(got, "[1] 1000000") == 0 && peak <= 8 * mb, "넉넉한 예산: %.100s (최고 %zu)", got, peak);
    free(got);

    /* UPDATE의 SET 식도 같다. 예산에 막히면 행은 그대로다 */
    {
        const char *update = "UPDATE t SET b = SUBSTR(REPLACE(REPLACE(REPLACE(b, 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 'a', 'aaaaaaaaaa'), 1, 3) "
                             "WHERE a = 1";
        got = run_budget(db, update, tight, &peak);
        CHECK(strncmp(got, "ERR 5 24 ", 9) == 0 && peak <= tight, "UPDATE 예산 %zu바이트: %.100s (최고 %zu)", tight, got, peak);
        free(got);
        expect_text(db, "SELECT LENGTH(b) FROM t", "[1] 1000");
        got = run_budget(db, update, 0, &peak);
        CHECK(strcmp(got, "CMD UPDATE 1") == 0 && peak >= 1000000, "UPDATE: %.100s (최고 %zu)", got, peak);
        free(got);
        expect_text(db, "SELECT b FROM t", "[1] 'aaa'");
    }
    /*
     * 컴파일한 프로그램(노드·인자 자리 배열)도 계상한다. 토큰이 둘 많고 컴파일 전에 끝나는 문장(없는 표)과 견주면
     * lexer·parser 몫은 같으므로 차이는 프로그램 몫이다. 노드 1001개는 노드마다 포인터 여덟 개보다 크다.
     */
    {
        size_t compiled = 0, rejected = 0;
        char *valid = repeated("SELECT 1", "+0", 500, "", "", "");
        char *early = repeated("SELECT 1", "+0", 500, " FROM nosuch", "", "");
        got = run_budget(db, valid, 0, &compiled);
        CHECK(strcmp(got, "[1] 1") == 0, "식 500개: %.100s", got);
        free(got);
        got = run_budget(db, early, 0, &rejected);
        CHECK(strncmp(got, "ERR 2 1 ", 8) == 0, "없는 표: %.100s", got);
        free(got);
        CHECK(compiled >= rejected + 1001 * 8 * sizeof(void *) - 4096,
              "프로그램 계상: 컴파일한 문장 %zu바이트, 컴파일 전에 끝난 문장 %zu바이트", compiled, rejected);
        free(valid);
        free(early);
    }
    assert(database_close(db) == DATABASE_OK);
}

/* ── 기본 키를 옮기는 UPDATE ── */

static void fill_keys(database_t *db, const char *table, int count)
{
    for (int from = 1; from <= count; from += 1000) {
        buffer_t sql = {NULL, 0, 0};
        put(&sql, "INSERT INTO %s VALUES ", table);
        for (int i = from; i < from + 1000 && i <= count; i++) put(&sql, "%s(%d, 'v%d')", i > from ? ", " : "", i, i);
        exec_ok(db, sql.text);
        free(sql.text);
    }
}

static void expect_pages_clean(database_t *db, const char *when)
{
    database_page_check_t check;
    database_status_t status = database_check_pages(db, &check);
    CHECK(status == DATABASE_OK && check.orphaned == 0 && check.conflicts == 0, "%s: 상태 %d, 고아 %llu, 충돌 %llu", when,
          (int)status, (unsigned long long)check.orphaned, (unsigned long long)check.conflicts);
}

static void test_primary_key_update_plan_matches_apply(void)
{
    printf("\n[test_primary_key_update_plan_matches_apply]\n");
    database_t *db = fresh_db();
    int n = 6000;
    char expected[128];
    exec_ok(db, "CREATE TABLE k (id INTEGER PRIMARY KEY, v TEXT)");
    fill_keys(db, "k", n);
    /* 모든 행의 키를 옆 행의 옛 키로 옮긴다. 끝난 결과가 유일하면 허용이고, 허용한 것은 실제로 적용된다 */
    snprintf(expected, sizeof(expected), "CMD UPDATE %d", n);
    expect_text(db, "UPDATE k SET id = id + 1", expected);
    snprintf(expected, sizeof(expected), "[1] %d|2|%d|%lld", n, n + 1, (long long)n * (n + 3) / 2);
    expect_text(db, "SELECT COUNT(*), MIN(id), MAX(id), SUM(id) FROM k", expected);
    expect_text(db, "SELECT v FROM k WHERE id = 2", "[1] 'v1'");
    snprintf(expected, sizeof(expected), "[1] 'v%d'", n);
    char probe[96];
    snprintf(probe, sizeof(probe), "SELECT v FROM k WHERE id = %d", n + 1);
    expect_text(db, probe, expected);
    expect_text(db, "SELECT id FROM k WHERE id = 1", "[0]");
    snprintf(expected, sizeof(expected), "CMD UPDATE %d", n);
    expect_text(db, "UPDATE k SET id = id - 1", expected);
    snprintf(expected, sizeof(expected), "[1] %d|1|%d|%lld", n, n, (long long)n * (n + 1) / 2);
    expect_text(db, "SELECT COUNT(*), MIN(id), MAX(id), SUM(id) FROM k", expected);
    expect_pages_clean(db, "전체 이동 뒤");

    /* 두 행 맞바꾸기와 세 행 돌리기 */
    expect_text(db, "UPDATE k SET id = 3 - id WHERE id <= 2", "CMD UPDATE 2");
    expect_text(db, "SELECT id, v FROM k WHERE id <= 2 ORDER BY id", "[2] 1|'v2'/ 2|'v1'");
    expect_text(db, "UPDATE k SET id = CASE id WHEN 10 THEN 11 WHEN 11 THEN 12 ELSE 10 END WHERE id BETWEEN 10 AND 12",
                "CMD UPDATE 3");
    expect_text(db, "SELECT id, v FROM k WHERE id BETWEEN 10 AND 12 ORDER BY id", "[3] 10|'v12'/ 11|'v10'/ 12|'v11'");
    /* 일부만 옮기되 비운 자리로 들어가는 경우와 멀리 옮기는 경우 */
    expect_text(db, "UPDATE k SET id = id + 1 WHERE id >= 5990", "CMD UPDATE 11");
    expect_text(db, "SELECT MIN(id), MAX(id), COUNT(*) FROM k WHERE id >= 5990", "[1] 5991|6001|11");
    expect_text(db, "UPDATE k SET id = id + 100000 WHERE id BETWEEN 100 AND 199", "CMD UPDATE 100");
    expect_text(db, "SELECT COUNT(*), MIN(id) FROM k WHERE id > 100000", "[1] 100|100100");
    expect_text(db, "UPDATE k SET id = id - 100000 WHERE id > 100000", "CMD UPDATE 100");
    /* RETURNING은 바뀐 뒤의 행이다(행 순서는 약속하지 않으므로 한 행씩 본다) */
    expect_text(db, "UPDATE k SET id = 41 - id WHERE id BETWEEN 20 AND 21 RETURNING id, v", "[2] 21|'v20'/ 20|'v21'");
    expect_text(db, "SELECT id, v FROM k WHERE id BETWEEN 20 AND 21 ORDER BY id", "[2] 20|'v21'/ 21|'v20'");
    expect_text(db, "UPDATE k SET id = 41 - id WHERE id = 20 OR id = 21", "CMD UPDATE 2");
    expect_text(db, "SELECT id, v FROM k WHERE id BETWEEN 20 AND 21 ORDER BY id", "[2] 20|'v20'/ 21|'v21'");

    /* 거절할 것은 적용 전에 거절하고 표는 그대로다 */
    expect_error(db, "UPDATE k SET id = id + 1 WHERE id = 500", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "UPDATE k SET id = id + 1 WHERE id BETWEEN 500 AND 600 AND id <> 550", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "UPDATE k SET id = 7 WHERE id IN (20, 21)", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "UPDATE k SET id = 30 WHERE id IN (30, 31)", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "UPDATE k SET id = id / 2 WHERE id BETWEEN 40 AND 41", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_text(db, "SELECT id, v FROM k WHERE id IN (20, 21, 30, 31, 500, 501, 550) ORDER BY id",
                "[7] 20|'v20'/ 21|'v21'/ 30|'v30'/ 31|'v31'/ 500|'v500'/ 501|'v501'/ 550|'v550'");
    snprintf(expected, sizeof(expected), "[1] %d", n);
    expect_text(db, "SELECT COUNT(*) FROM k", expected);
    expect_pages_clean(db, "거절 뒤");

    /* 유일 보조 색인이 있어도 같다 */
    exec_ok(db, "CREATE UNIQUE INDEX k_v ON k (v)");
    exec_ok(db, "CREATE TABLE names (k TEXT PRIMARY KEY, n INTEGER)");
    exec_ok(db, "INSERT INTO names VALUES ('a', 1), ('b', 2), ('c', 3)");
    expect_error(db, "UPDATE k SET id = id + 1 WHERE id BETWEEN 1000 AND 1999", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_text(db, "UPDATE k SET id = id + 1 WHERE id >= 1000", "CMD UPDATE 5001");
    expect_text(db, "SELECT id FROM k WHERE v = 'v1000'", "[1] 1001");
    expect_text(db, "SELECT v FROM k WHERE id = 1001", "[1] 'v1000'");
    expect_text(db, "UPDATE names SET k = CASE k WHEN 'a' THEN 'b' WHEN 'b' THEN 'c' ELSE 'a' END", "CMD UPDATE 3");
    expect_text(db, "SELECT k, n FROM names ORDER BY k", "[3] 'a'|3/ 'b'|1/ 'c'|2");
    expect_error(db, "UPDATE names SET k = 'b' WHERE k = 'a'", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_text(db, "UPDATE names SET k = k || 'x'", "CMD UPDATE 3");
    expect_text(db, "SELECT k, n FROM names ORDER BY k", "[3] 'ax'|3/ 'bx'|1/ 'cx'|2");
    expect_pages_clean(db, "색인 있는 이동 뒤");
    assert(database_close(db) == DATABASE_OK);
    assert(database_open(DB_PATH, true, &db) == DATABASE_OK);
    snprintf(expected, sizeof(expected), "[1] %d|1|%d", n, n + 2);
    expect_text(db, "SELECT COUNT(*), MIN(id), MAX(id) FROM k", expected);
    expect_text(db, "SELECT id FROM k WHERE v = 'v1000'", "[1] 1001");
    expect_text(db, "SELECT k, n FROM names ORDER BY k", "[3] 'ax'|3/ 'bx'|1/ 'cx'|2");
    expect_pages_clean(db, "재열기 뒤");
    assert(database_close(db) == DATABASE_OK);
}

/* ── GROUP BY의 서브쿼리 대응 ── */

static void test_group_by_does_not_match_other_subquery(void)
{
    printf("\n[test_group_by_does_not_match_other_subquery]\n");
    database_t *db = fresh_db();
    exec_ok(db, "CREATE TABLE t (a INTEGER PRIMARY KEY, b TEXT)");
    exec_ok(db, "INSERT INTO t VALUES (1, 'x'), (2, 'y'), (3, 'x')");
    /* 내용이 다른 서브쿼리를 GROUP BY 식의 값으로 바꿔 읽지 않는다 */
    expect_text(db, "SELECT (SELECT 1), COUNT(*) FROM t GROUP BY (SELECT 2)", "[1] 1|3");
    expect_text(db, "SELECT (SELECT 2), COUNT(*) FROM t GROUP BY (SELECT 2)", "[1] 2|3");
    expect_text(db, "SELECT EXISTS (SELECT 1 FROM t WHERE a > 100), COUNT(*) FROM t GROUP BY EXISTS (SELECT 1 FROM t)", "[1] 0|3");
    expect_text(db, "SELECT COUNT(*) FROM t GROUP BY (SELECT 2) HAVING (SELECT 1) = 1", "[1] 3");
    expect_text(db, "SELECT COUNT(*) FROM t GROUP BY (SELECT 2) HAVING (SELECT 1) = 2", "[0]");
    expect_text(db, "SELECT COUNT(*) FROM t GROUP BY (SELECT 2) ORDER BY (SELECT 1)", "[1] 3");
    /* 묶지 않은 열을 쓰는 식은 서브쿼리가 달라도 같아도 그룹 값이 아니다 */
    expect_error(db, "SELECT a IN (SELECT 5), COUNT(*) FROM t GROUP BY a IN (SELECT a FROM t)", RESULT_E_SEMANTIC,
                 DATABASE_CODE_NOT_GROUPED, "a");
    expect_error(db, "SELECT b, (SELECT COUNT(*) FROM t u WHERE u.b = t.b) FROM t GROUP BY (SELECT COUNT(*) FROM t u WHERE u.a = t.a)",
                 RESULT_E_SEMANTIC, DATABASE_CODE_NOT_GROUPED, "b");
    /* 서브쿼리가 없는 식의 대응은 그대로다 */
    expect_text(db, "SELECT b, COUNT(*) FROM t GROUP BY b ORDER BY b", "[2] 'x'|2/ 'y'|1");
    expect_text(db, "SELECT a % 2, COUNT(*) FROM t GROUP BY a % 2 ORDER BY 1", "[2] 0|1/ 1|2");
    expect_text(db, "SELECT b IN ('x'), COUNT(*) FROM t GROUP BY b IN ('x') ORDER BY 1", "[2] 0|1/ 1|2");
    assert(database_close(db) == DATABASE_OK);
}

/* ── 유일 색인 위반과 기본 키 중복의 문구 ── */

static void test_unique_index_error_names_the_index(void)
{
    printf("\n[test_unique_index_error_names_the_index]\n");
    database_t *db = fresh_db();
    exec_ok(db, "CREATE TABLE u (id INTEGER PRIMARY KEY, email TEXT)");
    exec_ok(db, "INSERT INTO u VALUES (1, 'a@x'), (2, 'b@x')");
    exec_ok(db, "CREATE UNIQUE INDEX u_email ON u (email)");
    char *got = query_ex(db, NULL, "INSERT INTO u VALUES (3, 'a@x')");
    CHECK(strncmp(got, "ERR 3 11 ", 9) == 0 && strstr(got, "유일 색인") != NULL && strstr(got, "기본 키") == NULL,
          "유일 색인 위반(INSERT): %s", got);
    free(got);
    got = query_ex(db, NULL, "UPDATE u SET email = 'a@x' WHERE id = 2");
    CHECK(strncmp(got, "ERR 3 11 ", 9) == 0 && strstr(got, "유일 색인") != NULL && strstr(got, "기본 키") == NULL,
          "유일 색인 위반(UPDATE): %s", got);
    free(got);
    got = query_ex(db, NULL, "INSERT INTO u VALUES (3, 'c@x'), (4, 'c@x')");
    CHECK(strncmp(got, "ERR 3 11 ", 9) == 0 && strstr(got, "유일 색인") != NULL && strstr(got, "기본 키") == NULL,
          "유일 색인 위반(한 문장의 두 행): %s", got);
    free(got);
    /* 기본 키 중복은 기본 키라고 말한다 */
    expect_error(db, "INSERT INTO u VALUES (1, 'z@x')", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "INSERT INTO u VALUES (5, 'e@x'), (5, 'f@x')", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_error(db, "UPDATE u SET id = 1 WHERE id = 2", RESULT_E_CONSTRAINT, 11, "기본 키");
    expect_text(db, "SELECT * FROM u ORDER BY id", "[2] 1|'a@x'/ 2|'b@x'");
    assert(database_close(db) == DATABASE_OK);
}

/* ── SUBSTR 경계(SQLite와 같은 의미. 격자 대조는 test_database_sqlite에 있다) ── */

static void test_substr_bounds(void)
{
    printf("\n[test_substr_bounds]\n");
    database_t *db = fresh_db();
    expect_text(db, "SELECT SUBSTR('hello', -5, 3), SUBSTR('hello', 0, 0), SUBSTR('hello', 0, 3), SUBSTR('hello', -2)",
                "[1] 'hel'|''|'he'|'lo'");
    expect_text(db, "SELECT SUBSTR('hello', 2, -1), SUBSTR('hello', 4, -9), SUBSTR('hello', -1, -2), SUBSTR('hello', 9, 2)",
                "[1] 'h'|'hel'|'ll'|''");
    expect_text(db, "SELECT SUBSTR('hello', -9, 5), SUBSTR('hello', -9), SUBSTR('hello', 6), SUBSTR('hello', 5, 9)",
                "[1] 'h'|'hello'|''|'o'");
    expect_text(db, "SELECT SUBSTR('가나다라', -2), SUBSTR('가나다라', 3, -2), SUBSTR('', 1, 1), SUBSTR('가나다라', 0, 2)",
                "[1] '다라'|'가나'|''|'가'");
    expect_text(db, "SELECT SUBSTR('hello', 9223372036854775807, 9223372036854775807), "
                    "SUBSTR('hello', -9223372036854775807, 9223372036854775807), SUBSTR('hello', 2, 9223372036854775807)",
                "[1] ''|'hello'|'ello'");
    assert(database_close(db) == DATABASE_OK);
}

/* 인자가 있으면 이름에 그 글자가 든 검사만 돌린다(하나가 죽어도 나머지를 따로 볼 수 있게) */
int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*run)(void);
    } TESTS[] = {
        {"deep_statements_fit_small_stack", test_deep_statements_fit_small_stack},
        {"update_cannot_null_primary_key", test_update_cannot_null_primary_key},
        {"create_index_rejects_trailing_tokens", test_create_index_rejects_trailing_tokens},
        {"substr_bounds", test_substr_bounds},
        {"expression_intermediates_are_charged", test_expression_intermediates_are_charged},
        {"primary_key_update_plan_matches_apply", test_primary_key_update_plan_matches_apply},
        {"group_by_does_not_match_other_subquery", test_group_by_does_not_match_other_subquery},
        {"unique_index_error_names_the_index", test_unique_index_error_names_the_index},
    };
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (argc < 2 || strstr(TESTS[i].name, argv[1]) != NULL) TESTS[i].run();
    }
    clean_dir();
    printf("\n%s: %d/%d\n", g_fail == 0 ? "ALL PASSED" : "FAILED", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
