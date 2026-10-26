#ifndef STORAGE_CANDIDATE_WAL_READ_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_READ_INTERNAL_H

#include "candidate_wal_internal.h"

typedef candidate_wal_status_t (*candidate_wal_read_t)(void *context, uint64_t lsn, uint8_t *out, size_t size);
typedef struct {
    const uint8_t *uuid;
    uint8_t *buffer;
    candidate_wal_read_t read;
    void *context;
} candidate_wal_reader_t;
typedef struct {
    uint64_t records, first_owner_epoch, highest_owner_epoch;
    bool is_first_behind;
} candidate_wal_suffix_count_t;

void candidate_wal_encode_segment(const uint8_t uuid[16], unsigned id, uint8_t out[64]);
candidate_wal_status_t candidate_wal_validate_segment(const uint8_t uuid[16], unsigned id, const uint8_t *p, size_t size);
candidate_wal_status_t candidate_wal_validate_frame_header(const uint8_t uuid[16], const uint8_t *p, size_t size,
    uint64_t lsn);
size_t candidate_wal_frame_bytes(const uint8_t *header);
candidate_wal_status_t candidate_wal_check_padding(const uint8_t *p, size_t size);
/* reader buffer에 frame 하나를 읽고 out payload는 그 buffer를 빌리며 end_lsn을 넘는 frame은 CORRUPT, 80B header가 segment 끝을 넘는 주소는 raw 읽기 실패와 같은 IO다. */
candidate_wal_status_t candidate_wal_read_frame(const candidate_wal_reader_t *reader, uint64_t lsn, uint64_t end_lsn,
    candidate_wal_record_t *out);
/* request 형식과 현재 cut만 대조하며 after 불일치는 STALE, 불가능한 범위는 INVALID다. */
candidate_wal_status_t candidate_wal_check_suffix(const candidate_wal_suffix_t *request, const candidate_wal_info_t *current);
/* before 끝부터 after 끝까지 padding·segment header·frame만 읽으며 IO·CORRUPT·UNSUPPORTED는 transport 실패지만 첫 기록 owner가 before 최고값보다 낮으면 visitor 전에 CORRUPT와 is_first_behind를 남기고 이는 transport 실패가 아니다. */
candidate_wal_status_t candidate_wal_read_suffix(const candidate_wal_reader_t *reader, const candidate_wal_suffix_t *request,
    candidate_wal_suffix_count_t *count);
/* 읽은 기록 수와 owner epoch가 captured before·after와 다르면 transport 판정 없이 CORRUPT다. */
candidate_wal_status_t candidate_wal_match_suffix(const candidate_wal_suffix_t *request,
    const candidate_wal_suffix_count_t *count);

#endif
