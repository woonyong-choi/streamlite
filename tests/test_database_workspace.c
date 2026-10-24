#include <assert.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sql/database_txn.h"
#include "../src/sql/database_internal.h"
#include "../src/sql/database_cache.h"
#include "../src/sql/database_workspace.h"
#include "alloc_audit.h"

#define DB_PATH "build/test_database_workspace_dir/workspace.db"
#define TEST_BUDGET (64u * 1024u * 1024u)

typedef struct {
    size_t used[3];
    size_t limit;
} budget_t;

static size_t writes, syncs, fail_write, fail_sync;
static bool control_seen;
static page_file_io_t system_io;
static mem_ledger_t query_ledger;
static const char *active_sql;

static ssize_t counted_write(int fd, const void *bytes, size_t size, off_t offset)
{
    if (offset < (off_t)(PAGE_FILE_CONTROL_PAGES * PAGE_FILE_PAGE_BYTES)) control_seen = true;
    if (++writes == fail_write) {
        errno = EIO;
        return -1;
    }
    return system_io.pwrite(fd, bytes, size, offset);
}

static int counted_sync(int fd)
{
    if (++syncs == fail_sync) {
        errno = EIO;
        return -1;
    }
    return system_io.fsync(fd);
}

static bool reserve(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *b = ctx;
    size_t used = b->used[0] + b->used[1] + b->used[2];
    if (bytes > b->limit - used) return false;
    b->used[scope] += bytes;
    return true;
}

static void release(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *b = ctx;
    assert(bytes <= b->used[scope]);
    b->used[scope] -= bytes;
}

static void allocated(void *ctx, size_t size, size_t live)
{
    (void)size;
    budget_t *b = ctx;
    if (live > b->used[0] + b->used[1] + b->used[2]) {
        fprintf(stderr, "allocation=%zu live=%zu scopes=%zu/%zu/%zu sql=%s\n", size, live,
                b->used[0], b->used[1], b->used[2], active_sql);
    }
    assert(live <= b->used[0] + b->used[1] + b->used[2]);
}

static const mem_ledger_ops_t LEDGER = {reserve, release, NULL, NULL};

static database_t *fresh(budget_t *budget, bool workspace, bool cache)
{
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    budget->limit = TEST_BUDGET;
    mem_ledger_t ledger = {&LEDGER, budget};
    query_ledger = ledger;
    database_t *db = NULL;
    assert(database_create_bounded(DB_PATH, &ledger, &db) == DATABASE_OK);
    assert(database_workspace_enabled(db) == (TOTE_TXN_WORKSPACE != 0));
    assert(database_workspace_set_enabled(db, workspace && getenv("WORKSPACE_NEGATIVE_CONTROL") == NULL));
    database_cache_set_enabled(db, cache);
    audit_ctx = budget;
    audit_on_alloc = allocated;
    return db;
}

static void close_database(database_t *db, budget_t *budget)
{
    assert(database_close(db) == DATABASE_OK);
    assert(audit_live == 0);
    assert(budget->used[0] == 0 && budget->used[1] == 0 && budget->used[2] == 0);
    audit_on_alloc = NULL;
    audit_ctx = NULL;
}

static database_exec_status_t execute(database_t *db, const char *sql, const lexer_binding_t *binding,
                                      const database_exec_options_t *options, result_t **out)
{
    active_sql = sql;
    database_exec_options_t bounded = options != NULL ? *options : (database_exec_options_t){0};
    bounded.ledger = &query_ledger;
    return database_work_active(db)
               ? database_exec_in_work(db, &bounded, (const uint8_t *)sql, strlen(sql), binding, out)
               : database_exec_ex(db, &bounded, (const uint8_t *)sql, strlen(sql), binding, out);
}

static void command(database_t *db, const char *sql)
{
    result_t *r = NULL;
    assert(execute(db, sql, NULL, NULL, &r) == DATABASE_EXEC_OK);
    assert(result_kind(r) == RESULT_KIND_COMMAND);
    result_release(&r);
}

static int64_t integer(database_t *db, const char *sql, bool published)
{
    result_t *r = NULL;
    database_exec_options_t options = {.ledger = &query_ledger};
    database_exec_status_t status = published
        ? database_exec_read_only(db, &options, (const uint8_t *)sql, strlen(sql), NULL, &r)
        : execute(db, sql, NULL, NULL, &r);
    assert(status == DATABASE_EXEC_OK && result_kind(r) == RESULT_KIND_ROWS);
    result_row_t row;
    assert(result_next(r, &row) == RESULT_NEXT_ROW && row.cell_count == 1);
    assert(row.cells[0].type == VALUE_INTEGER);
    int64_t value = row.cells[0].integer;
    result_release(&r);
    return value;
}

static void commit(database_t *db)
{
    database_work_result_t result = database_work_commit(db);
    assert(result.outcome == DATABASE_WORK_APPLIED);
    database_work_end(db, result.outcome);
}

static void seed(database_t *db)
{
    command(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER, body TEXT)");
    command(db, "CREATE UNIQUE INDEX by_v ON t(v)");
    command(db, "INSERT INTO t VALUES(1, 10, 'one'), (2, 20, 'two'), (3, 30, 'three')");
}

static void same_rows(database_t *db, sqlite3 *reference)
{
    const char *sql = "SELECT id,v,body FROM t ORDER BY id";
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(reference, sql, -1, &stmt, NULL) == SQLITE_OK);
    result_t *r = NULL;
    assert(execute(db, sql, NULL, NULL, &r) == DATABASE_EXEC_OK);
    assert(result_kind(r) == RESULT_KIND_ROWS);
    result_row_t row;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        assert(result_next(r, &row) == RESULT_NEXT_ROW && row.cell_count == 3);
        for (size_t i = 0; i < 2; i++) {
            assert(row.cells[i].type == VALUE_INTEGER);
            assert(row.cells[i].integer == sqlite3_column_int64(stmt, (int)i));
        }
        int length = sqlite3_column_bytes(stmt, 2);
        assert(row.cells[2].type == VALUE_TEXT && row.cells[2].size_bytes == (size_t)length);
        assert(memcmp(row.cells[2].bytes, sqlite3_column_text(stmt, 2), (size_t)length) == 0);
    }
    assert(result_next(r, &row) == RESULT_NEXT_DONE);
    result_release(&r);
    sqlite3_finalize(stmt);
}

static void sql_agrees_with_sqlite(bool workspace, bool cache)
{
    budget_t budget = {0};
    database_t *db = fresh(&budget, workspace, cache);
    seed(db);
    sqlite3 *reference = NULL;
    assert(sqlite3_open(":memory:", &reference) == SQLITE_OK);
    assert(sqlite3_exec(reference, "CREATE TABLE t(id INTEGER PRIMARY KEY,v INTEGER,body TEXT);"
                                  "CREATE UNIQUE INDEX by_v ON t(v);"
                                  "INSERT INTO t VALUES(1,10,'one'),(2,20,'two'),(3,30,'three')", NULL, NULL, NULL) == SQLITE_OK);
    const char *steps[] = {
        "UPDATE t SET v=v+1 WHERE id=1",
        "UPDATE t SET v=v+1 WHERE id=1",
        "DELETE FROM t WHERE id=2",
        "INSERT INTO t VALUES(4,40,'four')",
        "CREATE TABLE scratch(id INTEGER PRIMARY KEY)",
        "INSERT INTO scratch VALUES(9)",
        "DROP TABLE scratch"
    };
    assert(database_work_begin(db) == DATABASE_OK);
    assert(!database_workspace_set_enabled(db, !workspace));
    writes = syncs = 0;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        command(db, steps[i]);
        assert(sqlite3_exec(reference, steps[i], NULL, NULL, NULL) == SQLITE_OK);
        same_rows(db, reference);
        assert(integer(db, "SELECT v FROM t WHERE id=1", true) == 10);
    }
    if (workspace) assert(writes == 0 && syncs == 0);
    else assert(writes > 0 && syncs == 0);
    database_workspace_stats_t stats;
    database_workspace_stats(db, &stats);
    if (workspace) assert(stats.bytes > 0 && stats.peak_bytes <= DATABASE_WORKSPACE_BYTES);
    assert(stats.catalog_clones == (workspace ? 1u : sizeof(steps) / sizeof(steps[0])));
    commit(db);
    database_workspace_stats(db, &stats);
    if (workspace) assert(stats.buffered_writes > stats.physical_writes && stats.physical_writes > 0);
    same_rows(db, reference);
    database_page_check_t pages;
    audit_on_alloc = NULL;
    assert(database_check_pages(db, &pages) == DATABASE_OK);
    assert(pages.orphaned == 0 && pages.conflicts == 0);
    audit_on_alloc = allocated;
    printf("workspace=%d cache=%d writes=%zu buffered=%zu flushed=%zu clones=%zu peak=%zu\n",
           workspace, cache, writes, stats.buffered_writes, stats.physical_writes, stats.catalog_clones, stats.peak_bytes);
    close_database(db, &budget);
    assert(database_open(DB_PATH, false, &db) == DATABASE_OK);
    same_rows(db, reference);
    assert(database_close(db) == DATABASE_OK && audit_live == 0);
    sqlite3_close(reference);
}

static bool cancelled(void *ctx)
{
    (void)ctx;
    return true;
}

static void errors_require_rollback(void)
{
    budget_t budget = {0};
    database_t *db = fresh(&budget, true, true);
    seed(db);
    const char *bad[] = {"INSERT INTO t VALUES(9,20,'duplicate')", "UPDATE t SET v=20 WHERE id=1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(database_work_begin(db) == DATABASE_OK);
        command(db, "UPDATE t SET v=11 WHERE id=1");
        result_t *r = NULL;
        assert(execute(db, bad[i], NULL, NULL, &r) == DATABASE_EXEC_OK);
        assert(result_kind(r) == RESULT_KIND_ERROR);
        result_release(&r);
        assert(database_work_failed(db));
        assert(execute(db, "SELECT 1", NULL, NULL, &r) == DATABASE_EXEC_OK);
        assert(result_kind(r) == RESULT_KIND_ERROR);
        result_release(&r);
        assert(database_work_commit(db).outcome == DATABASE_WORK_NOT_APPLIED);
        assert(integer(db, "SELECT v FROM t WHERE id=1", true) == 10);
        database_work_end(db, DATABASE_WORK_NOT_APPLIED);
        assert(integer(db, "SELECT v FROM t WHERE id=1", false) == 10);
    }
    assert(database_work_begin(db) == DATABASE_OK);
    command(db, "DELETE FROM t WHERE id=2");
    database_cancel_t cancel = {cancelled, NULL};
    database_exec_options_t options = {.cancel = &cancel};
    result_t *r = NULL;
    assert(execute(db, "UPDATE t SET v=99 WHERE id=1", NULL, &options, &r) == DATABASE_EXEC_OK);
    assert(result_kind(r) == RESULT_KIND_ERROR && database_work_failed(db));
    result_release(&r);
    database_work_end(db, DATABASE_WORK_NOT_APPLIED);
    assert(integer(db, "SELECT count(*) FROM t", false) == 3);
    close_database(db, &budget);
}

static void overflow_and_workspace_limit(void)
{
    budget_t budget = {0};
    database_t *db = fresh(&budget, true, false);
    seed(db);
    size_t length = 18000;
    uint8_t *body = malloc(length);
    assert(body != NULL);
    memset(body, 'x', length);
    value_view_t param = {.type = VALUE_TEXT, .bytes = body, .size_bytes = length};
    lexer_binding_t binding = {&param, 1};
    assert(database_work_begin(db) == DATABASE_OK);
    result_t *r = NULL;
    assert(execute(db, "UPDATE t SET body=?1 WHERE id=1", &binding, NULL, &r) == DATABASE_EXEC_OK);
    assert(result_kind(r) == RESULT_KIND_COMMAND);
    result_release(&r);
    assert(integer(db, "SELECT length(body) FROM t WHERE id=1", false) == (int64_t)length);
    command(db, "DELETE FROM t WHERE id=1");
    command(db, "INSERT INTO t VALUES(1,10,'one')");
    commit(db);
    assert(integer(db, "SELECT length(body) FROM t WHERE id=1", false) == 3);
    free(body);

    assert(database_work_begin(db) == DATABASE_OK);
    length = DATABASE_WORKSPACE_BYTES + PAGE_BODY_BYTES;
    body = malloc(length);
    assert(body != NULL);
    memset(body, 'y', length);
    param = (value_view_t){.type = VALUE_TEXT, .bytes = body, .size_bytes = length};
    database_exec_status_t status = execute(db, "UPDATE t SET body=?1 WHERE id=1", &binding, NULL, &r);
    assert(status == DATABASE_EXEC_NOMEM ||
           (status == DATABASE_EXEC_OK && result_kind(r) == RESULT_KIND_ERROR &&
            result_outcome(r).error->error_class == RESULT_E_NOMEM));
    result_release(&r);
    assert(database_work_failed(db));
    database_workspace_stats_t stats;
    database_workspace_stats(db, &stats);
    assert(stats.peak_bytes <= DATABASE_WORKSPACE_BYTES);
    database_work_end(db, DATABASE_WORK_NOT_APPLIED);
    free(body);
    assert(integer(db, "SELECT length(body) FROM t WHERE id=1", false) == 3);
    close_database(db, &budget);
}

static size_t allocation_sequence(size_t failure, bool warmed, bool ddl)
{
    budget_t budget = {0};
    database_t *db = fresh(&budget, true, true);
    seed(db);
    assert(database_work_begin(db) == DATABASE_OK);
    if (warmed) command(db, "UPDATE t SET v=11 WHERE id=1");
    size_t start = audit_attempts;
    audit_fail_at = failure == 0 ? 0 : start + failure;
    result_t *r = NULL;
    const char *sql = ddl ? "CREATE TABLE extra(id INTEGER PRIMARY KEY, body TEXT DEFAULT 'default')"
                          : "UPDATE t SET v=v+1,body='changed' WHERE id=1 RETURNING v";
    database_exec_status_t status = execute(db, sql, NULL, NULL, &r);
    size_t attempts = audit_attempts - start;
    audit_fail_at = 0;
    if (status == DATABASE_EXEC_OK && result_kind(r) != RESULT_KIND_ERROR) {
        int64_t expected = ddl || !warmed ? 11 : 12;
        if (ddl) assert(result_kind(r) == RESULT_KIND_COMMAND);
        else {
            result_row_t row;
            assert(result_next(r, &row) == RESULT_NEXT_ROW && row.cells[0].integer == expected);
        }
        result_release(&r);
        commit(db);
        assert(integer(db, "SELECT v FROM t WHERE id=1", false) == expected);
    } else {
        assert(status == DATABASE_EXEC_NOMEM || result_kind(r) == RESULT_KIND_ERROR);
        result_release(&r);
        assert(database_work_failed(db));
        database_work_end(db, DATABASE_WORK_NOT_APPLIED);
        assert(integer(db, "SELECT v FROM t WHERE id=1", false) == 10);
    }
    close_database(db, &budget);
    return attempts;
}

static void repeated_delete_and_result_lifetime(void)
{
    budget_t budget = {0};
    database_t *db = fresh(&budget, true, true);
    seed(db);
    command(db, "UPDATE t SET v=100 WHERE id=1");
    uint32_t pages = 0;
    for (size_t turn = 0; turn < 64; turn++) {
        assert(database_work_begin(db) == DATABASE_OK);
        command(db, "DELETE FROM t WHERE id=2");
        command(db, "INSERT INTO t VALUES(2,20,'two')");
        command(db, "UPDATE t SET v=v+1 WHERE id=1");
        if (turn % 2 == 0) commit(db);
        else database_work_end(db, DATABASE_WORK_NOT_APPLIED);
        assert(integer(db, "SELECT count(*) FROM t", false) == 3);
        uint32_t current = page_file_allocated_pages(database_page_file(db));
        if (turn < 32) { if (current > pages) pages = current; }
        else assert(current <= pages);
    }
    assert(integer(db, "SELECT v FROM t WHERE id=1", false) == 132);
    database_page_check_t checked;
    audit_on_alloc = NULL;
    assert(database_check_pages(db, &checked) == DATABASE_OK);
    assert(checked.orphaned == 0 && checked.conflicts == 0);
    audit_on_alloc = allocated;
    assert(database_work_begin(db) == DATABASE_OK);
    command(db, "UPDATE t SET body='temporary' WHERE id=1");
    result_t *held = NULL;
    assert(execute(db, "SELECT body FROM t WHERE id=1", NULL, NULL, &held) == DATABASE_EXEC_OK);
    database_work_end(db, DATABASE_WORK_NOT_APPLIED);
    assert(database_close(db) == DATABASE_OK);
    result_row_t row;
    assert(result_next(held, &row) == RESULT_NEXT_ROW);
    assert(row.cells[0].size_bytes == 9 && memcmp(row.cells[0].bytes, "temporary", 9) == 0);
    result_release(&held);
    assert(audit_live == 0 && budget.used[0] == 0 && budget.used[1] == 0 && budget.used[2] == 0);
    audit_on_alloc = NULL;
    audit_ctx = NULL;
    assert(database_open(DB_PATH, false, &db) == DATABASE_OK);
    assert(integer(db, "SELECT length(body) FROM t WHERE id=1", false) == 3);
    assert(database_close(db) == DATABASE_OK && audit_live == 0);
}

static void commit_io_failures(void)
{
    size_t write_count = 0, sync_count = 0;
    for (size_t failure = 0; failure <= write_count + sync_count; failure++) {
        budget_t budget = {0};
        database_t *db = fresh(&budget, true, false);
        seed(db);
        assert(database_work_begin(db) == DATABASE_OK);
        command(db, "UPDATE t SET v=11 WHERE id=1");
        command(db, "DELETE FROM t WHERE id=2");
        writes = syncs = 0;
        control_seen = false;
        fail_write = failure > 0 && failure <= write_count ? failure : 0;
        fail_sync = failure > write_count ? failure - write_count : 0;
        database_work_result_t result = database_work_commit(db);
        fail_write = fail_sync = 0;
        if (failure == 0) {
            assert(result.outcome == DATABASE_WORK_APPLIED);
            write_count = writes;
            sync_count = syncs;
        } else assert(result.outcome == (control_seen ? DATABASE_WORK_UNKNOWN : DATABASE_WORK_NOT_APPLIED));
        database_work_end(db, result.outcome);
        if (result.outcome == DATABASE_WORK_NOT_APPLIED && !database_is_poisoned(db)) {
            assert(integer(db, "SELECT v FROM t WHERE id=1", false) == 10);
            assert(database_work_begin(db) == DATABASE_OK);
            command(db, "UPDATE t SET v=12 WHERE id=1");
            assert(integer(db, "SELECT v FROM t WHERE id=1", false) == 12);
            database_work_end(db, DATABASE_WORK_NOT_APPLIED);
        }
        if (result.outcome == DATABASE_WORK_UNKNOWN) {
            assert(database_is_poisoned(db));
            assert(database_work_begin(db) == DATABASE_IO);
        }
        close_database(db, &budget);
        assert(database_open(DB_PATH, false, &db) == DATABASE_OK);
        int64_t v = integer(db, "SELECT v FROM t WHERE id=1", false);
        int64_t rows = integer(db, "SELECT count(*) FROM t", false);
        assert((v == 10 && rows == 3) || (v == 11 && rows == 2));
        if (result.outcome == DATABASE_WORK_NOT_APPLIED) assert(v == 10 && rows == 3);
        assert(database_close(db) == DATABASE_OK && audit_live == 0);
    }
    printf("commit write failure sites=%zu sync failure sites=%zu\n", write_count, sync_count);
}

static void workspace_rejects_late_resident_attachment(void)
{
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    database_t *db = NULL;
    query_ledger = (mem_ledger_t){0};
    assert(database_create(DB_PATH, &db) == DATABASE_OK);
    assert(database_workspace_set_enabled(db, true));
    assert(database_work_begin(db) == DATABASE_OK);
    command(db, "CREATE TABLE t(id INTEGER PRIMARY KEY)");
    command(db, "INSERT INTO t VALUES(1)");
    budget_t budget = {.limit = TEST_BUDGET};
    mem_ledger_t ledger = {&LEDGER, &budget};
    assert(database_attach_ledger(db, &ledger) == DATABASE_INVALID);
    database_work_end(db, DATABASE_WORK_NOT_APPLIED);
    assert(database_attach_ledger(db, &ledger) == DATABASE_OK);
    close_database(db, &budget);
}

int main(void)
{
    mkdir("build/test_database_workspace_dir", 0700);
    system_io = page_file_default_io();
    page_file_io_t io = system_io;
    io.pwrite = counted_write;
    io.fsync = counted_sync;
    page_file_set_default_io(&io);
    for (int workspace = 0; workspace <= 1; workspace++) {
        for (int cache = 0; cache <= 1; cache++) sql_agrees_with_sqlite(workspace != 0, cache != 0);
    }
    errors_require_rollback();
    overflow_and_workspace_limit();
    for (int scenario = 0; scenario < 3; scenario++) {
        size_t allocations = allocation_sequence(0, scenario > 0, scenario == 2);
        for (size_t failure = 1; failure <= allocations; failure++) allocation_sequence(failure, scenario > 0, scenario == 2);
        printf("workspace allocation scenario=%d failure sites=%zu\n", scenario, allocations);
    }
    repeated_delete_and_result_lifetime();
    commit_io_failures();
    workspace_rejects_late_resident_attachment();
    page_file_set_default_io(NULL);
    puts("database workspace contract passed");
    return 0;
}
