#ifndef STORAGE_CANDIDATE_REDO_INTERNAL_H
#define STORAGE_CANDIDATE_REDO_INTERNAL_H

#include "storage/candidate_redo.h"

typedef struct candidate_redo_preview candidate_redo_preview_t;

candidate_wal_status_t candidate_redo_preview_open(candidate_wal_t *wal,
    const candidate_redo_options_t *options, candidate_redo_preview_t **out);
candidate_wal_status_t candidate_redo_preview_load(void *preview, candidate_page_ref_t ref,
    uint8_t out[CANDIDATE_PAGE_BYTES]);
candidate_redo_info_t candidate_redo_preview_info(const candidate_redo_preview_t *preview);
void candidate_redo_preview_close(candidate_redo_preview_t **preview);

#endif
