#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue/ack_queue.h"
#include "storage/page_file.h"

static uint64_t read_calls, read_bytes;

static void require(bool valid, const char *message)
{
    if (!valid) { fprintf(stderr, "%s\n", message); exit(2); }
}

static size_t parse_size(const char *text)
{
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    require(text[0] != '-' && end != text && *end == '\0' && errno == 0 && value <= SIZE_MAX,
            "invalid size");
    return (size_t)value;
}

static ssize_t counted_read(int fd, void *buffer, size_t size, off_t offset)
{
    ssize_t count = pread(fd, buffer, size, offset);
    read_calls++;
    if (count > 0) read_bytes += (uint64_t)count;
    return count;
}

typedef struct {
    uint64_t rows, pages, payload_bytes, payload_hash, last_id;
    size_t payload_peak;
    ackq_status_t status;
} observation_t;

static observation_t query(ackq_t *queue, session_t *session, char **argv)
{
    size_t limit = parse_size(argv[2]);
    bool with_payload = parse_size(argv[3]) != 0, all = parse_size(argv[4]) != 0;
    require(limit > 0 && limit <= 1024, "invalid page limit");
    ackq_item_t *items = calloc(limit, sizeof(*items));
    require(items != NULL, "item allocation failed");
    observation_t result = {.payload_hash = UINT64_C(14695981039346656037)};
    do {
        size_t count = 0, bytes = 0;
        result.status = ackq_list(queue, session, result.last_id, limit, with_payload, items, &count);
        result.pages++;
        if (result.status != ACKQ_OK) { require(count == 0, "partial success on error"); break; }
        for (size_t i = 0; i < count; i++) {
            require(items[i].id == result.last_id + 1, "unexpected row order");
            require(items[i].state == ACKQ_STATE_READY && items[i].created_at_ms == 1000,
                    "unexpected metadata");
            result.last_id = items[i].id;
            bytes += items[i].payload_size;
            for (size_t j = 0; j < items[i].payload_size; j++) {
                result.payload_hash ^= items[i].payload[j];
                result.payload_hash *= UINT64_C(1099511628211);
            }
        }
        result.rows += count;
        result.payload_bytes += bytes;
        if (bytes > result.payload_peak) result.payload_peak = bytes;
        ackq_list_release(items, count);
        if (count == 0) break;
    } while (all);
    free(items);
    return result;
}

int main(int argc, char **argv)
{
    require(argc == 7, "usage: pending_query_driver path page payload all query_bytes cancel");
    page_file_io_t io = {.pread = counted_read};
    page_file_set_default_io(&io);
    session_limits_t limits = {128u << 20, 64u << 20, parse_size(argv[5]), 2, 0};
    session_pool_t *pool = NULL;
    session_db_t *db = NULL;
    session_t *session = NULL;
    database_status_t db_status = DATABASE_OK;
    require(session_pool_open(&limits, &pool) == SESSION_OK, "pool open failed");
    session_pool_stats_t empty;
    require(session_pool_stats(pool, &empty) == SESSION_OK, "empty pool stats failed");
    require(session_pool_open_transactional(pool, argv[1], NULL, &db, &db_status) == SESSION_OK &&
            db_status == DATABASE_OK, "database open failed");
    require(session_open(db, &session) == SESSION_OK, "session open failed");
    ackq_options_t options = {.name = "ack_queue"};
    ackq_t *queue = NULL;
    require(ackq_open(session, &options, &queue) == ACKQ_OK, "queue open failed");
    require(session_close(session) == SESSION_OK, "setup session close failed");
    require(session_open(db, &session) == SESSION_OK, "query session open failed");
    session_pool_stats_t before, after;
    session_stats_t stats;
    database_scan_stats_t scan_before, scan_after;
    session_pool_stats(pool, &before);
    database_scan_stats(session_db_handle(db), &scan_before);
    read_calls = read_bytes = 0;
    if (parse_size(argv[6]) != 0) session_cancel(session);
    observation_t result = query(queue, session, argv);
    session_stats_get(session, &stats);
    session_pool_stats(pool, &after);
    database_scan_stats(session_db_handle(db), &scan_after);
    require(stats.query_used == 0 && stats.result_used == 0 && after.accounting_faults == 0,
            "query ownership leak");
    printf("{\"status\":%d,\"rows\":%" PRIu64 ",\"pages\":%" PRIu64
           ",\"payload_bytes\":%" PRIu64 ",\"payload_hash\":\"%016" PRIx64
           "\",\"payload_live_peak_bytes\":%zu,\"caller_metadata_bytes\":%zu,"
           "\"query_peak_bytes\":%zu,\"shared_peak_bytes\":%zu,\"baseline_bytes\":%zu,"
           "\"released_bytes\":%zu,\"refused\":%" PRIu64 ",\"rows_scanned\":%" PRIu64
           ",\"index_lookups\":%" PRIu64 ",\"index_pages\":%" PRIu64
           ",\"pread_calls\":%" PRIu64 ",\"pread_bytes\":%" PRIu64 "}\n",
           result.status, result.rows, result.pages, result.payload_bytes, result.payload_hash,
           result.payload_peak, parse_size(argv[2]) * sizeof(ackq_item_t), stats.peak, after.shared_peak,
           before.shared_used, after.shared_used, stats.refused,
           scan_after.rows_scanned - scan_before.rows_scanned,
           scan_after.index_lookups - scan_before.index_lookups,
           scan_after.index_pages_read - scan_before.index_pages_read, read_calls, read_bytes);
    ackq_close(&queue);
    require(session_close(session) == SESSION_OK, "query session close failed");
    require(session_pool_close_database(db, &db_status) == SESSION_OK && db_status == DATABASE_OK,
            "database close failed");
    session_pool_stats(pool, &after);
    require(after.shared_used == empty.shared_used && after.accounting_faults == 0, "pool ownership leak");
    require(session_pool_close(pool) == SESSION_OK, "pool close failed");
    return 0;
}
