#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage/overflow.h"

#define PAGE_BYTES 128u
#define MAX_PAGES 64u

static int malloc_calls;
static int fail_at;
static size_t largest_request;

void *overflow_test_malloc(size_t size)
{
    if (size > largest_request) largest_request = size;
    return ++malloc_calls == fail_at ? NULL : malloc(size);
}

typedef struct {
    uint8_t data[MAX_PAGES][PAGE_BYTES];
    bool live[MAX_PAGES];
    size_t live_count;
} store_t;

static bool store_allocate(void *context, uint32_t *id)
{
    store_t *store = context;
    for (uint32_t i = 1; i < MAX_PAGES; i++) {
        if (store->live[i]) continue;
        store->live[i] = true;
        store->live_count++;
        *id = i;
        return true;
    }
    return false;
}

static bool store_release(void *context, uint32_t id)
{
    store_t *store = context;
    if (!store->live[id]) return false;
    store->live[id] = false;
    store->live_count--;
    return true;
}

static bool store_read(void *context, uint32_t id, uint8_t *page)
{
    memcpy(page, ((store_t *)context)->data[id], PAGE_BYTES);
    return true;
}

static bool store_write(void *context, uint32_t id, const uint8_t *page)
{
    memcpy(((store_t *)context)->data[id], page, PAGE_BYTES);
    return true;
}

/* #81: 할당 실패는 어느 단계에서든 출력과 저장소 페이지 수를 바꾸지 않는다. */
int main(void)
{
    static store_t store;
    overflow_io_t io = {&store, PAGE_BYTES, store_allocate, store_release,
                        store_read, store_write};
    uint8_t bytes[500];
    for (size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (uint8_t)i;
    overflow_ref_t ref = {0};

    for (fail_at = 1; fail_at <= 2; fail_at++) {
        overflow_ref_t before = {9, 9};
        ref = before;
        malloc_calls = 0;
        assert(overflow_write(&io, bytes, sizeof(bytes), &ref) == OVERFLOW_NOMEM);
        assert(ref.head == 9 && ref.size_bytes == 9 && store.live_count == 0);
    }
    fail_at = 0;
    assert(overflow_write(&io, bytes, sizeof(bytes), &ref) == OVERFLOW_OK);
    size_t pages = store.live_count;

    for (fail_at = 1; fail_at <= 2; fail_at++) {
        uint8_t marker, *out = &marker;
        malloc_calls = 0;
        assert(overflow_read(&io, ref, &out) == OVERFLOW_NOMEM && out == &marker);
        malloc_calls = 0;
        assert(overflow_free(&io, ref) == OVERFLOW_NOMEM && store.live_count == pages);
    }
    fail_at = 0;

    /* #270: 크기가 체인 머리글과 어긋나는 참조는 그 크기로 할당하기 전에 거절한다. */
    overflow_ref_t huge = {ref.head, 0xFFFFFF00u};
    uint8_t untouched, *kept = &untouched;
    largest_request = 0;
    assert(overflow_read(&io, huge, &kept) == OVERFLOW_CORRUPT && kept == &untouched);
    assert(largest_request <= PAGE_BYTES);
    largest_request = 0;
    assert(overflow_free(&io, huge) == OVERFLOW_CORRUPT && store.live_count == pages);
    assert(largest_request <= PAGE_BYTES);

    uint8_t *out = NULL;
    assert(overflow_read(&io, ref, &out) == OVERFLOW_OK);
    assert(memcmp(out, bytes, sizeof(bytes)) == 0);
    free(out);
    assert(overflow_free(&io, ref) == OVERFLOW_OK && store.live_count == 0);
    puts("overflow: allocation failures keep outputs and pages; mismatched sizes are not allocated; next operation succeeds");
    return 0;
}
