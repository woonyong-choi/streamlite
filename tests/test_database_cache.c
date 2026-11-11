#include <assert.h>
#include <errno.h>
#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sql/database_txn.h"
#include "../src/sql/database_cache.h"
#include "../src/sql/database_internal.h"
#include "alloc_audit.h"

#define DB_PATH "build/test_database_cache_dir/cache.db"

typedef struct {
    size_t used[3];
    size_t limit;
    bool deny_resident;
} budget_t;

static bool reserve(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *b = ctx;
    size_t total = b->used[0] + b->used[1] + b->used[2];
    if ((scope == MEM_SCOPE_RESIDENT && b->deny_resident) || bytes > b->limit - total) return false;
    b->used[scope] += bytes;
    return true;
}

static void release(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *b = ctx;
    assert(bytes <= b->used[scope]);
    b->used[scope] -= bytes;
}

static const mem_ledger_ops_t OPS = {reserve, release, NULL, NULL};

static void on_allocate(void *ctx, size_t size, size_t live)
{
    (void)size;
    budget_t *b = ctx;
    assert(live <= b->used[0] + b->used[1] + b->used[2]);
}

static database_t *open_database(const mem_ledger_t *ledger)
{
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    database_t *db = NULL;
    assert(database_create_bounded(DB_PATH, ledger, &db) == DATABASE_OK);
    database_cache_set_enabled(db, true);
    return db;
}

static void build_flag_controls_default(void)
{
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    database_t *db = NULL;
    assert(database_create(DB_PATH, &db) == DATABASE_OK);
    result_t *result = NULL;
    const uint8_t sql[] = "SELECT 7";
    assert(database_exec(db, sql, sizeof(sql) - 1, NULL, &result) == DATABASE_EXEC_OK);
    assert(result_kind(result) == RESULT_KIND_ROWS);
    result_release(&result);
    database_cache_stats_t stats;
    database_cache_stats(db, &stats);
    assert(stats.entries == (STREAMLITE_SQL_TEMPLATE_CACHE ? 1u : 0u));
    assert(database_close(db) == DATABASE_OK && audit_live == 0);
}

static result_t *execute(database_t *db, const char *sql, const lexer_binding_t *binding,
                          const database_exec_options_t *opts)
{
    result_t *result = NULL;
    database_exec_status_t status = database_exec_ex(db, opts, (const uint8_t *)sql, strlen(sql), binding, &result);
    assert(status == DATABASE_EXEC_OK && result != NULL);
    return result;
}

static void command(database_t *db, const char *sql)
{
    result_t *result = execute(db, sql, NULL, NULL);
    assert(result_kind(result) == RESULT_KIND_COMMAND);
    result_release(&result);
}

static int64_t integer(database_t *db, const char *sql)
{
    result_t *result = execute(db, sql, NULL, NULL);
    result_row_t row;
    assert(result_next(result, &row) == RESULT_NEXT_ROW);
    assert(row.cell_count == 1 && row.cells[0].type == VALUE_INTEGER);
    int64_t value = row.cells[0].integer;
    assert(result_next(result, &row) == RESULT_NEXT_DONE);
    result_release(&result);
    return value;
}

static void bind_sqlite(sqlite3_stmt *stmt, const lexer_binding_t *binding)
{
    if (binding == NULL) return;
    for (uint32_t i = 0; i < binding->count; i++) {
        const value_view_t *v = &binding->values[i];
        int code;
        switch (v->type) {
            case VALUE_NULL: code = sqlite3_bind_null(stmt, (int)i + 1); break;
            case VALUE_INTEGER: code = sqlite3_bind_int64(stmt, (int)i + 1, v->integer); break;
            case VALUE_REAL: code = sqlite3_bind_double(stmt, (int)i + 1, v->real); break;
            case VALUE_TEXT:
                code = sqlite3_bind_text(stmt, (int)i + 1, (const char *)v->bytes, (int)v->size_bytes, SQLITE_TRANSIENT);
                break;
            case VALUE_BLOB:
                code = sqlite3_bind_blob(stmt, (int)i + 1, v->bytes, (int)v->size_bytes, SQLITE_TRANSIENT);
                break;
            default: abort();
        }
        assert(code == SQLITE_OK);
    }
}

static void compare_cell(const value_view_t *cell, sqlite3_stmt *stmt, int column)
{
    switch (cell->type) {
        case VALUE_NULL: assert(sqlite3_column_type(stmt, column) == SQLITE_NULL); break;
        case VALUE_INTEGER:
            assert(sqlite3_column_type(stmt, column) == SQLITE_INTEGER);
            assert(cell->integer == sqlite3_column_int64(stmt, column));
            break;
        case VALUE_REAL:
            assert(sqlite3_column_type(stmt, column) == SQLITE_FLOAT);
            assert(cell->real == sqlite3_column_double(stmt, column));
            break;
        case VALUE_TEXT:
        case VALUE_BLOB: {
            assert(sqlite3_column_type(stmt, column) == (cell->type == VALUE_TEXT ? SQLITE_TEXT : SQLITE_BLOB));
            const void *bytes = cell->type == VALUE_TEXT ? (const void *)sqlite3_column_text(stmt, column)
                                                         : sqlite3_column_blob(stmt, column);
            assert(cell->size_bytes == (size_t)sqlite3_column_bytes(stmt, column));
            assert(cell->size_bytes == 0 || memcmp(cell->bytes, bytes, cell->size_bytes) == 0);
            break;
        }
    }
}

static void compare_sqlite(database_t *db, sqlite3 *reference, const char *sql, const lexer_binding_t *binding)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(reference, sql, -1, &stmt, NULL) == SQLITE_OK);
    bind_sqlite(stmt, binding);
    result_t *result = execute(db, sql, binding, NULL);
    if (result_kind(result) == RESULT_KIND_ERROR) {
        fprintf(stderr, "unexpected native error: %s\n%s\n", sql, result_outcome(result).error->message);
        abort();
    }
    if (result_kind(result) == RESULT_KIND_COMMAND) assert(sqlite3_step(stmt) == SQLITE_DONE);
    else {
        result_row_t row;
        while (result_next(result, &row) == RESULT_NEXT_ROW) {
            assert(sqlite3_step(stmt) == SQLITE_ROW);
            assert(row.cell_count == (size_t)sqlite3_column_count(stmt));
            for (size_t i = 0; i < row.cell_count; i++) compare_cell(&row.cells[i], stmt, (int)i);
        }
        assert(result_outcome(result).state == RESULT_STATE_COMPLETE);
        assert(sqlite3_step(stmt) == SQLITE_DONE);
    }
    result_release(&result);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

static void binding_and_syntax_match_sqlite(void)
{
    database_t *db = open_database(NULL);
    sqlite3 *reference = NULL;
    assert(sqlite3_open(":memory:", &reference) == SQLITE_OK);
    compare_sqlite(db, reference, "CREATE TABLE t(id INTEGER PRIMARY KEY, n INTEGER, s TEXT)", NULL);
    compare_sqlite(db, reference, "INSERT INTO t VALUES (1,10,'alpha'),(2,20,'beta'),(3,30,'gamma')", NULL);
    const char *queries[] = {
        "SELECT ?1, ?1, ?2, NULL, 'sql literal', X'0001'",
        "SELECT CASE WHEN ?1 BETWEEN 0 AND 50 THEN CAST(?1 AS REAL) ELSE 0 END, -?1, (?1+2)*3",
        "WITH c(x) AS (SELECT ?1) SELECT x FROM c UNION ALL SELECT ?1+1 ORDER BY 1 DESC LIMIT 2 OFFSET 0",
        "SELECT a.id, b.n FROM t AS a LEFT JOIN t AS b ON a.id=b.id WHERE a.n>=?1 ORDER BY a.id DESC",
        "SELECT n/10, count(*), sum(n), max(n) FROM t WHERE n>=?1 GROUP BY n/10 HAVING count(*)>0 ORDER BY 1",
        "SELECT id FROM t WHERE id IN (SELECT id FROM t WHERE n>=?1) AND EXISTS (SELECT id FROM t WHERE id=1) ORDER BY id",
        "SELECT id FROM t WHERE s LIKE ?1 ESCAPE '!' OR id IN (2,3) ORDER BY id",
        "SELECT id,(SELECT n FROM t AS b WHERE b.id=a.id)+?1 FROM t AS a ORDER BY id",
        "SELECT DISTINCT n FROM (SELECT n FROM t UNION SELECT ?1) AS c ORDER BY n LIMIT ?2",
        "INSERT INTO t VALUES (?1,?2,'new') ON CONFLICT(id) DO UPDATE SET n=excluded.n WHERE t.n<>excluded.n RETURNING id,n",
        "UPDATE t SET n=?2 WHERE id=?1 RETURNING id,n",
        "DELETE FROM t WHERE id=?1 AND n=?2 RETURNING id"
    };
    for (size_t q = 0; q < sizeof(queries) / sizeof(queries[0]); q++) {
        value_view_t values[] = {{VALUE_INTEGER, 4, 0, NULL, 0}, {VALUE_INTEGER, 45, 0, NULL, 0}};
        lexer_binding_t binding = {values, q == 0 || q >= 8 ? 2 : 1};
        if (q == 6) values[0] = (value_view_t){VALUE_TEXT, 0, 0, (const uint8_t *)"a%", 2};
        for (int run = 0; run < 2; run++) {
            if (run != 0 && q != 6) values[0].integer = q >= 9 ? 5 : 9;
            compare_sqlite(db, reference, queries[q], &binding);
        }
    }
    value_view_t types[] = {
        {VALUE_NULL, 0, 0, NULL, 0}, {VALUE_INTEGER, 7, 0, NULL, 0},
        {VALUE_REAL, 0, 9.5, NULL, 0}, {VALUE_TEXT, 0, 0, (const uint8_t *)"h\xc3\xa9", 3},
        {VALUE_BLOB, 0, 0, (const uint8_t *)"\0\xff", 2},
        {VALUE_TEXT, 0, 0, (const uint8_t *)"", 0}
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        lexer_binding_t binding = {&types[i], 1};
        compare_sqlite(db, reference, "SELECT ?1,?1,'literal'", &binding);
    }
    value_view_t delta = {VALUE_INTEGER, 1, 0, NULL, 0};
    lexer_binding_t increment = {&delta, 1};
    for (int run = 0; run < 2; run++) {
        compare_sqlite(db, reference, "UPDATE t SET n=n+?1 WHERE id=1 RETURNING n", &increment);
    }
    uint8_t large[32768];
    memset(large, 'a', sizeof(large));
    value_view_t text = {VALUE_TEXT, 0, 0, large, sizeof(large)};
    lexer_binding_t large_binding = {&text, 1};
    database_cache_stats_t before_large, after_large, after_hit;
    database_cache_stats(db, &before_large);
    compare_sqlite(db, reference, "SELECT ?1 AS current_value", &large_binding);
    database_cache_stats(db, &after_large);
    assert(after_large.bytes - before_large.bytes < sizeof(large));
    memset(large, 'b', sizeof(large));
    compare_sqlite(db, reference, "SELECT ?1 AS current_value", &large_binding);
    database_cache_stats(db, &after_hit);
    assert(after_hit.bytes == after_large.bytes);
    database_cache_stats_t stats;
    database_cache_stats(db, &stats);
    assert(stats.hits >= 17 && stats.misses > 0);
    assert(database_close(db) == DATABASE_OK);
    assert(sqlite3_close(reference) == SQLITE_OK);
    assert(audit_live == 0);
}

typedef struct {
    result_error_class_t kind;
    int code;
    char message[256];
} error_snapshot_t;

static error_snapshot_t error_result(database_t *db, const char *sql, const lexer_binding_t *binding)
{
    result_t *result = execute(db, sql, binding, NULL);
    assert(result_kind(result) == RESULT_KIND_ERROR);
    result_error_t error = *result_outcome(result).error;
    error_snapshot_t snapshot = {.kind = error.error_class, .code = error.code};
    assert(error.message_len < sizeof(snapshot.message));
    memcpy(snapshot.message, error.message, error.message_len);
    result_release(&result);
    return snapshot;
}

static bool always_cancel(void *ctx) { (void)ctx; return true; }

static void invalid_binding_and_input_preserve_errors(void)
{
    database_t *db = open_database(NULL);
    const char *sql = "SELECT ?2,?1,?2";
    value_view_t values[] = {{VALUE_INTEGER, 7, 0, NULL, 0}, {VALUE_INTEGER, 9, 0, NULL, 0}};
    lexer_binding_t good = {values, 2};
    value_view_t invalid[] = {
        {VALUE_REAL, 0, NAN, NULL, 0}, {VALUE_TEXT, 0, 0, (const uint8_t *)"\xff", 1},
        {(value_type_t)99, 0, 0, NULL, 0}, {VALUE_INTEGER, 1, 0, NULL, 1},
        {VALUE_BLOB, 0, 0, (const uint8_t *)"bad", SIZE_MAX},
        {VALUE_TEXT, 0, 0, (const uint8_t *)"bad", (size_t)VALUE_MAX_BYTES + 1}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        value_view_t bad_values[] = {invalid[i], invalid[i]};
        lexer_binding_t bad = {bad_values, 2};
        database_cache_set_enabled(db, false);
        error_snapshot_t before = error_result(db, sql, &bad);
        database_cache_set_enabled(db, true);
        result_t *warm = execute(db, sql, &good, NULL);
        result_release(&warm);
        error_snapshot_t after = error_result(db, sql, &bad);
        assert(before.kind == after.kind && before.code == after.code);
        assert(strcmp(before.message, after.message) == 0);
    }
    lexer_binding_t wrong_count = {values, 1}, missing_values = {NULL, 2};
    const lexer_binding_t *bad_counts[] = {NULL, &wrong_count, &missing_values};
    for (size_t i = 0; i < 3; i++) {
        error_snapshot_t hit = error_result(db, sql, bad_counts[i]);
        database_cache_set_enabled(db, false);
        error_snapshot_t miss = error_result(db, sql, bad_counts[i]);
        assert(hit.kind == miss.kind && hit.code == miss.code && strcmp(hit.message, miss.message) == 0);
        database_cache_set_enabled(db, true);
        result_t *warm = execute(db, sql, &good, NULL);
        result_release(&warm);
    }
    char mutable[] = "SELECT 7";
    assert(integer(db, mutable) == 7);
    mutable[7] = '9';
    assert(integer(db, mutable) == 9);
    const char *bad_sql[] = {"SELECT '\xff'", "SELECT 7; SELECT 9", "SELECT ?0", "SELECT ?1,?3"};
    for (size_t i = 0; i < 4; i++) (void)error_result(db, bad_sql[i], NULL);
    database_cancel_t cancel = {always_cancel, NULL};
    database_exec_options_t opts = {.cancel = &cancel};
    result_t *cancelled = execute(db, "SELECT 9", NULL, &opts);
    assert(result_outcome(cancelled).error->error_class == RESULT_E_CANCELLED);
    result_release(&cancelled);
    assert(database_close(db) == DATABASE_OK && audit_live == 0);
}

static void schema_eviction_and_result_lifetime(void)
{
    database_t *db = open_database(NULL);
    command(db, "CREATE TABLE t(x INTEGER)");
    command(db, "INSERT INTO t VALUES (7)");
    assert(integer(db, "SELECT x FROM t") == 7);
    assert(integer(db, "SELECT x FROM t") == 7);
    command(db, "DROP TABLE t");
    command(db, "CREATE TABLE t(y INTEGER,x INTEGER)");
    command(db, "INSERT INTO t VALUES (1,9)");
    assert(integer(db, "SELECT x FROM t") == 9);
    database_t *other = NULL;
    assert(database_open(DB_PATH, true, &other) == DATABASE_OK);
    command(other, "CREATE INDEX ix ON t(x)");
    assert(integer(db, "SELECT x FROM t") == 9);
    command(other, "UPDATE t SET x=11");
    assert(integer(db, "SELECT x FROM t") == 11);
    command(other, "DROP TABLE t");
    command(other, "CREATE TABLE t(x INTEGER,z INTEGER)");
    command(other, "INSERT INTO t VALUES (13,3)");
    assert(integer(db, "SELECT x FROM t") == 13);
    assert(database_close(other) == DATABASE_OK);

    uint8_t payload[] = "retained";
    value_view_t value = {VALUE_TEXT, 0, 0, payload, sizeof(payload) - 1};
    lexer_binding_t binding = {&value, 1};
    result_t *held = execute(db, "SELECT ?1", &binding, NULL);
    memset(payload, 'x', sizeof(payload));
    for (int i = 0; i < 70; i++) {
        char sql[64];
        snprintf(sql, sizeof(sql), "SELECT %d", i);
        assert(integer(db, sql) == i);
    }
    database_cache_stats_t stats;
    database_cache_stats(db, &stats);
    assert(stats.entries == DATABASE_CACHE_MAX_ENTRIES && stats.evictions > 0);
    assert(stats.bytes <= DATABASE_CACHE_MAX_BYTES && stats.invalidations >= 4);
    database_cache_entry_t *pinned = database_cache_find(db, (const uint8_t *)"SELECT 69", 9);
    assert(pinned != NULL);
    database_cache_clear(db);
    database_cache_stats(db, &stats);
    assert(stats.entries == 1);
    statement_clone_t clone = {.binding = &(lexer_binding_t){NULL, 0}, .limit_bytes = SIZE_MAX};
    sqlstmt_t *copy = NULL;
    assert(database_cache_instantiate(pinned, &clone, &copy) == SQLSTMT_OK);
    sqlstmt_free(copy);
    database_cache_unpin(db, pinned);
    database_cache_stats(db, &stats);
    assert(stats.entries == 0);
    for (int run = 0; run < 60; run++) {
        char sql[2048] = "SELECT 0";
        size_t len = strlen(sql);
        for (int col = 1; col < 120; col++) len += (size_t)snprintf(sql + len, sizeof(sql) - len, ",%d", col);
        snprintf(sql + len, sizeof(sql) - len, " /*%d*/", run);
        result_t *wide = execute(db, sql, NULL, NULL);
        result_row_t cells;
        assert(result_next(wide, &cells) == RESULT_NEXT_ROW && cells.cell_count == 120);
        for (int col = 0; col < 120; col++) assert(cells.cells[col].integer == col);
        result_release(&wide);
    }
    database_cache_stats(db, &stats);
    assert(stats.entries < 60 && stats.bytes <= DATABASE_CACHE_MAX_BYTES);
    char *large = malloc(DATABASE_CACHE_MAX_SQL + 32);
    assert(large != NULL);
    memset(large, ' ', DATABASE_CACHE_MAX_SQL + 16);
    memcpy(large, "SELECT 17", 9);
    large[DATABASE_CACHE_MAX_SQL + 16] = 0;
    assert(integer(db, large) == 17);
    free(large);
    assert(database_close(db) == DATABASE_OK);
    result_row_t row;
    assert(result_next(held, &row) == RESULT_NEXT_ROW);
    assert(row.cells[0].size_bytes == 8 && memcmp(row.cells[0].bytes, "retained", 8) == 0);
    result_release(&held);
    assert(audit_live == 0);
}

static void budgeted_calls_keep_scope_ownership(void)
{
    budget_t budget = {.limit = SIZE_MAX};
    mem_ledger_t ledger = {&OPS, &budget};
    audit_on_alloc = on_allocate;
    audit_ctx = &budget;
    database_t *db = open_database(&ledger);
    database_exec_options_t opts = {.ledger = &ledger};
    size_t before = database_resident_bytes(db);
    for (int i = 0; i < 3; i++) {
        result_t *result = execute(db, "SELECT 7", NULL, &opts);
        result_release(&result);
        assert(budget.used[MEM_SCOPE_QUERY] == 0);
        assert(budget.used[MEM_SCOPE_RESIDENT] == database_resident_bytes(db));
    }
    assert(database_resident_bytes(db) > before);
    value_view_t bogus = {VALUE_BLOB, 0, 0, (const uint8_t *)"bad", SIZE_MAX};
    value_view_t valid = {VALUE_INTEGER, 7, 0, NULL, 0};
    for (int mode = 0; mode < 2; mode++) {
        database_cache_set_enabled(db, mode != 0);
        lexer_binding_t binding = {&valid, 1};
        result_t *warm = execute(db, "SELECT ?1", &binding, &opts);
        result_release(&warm);
        binding.values = &bogus;
        result_t *invalid = execute(db, "SELECT ?1", &binding, &opts);
        assert(result_outcome(invalid).error->error_class == RESULT_E_NOMEM);
        result_release(&invalid);
        assert(budget.used[MEM_SCOPE_QUERY] == 0);
    }
    budget.limit = budget.used[MEM_SCOPE_RESIDENT] + 64;
    result_t *refused = NULL;
    database_exec_status_t failed = database_exec_ex(db, &opts, (const uint8_t *)"SELECT 7", 8, NULL, &refused);
    assert(failed == DATABASE_EXEC_NOMEM ||
           (failed == DATABASE_EXEC_OK && result_outcome(refused).error->error_class == RESULT_E_NOMEM));
    result_release(&refused);
    assert(budget.used[MEM_SCOPE_QUERY] == 0);
    budget.limit = SIZE_MAX;
    opts.max_sql_bytes = 7;
    result_t *limited = execute(db, "SELECT 7", NULL, &opts);
    assert(result_outcome(limited).error->error_class == RESULT_E_LIMIT);
    result_release(&limited);
    opts.max_sql_bytes = 0;
    database_cache_set_enabled(db, false);
    database_cache_set_enabled(db, true);
    budget.deny_resident = true;
    result_t *uncached = execute(db, "SELECT 9", NULL, &opts);
    assert(result_kind(uncached) == RESULT_KIND_ROWS);
    result_release(&uncached);
    budget.deny_resident = false;
    result_t *held = execute(db, "SELECT 'alive'", NULL, &opts);
    size_t held_bytes = result_accounted_bytes(held);
    assert(budget.used[MEM_SCOPE_QUERY] == held_bytes);
    budget.used[MEM_SCOPE_QUERY] -= held_bytes;
    budget.used[MEM_SCOPE_RESULT] += held_bytes;
    result_rebind_ledger(held, &ledger, MEM_SCOPE_RESULT);
    assert(database_close(db) == DATABASE_OK);
    assert(budget.used[MEM_SCOPE_RESIDENT] == 0 && budget.used[MEM_SCOPE_QUERY] == 0);
    result_row_t row;
    assert(result_next(held, &row) == RESULT_NEXT_ROW && row.cells[0].size_bytes == 5);
    assert(memcmp(row.cells[0].bytes, "alive", 5) == 0);
    result_release(&held);
    assert(audit_live == 0 && budget.used[0] == 0 && budget.used[1] == 0 && budget.used[2] == 0);
    audit_on_alloc = NULL;
    audit_ctx = NULL;

    db = open_database(NULL);
    result_t *without_owner = execute(db, "SELECT 9", NULL, &opts);
    result_release(&without_owner);
    database_cache_stats_t stats;
    database_cache_stats(db, &stats);
    assert(stats.entries == 0 && stats.bytes == 0);
    assert(database_close(db) == DATABASE_OK && budget.used[0] == 0 && audit_live == 0);
}

static void cached_statements_preserve_transaction_failure(void)
{
    database_t *db = open_database(NULL);
    command(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, n INTEGER)");
    assert(database_close(db) == DATABASE_OK);
    assert(database_open_transactional(DB_PATH, NULL, &db) == DATABASE_OK);
    database_cache_set_enabled(db, true);
    database_conn_t *conn = NULL;
    assert(database_conn_open(db, NULL, &conn) == DATABASE_OK);
    const char *sql[] = {"BEGIN", "INSERT INTO t VALUES(1,7)", "SELECT n FROM t WHERE id=1",
                         "INSERT INTO t VALUES(1,9)", "SELECT n FROM t WHERE id=1", "ROLLBACK"};
    for (int run = 0; run < 2; run++) {
        for (size_t i = 0; i < 6; i++) {
            result_t *result = NULL;
            assert(database_conn_exec(conn, NULL, (const uint8_t *)sql[i], strlen(sql[i]), NULL, &result) == DATABASE_EXEC_OK);
            assert((result_kind(result) == RESULT_KIND_ERROR) == (i == 3 || i == 4));
            if (i == 4) assert(result_outcome(result).error->code == DATABASE_CODE_WORK_FAILED);
            result_release(&result);
        }
        assert(integer(db, "SELECT count(*) FROM t") == 0);
    }
    assert(database_conn_begin(conn, NULL) == TXN_OK);
    result_t *ddl = NULL;
    const char *change = "DROP TABLE t";
    assert(database_conn_exec(conn, NULL, (const uint8_t *)change, strlen(change), NULL, &ddl) == DATABASE_EXEC_OK);
    result_release(&ddl);
    assert(database_conn_rollback(conn) == TXN_OK);
    assert(integer(db, "SELECT count(*) FROM t") == 0);
    assert(database_conn_close(&conn) == DATABASE_OK);
    assert(database_close(db) == DATABASE_OK && audit_live == 0);
}

static void allocation_failures_leave_cache_and_data_usable(void)
{
    const char *sql = "SELECT ?1, CASE WHEN ?1>0 THEN 'yes' ELSE 'no' END, (SELECT ?1+1)";
    value_view_t value = {VALUE_INTEGER, 7, 0, NULL, 0};
    lexer_binding_t binding = {&value, 1};
    size_t warm_calls = 0, hit_calls = 0;
    for (int warm = 0; warm < 2; warm++) {
        database_t *db = open_database(NULL);
        if (warm) {
            result_t *result = execute(db, sql, &binding, NULL);
            result_release(&result);
        }
        size_t start = audit_attempts;
        result_t *result = execute(db, sql, &binding, NULL);
        result_release(&result);
        if (warm) hit_calls = audit_attempts - start;
        else warm_calls = audit_attempts - start;
        assert(database_close(db) == DATABASE_OK && audit_live == 0);
    }
    assert(warm_calls > 0 && hit_calls > 0);
    for (int warm = 0; warm < 2; warm++) {
        size_t calls = warm ? hit_calls : warm_calls;
        for (size_t fail = 1; fail <= calls; fail++) {
            database_t *db = open_database(NULL);
            if (warm) {
                result_t *first = execute(db, sql, &binding, NULL);
                result_release(&first);
            }
            size_t fail_at = audit_attempts + fail;
            audit_fail_at = fail_at;
            result_t *result = NULL;
            database_exec_status_t status = database_exec(db, (const uint8_t *)sql, strlen(sql), &binding, &result);
            audit_fail_at = 0;
            assert(status == DATABASE_EXEC_OK || status == DATABASE_EXEC_NOMEM);
            if (result != NULL) {
                assert(result_kind(result) == RESULT_KIND_ROWS);
                result_row_t row;
                assert(result_next(result, &row) == RESULT_NEXT_ROW && row.cell_count == 3);
                assert(row.cells[0].type == VALUE_INTEGER && row.cells[0].integer == 7);
                assert(row.cells[1].type == VALUE_TEXT && row.cells[1].size_bytes == 3);
                assert(memcmp(row.cells[1].bytes, "yes", 3) == 0);
                assert(row.cells[2].type == VALUE_INTEGER && row.cells[2].integer == 8);
                assert(result_next(result, &row) == RESULT_NEXT_DONE);
            }
            result_release(&result);
            value.integer = 9;
            result_t *retry = execute(db, sql, &binding, NULL);
            result_row_t row;
            assert(result_next(retry, &row) == RESULT_NEXT_ROW && row.cells[0].integer == 9);
            result_release(&retry);
            assert(database_close(db) == DATABASE_OK && audit_live == 0);
            value.integer = 7;
        }
    }
    printf("cache allocation failure sites: miss=%zu hit=%zu\n", warm_calls, hit_calls);
}

int main(void)
{
    printf("SQLite reference: %s; cache default: %d\n", sqlite3_libversion(), STREAMLITE_SQL_TEMPLATE_CACHE);
    assert(mkdir("build/test_database_cache_dir", 0700) == 0 || errno == EEXIST);
    build_flag_controls_default();
    binding_and_syntax_match_sqlite();
    invalid_binding_and_input_preserve_errors();
    schema_eviction_and_result_lifetime();
    budgeted_calls_keep_scope_ownership();
    cached_statements_preserve_transaction_failure();
    allocation_failures_leave_cache_and_data_usable();
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    assert(rmdir("build/test_database_cache_dir") == 0);
    puts("SQL template cache contract: passed");
    return 0;
}
