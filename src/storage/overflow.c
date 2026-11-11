#include <stdlib.h>
#include <string.h>

#include "storage/checksum.h"
#include "storage/le.h"
#include "storage/overflow.h"

#define OVERFLOW_HEADER_BYTES 28u
#define OVERFLOW_CHECKSUM_OFFSET 24u

typedef struct {
    uint32_t index;
    uint32_t next;
    uint32_t chunk;
    uint32_t total;
    uint32_t head;
} page_header_t;

/* 체크섬 필드를 뺀 페이지 전체의 CRC32C. 패딩 손상도 잡는다. */
static uint32_t checksum(const uint8_t *page, size_t page_bytes)
{
    uint32_t crc = crc32c(0, page, OVERFLOW_CHECKSUM_OFFSET);
    return crc32c(crc, page + OVERFLOW_HEADER_BYTES, page_bytes - OVERFLOW_HEADER_BYTES);
}

static bool io_valid(const overflow_io_t *io)
{
    return io != NULL && io->allocate != NULL && io->release != NULL &&
           io->read != NULL && io->write != NULL &&
           io->page_bytes >= OVERFLOW_MIN_PAGE_BYTES &&
           io->page_bytes <= OVERFLOW_MAX_PAGE_BYTES;
}

static size_t payload_bytes(const overflow_io_t *io)
{
    return io->page_bytes - OVERFLOW_HEADER_BYTES;
}

static size_t page_count(const overflow_io_t *io, size_t size_bytes)
{
    size_t payload = payload_bytes(io);
    return size_bytes / payload + (size_bytes % payload != 0);
}

static void encode_page(uint8_t *page, size_t page_bytes, page_header_t header,
                        const uint8_t *chunk)
{
    memset(page, 0, page_bytes);
    memcpy(page, "SLO2", 4);
    le_put_u32(page + 4, header.index);
    le_put_u32(page + 8, header.next);
    le_put_u32(page + 12, header.chunk);
    le_put_u32(page + 16, header.total);
    le_put_u32(page + 20, header.head);
    if (header.chunk > 0) memcpy(page + OVERFLOW_HEADER_BYTES, chunk, header.chunk);
    le_put_u32(page + OVERFLOW_CHECKSUM_OFFSET, checksum(page, page_bytes));
}

static bool decode_page(const uint8_t *page, size_t page_bytes, page_header_t *header)
{
    if (memcmp(page, "SLO2", 4) != 0) return false;
    header->index = le_get_u32(page + 4);
    header->next = le_get_u32(page + 8);
    header->chunk = le_get_u32(page + 12);
    header->total = le_get_u32(page + 16);
    header->head = le_get_u32(page + 20);
    if (header->chunk > page_bytes - OVERFLOW_HEADER_BYTES) return false;
    return le_get_u32(page + OVERFLOW_CHECKSUM_OFFSET) == checksum(page, page_bytes);
}

static void release_all(const overflow_io_t *io, const uint32_t *ids, size_t count,
                        bool *failed)
{
    for (size_t i = 0; i < count; i++) {
        if (!io->release(io->context, ids[i])) *failed = true;
    }
}

static overflow_status_t write_pages(const overflow_io_t *io, const uint8_t *bytes,
                                     size_t size_bytes, const uint32_t *ids,
                                     size_t count, uint8_t *page)
{
    size_t payload = payload_bytes(io);
    for (size_t i = 0; i < count; i++) {
        size_t offset = i * payload;
        size_t chunk = size_bytes - offset < payload ? size_bytes - offset : payload;
        page_header_t header = {(uint32_t)i, i + 1 < count ? ids[i + 1] : OVERFLOW_NO_PAGE,
                                (uint32_t)chunk, (uint32_t)size_bytes, ids[0]};
        encode_page(page, io->page_bytes, header, bytes + offset);
        if (!io->write(io->context, ids[i], page)) return OVERFLOW_IO;
    }
    return OVERFLOW_OK;
}

static int compare_ids(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

/* 같은 호출에서 받은 번호가 겹치면 true. scratch는 count개 이상이고 정렬 결과로 덮인다. */
static bool has_duplicate(const uint32_t *ids, size_t count, uint32_t *scratch)
{
    memcpy(scratch, ids, count * sizeof(uint32_t));
    qsort(scratch, count, sizeof(uint32_t), compare_ids);
    for (size_t i = 1; i < count; i++) {
        if (scratch[i] == scratch[i - 1]) return true;
    }
    return false;
}

/* 정렬된 scratch에서 중복을 건너뛰어 번호마다 한 번만 반납한다. */
static void release_unique(const overflow_io_t *io, const uint32_t *sorted, size_t count,
                           bool *failed)
{
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && sorted[i] == sorted[i - 1]) continue;
        if (!io->release(io->context, sorted[i])) *failed = true;
    }
}

static overflow_status_t allocate_pages(const overflow_io_t *io, uint32_t *ids,
                                        size_t count, size_t *allocated)
{
    for (size_t i = 0; i < count; i++) {
        uint32_t id = OVERFLOW_NO_PAGE;
        if (!io->allocate(io->context, &id)) return OVERFLOW_IO;
        if (id == OVERFLOW_NO_PAGE) return OVERFLOW_INVALID;
        ids[i] = id;
        *allocated = i + 1;
    }
    return OVERFLOW_OK;
}

overflow_status_t overflow_write(const overflow_io_t *io, const uint8_t *bytes,
                                 size_t size_bytes, overflow_ref_t *ref)
{
    if (!io_valid(io) || ref == NULL) return OVERFLOW_INVALID;
    if (size_bytes > UINT32_MAX) return OVERFLOW_LIMIT;
    if (size_bytes > 0 && bytes == NULL) return OVERFLOW_INVALID;
    if (size_bytes == 0) {
        *ref = (overflow_ref_t){OVERFLOW_NO_PAGE, 0};
        return OVERFLOW_OK;
    }
    size_t count = page_count(io, size_bytes);
    if (count > SIZE_MAX / (2 * sizeof(uint32_t))) return OVERFLOW_LIMIT;
    uint32_t *ids = malloc(2 * count * sizeof(uint32_t));
    uint8_t *page = malloc(io->page_bytes);
    if (ids == NULL || page == NULL) {
        free(ids);
        free(page);
        return OVERFLOW_NOMEM;
    }
    size_t allocated = 0;
    overflow_status_t status = allocate_pages(io, ids, count, &allocated);
    bool duplicate = status == OVERFLOW_OK && has_duplicate(ids, count, ids + count);
    if (duplicate) status = OVERFLOW_INVALID;
    if (status == OVERFLOW_OK) status = write_pages(io, bytes, size_bytes, ids, count, page);
    if (status == OVERFLOW_OK) {
        *ref = (overflow_ref_t){ids[0], (uint32_t)size_bytes};
    } else {
        bool failed = false;
        if (duplicate) {
            release_unique(io, ids + count, allocated, &failed);
        } else {
            release_all(io, ids, allocated, &failed);
        }
        if (failed) status = OVERFLOW_ROLLBACK_FAILED;
    }
    free(ids);
    free(page);
    return status;
}

static overflow_status_t check_page(const overflow_io_t *io, const uint8_t *page,
                                    size_t index, overflow_ref_t ref, size_t remaining,
                                    page_header_t *header)
{
    if (!decode_page(page, io->page_bytes, header)) return OVERFLOW_CORRUPT;
    size_t payload = payload_bytes(io);
    size_t expected = remaining < payload ? remaining : payload;
    if (header->index != index || header->total != ref.size_bytes) return OVERFLOW_CORRUPT;
    if (header->head != ref.head) return OVERFLOW_CORRUPT;
    if (header->chunk != expected) return OVERFLOW_CORRUPT;
    bool last = remaining == expected;
    if (last != (header->next == OVERFLOW_NO_PAGE)) return OVERFLOW_CORRUPT;
    return OVERFLOW_OK;
}

/*
 * 머리 페이지를 읽어 ref와 대조한다. ref의 크기는 호출자가 준 값이므로 이 검사를 지난 뒤에만 할당에 쓴다.
 * 머리글의 전체 크기가 ref와 같아야 하므로 손상된 ref가 큰 할당을 일으키지 못한다.
 */
static overflow_status_t read_head(const overflow_io_t *io, overflow_ref_t ref, uint8_t *page,
                                   page_header_t *header)
{
    if (!io->read(io->context, ref.head, page)) return OVERFLOW_IO;
    return check_page(io, page, 0, ref, ref.size_bytes, header);
}

/*
 * 머리 다음 페이지부터 체인을 검증하며 ids와 dest를 채운다. 둘 다 호출자가 크기를 보장하고 없으면 NULL이다.
 * head는 read_head가 검증한 머리글이고 머리 페이지의 몫은 호출자가 이미 채웠다.
 */
static overflow_status_t walk_chain(const overflow_io_t *io, overflow_ref_t ref, page_header_t head,
                                    uint8_t *page, uint32_t *ids, uint8_t *dest)
{
    size_t remaining = ref.size_bytes - head.chunk;
    uint32_t id = head.next;
    for (size_t index = 1; remaining > 0; index++) {
        if (id == OVERFLOW_NO_PAGE) return OVERFLOW_CORRUPT;
        if (!io->read(io->context, id, page)) return OVERFLOW_IO;
        page_header_t header;
        overflow_status_t status = check_page(io, page, index, ref, remaining, &header);
        if (status != OVERFLOW_OK) return status;
        if (ids != NULL) ids[index] = id;
        if (dest != NULL) {
            memcpy(dest + (ref.size_bytes - remaining), page + OVERFLOW_HEADER_BYTES,
                   header.chunk);
        }
        remaining -= header.chunk;
        id = header.next;
    }
    return OVERFLOW_OK;
}

static bool ref_valid(overflow_ref_t ref)
{
    return (ref.head == OVERFLOW_NO_PAGE) == (ref.size_bytes == 0);
}

overflow_status_t overflow_read(const overflow_io_t *io, overflow_ref_t ref,
                                uint8_t **bytes)
{
    if (!io_valid(io) || bytes == NULL || !ref_valid(ref)) return OVERFLOW_INVALID;
    if (ref.size_bytes == 0) {
        *bytes = NULL;
        return OVERFLOW_OK;
    }
    uint8_t *page = malloc(io->page_bytes);
    if (page == NULL) return OVERFLOW_NOMEM;
    page_header_t head;
    overflow_status_t status = read_head(io, ref, page, &head);
    if (status != OVERFLOW_OK) {
        free(page);
        return status;
    }
    uint8_t *data = malloc(ref.size_bytes);
    if (data == NULL) {
        free(page);
        return OVERFLOW_NOMEM;
    }
    memcpy(data, page + OVERFLOW_HEADER_BYTES, head.chunk);
    status = walk_chain(io, ref, head, page, NULL, data);
    free(page);
    if (status != OVERFLOW_OK) {
        free(data);
        return status;
    }
    *bytes = data;
    return OVERFLOW_OK;
}

overflow_status_t overflow_free(const overflow_io_t *io, overflow_ref_t ref)
{
    if (!io_valid(io) || !ref_valid(ref)) return OVERFLOW_INVALID;
    if (ref.size_bytes == 0) return OVERFLOW_OK;
    size_t count = page_count(io, ref.size_bytes);
    if (count > SIZE_MAX / sizeof(uint32_t)) return OVERFLOW_LIMIT;
    uint8_t *page = malloc(io->page_bytes);
    if (page == NULL) return OVERFLOW_NOMEM;
    page_header_t head;
    overflow_status_t status = read_head(io, ref, page, &head);
    if (status != OVERFLOW_OK) {
        free(page);
        return status;
    }
    uint32_t *ids = malloc(count * sizeof(uint32_t));
    if (ids == NULL) {
        free(page);
        return OVERFLOW_NOMEM;
    }
    ids[0] = ref.head;
    status = walk_chain(io, ref, head, page, ids, NULL);
    if (status == OVERFLOW_OK) {
        bool failed = false;
        release_all(io, ids, count, &failed);
        if (failed) status = OVERFLOW_UNRECLAIMED;
    }
    free(ids);
    free(page);
    return status;
}
