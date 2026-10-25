#ifndef STORAGE_CANDIDATE_RECOVERY_INTERNAL_H
#define STORAGE_CANDIDATE_RECOVERY_INTERNAL_H

#include <limits.h>
#include <sys/types.h>

#include "storage/candidate_recovery.h"
#include "storage/candidate_wal_witness.h"

struct candidate_recovery {
    candidate_wal_memory_t memory;
    candidate_wal_io_t io;
    candidate_wal_t *wal;
    candidate_control_t *control;
    candidate_outcome_witness_t *outcomes;
    candidate_wal_witness_t *terminal_history;
    candidate_recovery_info_t info;
    candidate_recovery_publication_t publication;
    candidate_recovery_extent_t extent;
    uint8_t uuid[16];
    char name[NAME_MAX + 1];
    int fd, directory_fd;
    dev_t device;
    ino_t inode;
    pid_t process_id;
    uint64_t owner_epoch;
    bool is_busy, is_poisoned, is_terminal_repair, allow_group_tail, is_ready_verify;
};

typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    const candidate_control_state_t *anchor;
    const candidate_wal_memory_t *memory;
    bool allow_group_tail;
} candidate_terminal_options_t;

candidate_wal_status_t candidate_terminal_prepare(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **out);
candidate_wal_status_t candidate_terminal_resolve(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_terminal_info_t *out);
candidate_wal_status_t candidate_terminal_repair_wal(candidate_wal_t *wal,
    const candidate_terminal_options_t *options, candidate_wal_witness_t **history,
    candidate_recovery_wal_info_t *out);

candidate_wal_status_t candidate_recovery_validate(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);
candidate_wal_status_t candidate_recovery_validate_preview(candidate_wal_t *wal,
    const candidate_control_state_t *anchor, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info);

candidate_wal_status_t candidate_recovery_own(const candidate_recovery_options_t *options,
    candidate_control_verify_t verify, candidate_recovery_t **out);
candidate_wal_status_t candidate_recovery_verify(void *recovery, const candidate_control_state_t *anchor);
candidate_wal_status_t candidate_recovery_check(candidate_recovery_t *recovery);
candidate_redo_target_t candidate_recovery_target(candidate_recovery_t *recovery);
candidate_wal_status_t candidate_recovery_load(void *recovery, candidate_page_ref_t ref, uint8_t bytes[CANDIDATE_PAGE_BYTES]);
size_t candidate_recovery_resident_bytes(const candidate_recovery_t *recovery);
candidate_wal_status_t candidate_recovery_page_offset(uint64_t id, uint64_t *offset);

#endif
