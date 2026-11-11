/*
 * test_page_file_durable.c -- commit 동기화 단계 검사 (#271, #260)
 *
 * 기본 진입점의 fsync는 fsync(2), full_sync는 Apple 플랫폼에서 F_FULLFSYNC 경로임을 확인한다. 단계마다 commit이 어떤
 * 동기화를 부르는지 세고(NORMAL은 fsync, FULL은 full_sync, OFF는 없음), OFF는 정상 close에서 한 번 동기화하며 내구 확인
 * 기록을 지우는지, 동기화 없이 끝난 프로세스의 commit이 다시 열 때 보이는지 확인한다. 저장 장치가 실제로 캐시를
 * 비우는지(전원 차단)는 확인하지 않는다.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "storage/le.h"
#include "storage/page_file.h"

#define DIR_NAME "__test__page_file_durable_dir"
#define DB_PATH DIR_NAME "/db.streamlite"

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
    unlink(DB_PATH);
    unlink(DB_PATH "-lock");
    unlink(DB_PATH ".partial");
    unlink(DIR_NAME "/plain");
    rmdir(DIR_NAME);
}

static int g_fsync_calls;
static int g_full_calls;
static int (*g_real_fsync)(int fd);
static int (*g_real_full)(int fd);

static int counting_fsync(int fd)
{
    g_fsync_calls++;
    return g_real_fsync(fd);
}

static int counting_full(int fd)
{
    g_full_calls++;
    return g_real_full(fd);
}

static void reset_counts(void)
{
    g_fsync_calls = 0;
    g_full_calls = 0;
}

/* 잠금 파일의 내구 확인 기록이 말하는 세대. 기록이 없으면 UINT64_MAX */
static uint64_t confirmed_generation(void)
{
    uint8_t record[32];
    int fd = open(DB_PATH "-lock", O_RDONLY);
    if (fd < 0) return UINT64_MAX;
    ssize_t got = pread(fd, record, sizeof(record), 0);
    close(fd);
    if (got != (ssize_t)sizeof(record) || memcmp(record + 8, "SLCG", 4) != 0) return UINT64_MAX;
    return le_get_u64(record + 12);
}

/* 한 페이지를 써서 commit한다 */
static page_file_status_t write_commit(page_file_t *file, uint8_t fill, uint32_t *page)
{
    uint8_t body[PAGE_BODY_BYTES];
    memset(body, fill, sizeof(body));
    page_file_status_t status = page_file_alloc(file, page);
    if (status == PAGE_FILE_OK) status = page_file_write(file, *page, PAGE_KIND_OVERFLOW, 0, body);
    return status == PAGE_FILE_OK ? page_file_commit(file) : status;
}

static bool reads_back(uint32_t page, uint8_t fill)
{
    page_file_t *file;
    uint8_t got[PAGE_BODY_BYTES];
    if (page_file_open(DB_PATH, false, &file, NULL) != PAGE_FILE_OK) return false;
    bool ok = page_file_read(file, page, PAGE_KIND_OVERFLOW, 0, got) == PAGE_FILE_OK && got[0] == fill;
    page_file_close(file);
    return ok;
}

static void test_default_entry_points(void)
{
    printf("\n[test_default_entry_points]\n");
    page_file_io_t libc = page_file_default_io();
    CHECK(libc.fsync == fsync, "NORMAL 단계의 기본 동기화는 fsync(2)");
    CHECK(libc.full_sync != NULL, "FULL 단계의 기본 동기화가 있음");
#ifdef __APPLE__
    CHECK(libc.full_sync != fsync, "Apple 플랫폼의 FULL 동기화는 fsync(2) 그대로가 아님(F_FULLFSYNC)");
#endif
    int fd = open(DIR_NAME "/plain", O_RDWR | O_CREAT, 0644);
    int dir = open(DIR_NAME, O_RDONLY);
    assert(fd >= 0 && dir >= 0 && write(fd, "sync", 4) == 4);
    CHECK(libc.full_sync(fd) == 0, "일반 파일 동기화 (errno %d)", errno);
    CHECK(libc.full_sync(dir) == 0, "디렉터리 동기화 (errno %d)", errno);
    /* F_FULLFSYNC를 지원하지 않는 대상은 fsync로 물러나므로 결과가 fsync와 같다 */
    int null = open("/dev/null", O_WRONLY);
    assert(null >= 0);
    int plain = fsync(null), plain_errno = errno;
    int ours = libc.full_sync(null);
    CHECK(ours == plain && (ours == 0 || errno == plain_errno), "미지원 대상은 fsync와 같은 결과 (%d, %d)", ours, plain);
    /* 실패를 fsync로 다시 시도해 성공으로 바꾸지 않는다 */
    close(null);
    errno = 0;
    CHECK(libc.full_sync(null) == -1 && errno == EBADF, "닫힌 fd는 실패 (errno %d)", errno);
    close(fd);
    close(dir);
}

static void test_levels(void)
{
    printf("\n[test_levels]\n");
    page_file_io_t libc = page_file_default_io();
    g_real_fsync = libc.fsync;
    g_real_full = libc.full_sync;
    page_file_io_t counted = libc;
    counted.fsync = counting_fsync;
    counted.full_sync = counting_full;
    page_file_set_default_io(&counted);

    page_file_t *file;
    uint32_t page;
    assert(page_file_create(DB_PATH, &file) == PAGE_FILE_OK);
    CHECK(page_file_durability(file) == PAGE_FILE_DURABILITY_NORMAL, "기본 단계는 NORMAL");
    reset_counts();
    CHECK(write_commit(file, 0x11, &page) == PAGE_FILE_OK && page_file_publish(file) == PAGE_FILE_OK, "생성 commit·게시");
    CHECK(g_fsync_calls >= 3 && g_full_calls == 0, "NORMAL 생성은 fsync만 (fsync %d, full %d)", g_fsync_calls, g_full_calls);

    reset_counts();
    CHECK(write_commit(file, 0x22, &page) == PAGE_FILE_OK, "NORMAL commit");
    CHECK(g_fsync_calls >= 1 && g_full_calls == 0, "NORMAL commit은 fsync (fsync %d, full %d)", g_fsync_calls, g_full_calls);
    CHECK(confirmed_generation() == page_file_control(file)->generation, "NORMAL commit은 내구 확인 기록을 남김");

    page_file_set_durability(file, PAGE_FILE_DURABILITY_FULL);
    reset_counts();
    CHECK(write_commit(file, 0x33, &page) == PAGE_FILE_OK, "FULL commit");
    CHECK(g_full_calls >= 1 && g_fsync_calls == 0, "FULL commit은 full_sync (fsync %d, full %d)", g_fsync_calls, g_full_calls);
    CHECK(confirmed_generation() == page_file_control(file)->generation, "FULL commit은 내구 확인 기록을 남김");

    page_file_set_durability(file, PAGE_FILE_DURABILITY_OFF);
    reset_counts();
    for (uint8_t i = 0; i < 5; i++) CHECK(write_commit(file, (uint8_t)(0x40 + i), &page) == PAGE_FILE_OK, "OFF commit %u", i);
    CHECK(g_fsync_calls == 0 && g_full_calls == 0, "OFF commit은 동기화하지 않음 (fsync %d, full %d)", g_fsync_calls,
          g_full_calls);
    CHECK(confirmed_generation() == 0, "OFF commit은 내구 확인 기록을 지움 (%llu)", (unsigned long long)confirmed_generation());
    page_file_set_durability(file, (page_file_durability_t)99);
    CHECK(page_file_durability(file) == PAGE_FILE_DURABILITY_NORMAL, "알 수 없는 단계는 NORMAL");
    page_file_set_durability(file, PAGE_FILE_DURABILITY_OFF);
    page_file_unlock(file);
    reset_counts();
    CHECK(page_file_close(file) == PAGE_FILE_OK, "OFF 핸들 close");
    CHECK(g_fsync_calls == 1, "OFF 핸들의 정상 close가 한 번 동기화 (%d)", g_fsync_calls);
    CHECK(reads_back(page, 0x44), "다시 열어 마지막 OFF commit을 읽음");
    page_file_set_default_io(NULL);
}

/* OFF 단계로 commit한 프로세스가 close 없이 끝나도 commit은 커널에 남아 다시 열 때 보인다 */
static void test_off_survives_process_exit(void)
{
    printf("\n[test_off_survives_process_exit]\n");
    page_file_t *file;
    uint32_t page = 0;
    assert(page_file_open(DB_PATH, true, &file, NULL) == PAGE_FILE_OK);
    page_file_unlock(file);
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    pid_t child = fork();
    if (child == 0) {
        page_file_t *mine;
        close(pipefd[0]);
        if (page_file_open(DB_PATH, true, &mine, NULL) != PAGE_FILE_OK) _exit(2);
        page_file_set_durability(mine, PAGE_FILE_DURABILITY_OFF);
        if (write_commit(mine, 0x77, &page) != PAGE_FILE_OK) _exit(3);
        if (write(pipefd[1], &page, sizeof(page)) != (ssize_t)sizeof(page)) _exit(4);
        _exit(0);   /* close·동기화 없이 끝난다 */
    }
    close(pipefd[1]);
    int status = 0;
    waitpid(child, &status, 0);
    bool got = read(pipefd[0], &page, sizeof(page)) == (ssize_t)sizeof(page);
    close(pipefd[0]);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && got, "자식 OFF commit (status %d)", status);
    CHECK(reads_back(page, 0x77), "close 없이 끝난 OFF commit이 다시 열 때 보임");
    page_file_close(file);
}

int main(void)
{
    clean_dir();
    assert(mkdir(DIR_NAME, 0755) == 0);
    test_default_entry_points();
    test_levels();
    test_off_survives_process_exit();
    clean_dir();
    printf("\n%s: %d/%d\n", g_fail == 0 ? "ALL PASSED" : "FAILED", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
