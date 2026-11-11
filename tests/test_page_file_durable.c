/*
 * test_page_file_durable.c -- 배포 빌드와 같은 기본 동기화 경로 검사 (#271)
 *
 * 다른 시험 바이너리는 STREAMLITE_TEST_PLAIN_FSYNC로 빌드해 기본 동기화가 fsync(2)다. 이 바이너리만 그 매크로 없이 빌드한
 * page_file을 링크해, Apple 플랫폼에서 기본 동기화가 F_FULLFSYNC 경로이고 생성·commit·게시·재개방이 그 경로로 끝나는지
 * 확인한다. 저장 장치가 실제로 캐시를 비우는지(전원 차단)는 확인하지 않는다.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int g_sync_calls;
static int (*g_real_sync)(int fd);

static int counting_sync(int fd)
{
    g_sync_calls++;
    return g_real_sync(fd);
}

int main(void)
{
    printf("\n[test_default_sync_is_durable]\n");
    clean_dir();
    assert(mkdir(DIR_NAME, 0755) == 0);
    page_file_io_t libc = page_file_default_io();
#ifdef __APPLE__
    CHECK(libc.fsync != fsync, "Apple 플랫폼의 기본 동기화는 fsync(2) 그대로가 아님(F_FULLFSYNC)");
#endif
    int fd = open(DIR_NAME "/plain", O_RDWR | O_CREAT, 0644);
    int dir = open(DIR_NAME, O_RDONLY);
    assert(fd >= 0 && dir >= 0 && write(fd, "sync", 4) == 4);
    CHECK(libc.fsync(fd) == 0, "일반 파일 동기화 (errno %d)", errno);
    CHECK(libc.fsync(dir) == 0, "디렉터리 동기화 (errno %d)", errno);
    /* F_FULLFSYNC를 지원하지 않는 대상은 fsync로 물러나므로 결과가 fsync와 같다 */
    int null = open("/dev/null", O_WRONLY);
    assert(null >= 0);
    int plain = fsync(null), plain_errno = errno;
    int ours = libc.fsync(null);
    CHECK(ours == plain && (ours == 0 || errno == plain_errno), "미지원 대상은 fsync와 같은 결과 (%d, %d)", ours, plain);
    /* 실패를 fsync로 다시 시도해 성공으로 바꾸지 않는다 */
    close(null);
    errno = 0;
    CHECK(libc.fsync(null) == -1 && errno == EBADF, "닫힌 fd는 실패 (errno %d)", errno);
    close(fd);
    close(dir);

    /* 기본 경로로 생성·commit·게시하고 다시 연다. 동기화는 모두 기본 진입점을 거친다 */
    g_real_sync = libc.fsync;
    page_file_io_t counted = libc;
    counted.fsync = counting_sync;
    page_file_set_default_io(&counted);
    page_file_t *file;
    uint32_t page;
    uint8_t body[PAGE_BODY_BYTES], got[PAGE_BODY_BYTES];
    memset(body, 0x5A, sizeof(body));
    assert(page_file_create(DB_PATH, &file) == PAGE_FILE_OK);
    assert(page_file_alloc(file, &page) == PAGE_FILE_OK);
    assert(page_file_write(file, page, PAGE_KIND_OVERFLOW, 0, body) == PAGE_FILE_OK);
    CHECK(page_file_commit(file) == PAGE_FILE_OK && page_file_publish(file) == PAGE_FILE_OK, "기본 경로로 commit·게시");
    CHECK(page_file_commit(file) == PAGE_FILE_OK && page_file_last_commit_outcome(file) == PAGE_FILE_COMMIT_APPLIED,
          "게시 뒤 commit");
    CHECK(g_sync_calls >= 4, "commit·게시·디렉터리 동기화가 기본 진입점을 거침 (%d번)", g_sync_calls);
    assert(page_file_close(file) == PAGE_FILE_OK);
    page_file_set_default_io(NULL);
    page_file_open_info_t info;
    CHECK(page_file_open(DB_PATH, false, &file, &info) == PAGE_FILE_OK && page_file_control(file)->generation == 2 &&
              info.loss == PAGE_FILE_LOSS_NONE && page_file_read(file, page, PAGE_KIND_OVERFLOW, 0, got) == PAGE_FILE_OK &&
              got[0] == 0x5A, "다시 열어 읽음");
    page_file_close(file);
    clean_dir();
    printf("\n%s: %d/%d\n", g_fail == 0 ? "ALL PASSED" : "FAILED", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
