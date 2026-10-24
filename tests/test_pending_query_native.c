#include <stdio.h>
#include <stdlib.h>

#include "queue/ack_queue.h"

static size_t malloc_count, malloc_failure, reserve_count, reserve_failure, checks;
static bool is_query;

static void require(bool valid, const char *message)
{
    checks++;
    if (!valid) { fprintf(stderr, "%s\n", message); exit(2); }
}

void *ackq_probe_malloc(size_t size)
{
    if (is_query && ++malloc_count == malloc_failure) return NULL;
    return malloc(size);
}

bool session_test_fail_reserve(void)
{
    return is_query && ++reserve_count == reserve_failure;
}

void session_test_locked(const char *where, size_t bytes)
{
    (void)where;
    require(bytes <= (128u << 20), "shared budget exceeded");
}

void session_test_returning(const void *object, const char *where)
{
    (void)object;
    (void)where;
}

static ackq_status_t attempt(ackq_t *queue, session_t *session)
{
    ackq_item_t items[4] = {0};
    size_t count = 99;
    malloc_count = reserve_count = 0;
    is_query = true;
    ackq_status_t status = ackq_list(queue, session, 0, 4, true, items, &count);
    is_query = false;
    if (status == ACKQ_OK) {
        require(count == 4 && items[0].id == 1 && items[3].id == 4, "list result mismatch");
        ackq_list_release(items, count);
    } else {
        require(status == ACKQ_NOMEM && count == 0, "OOM exposed partial success");
        for (size_t i = 0; i < 4; i++) require(items[i].payload == NULL, "OOM retained payload");
    }
    session_stats_t stats;
    require(session_stats_get(session, &stats) == SESSION_OK && stats.query_used == 0 && stats.result_used == 0,
            "query ledger leaked");
    return status;
}

int main(int argc, char **argv)
{
    require(argc == 2, "usage: test_pending_query_native fixture.totedb");
    session_limits_t limits = {128u << 20, 64u << 20, 32u << 20, 2, 0};
    session_pool_t *pool = NULL;
    session_db_t *db = NULL;
    session_t *session = NULL;
    database_status_t db_status;
    require(session_pool_open(&limits, &pool) == SESSION_OK, "pool open failed");
    session_pool_stats_t empty, baseline, after;
    session_pool_stats(pool, &empty);
    require(session_pool_open_transactional(pool, argv[1], NULL, &db, &db_status) == SESSION_OK &&
            db_status == DATABASE_OK, "database open failed");
    require(session_open(db, &session) == SESSION_OK, "session open failed");
    ackq_options_t options = {.name = "ack_queue"};
    ackq_t *queue = NULL;
    require(ackq_open(session, &options, &queue) == ACKQ_OK, "queue open failed");
    session_pool_stats(pool, &baseline);
    for (malloc_failure = 1; malloc_failure <= 4; malloc_failure++) {
        require(attempt(queue, session) == ACKQ_NOMEM, "payload allocation failure not observed");
        session_pool_stats(pool, &after);
        require(after.shared_used == baseline.shared_used, "payload failure retained ledger bytes");
    }
    malloc_failure = 0;
    size_t failures = 0;
    for (reserve_failure = 1; reserve_failure <= 512; reserve_failure++) {
        ackq_status_t status = attempt(queue, session);
        session_pool_stats(pool, &after);
        require(after.shared_used == baseline.shared_used && after.accounting_faults == 0,
                "reserve failure retained ledger bytes");
        if (status == ACKQ_OK) break;
        failures++;
    }
    require(failures > 0 && reserve_failure <= 512, "reserve fault sweep incomplete");
    reserve_failure = 0;
    require(attempt(queue, session) == ACKQ_OK, "read after failures failed");
    ackq_close(&queue);
    require(session_close(session) == SESSION_OK, "session close failed");
    require(session_pool_close_database(db, &db_status) == SESSION_OK && db_status == DATABASE_OK,
            "database close failed");
    session_pool_stats(pool, &after);
    require(after.shared_used == empty.shared_used && after.accounting_faults == 0, "close leaked");
    require(session_pool_close(pool) == SESSION_OK, "pool close failed");
    printf("{\"checks\":%zu,\"payload_failures\":4,\"reserve_failures\":%zu,\"released\":true}\n", checks, failures);
    return 0;
}
