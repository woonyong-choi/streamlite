#include "storage/candidate_wal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/checksum.h"

#define MAX_SEGMENTS (CANDIDATE_WAL_MAX_BYTES / CANDIDATE_WAL_SEGMENT_BYTES)
#define INTERRUPT_LIMIT 32u

struct candidate_wal {
    char prefix[PATH_MAX];
    char directory[PATH_MAX];
    char stem[NAME_MAX + 1];
    uint8_t uuid[16];
    uint8_t *buffer;
    candidate_wal_io_t io;
    candidate_wal_memory_t memory;
    candidate_wal_info_t info;
    uint64_t owner_epoch;
    uint64_t max_bytes;
    uint64_t position;
    uint64_t tail_offset;
    unsigned tail_segment;
    int lock_fd;
    int active_fd;
    pid_t process_id;
    bool is_poisoned;
    bool is_scanning;
    bool needs_force_all;
};

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false;
    return true;
}

static candidate_wal_status_t check_access(candidate_wal_t *w)
{
    if (w == NULL || w->is_scanning) return CANDIDATE_WAL_INVALID;
    if (w->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    return w->is_poisoned ? CANDIDATE_WAL_POISONED : CANDIDATE_WAL_OK;
}

static candidate_wal_status_t poison(candidate_wal_t *w)
{
    w->is_poisoned = true;
    return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t read_full(candidate_wal_t *w, int fd, uint8_t *p, size_t size, uint64_t offset)
{
    unsigned interrupted = 0;
    while (size > 0) {
        ssize_t n = w->io.pread(fd, p, size, (off_t)offset);
        if (n < 0 && errno == EINTR && interrupted++ < INTERRUPT_LIMIT) continue;
        if (n <= 0 || (size_t)n > size) { if (n == 0) errno = EIO; return CANDIDATE_WAL_IO; }
        p += n;
        size -= (size_t)n;
        offset += (uint64_t)n;
        interrupted = 0;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t write_full(candidate_wal_t *w, const uint8_t *p, size_t size, uint64_t offset)
{
    unsigned interrupted = 0;
    while (size > 0) {
        ssize_t n = w->io.pwrite(w->active_fd, p, size, (off_t)offset);
        if (n < 0 && errno == EINTR && interrupted++ < INTERRUPT_LIMIT) continue;
        if (n <= 0 || (size_t)n > size) { if (n == 0) errno = EIO; return CANDIDATE_WAL_IO; }
        p += n;
        size -= (size_t)n;
        offset += (uint64_t)n;
        interrupted = 0;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t sync_fd(candidate_wal_t *w, int fd)
{
    for (unsigned i = 0; i <= INTERRUPT_LIMIT; i++) {
        if (w->io.fsync(fd) == 0) return CANDIDATE_WAL_OK;
        if (errno != EINTR) break;
    }
    return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t sync_directory(candidate_wal_t *w)
{
    int fd = open(w->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return CANDIDATE_WAL_IO;
    candidate_wal_status_t status = sync_fd(w, fd);
    int saved = errno;
    if (close(fd) != 0 && status == CANDIDATE_WAL_OK) return CANDIDATE_WAL_IO;
    errno = saved;
    return status;
}

static int open_segment(candidate_wal_t *w, unsigned id, int flags)
{
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s.wal.%016" PRIx64, w->prefix, (uint64_t)id);
    if (n < 0 || (size_t)n >= sizeof(path)) { errno = ENAMETOOLONG; return -1; }
    return open(path, flags | O_CLOEXEC | O_NOFOLLOW, 0600);
}

static candidate_wal_status_t inventory(candidate_wal_t *w, unsigned *count)
{
    DIR *dir = opendir(w->directory);
    if (dir == NULL) return CANDIDATE_WAL_IO;
    bool seen[MAX_SEGMENTS] = {false};
    size_t stem_bytes = strlen(w->stem);
    unsigned highest = 0;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strncmp(name, w->stem, stem_bytes) != 0) continue;
        if (strncmp(name + stem_bytes, ".wal.", 5) != 0) continue;
        if (strlen(name) != stem_bytes + 21) { status = CANDIDATE_WAL_CORRUPT; break; }
        uint64_t id = 0;
        for (size_t i = stem_bytes + 5; i < stem_bytes + 21; i++) {
            char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                status = CANDIDATE_WAL_CORRUPT;
                break;
            }
            id = id * 16 + (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        if (status != CANDIDATE_WAL_OK) break;
        if (id >= w->max_bytes / CANDIDATE_WAL_SEGMENT_BYTES) { status = CANDIDATE_WAL_LIMIT; break; }
        seen[id] = true;
        if (id + 1 > highest) highest = (unsigned)id + 1;
    }
    if (errno != 0 && status == CANDIDATE_WAL_OK) status = CANDIDATE_WAL_IO;
    if (closedir(dir) != 0 && status == CANDIDATE_WAL_OK) status = CANDIDATE_WAL_IO;
    for (unsigned i = 0; i < highest; i++) if (!seen[i]) status = CANDIDATE_WAL_CORRUPT;
    *count = highest;
    return status;
}

static void encode_segment(candidate_wal_t *w, unsigned id, uint8_t out[64])
{
    memset(out, 0, 64);
    memcpy(out, "SLWAL001", 8);
    write_le(out + 8, 1, 2);
    write_le(out + 10, 64, 2);
    memcpy(out + 16, w->uuid, 16);
    write_le(out + 32, id, 8);
    write_le(out + 40, (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES, 8);
    write_le(out + 48, crc32c(0, out, 64), 4);
}

static candidate_wal_status_t validate_segment(candidate_wal_t *w, unsigned id, const uint8_t *p, size_t size)
{
    uint8_t expected[64];
    encode_segment(w, id, expected);
    if (memcmp(p, expected, size < 8 ? size : 8) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 12 && (read_le(p + 8, 2) != 1 || read_le(p + 10, 2) != 64))
        return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 32 && memcmp(p + 16, w->uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    return memcmp(p, expected, size) == 0 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t validate_frame_header(candidate_wal_t *w, size_t size, uint64_t lsn)
{
    const uint8_t *p = w->buffer;
    if (memcmp(p, "SLWR", size < 4 ? size : 4) != 0) return CANDIDATE_WAL_CORRUPT;
    if (size >= 6 && read_le(p + 4, 2) != 1) return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 8 && (read_le(p + 6, 2) < 1 || read_le(p + 6, 2) > 16))
        return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t total = size >= 12 ? read_le(p + 8, 4) : 96;
    if (total < 96 || total > CANDIDATE_WAL_RECORD_BYTES || total % 8 != 0) return CANDIDATE_WAL_CORRUPT;
    if (total > CANDIDATE_WAL_SEGMENT_BYTES - lsn % CANDIDATE_WAL_SEGMENT_BYTES)
        return CANDIDATE_WAL_CORRUPT;
    if (size >= 16 && ((read_le(p + 12, 4) + 103) & ~(uint64_t)7) != total)
        return CANDIDATE_WAL_CORRUPT;
    if (size >= 24 && read_le(p + 16, 8) != lsn) return CANDIDATE_WAL_CORRUPT;
    if (size >= 40 && read_le(p + 32, 8) >= lsn) return CANDIDATE_WAL_CORRUPT;
    if (size > 40 && memcmp(p + 40, w->uuid, size < 56 ? size - 40 : 16) != 0)
        return CANDIDATE_WAL_UNSUPPORTED;
    if (size >= 64 && read_le(p + 56, 8) == 0) return CANDIDATE_WAL_CORRUPT;
    if (size >= 68 && read_le(p + 64, 4) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    if (size > 72 && !is_zero(p + 72, size - 72)) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_later_frames(candidate_wal_t *w, int fd, unsigned id,
                                               uint64_t offset, uint64_t end)
{
    uint8_t block[8192];
    for (uint64_t at = offset + 8; at + 96 <= end; at += sizeof(block)) {
        size_t size = end - at < sizeof(block) ? (size_t)(end - at) : sizeof(block);
        candidate_wal_status_t status = read_full(w, fd, block, size, at);
        if (status != CANDIDATE_WAL_OK) return status;
        for (size_t j = 0; j + 4 <= size; j += 8) {
            if (memcmp(block + j, "SLWR", 4) != 0 || at + j + 96 > end) continue;
            status = read_full(w, fd, w->buffer, 80, at + j);
            if (status != CANDIDATE_WAL_OK) return status;
            uint64_t lsn = (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES + at + j;
            if (validate_frame_header(w, 80, lsn) != CANDIDATE_WAL_OK) continue;
            size_t total = (size_t)read_le(w->buffer + 8, 4);
            if (total > end - at - j) continue;
            status = read_full(w, fd, w->buffer, total, at + j);
            if (status != CANDIDATE_WAL_OK) return status;
            candidate_wal_record_t record;
            if (candidate_wal_decode(w->uuid, w->buffer, total, &record) == CANDIDATE_WAL_OK)
                return CANDIDATE_WAL_CORRUPT;
        }
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t mark_tail(candidate_wal_t *w, int fd, unsigned id,
                                      uint64_t offset, uint64_t size)
{
    if (id + 1 != w->info.segments) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status = check_later_frames(w, fd, id, offset, size);
    if (status != CANDIDATE_WAL_OK) return status;
    w->info.has_tail = true;
    w->info.tail_bytes = size - offset;
    w->tail_segment = id;
    w->tail_offset = offset;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t scan_padding(candidate_wal_t *w, int fd, unsigned id,
                                         uint64_t offset, uint64_t end)
{
    uint8_t bytes[8192];
    for (uint64_t at = offset; at < end;) {
        size_t size = end - at < sizeof(bytes) ? (size_t)(end - at) : sizeof(bytes);
        candidate_wal_status_t status = read_full(w, fd, bytes, size, at);
        if (status != CANDIDATE_WAL_OK) return status;
        if (!is_zero(bytes, size)) return CANDIDATE_WAL_CORRUPT;
        at += size;
    }
    if (id + 1 < w->info.segments) return CANDIDATE_WAL_OK;
    return mark_tail(w, fd, id, offset, end);
}

typedef struct {
    candidate_wal_visitor_t visitor;
    void *context;
} scan_visitor_t;

static candidate_wal_status_t scan_segment(candidate_wal_t *w, unsigned id, int fd,
                                         const scan_visitor_t *visitor)
{
    struct stat st;
    if (fstat(fd, &st) != 0) return CANDIDATE_WAL_IO;
    if (!S_ISREG(st.st_mode) || st.st_size < 0) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t size = (uint64_t)st.st_size;
    if (size > CANDIDATE_WAL_SEGMENT_BYTES) return CANDIDATE_WAL_CORRUPT;
    if (id + 1 < w->info.segments && size != CANDIDATE_WAL_SEGMENT_BYTES) return CANDIDATE_WAL_CORRUPT;
    size_t head = size < 64 ? (size_t)size : 64;
    candidate_wal_status_t status = read_full(w, fd, w->buffer, head, 0);
    if (status != CANDIDATE_WAL_OK) return status;
    status = validate_segment(w, id, w->buffer, head);
    if (status != CANDIDATE_WAL_OK) return status;
    if (head < 64) return id == 0 ? CANDIDATE_WAL_CORRUPT : mark_tail(w, fd, id, 0, size);
    w->info.valid_end_lsn = (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES + 64;
    for (uint64_t at = 64; at < size;) {
        size_t bytes = size - at < 80 ? (size_t)(size - at) : 80;
        status = read_full(w, fd, w->buffer, bytes, at);
        if (status != CANDIDATE_WAL_OK) return status;
        if (w->buffer[0] == 0) return scan_padding(w, fd, id, at, size);
        uint64_t lsn = (uint64_t)id * CANDIDATE_WAL_SEGMENT_BYTES + at;
        status = validate_frame_header(w, bytes, lsn);
        if (status != CANDIDATE_WAL_OK) return status;
        if (bytes < 80) return mark_tail(w, fd, id, at, size);
        size_t total = (size_t)read_le(w->buffer + 8, 4);
        if (total > size - at) return mark_tail(w, fd, id, at, size);
        status = read_full(w, fd, w->buffer, total, at);
        if (status != CANDIDATE_WAL_OK) return status;
        candidate_wal_record_t record;
        status = candidate_wal_decode(w->uuid, w->buffer, total, &record);
        if (status != CANDIDATE_WAL_OK) return status;
        if (record.owner_epoch < w->info.highest_owner_epoch) return CANDIDATE_WAL_CORRUPT;
        w->info.highest_owner_epoch = record.owner_epoch;
        w->info.records++;
        w->info.valid_end_lsn = lsn + total;
        if (visitor->visitor != NULL && !visitor->visitor(visitor->context, &record))
            return CANDIDATE_WAL_STOPPED;
        at += total;
    }
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_scan(candidate_wal_t *w, candidate_wal_visitor_t visitor,
                                        void *context, candidate_wal_info_t *out)
{
    candidate_wal_status_t status = check_access(w);
    if (status != CANDIDATE_WAL_OK) return status;
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_info_t saved = w->info;
    unsigned saved_tail_segment = w->tail_segment;
    uint64_t saved_tail_offset = w->tail_offset;
    w->info.records = 0;
    w->info.has_tail = false;
    w->info.tail_bytes = 0;
    w->info.highest_owner_epoch = 0;
    status = inventory(w, &w->info.segments);
    if (status == CANDIDATE_WAL_OK && w->info.segments == 0) status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK && saved.segments > 0 && saved.segments != w->info.segments)
        status = CANDIDATE_WAL_CORRUPT;
    scan_visitor_t target = {visitor, context};
    w->is_scanning = true;
    for (unsigned id = 0; status == CANDIDATE_WAL_OK && id < w->info.segments; id++) {
        int fd = open_segment(w, id, O_RDONLY);
        if (fd < 0) { status = CANDIDATE_WAL_IO; break; }
        status = scan_segment(w, id, fd, &target);
        if (close(fd) != 0 && status == CANDIDATE_WAL_OK) status = CANDIDATE_WAL_IO;
    }
    w->is_scanning = false;
    if (status == CANDIDATE_WAL_OK && w->info.valid_end_lsn < saved.durable_end_lsn)
        status = CANDIDATE_WAL_CORRUPT;
    if (status == CANDIDATE_WAL_OK && w->info.highest_owner_epoch > w->owner_epoch) status = CANDIDATE_WAL_STALE;
    if (status != CANDIDATE_WAL_OK) {
        w->info = saved;
        w->tail_segment = saved_tail_segment;
        w->tail_offset = saved_tail_offset;
        if (status != CANDIDATE_WAL_STOPPED) w->is_poisoned = true;
        return status;
    }
    *out = w->info;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t allocate_wal(const char *prefix, const candidate_wal_options_t *options,
                                         candidate_wal_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (prefix == NULL || options == NULL) return CANDIDATE_WAL_INVALID;
    if (options->owner_epoch == 0 || is_zero(options->uuid, 16)) return CANDIDATE_WAL_INVALID;
    size_t size = strlen(prefix);
    if (size == 0 || size + 22 >= PATH_MAX) return CANDIDATE_WAL_INVALID;
    const char *slash = strrchr(prefix, '/');
    const char *stem = slash == NULL ? prefix : slash + 1;
    if (*stem == 0 || strlen(stem) + 21 > NAME_MAX) return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL, allocate_default, release_default};
    if (options->memory != NULL) memory = *options->memory;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_io_t io = {pread, pwrite, fsync, ftruncate};
    if (options->io != NULL) io = *options->io;
    bool valid_io = io.pread != NULL && io.pwrite != NULL;
    valid_io = valid_io && io.fsync != NULL && io.ftruncate != NULL;
    uint64_t max_bytes = options->max_bytes == 0 ? CANDIDATE_WAL_MAX_BYTES : options->max_bytes;
    bool valid_limit = max_bytes >= CANDIDATE_WAL_SEGMENT_BYTES && max_bytes <= CANDIDATE_WAL_MAX_BYTES;
    if (!valid_io || !valid_limit || max_bytes % CANDIDATE_WAL_SEGMENT_BYTES != 0)
        return CANDIDATE_WAL_INVALID;
    candidate_wal_t *w = memory.allocate(memory.context, sizeof(*w));
    if (w == NULL) return CANDIDATE_WAL_NOMEM;
    memset(w, 0, sizeof(*w));
    w->memory = memory;
    w->lock_fd = -1;
    w->active_fd = -1;
    w->process_id = getpid();
    w->owner_epoch = options->owner_epoch;
    w->max_bytes = max_bytes;
    memcpy(w->uuid, options->uuid, 16);
    strcpy(w->prefix, prefix);
    strcpy(w->stem, stem);
    if (slash == NULL) strcpy(w->directory, ".");
    else if (slash == prefix) strcpy(w->directory, "/");
    else memcpy(w->directory, prefix, (size_t)(slash - prefix));
    w->io = io;
    w->buffer = memory.allocate(memory.context, CANDIDATE_WAL_RECORD_BYTES);
    if (w->buffer == NULL) {
        memory.release(memory.context, w, sizeof(*w));
        return CANDIDATE_WAL_NOMEM;
    }
    w->info.resident_bytes = sizeof(*w) + CANDIDATE_WAL_RECORD_BYTES;
    *out = w;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t lock_initial(candidate_wal_t *w, int flags)
{
    w->lock_fd = open_segment(w, 0, flags);
    if (w->lock_fd < 0) return errno == EEXIST ? CANDIDATE_WAL_EXISTS : CANDIDATE_WAL_IO;
    if (flock(w->lock_fd, LOCK_EX | LOCK_NB) != 0)
        return errno == EWOULDBLOCK ? CANDIDATE_WAL_BUSY : CANDIDATE_WAL_IO;
    w->active_fd = w->lock_fd;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_create(const char *prefix, const candidate_wal_options_t *options,
                                          candidate_wal_t **out)
{
    candidate_wal_t *w = NULL;
    candidate_wal_status_t status = allocate_wal(prefix, options, out);
    if (status != CANDIDATE_WAL_OK) return status;
    w = *out;
    unsigned count = 0;
    status = inventory(w, &count);
    if (status == CANDIDATE_WAL_OK && count > 0) status = CANDIDATE_WAL_EXISTS;
    if (status == CANDIDATE_WAL_OK) status = lock_initial(w, O_RDWR | O_CREAT | O_EXCL);
    if (status == CANDIDATE_WAL_OK) {
        encode_segment(w, 0, w->buffer);
        status = write_full(w, w->buffer, 64, 0);
    }
    if (status == CANDIDATE_WAL_OK) status = sync_fd(w, w->active_fd);
    if (status == CANDIDATE_WAL_OK) status = sync_directory(w);
    if (status != CANDIDATE_WAL_OK) { candidate_wal_close(out); return status; }
    w->position = 64;
    w->info.valid_end_lsn = 64;
    w->info.durable_end_lsn = 64;
    w->info.segments = 1;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_open(const char *prefix, const candidate_wal_options_t *options,
                                        candidate_wal_t **out)
{
    candidate_wal_status_t status = allocate_wal(prefix, options, out);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_t *w = *out;
    status = lock_initial(w, O_RDWR);
    candidate_wal_info_t info;
    if (status == CANDIDATE_WAL_OK) status = candidate_wal_scan(w, NULL, NULL, &info);
    if (status == CANDIDATE_WAL_OK && info.segments > 1) {
        w->active_fd = open_segment(w, info.segments - 1, O_RDWR);
        if (w->active_fd < 0) status = CANDIDATE_WAL_IO;
    }
    if (status != CANDIDATE_WAL_OK) { candidate_wal_close(out); return status; }
    w->position = info.valid_end_lsn;
    w->needs_force_all = true;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t rotate(candidate_wal_t *w)
{
    unsigned next = w->info.segments;
    if ((uint64_t)(next + 1) * CANDIDATE_WAL_SEGMENT_BYTES > w->max_bytes) return CANDIDATE_WAL_LIMIT;
    uint64_t offset = w->position - (uint64_t)(next - 1) * CANDIDATE_WAL_SEGMENT_BYTES;
    memset(w->buffer, 0, CANDIDATE_WAL_RECORD_BYTES);
    while (offset < CANDIDATE_WAL_SEGMENT_BYTES) {
        size_t size = CANDIDATE_WAL_SEGMENT_BYTES - offset;
        if (size > CANDIDATE_WAL_RECORD_BYTES) size = CANDIDATE_WAL_RECORD_BYTES;
        if (write_full(w, w->buffer, size, offset) != CANDIDATE_WAL_OK) return poison(w);
        offset += size;
    }
    if (sync_fd(w, w->active_fd) != CANDIDATE_WAL_OK) return poison(w);
    if (w->active_fd != w->lock_fd) {
        int previous_fd = w->active_fd;
        w->active_fd = -1;
        if (close(previous_fd) != 0) return poison(w);
    }
    w->active_fd = open_segment(w, next, O_RDWR | O_CREAT | O_EXCL);
    if (w->active_fd < 0) return poison(w);
    encode_segment(w, next, w->buffer);
    if (write_full(w, w->buffer, 64, 0) != CANDIDATE_WAL_OK) return poison(w);
    if (sync_fd(w, w->active_fd) != CANDIDATE_WAL_OK) return poison(w);
    if (sync_directory(w) != CANDIDATE_WAL_OK) return poison(w);
    w->position = (uint64_t)next * CANDIDATE_WAL_SEGMENT_BYTES + 64;
    w->info.segments++;
    w->info.valid_end_lsn = w->position;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_append(candidate_wal_t *w, const candidate_wal_record_t *record,
                                          uint64_t *end_lsn)
{
    candidate_wal_status_t status = check_access(w);
    if (status != CANDIDATE_WAL_OK) return status;
    if (record == NULL || end_lsn == NULL) return CANDIDATE_WAL_INVALID;
    if (w->info.has_tail) return CANDIDATE_WAL_TAIL;
    if (record->type < 1 || record->type > 16) return CANDIDATE_WAL_UNSUPPORTED;
    if (record->payload_bytes > CANDIDATE_WAL_RECORD_BYTES - 96) return CANDIDATE_WAL_LIMIT;
    if (record->payload_bytes > 0 && record->payload == NULL) return CANDIDATE_WAL_INVALID;
    if (record->previous_lsn >= w->position) return CANDIDATE_WAL_INVALID;
    size_t total = (96 + record->payload_bytes + 7) & ~(size_t)7;
    uint64_t offset = w->position - (uint64_t)(w->info.segments - 1) * CANDIDATE_WAL_SEGMENT_BYTES;
    uint64_t remaining = CANDIDATE_WAL_SEGMENT_BYTES - offset;
    if (total > remaining) {
        status = rotate(w);
        if (status != CANDIDATE_WAL_OK) return status;
    }
    candidate_wal_record_t actual = *record;
    actual.lsn = w->position;
    actual.owner_epoch = w->owner_epoch;
    status = candidate_wal_encode(w->uuid, &actual, w->buffer, CANDIDATE_WAL_RECORD_BYTES, &total);
    if (status != CANDIDATE_WAL_OK) return status;
    if (write_full(w, w->buffer, total, w->position % CANDIDATE_WAL_SEGMENT_BYTES) != CANDIDATE_WAL_OK)
        return poison(w);
    w->position += total;
    w->info.records++;
    w->info.highest_owner_epoch = w->owner_epoch;
    w->info.valid_end_lsn = w->position;
    *end_lsn = w->position;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_force(candidate_wal_t *w, uint64_t through_end_lsn)
{
    candidate_wal_status_t status = check_access(w);
    if (status != CANDIDATE_WAL_OK) return status;
    if (w->info.has_tail) return CANDIDATE_WAL_TAIL;
    if (through_end_lsn > w->info.valid_end_lsn) return CANDIDATE_WAL_INVALID;
    if (through_end_lsn <= w->info.durable_end_lsn) return CANDIDATE_WAL_OK;
    unsigned first = w->needs_force_all ? 0 : w->info.segments - 1;
    for (unsigned id = first; id < w->info.segments; id++) {
        int fd = open_segment(w, id, O_RDWR);
        if (fd < 0) return poison(w);
        status = sync_fd(w, fd);
        if (close(fd) != 0) status = CANDIDATE_WAL_IO;
        if (status != CANDIDATE_WAL_OK) return poison(w);
    }
    if (w->needs_force_all && sync_directory(w) != CANDIDATE_WAL_OK) return poison(w);
    w->needs_force_all = false;
    w->info.durable_end_lsn = w->info.valid_end_lsn;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_wal_repair_tail(candidate_wal_t *w)
{
    candidate_wal_status_t status = check_access(w);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_wal_info_t info;
    status = candidate_wal_scan(w, NULL, NULL, &info);
    if (status != CANDIDATE_WAL_OK || !info.has_tail) return status;
    if (w->io.ftruncate(w->active_fd, (off_t)w->tail_offset) != 0) return poison(w);
    uint64_t offset = w->tail_offset;
    if (offset == 0) {
        encode_segment(w, w->tail_segment, w->buffer);
        if (write_full(w, w->buffer, 64, 0) != CANDIDATE_WAL_OK) return poison(w);
        offset = 64;
    }
    if (sync_fd(w, w->active_fd) != CANDIDATE_WAL_OK) return poison(w);
    w->position = (uint64_t)w->tail_segment * CANDIDATE_WAL_SEGMENT_BYTES + offset;
    w->info.valid_end_lsn = w->position;
    w->info.has_tail = false;
    w->info.tail_bytes = 0;
    w->needs_force_all = true;
    return CANDIDATE_WAL_OK;
}

candidate_wal_info_t candidate_wal_info(const candidate_wal_t *w)
{
    return w == NULL ? (candidate_wal_info_t){0} : w->info;
}

candidate_wal_status_t candidate_wal_close(candidate_wal_t **wal)
{
    if (wal == NULL) return CANDIDATE_WAL_INVALID;
    candidate_wal_t *w = *wal;
    if (w == NULL) return CANDIDATE_WAL_OK;
    if (w->is_scanning) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = CANDIDATE_WAL_OK;
    if (w->active_fd >= 0 && w->active_fd != w->lock_fd) {
        if (close(w->active_fd) != 0) status = CANDIDATE_WAL_IO;
    }
    if (w->lock_fd >= 0 && close(w->lock_fd) != 0) status = CANDIDATE_WAL_IO;
    candidate_wal_memory_t memory = w->memory;
    if (w->buffer != NULL) memory.release(memory.context, w->buffer, CANDIDATE_WAL_RECORD_BYTES);
    memory.release(memory.context, w, sizeof(*w));
    *wal = NULL;
    return status;
}
