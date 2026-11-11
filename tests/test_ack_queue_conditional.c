#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include "ack_queue_test_util.h"
#include "../src/queue/ack_queue_internal.h"
#include "../src/sql/database_cache.h"
#include "../src/sql/database_workspace.h"
#include "storage/page_file.h"

static size_t sql_calls, bindings, binding_bytes, sql_bytes, allocations, copy_bytes;
static size_t fail_allocation, cancel_sql, reads, writes, syncs;
static size_t query_peak, result_peak;
static uint64_t wall_start, cpu_start;

static uint64_t now_ns(clockid_t clock)
{
    struct timespec value;
    assert(clock_gettime(clock, &value) == 0);
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

void *ackq_probe_malloc(size_t size)
{
    allocations++;
    if (fail_allocation != 0 && allocations == fail_allocation) return NULL;
    copy_bytes += size;
    return malloc(size);
}

database_exec_status_t ackq_probe_session_exec(session_t *session, const session_exec_options_t *options,
                                                const uint8_t *sql, size_t size, const lexer_binding_t *binding,
                                                result_t **out)
{
    sql_calls++;
    sql_bytes += size;
    if (binding != NULL) {
        bindings += binding->count;
        for (size_t i = 0; i < binding->count; i++) binding_bytes += binding->values[i].size_bytes;
    }
    if (cancel_sql != 0 && sql_calls == cancel_sql) session_cancel(session);
    database_exec_status_t status = session_exec(session, options, sql, size, binding, out);
    session_stats_t stats;
    assert(session_stats_get(session, &stats) == SESSION_OK);
    if (stats.last_query_peak > query_peak) query_peak = stats.last_query_peak;
    size_t bytes = *out != NULL ? result_accounted_bytes(*out) : 0;
    if (bytes > result_peak) result_peak = bytes;
    return status;
}

static ssize_t observed_read(int fd, void *bytes, size_t size, off_t offset)
{
    reads++;
    return pread(fd, bytes, size, offset);
}

static ssize_t observed_write(int fd, const void *bytes, size_t size, off_t offset)
{
    writes++;
    return pwrite(fd, bytes, size, offset);
}

static int observed_sync(int fd)
{
    syncs++;
    return fsync(fd);
}

static void reset_observation(void)
{
    sql_calls = bindings = binding_bytes = sql_bytes = allocations = copy_bytes = 0;
    reads = writes = syncs = fail_allocation = cancel_sql = 0;
    query_peak = result_peak = 0;
    wall_start = now_ns(CLOCK_MONOTONIC);
    cpu_start = now_ns(CLOCK_PROCESS_CPUTIME_ID);
}

static void observation(const char *label, bool candidate, database_scan_stats_t before, session_t *db)
{
    database_scan_stats_t after;
    database_scan_stats(raw_db(db), &after);
    uint64_t wall_ns = now_ns(CLOCK_MONOTONIC) - wall_start;
    uint64_t cpu_ns = now_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    printf("{\"case\":\"%s\",\"candidate\":%d,\"sql_calls\":%zu,\"bindings\":%zu,"
           "\"variable_binding_bytes\":%zu,\"sql_bytes\":%zu,\"copy_allocated_bytes\":%zu,"
           "\"query_peak_bytes\":%zu,\"result_peak_bytes\":%zu,\"wall_ns\":%llu,\"cpu_ns\":%llu,"
           "\"rows_scanned\":%llu,\"index_lookups\":%llu,\"index_pages_read\":%llu,"
           "\"pread\":%zu,\"pwrite\":%zu,\"fsync\":%zu}\n",
           label, candidate, sql_calls, bindings, binding_bytes, sql_bytes, copy_bytes,
           query_peak, result_peak, (unsigned long long)wall_ns, (unsigned long long)cpu_ns,
           (unsigned long long)(after.rows_scanned - before.rows_scanned),
           (unsigned long long)(after.index_lookups - before.index_lookups),
           (unsigned long long)(after.index_pages_read - before.index_pages_read), reads, writes, syncs);
    CHECK(after.rows_scanned == before.rows_scanned, "%s: candidate selection avoids a full table scan", label);
    CHECK(conn_settled(db), "%s: query and result owners released", label);
}

static ackq_t *setup(session_t **db, unsigned condition, bool candidate)
{
    make_db();
    *db = open_db();
    assert(*db != NULL);
    database_cache_set_enabled(raw_db(*db), (condition & 1u) != 0);
    assert(database_workspace_set_enabled(raw_db(*db), (condition & 2u) != 0));
    char name[ACKQ_NAME_MAX + 1];
    memset(name, 'q', ACKQ_NAME_MAX);
    name[ACKQ_NAME_MAX] = 0;
    ackq_options_t options = qopts(name, 3);
    ackq_t *queue = NULL;
    assert(ackq_install(*db, &options, &queue) == ACKQ_OK);
    queue->conditional_enabled = candidate;
    return queue;
}

static void update(session_t *db, const ackq_t *queue, const char *change)
{
    char sql[512];
    snprintf(sql, sizeof(sql), "UPDATE %s %s", queue->table, change);
    assert(exec_ok(db, sql));
}

static void test_minimum_and_terminal(unsigned condition, bool candidate)
{
    session_t *db;
    ackq_t *queue = setup(&db, condition, candidate);
    g_now = 1000;
    uint64_t id;
    for (int i = 0; i < 6; i++) assert(enq(queue, db, "payload", NULL, &id, NULL) == ACKQ_OK);
    /* due 형식(#260)은 상태를 바꿀 때 due도 같은 규칙으로 맞춘다(ack_queue_internal.h의 구간) */
    assert(queue->due_column);
    update(db, queue, "SET ready_at = 2000, due = 2000 WHERE id = 1");
    update(db, queue, "SET state = 1, lease_until = 1100, fence = 1, attempts = 1, due = 1100 WHERE id = 2");
    update(db, queue, "SET attempts = 3 WHERE id = 3");
    update(db, queue, "SET state = 1, lease_until = 1100, attempts = 3, due = -2305843009213693948 WHERE id = 4");
    update(db, queue, "SET state = 2, due = -4611686018427387899 WHERE id = 5");
    update(db, queue, "SET state = 3, fence = 9, due = -3458764513820540922 WHERE id = 6");
    g_now = 1100;
    ackq_claim_t items[4] = {0};
    size_t count = 0;
    database_scan_stats_t before;
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_claim(queue, db, 4, 1000, items, &count) == ACKQ_OK && count == 2, "two eligible items only");
    /* due 순서: READY 3(ready_at 1000) 다음 만료 LEASED 2(lease_until 1100) */
    CHECK(items[0].id == 3 && items[1].id == 2, "earliest due merges READY and expired LEASED");
    CHECK(items[1].fence == 2 && items[1].attempts == 2 && items[0].attempts == 4, "lease fence and READY attempt semantics");
    observation("partial_batch", candidate, before, db);
    ackq_claims_release(items, count);

    assert(enq(queue, db, "new", NULL, &id, NULL) == ACKQ_OK);
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_claim(queue, db, 1, 1000, items, &count) == ACKQ_OK && count == 1 && items[0].id == id, "normal claim");
    CHECK(sql_calls == (candidate ? 1u : 2u), "normal claim execution boundaries");
    observation("normal_claim", candidate, before, db);
    uint64_t fence = items[0].fence;
    ackq_claims_release(items, count);
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_ack(queue, db, id, fence) == ACKQ_OK, "normal ACK");
    CHECK(sql_calls == (candidate ? 1u : 2u), "normal ACK execution boundaries");
    observation("normal_ack", candidate, before, db);
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_ack(queue, db, id, fence) == ACKQ_OK, "duplicate ACK with same fence");
    CHECK(sql_calls == (candidate ? 2u : 1u), "duplicate ACK includes readback");
    observation("duplicate_ack", candidate, before, db);
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_ack(queue, db, id, fence + 1) == ACKQ_STALE, "stale fence");
    observation("stale_ack", candidate, before, db);
    CHECK(ackq_ack(queue, db, 6, 9) == ACKQ_STALE, "FAILED is never duplicate ACK");
    CHECK(ackq_ack(queue, db, UINT64_MAX, 1) == ACKQ_NOT_FOUND, "out-of-range ID remains missing");
    CHECK(ackq_claim_id(queue, db, 1, 1000, &items[0]) == ACKQ_STALE, "not-ready ID remains stale");
    CHECK(ackq_claim_id(queue, db, 10000, 1000, &items[0]) == ACKQ_NOT_FOUND, "missing claim ID");
    database_scan_stats(raw_db(db), &before);
    reset_observation();
    CHECK(ackq_claim(queue, db, 1, 10, items, &count) == ACKQ_OK && count == 0, "empty eligible set");
    CHECK(sql_calls == (candidate ? 2u : 1u) && writes == 0 && syncs == 0, "empty claim readback never publishes");
    observation("empty_claim", candidate, before, db);
    ackq_close(&queue);
    CHECK(close_db(db), "close after terminal checks");
}

/* 이전 판 라이브러리처럼 due를 고치지 않고 상태만 바꾼 행은 due가 claim 구간에 남아도 받지 않고 머리를 막지 않는다 */
static void test_wrong_due(unsigned condition, bool candidate)
{
    session_t *db;
    ackq_t *queue = setup(&db, condition, candidate);
    g_now = 1000;
    uint64_t id;
    for (int i = 0; i < 5; i++) assert(enq(queue, db, "payload", NULL, &id, NULL) == ACKQ_OK);
    update(db, queue, "SET state = 1, lease_until = 61000, fence = 1, attempts = 1 WHERE id = 1");
    update(db, queue, "SET state = 2, fence = 1, attempts = 1 WHERE id = 2");
    update(db, queue, "SET state = 3, fence = 1, attempts = 1 WHERE id = 3");
    update(db, queue, "SET ready_at = 900000, fence = 1, attempts = 1 WHERE id = 4");
    g_now = 2000;
    ackq_claim_t items[4] = {0};
    size_t count = 0;
    CHECK(ackq_claim(queue, db, 4, 1000, items, &count) == ACKQ_OK && count == 1 && items[0].id == 5,
          "wrong due: only the due READY item");
    ackq_claims_release(items, count);
    CHECK(ackq_claim(queue, db, 4, 1000, items, &count) == ACKQ_OK && count == 0, "wrong due: live lease and finished jobs stay");
    CHECK(ackq_claim_id(queue, db, 1, 1000, &items[0]) == ACKQ_STALE && ackq_claim_id(queue, db, 2, 1000, &items[0]) == ACKQ_STALE,
          "wrong due: claim by ID stays stale");
    ackq_close(&queue);
    CHECK(close_db(db), "close after wrong due checks");
}

static void test_limits_and_result_lifetime(unsigned condition, bool candidate)
{
    session_t *db;
    ackq_t *queue = setup(&db, condition, candidate);
    g_now = 100;
    uint64_t id;
    assert(enq(queue, db, "owned", NULL, &id, NULL) == ACKQ_OK);
    update(db, queue, "SET fence = 9223372036854775807 WHERE id = 1");
    assert(enq(queue, db, "later", NULL, &id, NULL) == ACKQ_OK);
    ackq_claim_t items[2] = {0};
    size_t count = 9;
    CHECK(ackq_claim(queue, db, 1, 10, items, &count) == ACKQ_LIMIT && count == 0, "never skip overflowing minimum");
    CHECK(ackq_claim_id(queue, db, 1, 10, &items[0]) == ACKQ_LIMIT, "ID overflow readback");
    update(db, queue, "SET fence = 0, attempts = 9223372036854775807 WHERE id = 1");
    CHECK(ackq_claim(queue, db, 1, 10, items, &count) == ACKQ_LIMIT && count == 0, "attempt overflow keeps minimum");
    update(db, queue, "SET attempts = 0 WHERE id = 1");
    CHECK(ackq_claim_id(queue, db, 1, 10, &items[0]) == ACKQ_OK, "ID normal claim");
    CHECK(ackq_fail(queue, db, 1, items[0].fence, "first") == ACKQ_OK, "fail transition");
    CHECK(ackq_fail(queue, db, 1, items[0].fence, "second") == ACKQ_OK, "duplicate fail");
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT count(*) FROM %s WHERE id = 1 AND note = 'first'", queue->table);
    CHECK(scalar_int(db, sql) == 1, "duplicate fail preserves first note");
    ackq_claims_release(items, 1);
    CHECK(ackq_claim_id(queue, db, 2, 10, &items[0]) == ACKQ_OK, "second item claimed");
    ackq_close(&queue);
    CHECK(close_db(db), "close with caller-owned payload");
    CHECK(items[0].payload_size == 5 && memcmp(items[0].payload, "later", 5) == 0, "payload survives queue and DB close");
    ackq_claims_release(items, 1);
    db = open_db();
    CHECK(db != NULL, "reopen after conditional updates");
    CHECK(scalar_int(db, sql) == 1, "terminal note survives reopen");
    CHECK(close_db(db), "close reopened DB");
}

static void test_cancel_oom_and_busy(unsigned condition, bool candidate)
{
    session_t *db;
    ackq_t *queue = setup(&db, condition, candidate);
    g_now = 100;
    uint64_t id;
    assert(enq(queue, db, "payload", NULL, &id, NULL) == ACKQ_OK);
    ackq_claim_t item = {0};
    size_t count = 1;
    reset_observation();
    cancel_sql = 1;
    CHECK(ackq_claim(queue, db, 1, 10, &item, &count) == ACKQ_CANCELLED && count == 0, "cancel clears returned claims");
    reset_observation();
    fail_allocation = 1;
    CHECK(ackq_claim(queue, db, 1, 10, &item, &count) == ACKQ_NOMEM && count == 0, "payload allocation fails cleanly");
    reset_observation();
    session_t *txn = begin_txn(db);
    assert(txn != NULL);
    fail_allocation = 1;
    CHECK(ackq_claim(queue, txn, 1, 10, &item, &count) == ACKQ_NOMEM && count == 0, "caller-owned transaction OOM");
    CHECK(session_txn_active(txn), "caller owns rollback after payload OOM");
    reset_observation();
    CHECK(rollback_txn(&txn) == TXN_OK, "explicit rollback after OOM");
    assert(enq(queue, db, "second", NULL, &id, NULL) == ACKQ_OK);
    for (size_t point = 1; point <= 4; point++) {
        txn = begin_txn(db);
        assert(txn != NULL);
        ackq_claim_t batch[2] = {0};
        reset_observation();
        cancel_sql = point;
        ackq_status_t status = ackq_claim(queue, txn, 2, 10, batch, &count);
        CHECK((status == ACKQ_CANCELLED && count == 0) || (status == ACKQ_OK && count == 2),
              "partial batch cancellation returns no partial result");
        if (status == ACKQ_CANCELLED) CHECK(session_txn_failed(txn), "SQL cancellation fails caller transaction");
        ackq_claims_release(batch, count);
        reset_observation();
        CHECK(rollback_txn(&txn) == TXN_OK, "rollback canceled or successful comparison batch");
    }
    reset_observation();
    fail_allocation = 2;
    ackq_claim_t batch[2] = {0};
    CHECK(ackq_claim(queue, db, 2, 10, batch, &count) == ACKQ_NOMEM && count == 0, "second payload OOM discards whole batch");
    reset_observation();
    txn = begin_txn(db);
    assert(txn != NULL);
    queue->begin_timeout_ms = 1;
    CHECK(ackq_claim(queue, db, 1, 10, &item, &count) == ACKQ_BUSY && count == 0, "writer timeout preserves queue");
    CHECK(rollback_txn(&txn) == TXN_OK, "release writer");
    CHECK(ackq_claim(queue, db, 1, 10, &item, &count) == ACKQ_OK && count == 1 && item.fence == 1, "failed operations did not claim");
    ackq_claims_release(&item, count);
    CHECK(conn_settled(db), "error owners released");
    ackq_close(&queue);
    CHECK(close_db(db), "close after failures");
}

static void test_query_budget(unsigned condition, bool candidate)
{
    session_t *db;
    ackq_t *queue = setup(&db, condition, candidate);
    uint64_t id;
    g_now = 100;
    assert(enq(queue, db, "budget", NULL, &id, NULL) == ACKQ_OK);
    char name[ACKQ_NAME_MAX + 1];
    memcpy(name, queue->name, sizeof(name));
    ackq_close(&queue);
    assert(close_db(db));
    session_limits_t limits = {16u * 1024u * 1024u, 1024u * 1024u, 96u * 1024u, 4, 0};
    db = open_conn(g_db_path, true, true, &limits);
    assert(db != NULL);
    database_cache_set_enabled(raw_db(db), (condition & 1u) != 0);
    assert(database_workspace_set_enabled(raw_db(db), (condition & 2u) != 0));
    ackq_options_t options = qopts(name, 3);
    assert(ackq_open(db, &options, &queue) == ACKQ_OK);
    queue->conditional_enabled = candidate;
    ackq_claim_t item = {0};
    size_t count = 0;
    reset_observation();
    ackq_status_t status = ackq_claim(queue, db, 1, 10, &item, &count);
    CHECK(status == (candidate ? ACKQ_NOMEM : ACKQ_OK), "fixed query budget preserves refusal instead of raising limits");
    CHECK(count == (candidate ? 0u : 1u) && !session_txn_active(db), "budget outcome owns no unfinished implicit transaction");
    ackq_claims_release(&item, count);
    CHECK(conn_settled(db), "query-budget refusal releases query and result owners");
    if (candidate) {
        session_t *txn = begin_txn(db);
        assert(txn != NULL);
        CHECK(ackq_claim(queue, txn, 1, 10, &item, &count) == ACKQ_NOMEM && count == 0, "bounded SQL fails caller transaction");
        CHECK(session_txn_failed(txn), "SQL allocation refusal needs explicit rollback");
        CHECK(session_txn_commit(txn, NULL) == TXN_INVALID, "failed caller transaction cannot commit");
        CHECK(rollback_txn(&txn) == TXN_OK, "explicit rollback after SQL memory refusal");
    }
    ackq_close(&queue);
    CHECK(close_db(db), "close bounded session");
    db = open_db();
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT fence FROM streamlite_queue_%s WHERE id = 1", name);
    CHECK(scalar_int(db, sql) == (candidate ? 0 : 1), "reopened state matches success or definite refusal");
    CHECK(close_db(db), "close budget audit");
}

static void test_full_batch_observation(unsigned condition, bool candidate)
{
    static const size_t sizes[] = {1, 4, 16, 32};
    for (size_t at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++) {
        session_t *db;
        ackq_t *queue = setup(&db, condition, candidate);
        g_now = 100;
        for (size_t i = 0; i < sizes[at]; i++) {
            uint64_t id;
            assert(enq(queue, db, "batch", NULL, &id, NULL) == ACKQ_OK);
        }
        ackq_claim_t items[32] = {0};
        size_t count = 0;
        database_scan_stats_t before;
        database_scan_stats(raw_db(db), &before);
        reset_observation();
        CHECK(ackq_claim(queue, db, sizes[at], 10, items, &count) == ACKQ_OK && count == sizes[at], "complete batch size");
        for (size_t i = 0; i < count; i++) CHECK(items[i].id == i + 1, "complete batch keeps minimum-ID order");
        char label[32];
        snprintf(label, sizeof(label), "full_batch_%zu", sizes[at]);
        observation(label, candidate, before, db);
        ackq_claims_release(items, count);
        ackq_close(&queue);
        CHECK(close_db(db), "close batch observation");
    }
}

int main(void)
{
    set_dir("build/test_ack_queue_conditional_dir");
    page_file_io_t io = {observed_read, observed_write, observed_sync, link, unlink, NULL};
    page_file_set_default_io(&io);
    for (unsigned condition = 0; condition < 4; condition++) {
        for (unsigned candidate = 0; candidate < 2; candidate++) {
            printf("condition=%u candidate=%u\n", condition, candidate);
            test_minimum_and_terminal(condition, candidate != 0);
            if (g_fail != 0) return 1;
            test_wrong_due(condition, candidate != 0);
            if (g_fail != 0) return 1;
            test_limits_and_result_lifetime(condition, candidate != 0);
            if (g_fail != 0) return 1;
            test_cancel_oom_and_busy(condition, candidate != 0);
            if (g_fail != 0) return 1;
            test_query_budget(condition, candidate != 0);
            if (g_fail != 0) return 1;
            if (condition == 0 || condition == 3) test_full_batch_observation(condition, candidate != 0);
            if (g_fail != 0) return 1;
        }
    }
    page_file_set_default_io(NULL);
    remove_dir();
    printf("conditional queue: %d checks, %d failures\n", g_run, g_fail);
    return g_fail != 0;
}
