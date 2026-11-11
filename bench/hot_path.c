/*
 * hot_path.c — 자동 commit·거래·PK 조회·큐 경로를 StreamLite C API와 시스템 SQLite C API로 같은 순서에 실행해 잰다(#260).
 *
 * StreamLite는 명령줄로 받은 공유 라이브러리를 dlopen하므로 같은 바이너리로 다른 commit의 빌드를 비교한다. 이전 판에 없는
 * streamlite_open_ex가 없으면 동기화 단계 인자를 쓰지 않고 연다. 결과는 한 줄에 하나씩 표준 출력에 적는다.
 *
 *   cc -O2 -Iinclude -o build/hot_path bench/hot_path.c -lsqlite3
 *   build/hot_path sl <libstreamlite> <db> <mode> <n> [arg]   # 환경 SL_DURABILITY=normal|full|off (open_ex가 있을 때)
 *   build/hot_path sq <wal|walnormal> <db> <mode> <n> [arg]
 *
 * mode
 *   insert_auto n      자동 commit INSERT n건 뒤 PK 점 조회 n건
 *   insert_txn n       거래 하나에 INSERT n건 뒤 PK 점 조회 n건
 *   queue_cycle n      put·get·ack을 한 건씩 자동 commit으로 n번
 *   queue_batch n b    b건씩 거래 하나로 put, 다음 거래로 get, 다음 거래로 ack(n/b번)
 *   backlog n leased acked   LEASED leased건과 ACKED acked건을 만든 뒤 put·get·ack을 각각 n번 잰다(stats 한 번 포함)
 *   pollers n p        빈 큐를 쉬지 않고 get하는 프로세스 p개를 띄운 채 다른 큐에 put n건을 잰다
 *
 * 파일·잠금·WAL은 실행 전에 지우지 않는다. 호출자가 빈 경로를 준다.
 */
#define _DARWIN_C_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "streamlite.h"

#define DIE(...)                          \
    do {                                  \
        fprintf(stderr, __VA_ARGS__);     \
        fprintf(stderr, "\n");            \
        exit(2);                          \
    } while (0)

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double cpu_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static char LABEL[96];
/* 시스템 호출 계수 interposer(DYLD_INSERT_LIBRARIES)가 있으면 단계마다 0으로 돌리고 단계 끝에 출력한다. 없으면 NULL */
static void (*count_reset)(void);
static void (*count_report)(const char *, double);
static char VAL[101];
static uint8_t PAYLOAD[256];

static void report(const char *phase, double t0, double c0, double ops)
{
    double wall = now_s() - t0, cpu = cpu_s() - c0;
    printf("%-22s %-16s n=%-7.0f wall=%9.4fs %10.1f ops/s %10.1f us/op cpu=%8.1f us/op\n", LABEL, phase, ops, wall,
           ops / wall, wall / ops * 1e6, cpu / ops * 1e6);
    fflush(stdout);
    if (count_report != NULL) count_report(phase, ops);
}

/* ── StreamLite ── */

static int (*sl_open)(const char *, int, streamlite_db_t **);
static int (*sl_open_ex)(const char *, int, const streamlite_open_options_t *, streamlite_db_t **);
static int (*sl_close)(streamlite_db_t **);
static int (*sl_exec)(streamlite_db_t *, streamlite_txn_t *, const char *, const streamlite_param_t *, size_t, uint8_t **,
                      size_t *);
static int (*sl_begin)(streamlite_db_t *, streamlite_txn_t **);
static int (*sl_commit)(streamlite_txn_t **);
static void (*sl_free)(void *);
static int (*slq_open)(streamlite_db_t *, const char *, int, const streamlite_queue_options_t *, streamlite_queue_t **);
static void (*slq_close)(streamlite_queue_t **);
static int (*slq_put)(streamlite_queue_t *, streamlite_txn_t *, const uint8_t *, size_t, const char *, uint64_t *, int *);
static int (*slq_get)(streamlite_queue_t *, streamlite_txn_t *, uint64_t, uint64_t, streamlite_queue_item_t *);
static void (*slq_item_free)(streamlite_queue_item_t *);
static int (*slq_ack)(streamlite_queue_t *, streamlite_txn_t *, uint64_t, uint64_t);
static int (*slq_stats)(streamlite_queue_t *, streamlite_txn_t *, uint64_t[4]);

static void load_sl(const char *path)
{
    void *h = dlopen(path, RTLD_NOW);
    if (h == NULL) DIE("dlopen: %s", dlerror());
#define SYM(v, n)                                      \
    do {                                               \
        *(void **)&v = dlsym(h, n);                    \
        if (v == NULL) DIE("missing %s", n);           \
    } while (0)
    SYM(sl_open, "streamlite_open");
    SYM(sl_close, "streamlite_close");
    SYM(sl_exec, "streamlite_exec");
    SYM(sl_begin, "streamlite_begin");
    SYM(sl_commit, "streamlite_commit");
    SYM(sl_free, "streamlite_free");
    SYM(slq_open, "streamlite_queue_open");
    SYM(slq_close, "streamlite_queue_close");
    SYM(slq_put, "streamlite_queue_put");
    SYM(slq_get, "streamlite_queue_get");
    SYM(slq_item_free, "streamlite_queue_item_free");
    SYM(slq_ack, "streamlite_queue_ack");
    SYM(slq_stats, "streamlite_queue_stats");
    *(void **)&sl_open_ex = dlsym(h, "streamlite_open_ex");
}

static streamlite_db_t *sl_connect(const char *path)
{
    streamlite_db_t *db = NULL;
    const char *level = getenv("SL_DURABILITY");
    int rc;
    if (sl_open_ex != NULL && level != NULL) {
        streamlite_open_options_t options = {0};
        options.durability = strcmp(level, "full") == 0  ? STREAMLITE_DURABILITY_FULL
                             : strcmp(level, "off") == 0 ? STREAMLITE_DURABILITY_OFF
                                                         : STREAMLITE_DURABILITY_NORMAL;
        rc = sl_open_ex(path, 1, &options, &db);
    } else {
        if (level != NULL && strcmp(level, "normal") != 0) DIE("this library has no streamlite_open_ex");
        rc = sl_open(path, 1, &db);
    }
    if (rc != 0) DIE("open rc=%d", rc);
    return db;
}

static void sl_run(streamlite_db_t *db, streamlite_txn_t *txn, const char *sql, const streamlite_param_t *p, size_t np)
{
    uint8_t *out = NULL;
    size_t size = 0;
    int rc = sl_exec(db, txn, sql, p, np, &out, &size);
    if (rc != 0) DIE("exec rc=%d (%s)", rc, sql);
    if (size > 0 && out[0] == 2) DIE("sql error: %.*s (%s)", (int)(size > 13 ? size - 13 : 0), out + 13, sql);
    sl_free(out);
}

static streamlite_queue_t *sl_queue(streamlite_db_t *db, const char *name)
{
    streamlite_queue_t *q = NULL;
    int rc;
    while ((rc = slq_open(db, name, 1, NULL, &q)) == STREAMLITE_BUSY) usleep(1000);
    if (rc != 0) DIE("queue open rc=%d", rc);
    return q;
}

static streamlite_txn_t *sl_txn(streamlite_db_t *db)
{
    streamlite_txn_t *txn = NULL;
    int rc;
    while ((rc = sl_begin(db, &txn)) == STREAMLITE_BUSY) usleep(500);
    if (rc != 0) DIE("begin rc=%d", rc);
    return txn;
}

static void sl_done(streamlite_txn_t *txn)
{
    int rc = sl_commit(&txn);
    if (rc != 0) DIE("commit rc=%d", rc);
}

static void sl_put(streamlite_queue_t *q, streamlite_txn_t *txn)
{
    uint64_t id;
    int rc;
    while ((rc = slq_put(q, txn, PAYLOAD, sizeof(PAYLOAD), NULL, &id, NULL)) == STREAMLITE_BUSY && txn == NULL) usleep(200);
    if (rc != 0) DIE("put rc=%d", rc);
}

static void sl_get(streamlite_queue_t *q, streamlite_txn_t *txn, uint64_t lease_ms, streamlite_queue_item_t *item)
{
    int rc = slq_get(q, txn, 0, lease_ms, item);
    if (rc != 0) DIE("get rc=%d", rc);
}

static void sl_ack(streamlite_queue_t *q, streamlite_txn_t *txn, streamlite_queue_item_t *item)
{
    int rc = slq_ack(q, txn, item->id, item->fence);
    if (rc != 0) DIE("ack rc=%d", rc);
    slq_item_free(item);
}

static void sl_sql(const char *path, const char *mode, int n)
{
    double t0, c0;
    streamlite_db_t *db = sl_connect(path);
    sl_run(db, NULL, "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)", NULL, 0);
    streamlite_param_t p[2] = {{STREAMLITE_PARAM_INTEGER, 0, 0, NULL, 0}, {STREAMLITE_PARAM_TEXT, 0, 0, (const uint8_t *)VAL, 100}};
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    if (strcmp(mode, "insert_auto") == 0) {
        for (int i = 0; i < n; i++) {
            p[0].integer = i + 1;
            sl_run(db, NULL, "INSERT INTO t VALUES (?, ?)", p, 2);
        }
    } else {
        streamlite_txn_t *txn = sl_txn(db);
        for (int i = 0; i < n; i++) {
            p[0].integer = i + 1;
            sl_run(db, txn, "INSERT INTO t VALUES (?, ?)", p, 2);
        }
        sl_done(txn);
    }
    report(mode, t0, c0, n);
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    for (int i = 0; i < n; i++) {
        p[0].integer = (int64_t)((uint64_t)i * 2654435761u % (uint64_t)n) + 1;
        sl_run(db, NULL, "SELECT v FROM t WHERE id = ?", p, 1);
    }
    report("select_pk", t0, c0, n);
    sl_close(&db);
}

static void sl_cycle(streamlite_queue_t *q, int n)
{
    for (int i = 0; i < n; i++) {
        streamlite_queue_item_t item = {0};
        sl_put(q, NULL);
        sl_get(q, NULL, 600000, &item);
        sl_ack(q, NULL, &item);
    }
}

static void sl_batch(streamlite_db_t *db, streamlite_queue_t *q, int n, int b)
{
    streamlite_queue_item_t *items = calloc((size_t)b, sizeof(*items));
    for (int done = 0; done + b <= n; done += b) {
        streamlite_txn_t *txn = sl_txn(db);
        for (int i = 0; i < b; i++) sl_put(q, txn);
        sl_done(txn);
        txn = sl_txn(db);
        for (int i = 0; i < b; i++) sl_get(q, txn, 600000, &items[i]);
        sl_done(txn);
        txn = sl_txn(db);
        for (int i = 0; i < b; i++) sl_ack(q, txn, &items[i]);
        sl_done(txn);
    }
    free(items);
}

/* LEASED leased건(긴 lease)과 ACKED acked건을 500건 거래로 만든다 */
static void sl_backlog_setup(streamlite_db_t *db, streamlite_queue_t *q, int leased, int acked)
{
    const int chunk = 500;
    streamlite_queue_item_t *items = calloc(chunk, sizeof(*items));
    for (int done = 0; done < acked; done += chunk) {
        int m = acked - done < chunk ? acked - done : chunk;
        streamlite_txn_t *txn = sl_txn(db);
        for (int i = 0; i < m; i++) sl_put(q, txn);
        for (int i = 0; i < m; i++) sl_get(q, txn, 600000, &items[i]);
        for (int i = 0; i < m; i++) sl_ack(q, txn, &items[i]);
        sl_done(txn);
    }
    for (int done = 0; done < leased; done += chunk) {
        int m = leased - done < chunk ? leased - done : chunk;
        streamlite_txn_t *txn = sl_txn(db);
        for (int i = 0; i < m; i++) sl_put(q, txn);
        for (int i = 0; i < m; i++) {
            sl_get(q, txn, 24ull * 3600 * 1000, &items[i]);
            slq_item_free(&items[i]);
        }
        sl_done(txn);
    }
    free(items);
}

static void sl_backlog(streamlite_db_t *db, streamlite_queue_t *q, int n, int leased, int acked)
{
    double t0, c0;
    sl_backlog_setup(db, q, leased, acked);
    streamlite_queue_item_t *items = calloc((size_t)n, sizeof(*items));
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    for (int i = 0; i < n; i++) sl_put(q, NULL);
    report("backlog.put", t0, c0, n);
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    for (int i = 0; i < n; i++) sl_get(q, NULL, 600000, &items[i]);
    report("backlog.get", t0, c0, n);
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    for (int i = 0; i < n; i++) sl_ack(q, NULL, &items[i]);
    report("backlog.ack", t0, c0, n);
    uint64_t counts[4];
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    if (slq_stats(q, NULL, counts) != 0) DIE("stats");
    report("backlog.stats", t0, c0, 1);
    free(items);
}

static void sl_poller(const char *path)
{
    streamlite_db_t *db = sl_connect(path);
    streamlite_queue_t *q = sl_queue(db, "idle");
    for (;;) {
        streamlite_queue_item_t item = {0};
        int rc = slq_get(q, NULL, 0, 1000, &item);
        if (rc == STREAMLITE_OK) slq_item_free(&item);
    }
}

static void sl_queue_mode(const char *path, const char *mode, int n, int arg, int arg2)
{
    double t0, c0;
    streamlite_db_t *db = sl_connect(path);
    streamlite_queue_t *q = sl_queue(db, "jobs");
    if (strcmp(mode, "queue_cycle") == 0) {
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        sl_cycle(q, n);
        report("q.cycle", t0, c0, n);
    } else if (strcmp(mode, "queue_batch") == 0) {
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        sl_batch(db, q, n, arg);
        report("q.batch", t0, c0, n / arg * arg);
    } else if (strcmp(mode, "backlog") == 0) {
        sl_backlog(db, q, n, arg, arg2);
    } else if (strcmp(mode, "pollers") == 0) {
        streamlite_queue_t *idle = sl_queue(db, "idle");
        slq_close(&idle);
        pid_t *kids = calloc((size_t)arg, sizeof(pid_t));
        for (int i = 0; i < arg; i++) {
            kids[i] = fork();
            if (kids[i] == 0) sl_poller(path);
        }
        usleep(300000);
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) sl_put(q, NULL);
        report("pollers.put", t0, c0, n);
        for (int i = 0; i < arg; i++) kill(kids[i], SIGKILL);
        for (int i = 0; i < arg; i++) waitpid(kids[i], NULL, 0);
        free(kids);
    } else {
        DIE("unknown mode %s", mode);
    }
    slq_close(&q);
    sl_close(&db);
}

/* ── SQLite ── */

/* 여러 프로세스가 동시에 열 때의 일시적인 SQLITE_PROTOCOL·BUSY는 잠깐 쉬고 다시 시도한다 */
static void sq_exec(sqlite3 *db, const char *sql)
{
    for (int tries = 0;; tries++) {
        char *err = NULL;
        int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
        if (rc == SQLITE_OK) return;
        if ((rc != SQLITE_PROTOCOL && rc != SQLITE_BUSY) || tries > 1000) DIE("sqlite: %s (%s)", err, sql);
        sqlite3_free(err);
        usleep(1000);
    }
}

static sqlite3_stmt *sq_prep(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) DIE("prepare: %s (%s)", sqlite3_errmsg(db), sql);
    return s;
}

static void step_done(sqlite3 *db, sqlite3_stmt *s)
{
    int rc;
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {}
    if (rc != SQLITE_DONE) DIE("step: %s", sqlite3_errmsg(db));
    sqlite3_reset(s);
}

/* setup이 거짓이면(다른 프로세스가 이미 만든 파일) 파일 단위 PRAGMA를 다시 걸지 않는다 */
static sqlite3 *sq_open_with(const char *jm, const char *path, int setup)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) DIE("sqlite open");
    sqlite3_busy_timeout(db, 5000);
    if (setup) {
        sq_exec(db, "PRAGMA page_size=8192");
        sq_exec(db, "PRAGMA journal_mode=WAL");
    }
    sq_exec(db, strcmp(jm, "walnormal") == 0 ? "PRAGMA synchronous=NORMAL" : "PRAGMA synchronous=FULL");
    sq_exec(db, "PRAGMA cache_size=256");
    return db;
}

static sqlite3 *sq_open(const char *jm, const char *path) { return sq_open_with(jm, path, 1); }

/* StreamLite 큐 표와 같은 열·색인(state, idem)과 같은 claim 계약(만료 lease 탐침 포함) */
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *put, *begin, *pick, *pick2, *claim, *commit, *ack, *count;
} sqq_t;

static void sqq_open_with(sqq_t *q, const char *jm, const char *path, const char *table, int setup)
{
    char sql[512];
    q->db = sq_open_with(jm, path, setup);
    snprintf(sql, sizeof(sql),
             "CREATE TABLE IF NOT EXISTS %s (id INTEGER PRIMARY KEY AUTOINCREMENT, state INTEGER NOT NULL, ready_at INTEGER "
             "NOT NULL, lease_until INTEGER NOT NULL, fence INTEGER NOT NULL, attempts INTEGER NOT NULL, idem TEXT NOT NULL, "
             "payload BLOB NOT NULL, note TEXT NOT NULL, created_at INTEGER NOT NULL)", table);
    sq_exec(q->db, sql);
    snprintf(sql, sizeof(sql), "CREATE INDEX IF NOT EXISTS %s_state ON %s (state)", table, table);
    sq_exec(q->db, sql);
    snprintf(sql, sizeof(sql), "CREATE INDEX IF NOT EXISTS %s_idem ON %s (idem)", table, table);
    sq_exec(q->db, sql);
    snprintf(sql, sizeof(sql), "INSERT INTO %s (state, ready_at, lease_until, fence, attempts, idem, payload, note, created_at) "
                               "VALUES (0, ?, 0, 0, 0, '', ?, '', ?) RETURNING id", table);
    q->put = sq_prep(q->db, sql);
    q->begin = sq_prep(q->db, "BEGIN IMMEDIATE");
    snprintf(sql, sizeof(sql), "SELECT id, fence, attempts FROM %s WHERE state = 0 AND ready_at <= ? ORDER BY id LIMIT 1", table);
    q->pick = sq_prep(q->db, sql);
    snprintf(sql, sizeof(sql), "SELECT id, fence, attempts FROM %s WHERE state = 1 AND lease_until <= ? ORDER BY id LIMIT 1", table);
    q->pick2 = sq_prep(q->db, sql);
    snprintf(sql, sizeof(sql), "UPDATE %s SET state = 1, lease_until = ?, fence = ?, attempts = ? WHERE id = ? RETURNING payload", table);
    q->claim = sq_prep(q->db, sql);
    q->commit = sq_prep(q->db, "COMMIT");
    snprintf(sql, sizeof(sql), "UPDATE %s SET state = 2, lease_until = 0, note = '' WHERE id = ? AND state = 1 AND fence = ?", table);
    q->ack = sq_prep(q->db, sql);
    snprintf(sql, sizeof(sql), "SELECT state, COUNT(*) FROM %s GROUP BY state", table);
    q->count = sq_prep(q->db, sql);
}

static void sqq_open(sqq_t *q, const char *jm, const char *path, const char *table) { sqq_open_with(q, jm, path, table, 1); }

static int64_t sq_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sqq_put(sqq_t *q)
{
    sqlite3_bind_int64(q->put, 1, sq_now());
    sqlite3_bind_blob(q->put, 2, PAYLOAD, sizeof(PAYLOAD), SQLITE_STATIC);
    sqlite3_bind_int64(q->put, 3, sq_now());
    step_done(q->db, q->put);
}

/* in_txn이 거짓이면 BEGIN IMMEDIATE·COMMIT으로 감싼다 */
static int sqq_get(sqq_t *q, int in_txn, int64_t lease_ms, int64_t *id, int64_t *fence)
{
    int64_t now = sq_now();
    if (!in_txn) step_done(q->db, q->begin);
    sqlite3_bind_int64(q->pick2, 1, now);
    int rc = sqlite3_step(q->pick2);
    sqlite3_stmt *picked = q->pick2;
    if (rc != SQLITE_ROW) {
        sqlite3_reset(q->pick2);
        sqlite3_bind_int64(q->pick, 1, now);
        rc = sqlite3_step(q->pick);
        picked = q->pick;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_reset(picked);
        if (!in_txn) step_done(q->db, q->commit);
        return 0;
    }
    *id = sqlite3_column_int64(picked, 0);
    *fence = sqlite3_column_int64(picked, 1) + 1;
    int64_t attempts = sqlite3_column_int64(picked, 2) + 1;
    sqlite3_reset(picked);
    sqlite3_bind_int64(q->claim, 1, now + lease_ms);
    sqlite3_bind_int64(q->claim, 2, *fence);
    sqlite3_bind_int64(q->claim, 3, attempts);
    sqlite3_bind_int64(q->claim, 4, *id);
    step_done(q->db, q->claim);
    if (!in_txn) step_done(q->db, q->commit);
    return 1;
}

static void sqq_ack(sqq_t *q, int64_t id, int64_t fence)
{
    sqlite3_bind_int64(q->ack, 1, id);
    sqlite3_bind_int64(q->ack, 2, fence);
    step_done(q->db, q->ack);
    if (sqlite3_changes(q->db) != 1) DIE("ack changed %d", sqlite3_changes(q->db));
}

static void sq_sql(const char *jm, const char *path, const char *mode, int n)
{
    double t0, c0;
    sqlite3 *db = sq_open(jm, path);
    sq_exec(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");
    sqlite3_stmt *ins = sq_prep(db, "INSERT INTO t VALUES (?, ?)");
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    if (strcmp(mode, "insert_txn") == 0) sq_exec(db, "BEGIN");
    for (int i = 0; i < n; i++) {
        sqlite3_bind_int64(ins, 1, i + 1);
        sqlite3_bind_text(ins, 2, VAL, 100, SQLITE_STATIC);
        step_done(db, ins);
    }
    if (strcmp(mode, "insert_txn") == 0) sq_exec(db, "COMMIT");
    report(mode, t0, c0, n);
    sqlite3_stmt *sel = sq_prep(db, "SELECT v FROM t WHERE id = ?");
    t0 = now_s();
    c0 = cpu_s();
    if (count_reset != NULL) count_reset();
    for (int i = 0; i < n; i++) {
        sqlite3_bind_int64(sel, 1, (int64_t)((uint64_t)i * 2654435761u % (uint64_t)n) + 1);
        if (sqlite3_step(sel) != SQLITE_ROW) DIE("select miss");
        sqlite3_reset(sel);
    }
    report("select_pk", t0, c0, n);
    sqlite3_finalize(ins);
    sqlite3_finalize(sel);
    sqlite3_close(db);
}

static void sq_queue_mode(const char *jm, const char *path, const char *mode, int n, int arg, int arg2)
{
    double t0, c0;
    sqq_t q;
    int64_t id, fence;
    pid_t *kids = NULL;
    if (strcmp(mode, "pollers") == 0) {
        /* SQLite 연결을 연 채 fork하지 않는다. 표를 만들고 닫은 뒤 poller를 띄우고 생산자 연결을 연다 */
        sqq_t setup;
        sqq_open(&setup, jm, path, "idle");
        sqlite3_close_v2(setup.db);
        sqq_open(&setup, jm, path, "jobs");
        sqlite3_close_v2(setup.db);
        kids = calloc((size_t)arg, sizeof(pid_t));
        for (int i = 0; i < arg; i++) {
            kids[i] = fork();
            if (kids[i] == 0) {
                sqq_t mine;
                sqq_open_with(&mine, jm, path, "idle", 0);
                for (;;) if (sqq_get(&mine, 0, 1000, &id, &fence)) sqq_ack(&mine, id, fence);
            }
        }
        usleep(300000);
    }
    sqq_open_with(&q, jm, path, "jobs", kids == NULL);
    if (kids != NULL) {
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) sqq_put(&q);
        report("pollers.put", t0, c0, n);
        for (int i = 0; i < arg; i++) kill(kids[i], SIGKILL);
        for (int i = 0; i < arg; i++) waitpid(kids[i], NULL, 0);
        free(kids);
    } else if (strcmp(mode, "queue_cycle") == 0) {
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) {
            sqq_put(&q);
            if (!sqq_get(&q, 0, 600000, &id, &fence)) DIE("empty");
            sqq_ack(&q, id, fence);
        }
        report("q.cycle", t0, c0, n);
    } else if (strcmp(mode, "queue_batch") == 0) {
        int64_t *ids = calloc((size_t)arg, sizeof(int64_t)), *fences = calloc((size_t)arg, sizeof(int64_t));
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int done = 0; done + arg <= n; done += arg) {
            sq_exec(q.db, "BEGIN IMMEDIATE");
            for (int i = 0; i < arg; i++) sqq_put(&q);
            sq_exec(q.db, "COMMIT");
            sq_exec(q.db, "BEGIN IMMEDIATE");
            for (int i = 0; i < arg; i++) if (!sqq_get(&q, 1, 600000, &ids[i], &fences[i])) DIE("empty");
            sq_exec(q.db, "COMMIT");
            sq_exec(q.db, "BEGIN IMMEDIATE");
            for (int i = 0; i < arg; i++) sqq_ack(&q, ids[i], fences[i]);
            sq_exec(q.db, "COMMIT");
        }
        report("q.batch", t0, c0, n / arg * arg);
        free(ids);
        free(fences);
    } else if (strcmp(mode, "backlog") == 0) {
        sq_exec(q.db, "BEGIN IMMEDIATE");
        for (int i = 0; i < arg2; i++) {
            sqq_put(&q);
            if (!sqq_get(&q, 1, 600000, &id, &fence)) DIE("empty");
            sqq_ack(&q, id, fence);
        }
        for (int i = 0; i < arg; i++) sqq_put(&q);
        for (int i = 0; i < arg; i++) if (!sqq_get(&q, 1, 24ll * 3600 * 1000, &id, &fence)) DIE("empty");
        sq_exec(q.db, "COMMIT");
        int64_t *ids = calloc((size_t)n, sizeof(int64_t)), *fences = calloc((size_t)n, sizeof(int64_t));
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) sqq_put(&q);
        report("backlog.put", t0, c0, n);
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) if (!sqq_get(&q, 0, 600000, &ids[i], &fences[i])) DIE("empty");
        report("backlog.get", t0, c0, n);
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        for (int i = 0; i < n; i++) sqq_ack(&q, ids[i], fences[i]);
        report("backlog.ack", t0, c0, n);
        t0 = now_s();
        c0 = cpu_s();
    if (count_reset != NULL) count_reset();
        step_done(q.db, q.count);
        report("backlog.stats", t0, c0, 1);
        free(ids);
        free(fences);
    } else {
        DIE("unknown mode %s", mode);
    }
    sqlite3_close_v2(q.db);
}

int main(int argc, char **argv)
{
    if (argc < 6) DIE("usage: hot_path sl <lib>|sq <wal|walnormal> <db> <mode> <n> [arg] [arg2]");
    *(void **)&count_reset = dlsym(RTLD_DEFAULT, "count_reset");
    *(void **)&count_report = dlsym(RTLD_DEFAULT, "count_report");
    memset(VAL, 'x', 100);
    memset(PAYLOAD, 'p', sizeof(PAYLOAD));
    const char *mode = argv[4];
    int n = atoi(argv[5]);
    int arg = argc > 6 ? atoi(argv[6]) : 0, arg2 = argc > 7 ? atoi(argv[7]) : 0;
    int is_sql = strcmp(mode, "insert_auto") == 0 || strcmp(mode, "insert_txn") == 0;
    if (strcmp(argv[1], "sl") == 0) {
        load_sl(argv[2]);
        const char *level = getenv("SL_DURABILITY");
        snprintf(LABEL, sizeof(LABEL), "SL:%s%s%s", getenv("SL_TAG") != NULL ? getenv("SL_TAG") : "lib", level ? "/" : "",
                 level ? level : "");
        if (is_sql) sl_sql(argv[3], mode, n);
        else sl_queue_mode(argv[3], mode, n, arg, arg2);
    } else {
        snprintf(LABEL, sizeof(LABEL), "SQLite:%s", argv[2]);
        if (is_sql) sq_sql(argv[2], argv[3], mode, n);
        else sq_queue_mode(argv[2], argv[3], mode, n, arg, arg2);
    }
    return 0;
}
