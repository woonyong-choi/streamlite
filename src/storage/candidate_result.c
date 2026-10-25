#include "storage/candidate_page.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_digest.h"

#define RESULT_OVERFLOW_PAGES ((CANDIDATE_RESULT_MAX_BYTES + CANDIDATE_PAGE_OVERFLOW_BYTES - 1) / CANDIDATE_PAGE_OVERFLOW_BYTES)

struct candidate_page_result {
    candidate_wal_memory_t memory;
    uint8_t *bytes;
    size_t size;
};

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static candidate_wal_status_t read_overflow(const candidate_result_read_t *request,
    candidate_page_ref_t ref, uint8_t *output)
{
    candidate_page_ref_t seen[RESULT_OVERFLOW_PAGES];
    unsigned count = 0;
    size_t at = 0;
    while (ref.id != 0) {
        if (count == RESULT_OVERFLOW_PAGES || ref.id == request->ref.page.id) return CANDIDATE_WAL_CORRUPT;
        for (unsigned i = 0; i < count; i++) if (seen[i].id == ref.id) return CANDIDATE_WAL_CORRUPT;
        seen[count++] = ref;
        uint8_t page[CANDIDATE_PAGE_BYTES] = {0};
        candidate_wal_status_t status = request->load(request->context, ref, page);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_page_info_t info;
        status = candidate_page_decode(request->uuid, page, sizeof(page), &info);
        if (status != CANDIDATE_WAL_OK) return status;
        if (info.type != 2 || info.location.ref.id != ref.id) return CANDIDATE_WAL_CORRUPT;
        if (info.location.ref.generation != ref.generation) return CANDIDATE_WAL_CORRUPT;
        size_t size = (size_t)read_le(page + 80, 4);
        size_t remaining = (size_t)request->result_bytes - at;
        size_t expected = remaining < CANDIDATE_PAGE_OVERFLOW_BYTES ? remaining : CANDIDATE_PAGE_OVERFLOW_BYTES;
        if (size != expected || size == 0) return CANDIDATE_WAL_CORRUPT;
        memcpy(output + at, page + 88, size); at += size;
        ref = (candidate_page_ref_t){read_le(page + 64, 8), read_le(page + 72, 8)};
        if ((ref.id == 0) != (at == request->result_bytes)) return CANDIDATE_WAL_CORRUPT;
    }
    return at == request->result_bytes ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_page_read_result(const candidate_result_read_t *request,
    candidate_page_result_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (request == NULL || request->load == NULL) return CANDIDATE_WAL_INVALID;
    candidate_result_read_t saved = *request;
    request = &saved;
    if (request->transaction_id == 0) return CANDIDATE_WAL_INVALID;
    if (request->ref.page.id == 0 || request->ref.page.generation == 0) return CANDIDATE_WAL_INVALID;
    if (request->ref.slot_generation == 0) return CANDIDATE_WAL_INVALID;
    uint8_t uuid = 0;
    for (unsigned i = 0; i < 16; i++) uuid |= request->uuid[i];
    if (uuid == 0) return CANDIDATE_WAL_INVALID;
    if (request->result_bytes > CANDIDATE_RESULT_MAX_BYTES) return CANDIDATE_WAL_LIMIT;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (request->memory != NULL) memory = *request->memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    uint8_t page[CANDIDATE_PAGE_BYTES] = {0};
    candidate_wal_status_t status = request->load(request->context, request->ref.page, page);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_page_version_t version;
    status = candidate_page_get_version(request->uuid, page, sizeof(page), request->ref, &version);
    if (status != CANDIDATE_WAL_OK) return status;
    if (version.transaction_id != request->transaction_id || version.is_tombstone) return CANDIDATE_WAL_CORRUPT;
    if (version.payload_bytes != request->result_bytes) return CANDIDATE_WAL_CORRUPT;
    candidate_page_result_t *result = memory.allocate(memory.context, sizeof(*result));
    if (result == NULL) return CANDIDATE_WAL_NOMEM;
    *result = (candidate_page_result_t){.memory = memory, .size = (size_t)request->result_bytes};
    if (result->size > 0) {
        result->bytes = memory.allocate(memory.context, result->size);
        if (result->bytes == NULL) { candidate_page_result_close(&result); return CANDIDATE_WAL_NOMEM; }
        if (version.overflow.id == 0) memcpy(result->bytes, version.inline_bytes, result->size);
        else status = read_overflow(request, version.overflow, result->bytes);
    }
    uint8_t digest[32];
    if (status == CANDIDATE_WAL_OK) status = candidate_digest_sha256(result->bytes, result->size, digest);
    if (status == CANDIDATE_WAL_OK && memcmp(digest, request->digest, 32) != 0) status = CANDIDATE_WAL_CORRUPT;
    if (status != CANDIDATE_WAL_OK) { candidate_page_result_close(&result); return status; }
    *out = result; return CANDIDATE_WAL_OK;
}

const uint8_t *candidate_page_result_bytes(const candidate_page_result_t *result)
{
    return result == NULL ? NULL : result->bytes;
}

size_t candidate_page_result_size(const candidate_page_result_t *result)
{
    return result == NULL ? 0 : result->size;
}

void candidate_page_result_close(candidate_page_result_t **output)
{
    if (output == NULL || *output == NULL) return;
    candidate_page_result_t *result = *output;
    candidate_wal_memory_t memory = result->memory;
    if (result->bytes != NULL) memory.release(memory.context, result->bytes, result->size);
    memory.release(memory.context, result, sizeof(*result)); *output = NULL;
}
