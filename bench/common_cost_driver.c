#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "../src/benchmark.h"
#include "../src/sql/database_cache.h"
#include "../src/sql/database_internal.h"
#include "../src/sql/database_workspace.h"

#define SHARED_BYTES (128u * 1024u * 1024u)
#define MAX_OPERATIONS 100000u

typedef struct {
    size_t used[3], peaks[3], peak, refusals;
} budget_t;

typedef struct {
    const char *path, *pattern;
    size_t rows, operations, group, payload;
    bool cache, workspace, drop_first;
} settings_t;

typedef struct {
    uint64_t begin_ns, statements_ns, commit_ns, active_ns;
    uint64_t open_ns, close_ns, reopen_verify_ns, wall_ns, cpu_ns;
    uint64_t audit_ns, audit_cpu_ns;
    database_page_check_t pages;
    size_t completed, works, buffered_writes, workspace_flushed_images, catalog_clones, workspace_peak;
    size_t cache_hits, cache_misses, cache_evictions, cache_resets;
    size_t baseline_rss, peak_rss, retained_bytes;
    uint64_t *latencies;
    database_workspace_stats_t *work_stats;
#ifdef TOTE_BENCHMARK
    tb_bench_cost phases[TB_PHASE_COUNT];
    uint64_t incomplete, dropped, overflow, records;
#endif
} observation_t;

static uint64_t clock_ns(clockid_t id)
{
    struct timespec now;
    if (clock_gettime(id, &now) != 0) { perror("clock_gettime"); exit(2); }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void require(bool valid, const char *message)
{
    if (!valid) { fprintf(stderr, "%s\n", message); exit(2); }
}

static size_t rss_peak(void)
{
    struct rusage usage;
    require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
#ifdef __APPLE__
    return (size_t)usage.ru_maxrss;
#else
    return (size_t)usage.ru_maxrss * 1024;
#endif
}

static bool reserve(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *budget = ctx;
    size_t sum = budget->used[0] + budget->used[1] + budget->used[2];
    if (bytes > SHARED_BYTES - sum) { budget->refusals++; return false; }
    budget->used[scope] += bytes;
    if (budget->used[scope] > budget->peaks[scope]) budget->peaks[scope] = budget->used[scope];
    if (sum + bytes > budget->peak) budget->peak = sum + bytes;
    return true;
}

static void release(void *ctx, mem_scope_t scope, size_t bytes)
{
    budget_t *budget = ctx;
    require(bytes <= budget->used[scope], "ledger underflow");
    budget->used[scope] -= bytes;
}

static const mem_ledger_ops_t LEDGER = {reserve, release, NULL, NULL};

static void drain(observation_t *out)
{
#ifdef TOTE_BENCHMARK
    tb_bench_record records[TB_BENCH_RECORDS];
    size_t count = tb_bench_drain(records, TB_BENCH_RECORDS);
    if (out == NULL) return;
    out->records += count;
    for (size_t i = 0; i < count; i++) {
        out->incomplete += records[i].incomplete;
        out->dropped += records[i].dropped;
        out->overflow += records[i].overflow;
        for (size_t p = 0; p < TB_PHASE_COUNT; p++) {
            tb_bench_cost *to = &out->phases[p], *from = &records[i].phases[p];
            to->wall_ns += from->wall_ns;
            to->cpu_ns += from->cpu_ns;
            to->exclusive_wall_ns += from->exclusive_wall_ns;
            to->exclusive_cpu_ns += from->exclusive_cpu_ns;
            to->attempts += from->attempts;
            to->bytes += from->bytes;
            to->errors += from->errors;
            to->short_io += from->short_io;
            to->eintr += from->eintr;
        }
    }
#else
    (void)out;
#endif
}

static void command(database_t *db, const char *sql, const lexer_binding_t *binding,
                    const mem_ledger_t *ledger)
{
    TB_REQUEST_SCOPE(request);
    database_exec_options_t options = {.ledger = ledger};
    result_t *result = NULL;
    database_exec_status_t status = database_work_active(db)
        ? database_exec_in_work(db, &options, (const uint8_t *)sql, strlen(sql), binding, &result)
        : database_exec_ex(db, &options, (const uint8_t *)sql, strlen(sql), binding, &result);
    require(status == DATABASE_EXEC_OK && result != NULL, "statement allocation failed");
    if (result_kind(result) == RESULT_KIND_ERROR) {
        const result_error_t *error = result_outcome(result).error;
        fprintf(stderr, "%s: %.*s\n", sql, (int)error->message_len, error->message);
    }
    require(result_kind(result) == RESULT_KIND_COMMAND, "statement failed");
    require(result_command(result)->affected_rows == 1 || strncmp(sql, "CREATE", 6) == 0,
            "unexpected affected rows");
    result_release(&result);
}

static void begin_work(database_t *db)
{
    TB_REQUEST_SCOPE(request);
    require(database_work_begin(db) == DATABASE_OK, "begin failed");
}

static void commit_work(database_t *db)
{
    TB_REQUEST_SCOPE(request);
    database_work_result_t result = database_work_commit(db);
    if (result.outcome != DATABASE_WORK_APPLIED) {
        fprintf(stderr, "commit outcome=%d nomem=%d errno=%d\n",
                result.outcome, result.nomem, result.saved_errno);
    }
    database_work_end(db, result.outcome);
    require(result.outcome == DATABASE_WORK_APPLIED, "commit failed");
}

static database_t *open_database(const settings_t *settings, const mem_ledger_t *ledger)
{
    database_t *db = NULL;
    require(database_open_bounded(settings->path, true, ledger, &db) == DATABASE_OK, "open failed");
    database_cache_set_enabled(db, settings->cache);
    require(database_workspace_set_enabled(db, settings->workspace), "workspace setting failed");
    return db;
}

static void create_fixture(const settings_t *settings)
{
    database_t *db = NULL;
    require(database_create(settings->path, &db) == DATABASE_OK, "fixture already exists or create failed");
    database_cache_set_enabled(db, false);
    require(database_workspace_set_enabled(db, false), "fixture workspace setting failed");
    command(db, "CREATE TABLE counters (id INTEGER PRIMARY KEY, counter INTEGER NOT NULL, payload TEXT NOT NULL)",
            NULL, NULL);
    uint8_t *payload = malloc(settings->payload);
    require(payload != NULL, "fixture payload allocation failed");
    memset(payload, 'x', settings->payload);
    for (size_t i = 0; i < settings->rows; i++) {
        if (i % 32 == 0) begin_work(db);
        value_view_t values[] = {{.type = VALUE_INTEGER, .integer = (int64_t)i + 1},
                                {.type = VALUE_TEXT, .bytes = payload, .size_bytes = settings->payload}};
        lexer_binding_t binding = {values, 2};
        command(db, "INSERT INTO counters VALUES (?,0,?)", &binding, NULL);
        drain(NULL);
        if (i % 32 == 31 || i + 1 == settings->rows) commit_work(db);
        drain(NULL);
    }
    free(payload);
    require(database_close(db) == DATABASE_OK, "fixture close failed");
    puts("{\"fixture\":true}");
}

static uint64_t hash_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t verify_rows(database_t *db, const settings_t *settings, const mem_ledger_t *ledger)
{
    const char *sql = "SELECT id,counter,payload FROM counters ORDER BY id";
    database_exec_options_t options = {.ledger = ledger};
    result_t *result = NULL;
    require(database_exec_ex(db, &options, (const uint8_t *)sql, strlen(sql), NULL, &result) == DATABASE_EXEC_OK,
            "verification allocation failed");
    require(result_kind(result) == RESULT_KIND_ROWS, "verification query failed");
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t rows = 0;
    result_row_t row;
    result_next_t status;
    while ((status = result_next(result, &row)) == RESULT_NEXT_ROW) {
        require(row.cell_count == 3, "wrong verification column count");
        require(row.cells[0].type == VALUE_INTEGER && row.cells[1].type == VALUE_INTEGER,
                "wrong integer types");
        require(row.cells[0].integer == (int64_t)rows + 1, "wrong id ordering");
        require(row.cells[2].type == VALUE_TEXT && row.cells[2].size_bytes == settings->payload,
                "wrong payload type or length");
        for (size_t c = 0; c < 2; c++) {
            uint64_t value = (uint64_t)row.cells[c].integer;
            for (size_t b = 0; b < 8; b++) hash = hash_byte(hash, (uint8_t)(value >> (8 * b)));
        }
        for (size_t b = 0; b < row.cells[2].size_bytes; b++) hash = hash_byte(hash, row.cells[2].bytes[b]);
        rows++;
    }
    require(status == RESULT_NEXT_DONE && rows == settings->rows, "incomplete verification rows");
    result_release(&result);
    return hash;
}

static void capture_cache(database_t *db, database_cache_stats_t *previous, observation_t *out)
{
    database_cache_stats_t current;
    database_cache_stats(db, &current);
    if (current.hits < previous->hits || current.misses < previous->misses) {
        out->cache_resets++;
        *previous = (database_cache_stats_t){0};
    }
    out->cache_hits += current.hits - previous->hits;
    out->cache_misses += current.misses - previous->misses;
    out->cache_evictions += current.evictions - previous->evictions;
    *previous = current;
}

static void update_row(database_t *db, size_t index, const settings_t *settings, const mem_ledger_t *ledger)
{
    char sql[160];
    size_t slot = strcmp(settings->pattern, "miss") == 0 ? index % 96 : index;
    const char *base = "UPDATE counters SET counter=counter+? WHERE id=?";
    if (strcmp(settings->pattern, "hit") == 0) snprintf(sql, sizeof(sql), "%s", base);
    else snprintf(sql, sizeof(sql), "%s /* slot=%zu */", base, slot);
    value_view_t values[] = {{.type = VALUE_INTEGER, .integer = 1},
                            {.type = VALUE_INTEGER, .integer = (int64_t)((index * 17 + 257) % settings->rows) + 1}};
    lexer_binding_t binding = {values, 2};
    if (!(settings->drop_first && index == 0)) command(db, sql, &binding, ledger);
}

static void run_operations(database_t *db, const settings_t *settings, const mem_ledger_t *ledger,
                           observation_t *out)
{
    database_cache_stats_t previous;
    database_cache_stats(db, &previous);
    uint64_t active = clock_ns(CLOCK_MONOTONIC);
    for (size_t i = 0; i < settings->operations; i += settings->group) {
        uint64_t start = clock_ns(CLOCK_MONOTONIC);
        begin_work(db);
        uint64_t prepared = clock_ns(CLOCK_MONOTONIC);
        out->begin_ns += prepared - start;
        drain(out);
        size_t end = i + settings->group;
        for (size_t j = i; j < end; j++) {
            update_row(db, j, settings, ledger);
            capture_cache(db, &previous, out);
            drain(out);
        }
        uint64_t executed = clock_ns(CLOCK_MONOTONIC);
        out->statements_ns += executed - prepared;
        commit_work(db);
        uint64_t committed = clock_ns(CLOCK_MONOTONIC);
        out->commit_ns += committed - executed;
        out->latencies[out->works++] = committed - start;
        out->completed += end - i;
        database_workspace_stats_t workspace;
        database_workspace_stats(db, &workspace);
        out->work_stats[out->works - 1] = workspace;
        out->buffered_writes += workspace.buffered_writes;
        out->workspace_flushed_images += workspace.physical_writes;
        out->catalog_clones += workspace.catalog_clones;
        if (workspace.peak_bytes > out->workspace_peak) out->workspace_peak = workspace.peak_bytes;
        drain(out);
    }
    out->active_ns = clock_ns(CLOCK_MONOTONIC) - active;
}

static void print_result(const settings_t *settings, const budget_t *budget,
                         const observation_t *out, uint64_t hash)
{
    printf("{\"schema\":1,\"completed\":%zu,\"works\":%zu,\"digest\":\"%016" PRIx64 "\","
           "\"cache\":%d,\"workspace\":%d,\"instrumented\":%d,\"drop_first\":%d,",
           out->completed, out->works, hash, settings->cache, settings->workspace,
#ifdef TOTE_BENCHMARK
           1,
#else
           0,
#endif
           settings->drop_first);
    printf("\"wall_ns\":%" PRIu64 ",\"cpu_ns\":%" PRIu64 ",\"active_ns\":%" PRIu64 ","
           "\"open_ns\":%" PRIu64 ",\"close_ns\":%" PRIu64 ",\"reopen_verify_ns\":%" PRIu64 ","
           "\"begin_ns\":%" PRIu64 ",\"statements_ns\":%" PRIu64 ",\"commit_ns\":%" PRIu64 ",",
           out->wall_ns, out->cpu_ns, out->active_ns, out->open_ns, out->close_ns,
           out->reopen_verify_ns, out->begin_ns, out->statements_ns, out->commit_ns);
    printf("\"audit_ns\":%" PRIu64 ",\"audit_cpu_ns\":%" PRIu64 ","
           "\"completion_ns\":%" PRIu64 ",\"completion_cpu_ns\":%" PRIu64 ","
           "\"data_pages\":%" PRIu64 ",\"reachable_pages\":%" PRIu64 ",\"free_pages\":%" PRIu64 ",",
           out->audit_ns, out->audit_cpu_ns, out->wall_ns - out->audit_ns,
           out->cpu_ns - out->audit_cpu_ns, out->pages.data_pages, out->pages.reachable, out->pages.free);
    printf("\"baseline_rss_bytes\":%zu,\"peak_rss_bytes\":%zu,\"ledger_peak_bytes\":%zu,"
           "\"scope_peaks\":[%zu,%zu,%zu],\"retained_bytes\":%zu,\"released_bytes\":%zu,"
           "\"refusals\":%zu,\"workspace_peak_bytes\":%zu,\"buffered_writes\":%zu,"
           "\"workspace_flushed_images\":%zu,\"catalog_clones\":%zu,\"cache_hits\":%zu,"
           "\"cache_misses\":%zu,\"cache_evictions\":%zu,\"cache_resets\":%zu,",
           out->baseline_rss, out->peak_rss, budget->peak, budget->peaks[0], budget->peaks[1], budget->peaks[2],
           out->retained_bytes, budget->used[0] + budget->used[1] + budget->used[2], budget->refusals,
           out->workspace_peak, out->buffered_writes, out->workspace_flushed_images, out->catalog_clones,
           out->cache_hits, out->cache_misses, out->cache_evictions, out->cache_resets);
    printf("\"latencies_ns\":[");
    for (size_t i = 0; i < out->works; i++) printf("%s%" PRIu64, i ? "," : "", out->latencies[i]);
    printf("],\"work_stats\":[");
    for (size_t i = 0; i < out->works; i++) {
        const database_workspace_stats_t *work = &out->work_stats[i];
        printf("%s{\"outcome\":\"APPLIED\",\"peak_bytes\":%zu,\"buffered_writes\":%zu,"
               "\"workspace_flushed_images\":%zu,\"catalog_clones\":%zu}", i ? "," : "", work->peak_bytes,
               work->buffered_writes, work->physical_writes, work->catalog_clones);
    }
    printf("],\"diagnostics\":");
#ifdef TOTE_BENCHMARK
    printf("{\"records\":%" PRIu64 ",\"incomplete\":%" PRIu64 ",\"dropped\":%" PRIu64
           ",\"overflow\":%" PRIu64 ",\"phases\":[", out->records, out->incomplete, out->dropped, out->overflow);
    for (size_t i = 0; i < TB_PHASE_COUNT; i++) {
        const tb_bench_cost *p = &out->phases[i];
        printf("%s{\"wall_ns\":%" PRIu64 ",\"cpu_ns\":%" PRIu64 ",\"exclusive_wall_ns\":%" PRIu64
               ",\"exclusive_cpu_ns\":%" PRIu64 ",\"attempts\":%" PRIu64 ",\"bytes\":%" PRIu64
               ",\"errors\":%" PRIu64 ",\"short_io\":%" PRIu64 ",\"eintr\":%" PRIu64 "}",
               i ? "," : "", p->wall_ns, p->cpu_ns, p->exclusive_wall_ns, p->exclusive_cpu_ns,
               p->attempts, p->bytes, p->errors, p->short_io, p->eintr);
    }
    printf("]}");
#else
    printf("null");
#endif
    printf("}\n");
}

static void run_trial(const settings_t *settings)
{
    observation_t out = {0};
    out.latencies = calloc(settings->operations, sizeof(*out.latencies));
    out.work_stats = calloc(settings->operations, sizeof(*out.work_stats));
    require(out.latencies != NULL && out.work_stats != NULL, "observation allocation failed");
    out.baseline_rss = rss_peak();
    budget_t budget = {0};
    mem_ledger_t ledger = {&LEDGER, &budget};
    uint64_t start = clock_ns(CLOCK_MONOTONIC), cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    database_t *db = open_database(settings, &ledger);
    out.open_ns = clock_ns(CLOCK_MONOTONIC) - start;
    begin_work(db);
    update_row(db, 0, settings, &ledger);
    database_work_end(db, DATABASE_WORK_NOT_APPLIED);
    drain(NULL);
    run_operations(db, settings, &ledger, &out);
    out.retained_bytes = database_resident_bytes(db);
    uint64_t audit = clock_ns(CLOCK_MONOTONIC), audit_cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t hash = verify_rows(db, settings, &ledger);
    out.audit_ns = clock_ns(CLOCK_MONOTONIC) - audit;
    out.audit_cpu_ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - audit_cpu;
    uint64_t closing = clock_ns(CLOCK_MONOTONIC);
    require(database_close(db) == DATABASE_OK, "close failed");
    out.close_ns = clock_ns(CLOCK_MONOTONIC) - closing;
    uint64_t reopening = clock_ns(CLOCK_MONOTONIC);
    db = open_database(settings, &ledger);
    audit = clock_ns(CLOCK_MONOTONIC);
    audit_cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    require(verify_rows(db, settings, &ledger) == hash, "reopen mismatch");
    require(database_check_pages(db, &out.pages) == DATABASE_OK, "page check failed");
    require(out.pages.orphaned == 0 && out.pages.conflicts == 0, "page accounting failed");
    out.audit_ns += clock_ns(CLOCK_MONOTONIC) - audit;
    out.audit_cpu_ns += clock_ns(CLOCK_PROCESS_CPUTIME_ID) - audit_cpu;
    require(database_close(db) == DATABASE_OK, "reopen close failed");
    out.reopen_verify_ns = clock_ns(CLOCK_MONOTONIC) - reopening;
    out.wall_ns = clock_ns(CLOCK_MONOTONIC) - start;
    out.cpu_ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    out.peak_rss = rss_peak();
    require(budget.used[0] + budget.used[1] + budget.used[2] == 0, "ledger leak after close");
    print_result(settings, &budget, &out, hash);
    free(out.latencies);
    free(out.work_stats);
}

static size_t parse_size(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    require(*text != '\0' && *end == '\0' && value > 0 && value <= MAX_OPERATIONS, "invalid numeric input");
    return (size_t)value;
}

int main(int argc, char **argv)
{
    require(argc == 11, "usage: driver fixture|run path rows operations group payload hit|miss|unique cache workspace drop-first");
    for (int i = 8; i < 11; i++) {
        require(strcmp(argv[i], "0") == 0 || strcmp(argv[i], "1") == 0, "invalid boolean input");
    }
    settings_t settings = {.path = argv[2], .rows = parse_size(argv[3]), .operations = parse_size(argv[4]),
                           .group = parse_size(argv[5]), .payload = parse_size(argv[6]), .pattern = argv[7],
                           .cache = strcmp(argv[8], "1") == 0, .workspace = strcmp(argv[9], "1") == 0,
                           .drop_first = strcmp(argv[10], "1") == 0};
    require(settings.operations % settings.group == 0, "operations must divide into complete groups");
    require(strcmp(settings.pattern, "hit") == 0 || strcmp(settings.pattern, "miss") == 0 ||
            strcmp(settings.pattern, "unique") == 0, "invalid SQL pattern");
    if (strcmp(argv[1], "fixture") == 0) create_fixture(&settings);
    else { require(strcmp(argv[1], "run") == 0, "invalid mode"); run_trial(&settings); }
    return 0;
}
