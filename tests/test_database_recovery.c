/*
 * test_database_recovery.c -- 열기·생성 경로의 손상 통지와 복구 계약 검사 (#271)
 *
 * - 성공을 돌려준 최신 commit이 손상되면 쓰기 열기는 DATABASE_LOST_COMMIT으로 거절하고 파일을 바꾸지 않는다. 읽기 열기는
 *   이전 세대로 열리고 database_open_report로 그 사실을 알린다. 받아들인다고 밝힌 열기만 이어 쓴다.
 * - 내구 확인 기록이 없으면(전원 차단 뒤의 끝나지 않은 commit과 구분되지 않음) 쓰기 열기가 스스로 복구하고 보고로 알린다.
 * - 이미 열린 쓰기 핸들도 손상된 세대를 덮어쓰지 않는다.
 * - 생성 도중 죽은 흔적(.partial)과 게시 뒤 정리 실패가 다음 생성을 막지 않는다.
 * 손상은 파일 바이트를 직접 바꿔 만든다. 실제 전원 차단은 시험하지 않는다.
 */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sql/database.h"
#include "sql/result.h"
#include "storage/le.h"
#include "storage/page_file.h"

#define DIR_NAME "build/test_database_recovery_dir"
#define DB_PATH DIR_NAME "/db.streamlite"
#define LOCK_PATH DB_PATH "-lock"
#define PARTIAL_PATH DB_PATH ".partial"

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

static void fresh_dir(void)
{
    clean_dir();
    assert(mkdir(DIR_NAME, 0700) == 0);
}

static bool exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(length > 0 ? (size_t)length : 1);
    assert(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

/* 같은 inode에 다시 쓴다(잠금 파일의 내구 확인 기록이 이 파일을 가리킨 채로 남는다) */
static void spit(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL && fwrite(bytes, 1, size, file) == size);
    fclose(file);
}

static uint64_t file_hash(const char *path)
{
    size_t size;
    uint8_t *bytes = slurp(path, &size);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
    free(bytes);
    return hash;
}

/* 오류 결과면 그 code, 아니면 0. rows가 NULL이 아니면 행 수를 센다 */
static int run(database_t *db, const char *sql, size_t *rows)
{
    result_t *result = NULL;
    database_exec_status_t status = database_exec(db, (const uint8_t *)sql, strlen(sql), NULL, &result);
    assert(status == DATABASE_EXEC_OK && result != NULL);
    int code = 0;
    if (result_kind(result) == RESULT_KIND_ERROR) {
        code = result_outcome(result).error->code;
    } else if (result_kind(result) == RESULT_KIND_ROWS) {
        result_row_t row;
        size_t count = 0;
        while (result_next(result, &row) == RESULT_NEXT_ROW) count++;
        if (rows != NULL) *rows = count;
    }
    result_release(&result);
    return code;
}

static size_t count_rows(database_t *db)
{
    size_t rows = SIZE_MAX;
    int code = run(db, "SELECT id FROM t", &rows);
    return code == 0 ? rows : SIZE_MAX;
}

/* 세대가 큰 제어 슬롯의 페이지 오프셋 */
static size_t newest_control(const uint8_t *bytes)
{
    uint64_t a = le_get_u64(bytes + 32 + 28), b = le_get_u64(bytes + PAGE_FILE_PAGE_BYTES + 32 + 28);
    return b > a ? PAGE_FILE_PAGE_BYTES : 0;
}

/* 최신 commit이 쓴 페이지(manifest 첫 항목) 하나의 1비트를 뒤집는다. 제어 페이지는 그대로다 */
static void damage_newest_commit_page(void)
{
    size_t size;
    uint8_t *bytes = slurp(DB_PATH, &size);
    const uint8_t *body = bytes + newest_control(bytes) + PAGE_ENVELOPE_BYTES;
    assert(le_get_u32(body + 76) > 0);            /* manifest 항목 수 */
    uint32_t page = le_get_u32(body + 80);        /* 첫 항목의 페이지 번호 */
    assert((size_t)(page + 1) * PAGE_FILE_PAGE_BYTES <= size);
    bytes[(size_t)page * PAGE_FILE_PAGE_BYTES + 100] ^= 0x01;
    spit(DB_PATH, bytes, size);
    free(bytes);
}

/* 최신 제어 페이지 자체의 1비트를 뒤집는다 */
static void damage_newest_control(void)
{
    size_t size;
    uint8_t *bytes = slurp(DB_PATH, &size);
    bytes[newest_control(bytes) + 200] ^= 0x01;
    spit(DB_PATH, bytes, size);
    free(bytes);
}

/* id 1, 2를 문장마다 commit한 DB. 닫은 상태로 돌려준다 */
static void build_two_rows(void)
{
    fresh_dir();
    database_t *db;
    assert(database_create(DB_PATH, &db) == DATABASE_OK);
    assert(run(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)", NULL) == 0);
    assert(run(db, "INSERT INTO t VALUES (1, 10)", NULL) == 0);
    assert(run(db, "INSERT INTO t VALUES (2, 20)", NULL) == 0);
    assert(database_close(db) == DATABASE_OK);
}

static void test_lost_commit_refuses_writable_open(void (*damage)(void), const char *label)
{
    printf("\n[test_lost_commit_refuses_writable_open: %s]\n", label);
    build_two_rows();
    size_t good_size;
    uint8_t *good = slurp(DB_PATH, &good_size);
    damage();
    uint64_t damaged = file_hash(DB_PATH);

    database_t *db = NULL;
    database_open_report_t report;
    database_status_t status = database_open(DB_PATH, true, &db);
    CHECK(status == DATABASE_LOST_COMMIT && db == NULL, "쓰기 열기 거절 (%d)", (int)status);
    if (status == DATABASE_OK) {
        printf("  관측: 쓰기 열기 성공, 행 %zu개\n", count_rows(db));
        run(db, "INSERT INTO t VALUES (3, 30)", NULL);
        database_close(db);
    }
    CHECK(file_hash(DB_PATH) == damaged, "거절한 열기는 파일을 바꾸지 않음");

    status = database_open(DB_PATH, false, &db);
    CHECK(status == DATABASE_OK, "읽기 열기는 이전 세대로 열림 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        CHECK(report.fell_back && report.loss == DATABASE_LOSS_CONFIRMED, "물러남과 손실 확정을 조회 (loss %d)", (int)report.loss);
        CHECK(count_rows(db) == 1, "이전 세대의 행 1개 (%zu)", count_rows(db));
        assert(database_close(db) == DATABASE_OK);
    }
    CHECK(file_hash(DB_PATH) == damaged, "읽기 열기도 파일을 바꾸지 않음");

    /* 덮어쓰지 않았으므로 손상된 바이트를 되돌리면 승인된 세대가 돌아온다 */
    spit(DB_PATH, good, good_size);
    status = database_open(DB_PATH, true, &db);
    CHECK(status == DATABASE_OK, "복원 뒤 쓰기 열기 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        CHECK(!report.fell_back && report.loss == DATABASE_LOSS_NONE && count_rows(db) == 2, "승인된 행 2개가 그대로");
        assert(database_close(db) == DATABASE_OK);
    }

    /* 받아들인다고 밝힌 열기는 이전 세대에서 이어 쓰고, 그 뒤에는 정상 파일이다 */
    damage();
    status = database_open_accepting_loss(DB_PATH, &db);
    CHECK(status == DATABASE_OK, "받아들인 쓰기 열기 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        CHECK(report.loss == DATABASE_LOSS_CONFIRMED && count_rows(db) == 1, "보고는 손실 확정, 행 1개");
        CHECK(run(db, "INSERT INTO t VALUES (3, 30)", NULL) == 0 && count_rows(db) == 2, "이어서 INSERT");
        database_open_report(db, &report);
        CHECK(report.loss == DATABASE_LOSS_CONFIRMED, "열 때의 판정은 commit 뒤에도 조회됨");
        assert(database_close(db) == DATABASE_OK);
    }
    status = database_open(DB_PATH, true, &db);
    CHECK(status == DATABASE_OK, "이어 쓴 뒤 보통 쓰기 열기 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        database_page_check_t check;
        CHECK(!report.fell_back && count_rows(db) == 2, "물러나지 않고 행 2개");
        CHECK(database_check_pages(db, &check) == DATABASE_OK && check.orphaned == 0 && check.conflicts == 0,
              "페이지 회계 0");
        assert(database_close(db) == DATABASE_OK);
    }
    free(good);
    clean_dir();
}

static void test_unconfirmed_fallback_recovers_and_reports(void)
{
    printf("\n[test_unconfirmed_fallback_recovers_and_reports]\n");
    fresh_dir();
    database_t *db;
    assert(database_create(DB_PATH, &db) == DATABASE_OK);
    assert(run(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)", NULL) == 0);
    assert(run(db, "INSERT INTO t VALUES (1, 10)", NULL) == 0);
    /* 여기까지 fsync가 끝났다고 적힌 잠금 파일. 다음 commit의 fsync 전에 전원이 끊긴 상태를 만들 때 되돌린다 */
    size_t lock_size;
    uint8_t *lock = slurp(LOCK_PATH, &lock_size);
    assert(run(db, "INSERT INTO t VALUES (2, 20)", NULL) == 0);
    assert(database_close(db) == DATABASE_OK);
    damage_newest_commit_page();
    spit(LOCK_PATH, lock, lock_size);
    free(lock);

    database_open_report_t report;
    database_status_t status = database_open(DB_PATH, true, &db);
    CHECK(status == DATABASE_OK, "끝나지 않은 commit과 구분되지 않으면 쓰기 열기가 스스로 복구 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        CHECK(report.fell_back && report.rolled_back && report.loss == DATABASE_LOSS_POSSIBLE,
              "물러난 사실은 보고로 알림 (loss %d)", (int)report.loss);
        CHECK(count_rows(db) == 1 && run(db, "INSERT INTO t VALUES (2, 21)", NULL) == 0 && count_rows(db) == 2,
              "이전 세대에서 이어 씀");
        assert(database_close(db) == DATABASE_OK);
    }
    status = database_open(DB_PATH, false, &db);
    CHECK(status == DATABASE_OK, "다시 열기 (%d)", (int)status);
    if (status == DATABASE_OK) {
        database_open_report(db, &report);
        CHECK(!report.fell_back && count_rows(db) == 2, "다음 commit 뒤에는 물러나지 않음");
        assert(database_close(db) == DATABASE_OK);
    }
    clean_dir();
}

static void test_open_handle_does_not_overwrite_lost_commit(void)
{
    printf("\n[test_open_handle_does_not_overwrite_lost_commit]\n");
    build_two_rows();
    database_t *held, *other;
    assert(database_open(DB_PATH, true, &held) == DATABASE_OK);
    assert(count_rows(held) == 2);
    assert(database_open(DB_PATH, true, &other) == DATABASE_OK);
    assert(run(other, "INSERT INTO t VALUES (3, 30)", NULL) == 0);
    assert(database_close(other) == DATABASE_OK);
    damage_newest_commit_page();
    uint64_t damaged = file_hash(DB_PATH);

    int code = run(held, "INSERT INTO t VALUES (4, 40)", NULL);
    CHECK(code != 0, "열려 있던 쓰기 핸들의 변경 문장은 오류 (code %d)", code);
    CHECK(file_hash(DB_PATH) == damaged, "손상된 세대를 덮어쓰지 않음");
    CHECK(count_rows(held) == 2, "읽기는 이전 세대의 행 2개 (%zu)", count_rows(held));
    assert(database_close(held) == DATABASE_OK);
    clean_dir();
}

/* ── 생성 ── */

static int g_unlink_fail_left;
static int g_unlink_calls;

static int failing_unlink(const char *path)
{
    g_unlink_calls++;
    if (g_unlink_fail_left > 0) {
        g_unlink_fail_left--;
        errno = EIO;
        return -1;
    }
    return unlink(path);
}

static void test_create_is_retryable(void)
{
    printf("\n[test_create_is_retryable]\n");
    fresh_dir();
    database_t *db = NULL;

    /* 생성 도중 죽은 프로세스가 남긴 partial */
    spit(PARTIAL_PATH, (const uint8_t *)"leftover", 8);
    database_status_t status = database_create(DB_PATH, &db);
    CHECK(status == DATABASE_OK && !exists(PARTIAL_PATH), "남은 partial이 생성을 막지 않음 (%d)", (int)status);
    if (status == DATABASE_OK) assert(database_close(db) == DATABASE_OK);

    /* 게시 뒤 partial 삭제가 한 번 실패: 실패로 돌려주되 게시한 이름을 거둬 다시 만들 수 있다 */
    fresh_dir();
    page_file_io_t io = page_file_default_io();
    io.unlink = failing_unlink;
    page_file_set_default_io(&io);
    g_unlink_fail_left = 1;
    status = database_create(DB_PATH, &db);
    CHECK(status == DATABASE_IO, "정리가 실패한 생성은 실패 (%d)", (int)status);
    CHECK(!exists(DB_PATH) && !exists(PARTIAL_PATH), "실패한 생성은 게시된 파일을 남기지 않음");
    status = database_create(DB_PATH, &db);
    CHECK(status == DATABASE_OK, "같은 경로로 다시 생성 (%d)", (int)status);
    if (status == DATABASE_OK) assert(database_close(db) == DATABASE_OK);

    /* 거두지도 못하면 게시된 빈 DB가 남는다. 다음 생성은 EXISTS이고 그 파일은 열린다 */
    fresh_dir();
    g_unlink_fail_left = 1000;
    status = database_create(DB_PATH, &db);
    CHECK(status == DATABASE_IO && exists(DB_PATH), "거두지 못한 생성은 실패이고 파일이 남음 (%d)", (int)status);
    g_unlink_fail_left = 0;
    page_file_set_default_io(NULL);
    CHECK(database_create(DB_PATH, &db) == DATABASE_EXISTS, "남은 파일 위에 다시 만들지 않음");
    status = database_open(DB_PATH, true, &db);
    CHECK(status == DATABASE_OK && database_table_count(db) == 0, "남은 파일은 빈 DB로 열림 (%d)", (int)status);
    if (status == DATABASE_OK) assert(database_close(db) == DATABASE_OK);
    clean_dir();
}

int main(void)
{
    mkdir("build", 0755);
    test_lost_commit_refuses_writable_open(damage_newest_commit_page, "commit이 쓴 페이지 손상");
    test_lost_commit_refuses_writable_open(damage_newest_control, "최신 제어 페이지 손상");
    test_unconfirmed_fallback_recovers_and_reports();
    test_open_handle_does_not_overwrite_lost_commit();
    test_create_is_retryable();
    clean_dir();
    printf("\n%s: %d/%d\n", g_fail == 0 ? "ALL PASSED" : "FAILED", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
