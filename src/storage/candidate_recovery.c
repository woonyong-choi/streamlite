#include "candidate_recovery_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define INTERRUPT_LIMIT 32u

struct candidate_recovery {
    candidate_wal_memory_t memory;
    candidate_wal_io_t io;
    candidate_wal_t *wal;
    candidate_control_t *control;
    candidate_outcome_witness_t *outcomes;
    candidate_wal_witness_t *terminal_history;
    candidate_recovery_info_t info;
    uint8_t uuid[16];
    char name[NAME_MAX + 1];
    int fd, directory_fd;
    dev_t device;
    ino_t inode;
    pid_t process_id;
    bool is_busy, is_poisoned, is_terminal_repair;
};

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

static bool has_owned_file(candidate_recovery_t *r)
{
    struct stat st;
    if (fstatat(r->directory_fd,r->name,&st,AT_SYMLINK_NOFOLLOW) != 0) return false;
    return S_ISREG(st.st_mode) && st.st_dev == r->device && st.st_ino == r->inode;
}

static candidate_wal_status_t check_file(candidate_recovery_t *r)
{
    if (r->process_id != getpid()) return CANDIDATE_WAL_FORKED;
    if (r->is_poisoned) return CANDIDATE_WAL_POISONED;
    if (has_owned_file(r)) return CANDIDATE_WAL_OK;
    r->is_poisoned = true; return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t page_offset(uint64_t id, uint64_t *offset)
{
    if (id == 0) return CANDIDATE_WAL_INVALID;
    if (id > (uint64_t)INT64_MAX / 8192) return CANDIDATE_WAL_LIMIT;
    *offset = (id - 1) * 8192; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t read_target(void *context, uint64_t id, uint8_t bytes[8192], bool *present)
{
    candidate_recovery_t *r = context; uint64_t offset;
    candidate_wal_status_t status = check_file(r);
    if (status == CANDIDATE_WAL_OK) status = page_offset(id,&offset);
    if (status != CANDIDATE_WAL_OK) return status;
    memset(bytes,0,8192); size_t at = 0; unsigned interrupted = 0;
    while (at < 8192) {
        ssize_t n = r->io.pread(r->fd,bytes + at,8192 - at,(off_t)(offset + at));
        if (n < 0 && errno == EINTR && interrupted++ < INTERRUPT_LIMIT) continue;
        if (n < 0 || (size_t)n > 8192 - at) return CANDIDATE_WAL_IO;
        if (n == 0) break;
        at += (size_t)n; interrupted = 0;
    }
    *present = at != 0;
    return check_file(r);
}

static candidate_wal_status_t write_target(void *context, uint64_t id, const uint8_t bytes[8192])
{
    candidate_recovery_t *r = context; uint64_t offset;
    candidate_wal_status_t status = check_file(r);
    if (status == CANDIDATE_WAL_OK) status = page_offset(id,&offset);
    if (status != CANDIDATE_WAL_OK) return status;
    size_t at = 0; unsigned interrupted = 0;
    while (at < 8192) {
        ssize_t n = r->io.pwrite(r->fd,bytes + at,8192 - at,(off_t)(offset + at));
        if (n < 0 && errno == EINTR && interrupted++ < INTERRUPT_LIMIT) continue;
        if (n <= 0 || (size_t)n > 8192 - at) return CANDIDATE_WAL_IO;
        at += (size_t)n; interrupted = 0;
    }
    return check_file(r);
}

static candidate_wal_status_t sync_target(void *context)
{
    candidate_recovery_t *r = context;
    candidate_wal_status_t status = check_file(r); if (status != CANDIDATE_WAL_OK) return status;
    for (unsigned i = 0; i <= INTERRUPT_LIMIT; i++) {
        if (r->io.fsync(r->fd) == 0) return check_file(r);
        if (errno != EINTR) break;
    }
    return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t load_target(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    bool present = false;
    candidate_wal_status_t status = read_target(context,ref.id,bytes,&present);
    if (status == CANDIDATE_WAL_OK && !present) return CANDIDATE_WAL_CORRUPT;
    return status;
}

static candidate_wal_status_t verify_anchor(void *context, const candidate_control_state_t *anchor)
{
    candidate_recovery_t *r = context;
    candidate_outcome_witness_close(&r->outcomes);
    r->info = (candidate_recovery_info_t){.anchor = *anchor};
    if (r->is_terminal_repair) {
        candidate_wal_witness_close(&r->terminal_history);
        candidate_terminal_options_t request = {.anchor = anchor,.memory = &r->memory}; memcpy(request.uuid,r->uuid,16);
        return candidate_terminal_prepare(r->wal,&request,&r->terminal_history);
    }
    if (anchor->catalog_root.id != 0) return CANDIDATE_WAL_UNSUPPORTED;
    candidate_redo_options_t redo = {.checkpoint = anchor,.memory = &r->memory,
        .target = {r,read_target,write_target,sync_target}};
    memcpy(redo.uuid,r->uuid,16);
    candidate_wal_status_t status = candidate_redo_execute(r->wal,&redo,&r->info.redo);
    candidate_catalog_load_t load = {.root = anchor->outcome_root,.load = load_target,.context = r,.memory = &r->memory};
    memcpy(load.uuid,r->uuid,16);
    if (status == CANDIDATE_WAL_OK) status = candidate_recovery_validate(r->wal,anchor,&load,&r->info);
    if (status == CANDIDATE_WAL_OK) {
        candidate_outcome_witness_options_t options = {.catalog = load,.checkpoint = anchor};
        status = candidate_outcome_witness_load(r->wal,&options,&r->outcomes);
    }
    if (status == CANDIDATE_WAL_OK) status = check_file(r);
    return status;
}

static bool copy_path(const char *source, char target[PATH_MAX])
{
    if (source == NULL) return false;
    size_t n = strnlen(source,PATH_MAX);
    if (n == 0 || n == PATH_MAX) return false;
    memcpy(target,source,n + 1); return true;
}

static candidate_wal_status_t open_data(candidate_recovery_t *r, const char *path)
{
    const char *slash = strrchr(path,'/'), *name = slash == NULL ? path : slash + 1;
    size_t n = strlen(name);
    if (n == 0 || n > NAME_MAX || strcmp(name,".") == 0 || strcmp(name,"..") == 0) return CANDIDATE_WAL_INVALID;
    memcpy(r->name,name,n + 1); char directory[PATH_MAX];
    if (slash == NULL) memcpy(directory,".",2);
    else { size_t length = slash == path ? 1 : (size_t)(slash - path); memcpy(directory,path,length); directory[length] = 0; }
    r->directory_fd = open(directory,O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (r->directory_fd < 0) return CANDIDATE_WAL_IO;
    r->fd = openat(r->directory_fd,r->name,O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (r->fd < 0) return CANDIDATE_WAL_IO;
    struct stat st;
    if (fstat(r->fd,&st) != 0 || !S_ISREG(st.st_mode)) return CANDIDATE_WAL_IO;
    r->device = st.st_dev; r->inode = st.st_ino;
    if (flock(r->fd,LOCK_EX | LOCK_NB) != 0)
        return errno == EWOULDBLOCK || errno == EAGAIN ? CANDIDATE_WAL_BUSY : CANDIDATE_WAL_IO;
    return check_file(r);
}

static candidate_wal_status_t open_recovery(const candidate_recovery_options_t *options, bool repairing, candidate_recovery_t **out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    if (options == NULL) return CANDIDATE_WAL_INVALID;
    candidate_recovery_options_t saved = *options;
    char wal_path[PATH_MAX], control_path[PATH_MAX], data_path[PATH_MAX];
    if (!copy_path(saved.wal_prefix,wal_path) || !copy_path(saved.control_path,control_path) || !copy_path(saved.data_path,data_path))
        return CANDIDATE_WAL_INVALID;
    candidate_wal_memory_t memory = {NULL,allocate_default,release_default};
    candidate_wal_io_t io = {pread,pwrite,fsync,ftruncate};
    if (saved.memory != NULL) memory = *saved.memory;
    if (saved.io != NULL) io = *saved.io;
    if (memory.allocate == NULL || memory.release == NULL) return CANDIDATE_WAL_INVALID;
    if (io.pread == NULL || io.pwrite == NULL || io.fsync == NULL || io.ftruncate == NULL) return CANDIDATE_WAL_INVALID;
    uint8_t uuid = 0; for (unsigned i = 0; i < 16; i++) uuid |= saved.uuid[i];
    if (uuid == 0 || saved.owner_epoch == 0) return CANDIDATE_WAL_INVALID;
    candidate_recovery_t *r = memory.allocate(memory.context,sizeof(*r));
    if (r == NULL) return CANDIDATE_WAL_NOMEM;
    memset(r,0,sizeof(*r)); r->memory = memory; r->io = io; memcpy(r->uuid,saved.uuid,16);
    r->process_id = getpid(); r->fd = -1; r->directory_fd = -1; r->is_busy = true;
    r->is_terminal_repair = repairing;
    candidate_wal_status_t status = open_data(r,data_path);
    if (status == CANDIDATE_WAL_OK) {
        candidate_wal_options_t wo = {.owner_epoch = saved.owner_epoch,.io = &r->io,.memory = &r->memory}; memcpy(wo.uuid,r->uuid,16);
        status = candidate_wal_open(wal_path,&wo,&r->wal);
    }
    if (status == CANDIDATE_WAL_OK) {
        candidate_control_options_t co = {.backend = CANDIDATE_CONTROL_WAL,.io = &r->io,.memory = &r->memory,
            .verify = verify_anchor,.context = r}; memcpy(co.uuid,r->uuid,16);
        status = candidate_control_open(control_path,&co,&r->control);
    }
    if (status != CANDIDATE_WAL_OK) { r->is_busy = false; candidate_recovery_close(&r); *out = NULL; return status; }
    r->info.resident_bytes = sizeof(*r) + candidate_wal_info(r->wal).resident_bytes;
    r->info.resident_bytes += candidate_control_info(r->control).resident_bytes;
    r->info.resident_bytes += candidate_outcome_witness_info(r->outcomes).resident_bytes;
    r->info.resident_bytes += candidate_wal_witness_info(r->terminal_history).resident_bytes;
    r->is_busy = false; *out = r;
    return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_recovery_open(const candidate_recovery_options_t *options, candidate_recovery_t **out)
{
    return open_recovery(options,false,out);
}

candidate_wal_status_t candidate_recovery_resolve_terminals(const candidate_recovery_options_t *options,
    candidate_recovery_terminal_info_t *out)
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    *out = (candidate_recovery_terminal_info_t){0};
    if (options == NULL) return CANDIDATE_WAL_INVALID;
    uint64_t owner_epoch = options->owner_epoch;
    candidate_recovery_t *r = NULL; candidate_recovery_terminal_info_t info = {0};
    candidate_wal_status_t status = open_recovery(options,true,&r);
    if (status != CANDIDATE_WAL_OK) return status;
    r->is_busy = true;
    candidate_terminal_options_t request = {.owner_epoch = owner_epoch,.anchor = &r->info.anchor,.memory = &r->memory};
    memcpy(request.uuid,r->uuid,16);
    status = check_file(r);
    if (status == CANDIDATE_WAL_OK) status = candidate_terminal_resolve(r->wal,&request,&r->terminal_history,&info);
    if (status == CANDIDATE_WAL_OK) status = check_file(r);
    r->is_busy = false;
    if (candidate_recovery_close(&r) != CANDIDATE_WAL_OK && status == CANDIDATE_WAL_OK) status = CANDIDATE_WAL_IO;
    if (status == CANDIDATE_WAL_OK) *out = info;
    return status;
}

candidate_wal_status_t candidate_recovery_query(candidate_recovery_t *r,
    const candidate_catalog_query_t *query, candidate_recovery_answer_t *answer)
{
    if (answer == NULL) return CANDIDATE_WAL_INVALID;
    candidate_catalog_query_t saved;
    if (query != NULL) saved = *query;
    *answer = (candidate_recovery_answer_t){.lookup = CANDIDATE_CATALOG_UNAVAILABLE};
    if (r == NULL || query == NULL) return CANDIDATE_WAL_INVALID;
    if (r->is_busy) return CANDIDATE_WAL_INVALID;
    candidate_wal_status_t status = check_file(r); if (status != CANDIDATE_WAL_OK) return status;
    r->is_busy = true;
    candidate_recovery_answer_t result = {0}; const candidate_outcome_t *found = NULL;
    result.lookup = candidate_outcome_witness_lookup(r->outcomes,&saved,&found);
    if (found != NULL) {
        result.outcome = *found;
        if (found->result.page.id != 0) {
            candidate_result_read_t request = {.ref = found->result,.transaction_id = found->transaction_id,
                .result_bytes = found->result_bytes,.load = load_target,.context = r,.memory = &r->memory};
            memcpy(request.uuid,r->uuid,16); memcpy(request.digest,found->result_digest,32);
            status = candidate_page_read_result(&request,&result.body);
        }
    }
    if (status == CANDIDATE_WAL_OK) status = check_file(r);
    if (status == CANDIDATE_WAL_OK) *answer = result;
    else candidate_page_result_close(&result.body);
    r->is_busy = false; return status;
}

candidate_recovery_info_t candidate_recovery_info(const candidate_recovery_t *r)
{
    return r == NULL ? (candidate_recovery_info_t){0} : r->info;
}

candidate_wal_status_t candidate_recovery_close(candidate_recovery_t **out)
{
    if (out == NULL || *out == NULL) return CANDIDATE_WAL_OK;
    candidate_recovery_t *r = *out;
    if (r->is_busy) return CANDIDATE_WAL_INVALID;
    *out = NULL;
    candidate_wal_status_t status = candidate_control_close(&r->control);
    if (candidate_wal_close(&r->wal) != CANDIDATE_WAL_OK) status = CANDIDATE_WAL_IO;
    if (r->fd >= 0 && close(r->fd) != 0) status = CANDIDATE_WAL_IO;
    if (r->directory_fd >= 0 && close(r->directory_fd) != 0) status = CANDIDATE_WAL_IO;
    candidate_outcome_witness_close(&r->outcomes);
    candidate_wal_witness_close(&r->terminal_history);
    candidate_wal_memory_t memory = r->memory; memory.release(memory.context,r,sizeof(*r));
    return status;
}
