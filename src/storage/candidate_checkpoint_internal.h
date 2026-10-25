#ifndef STORAGE_CANDIDATE_CHECKPOINT_INTERNAL_H
#define STORAGE_CANDIDATE_CHECKPOINT_INTERNAL_H

#include "storage/candidate_checkpoint.h"

uint64_t candidate_checkpoint_read(const uint8_t *p, unsigned size);
uint32_t candidate_checkpoint_chunks(const candidate_checkpoint_summary_t *summary);
candidate_wal_status_t candidate_checkpoint_decode_chunk(const uint8_t *bytes, size_t size,
    candidate_checkpoint_chunk_t *out);

#endif
