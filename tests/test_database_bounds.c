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
    };
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (argc < 2 || strstr(TESTS[i].name, argv[1]) != NULL) TESTS[i].run();
    }
    clean_dir();
    printf("\n%s: %d/%d\n", g_fail == 0 ? "ALL PASSED" : "FAILED", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
