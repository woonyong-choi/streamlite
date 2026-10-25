#include "storage/candidate_control.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define INTERRUPT_LIMIT 32u

struct candidate_control {
    candidate_control_info_t info;
    candidate_wal_io_t io;
    candidate_wal_memory_t memory;
    candidate_control_verify_t verify;
    void *context;
    uint8_t uuid[16], buffer[CANDIDATE_CONTROL_BYTES];
    char name[NAME_MAX + 1];
    uint32_t backend;
    int fd, directory_fd;
    dev_t device;
    ino_t inode;
    pid_t process_id;
    bool is_busy;
};

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool has_owned_file(candidate_control_t *c)
{
    struct stat st;
    if (fstatat(c->directory_fd, c->name, &st, AT_SYMLINK_NOFOLLOW) != 0) return false;
    return S_ISREG(st.st_mode) && st.st_dev == c->device && st.st_ino == c->inode;
}

static candidate_wal_status_t poison(candidate_control_t *c)
{
    c->info.is_poisoned = true; return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t check_access(candidate_control_t *c)
{
    if (c == NULL || c->is_busy) return CANDIDATE_WAL_INVALID;
    if (c->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    if (c->info.is_poisoned) return CANDIDATE_WAL_POISONED;
    return has_owned_file(c) ? CANDIDATE_WAL_OK : poison(c);
}

static candidate_wal_status_t sync_fd(candidate_control_t *c, int fd)
{
    for (unsigned i = 0; i <= INTERRUPT_LIMIT; i++) {
        if (c->io.fsync(fd) == 0) return CANDIDATE_WAL_OK;
        if (errno != EINTR) break;
    }
    return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t transfer(candidate_control_t *c, uint8_t *p, size_t size, uint64_t offset, bool writing)
{
    unsigned interrupted = 0;
    while (size > 0) {
        ssize_t n = writing ? c->io.pwrite(c->fd, p, size, (off_t)offset) : c->io.pread(c->fd, p, size, (off_t)offset);
        if (n < 0 && errno == EINTR && interrupted++ < INTERRUPT_LIMIT) continue;
        if (n <= 0 || (size_t)n > size) return CANDIDATE_WAL_IO;
        p += n; size -= (size_t)n; offset += (uint64_t)n; interrupted = 0;
    }
    return CANDIDATE_WAL_OK;
}

static bool follows(const candidate_control_state_t *old, const candidate_control_state_t *next)
{
    if (next->generation <= old->generation || next->checkpoint_id <= old->checkpoint_id) return false;
    if (next->owner_epoch < old->owner_epoch || next->transaction_highwater < old->transaction_highwater) return false;
    if (next->page_highwater < old->page_highwater || next->request_epoch < old->request_epoch) return false;
    if (next->retention_floor < old->retention_floor || next->visible_csn < old->visible_csn || next->gc_horizon < old->gc_horizon) return false;
    return next->checkpoint_begin >= old->checkpoint_begin && next->checkpoint_end >= old->checkpoint_end && next->redo_begin >= old->redo_begin;
}

static unsigned highest(const candidate_control_t *c)
{
    if ((c->info.valid_mask & 1) == 0) return 1;
    if ((c->info.valid_mask & 2) == 0) return 0;
    return c->info.slots[0].generation > c->info.slots[1].generation ? 0 : 1;
}

static void update_floor(candidate_control_t *c)
{
    c->info.redo_floor = 0;
    for (unsigned i = 0; i < 2; i++) {
        if ((c->info.valid_mask & (1u << i)) == 0) continue;
        uint64_t redo = c->info.slots[i].redo_begin;
        if (c->info.redo_floor == 0 || redo < c->info.redo_floor) c->info.redo_floor = redo;
    }
}

static candidate_wal_status_t read_slots(candidate_control_t *c)
{
    struct stat st;
    if (fstat(c->fd, &st) != 0) return CANDIDATE_WAL_IO;
    if (st.st_size < CANDIDATE_CONTROL_BYTES || st.st_size > CANDIDATE_CONTROL_FILE_BYTES) return CANDIDATE_WAL_CORRUPT;
    for (unsigned i = 0; i < 2; i++) {
        if ((uint64_t)st.st_size < (uint64_t)(i + 1) * CANDIDATE_CONTROL_BYTES) continue;
        candidate_wal_status_t s = transfer(c, c->buffer, sizeof(c->buffer), (uint64_t)i * sizeof(c->buffer), false);
        if (s != CANDIDATE_WAL_OK) return s;
        s = candidate_control_decode(c->uuid, c->buffer, sizeof(c->buffer), &c->info.slots[i]);
        if (s == CANDIDATE_WAL_CORRUPT) continue;
        if (s != CANDIDATE_WAL_OK) return s;
        if (c->info.slots[i].slot != i) return CANDIDATE_WAL_CORRUPT;
        if (c->info.slots[i].backend != c->backend) return CANDIDATE_WAL_UNSUPPORTED;
        c->info.valid_mask |= 1u << i;
    }
    if (c->info.valid_mask == 0) return CANDIDATE_WAL_CORRUPT;
    unsigned first = highest(c);
    if (c->info.valid_mask == 3 && !follows(&c->info.slots[1 - first], &c->info.slots[first])) return CANDIDATE_WAL_CORRUPT;
    update_floor(c);
    for (unsigned at = 0; at < 2; at++) {
        unsigned i = at == 0 ? first : 1 - first;
        if ((c->info.valid_mask & (1u << i)) == 0) continue;
        candidate_wal_status_t s = c->verify(c->context, &c->info.slots[i]);
        if (!has_owned_file(c)) return poison(c);
        if (s == CANDIDATE_WAL_OK) { c->info.verified_mask |= 1u << i; c->info.selected = (int)i; return s; }
        if (s != CANDIDATE_WAL_CORRUPT) return s;
    }
    return CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t release_control(candidate_control_t *c)
{
    candidate_wal_status_t s = CANDIDATE_WAL_OK;
    if (c->fd >= 0 && close(c->fd) != 0) s = CANDIDATE_WAL_IO;
    if (c->directory_fd >= 0 && close(c->directory_fd) != 0) s = CANDIDATE_WAL_IO;
    candidate_wal_memory_t memory = c->memory; memory.release(memory.context, c, sizeof(*c)); return s;
}

static candidate_wal_status_t allocate_control(const char *path, const candidate_control_options_t *options,
    candidate_control_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (path == NULL || options == NULL || options->verify == NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_options_t saved = *options; options = &saved;
    size_t length = strnlen(path, PATH_MAX);
    if (length == 0 || length >= PATH_MAX) return CANDIDATE_WAL_INVALID;
    uint8_t uuid = 0; for (unsigned i = 0; i < 16; i++) uuid |= options->uuid[i];
    if (uuid == 0 || (options->backend != CANDIDATE_CONTROL_WAL && options->backend != CANDIDATE_CONTROL_COW)) return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    candidate_wal_io_t io = {pread,pwrite,fsync,ftruncate};
    if (options->memory != NULL) memory = *options->memory;
    if (options->io != NULL) io = *options->io;
    if (memory.allocate == NULL || memory.release == NULL || io.pread == NULL || io.pwrite == NULL || io.fsync == NULL) return CANDIDATE_WAL_INVALID;
    const char *slash = strrchr(path, '/'); const char *name = slash == NULL ? path : slash + 1;
    size_t name_bytes = strlen(name);
    if (name_bytes == 0 || name_bytes > NAME_MAX || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return CANDIDATE_WAL_INVALID;
    char directory[PATH_MAX];
    if (slash == NULL) memcpy(directory, ".", 2);
    else { size_t size = slash == path ? 1 : (size_t)(slash - path); memcpy(directory, path, size); directory[size] = 0; }
    candidate_control_t *c = memory.allocate(memory.context, sizeof(*c));
    if (c == NULL) return CANDIDATE_WAL_NOMEM;
    memset(c, 0, sizeof(*c)); c->memory = memory; c->io = io; c->verify = options->verify; c->context = options->context;
    memcpy(c->uuid, options->uuid, 16); memcpy(c->name, name, name_bytes + 1); c->backend = options->backend;
    c->fd = -1; c->directory_fd = -1; c->info.selected = -1; c->info.resident_bytes = sizeof(*c); c->process_id = getpid();
    c->directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (c->directory_fd < 0) { release_control(c); return CANDIDATE_WAL_IO; }
    *out = c; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t open_control(const char *path, const candidate_control_options_t *options,
    bool creating, candidate_control_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    candidate_control_t *c = NULL; candidate_wal_status_t s = allocate_control(path, options, &c);
    if (s != CANDIDATE_WAL_OK) { if (out != NULL) *out = NULL; return s; }
    c->is_busy = true;
    c->fd = openat(c->directory_fd, c->name, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (creating ? O_CREAT | O_EXCL : 0), 0600);
    if (c->fd < 0) s = creating && errno == EEXIST ? CANDIDATE_WAL_EXISTS : CANDIDATE_WAL_IO;
    struct stat st;
    if (s == CANDIDATE_WAL_OK && (fstat(c->fd, &st) != 0 || !S_ISREG(st.st_mode))) s = CANDIDATE_WAL_IO;
    if (s == CANDIDATE_WAL_OK) {
        c->device = st.st_dev; c->inode = st.st_ino;
        if (flock(c->fd, LOCK_EX | LOCK_NB) != 0) s = errno == EWOULDBLOCK || errno == EAGAIN ? CANDIDATE_WAL_BUSY : CANDIDATE_WAL_IO;
    }
    if (s == CANDIDATE_WAL_OK && !has_owned_file(c)) s = CANDIDATE_WAL_IO;
    if (s == CANDIDATE_WAL_OK && creating) {
        for (unsigned i = 0; i < 2 && s == CANDIDATE_WAL_OK; i++) s = transfer(c, c->buffer, sizeof(c->buffer), (uint64_t)i * sizeof(c->buffer), true);
        if (s == CANDIDATE_WAL_OK) s = sync_fd(c, c->fd);
        if (s == CANDIDATE_WAL_OK) s = sync_fd(c, c->directory_fd);
    } else if (s == CANDIDATE_WAL_OK) s = read_slots(c);
    if (s == CANDIDATE_WAL_OK && !has_owned_file(c)) s = CANDIDATE_WAL_IO;
    if (s != CANDIDATE_WAL_OK) { release_control(c); return s; }
    c->is_busy = false; *out = c; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_control_create(const char *path, const candidate_control_options_t *options, candidate_control_t **out)
{
    return open_control(path, options, true, out);
}

candidate_wal_status_t candidate_control_open(const char *path, const candidate_control_options_t *options, candidate_control_t **out)
{
    return open_control(path, options, false, out);
}

candidate_wal_status_t candidate_control_publish(candidate_control_t *c, const candidate_control_state_t *next)
{
    candidate_wal_status_t s = check_access(c);
    if (s != CANDIDATE_WAL_OK) return s;
    if (next == NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_state_t actual = *next;
    if (actual.backend != c->backend || memcmp(actual.uuid, c->uuid, 16) != 0) return CANDIDATE_WAL_UNSUPPORTED;
    uint64_t generation = c->info.valid_mask == 0 ? 0 : c->info.slots[highest(c)].generation;
    unsigned target = c->info.selected < 0 ? 0 : 1u - (unsigned)c->info.selected;
    if (generation == UINT64_MAX || (c->info.valid_mask != 0 && c->info.slots[highest(c)].checkpoint_id == UINT64_MAX)) return CANDIDATE_WAL_LIMIT;
    generation++;
    if ((generation - 1) % 2 != target) { if (generation == UINT64_MAX) return CANDIDATE_WAL_LIMIT; generation++; }
    actual.generation = generation; actual.slot = target;
    s = candidate_control_encode(&actual, c->buffer, sizeof(c->buffer));
    if (s != CANDIDATE_WAL_OK) return s;
    if (c->info.valid_mask != 0 && !follows(&c->info.slots[highest(c)], &actual)) return CANDIDATE_WAL_STALE;
    c->is_busy = true;
    s = c->verify(c->context, &actual);
    if (!has_owned_file(c)) s = poison(c);
    if (s == CANDIDATE_WAL_OK) {
        s = transfer(c, c->buffer, sizeof(c->buffer), (uint64_t)target * sizeof(c->buffer), true);
        if (s == CANDIDATE_WAL_OK) s = sync_fd(c, c->fd);
        if (s != CANDIDATE_WAL_OK || !has_owned_file(c)) s = poison(c);
        else {
            c->info.slots[target] = actual; c->info.valid_mask |= 1u << target;
            c->info.verified_mask |= 1u << target; c->info.selected = (int)target; update_floor(c);
        }
    }
    c->is_busy = false; return s;
}

candidate_control_info_t candidate_control_info(const candidate_control_t *c)
{
    return c == NULL ? (candidate_control_info_t){.selected = -1} : c->info;
}

candidate_wal_status_t candidate_control_close(candidate_control_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_t *c = *out;
    if (c == NULL) return CANDIDATE_WAL_OK;
    if (c->is_busy) return CANDIDATE_WAL_INVALID;
    *out = NULL; return release_control(c);
}
