#ifndef STORAGE_CANDIDATE_WAL_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_INTERNAL_H

#include "storage/candidate_wal.h"

typedef struct {
    uint64_t position;
    unsigned segments;
} candidate_wal_cursor_t;

candidate_wal_cursor_t candidate_wal_cursor(const candidate_wal_t *wal);
candidate_wal_status_t candidate_wal_place(const candidate_wal_t *wal, candidate_wal_cursor_t *cursor,
    size_t payload_bytes, uint64_t *lsn);
candidate_wal_status_t candidate_wal_read_at(candidate_wal_t *wal, uint64_t lsn, candidate_wal_record_t *out);
candidate_wal_status_t candidate_wal_pin(candidate_wal_t *wal);
/* 처음이면 실제 관측 view를 pin하고, 이미 pin이면 현재 파일을 그 view와 다시 대조한다. 불일치는 영구 거부다. */
candidate_wal_status_t candidate_wal_ensure_pin(candidate_wal_t *wal);
void candidate_wal_reject(candidate_wal_t *wal);
/* 파일 내용을 읽지 않고 기존 접근 거부를 그대로 돌려주며 handle UUID가 uuid와 다르면 UNSUPPORTED다. */
candidate_wal_status_t candidate_wal_check_uuid(candidate_wal_t *wal, const uint8_t uuid[16]);

#endif
