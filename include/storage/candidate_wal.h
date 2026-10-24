#ifndef STORAGE_CANDIDATE_WAL_H
#define STORAGE_CANDIDATE_WAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define CANDIDATE_WAL_SEGMENT_BYTES (4u * 1024u * 1024u)
#define CANDIDATE_WAL_HEADER_BYTES 64u
#define CANDIDATE_WAL_RECORD_HEADER_BYTES 80u
#define CANDIDATE_WAL_FOOTER_BYTES 16u
#define CANDIDATE_WAL_RECORD_BYTES (1024u * 1024u)
#define CANDIDATE_WAL_MAX_BYTES (256u * 1024u * 1024u)

typedef enum {
    CANDIDATE_WAL_OK,
    CANDIDATE_WAL_INVALID,
    CANDIDATE_WAL_NOMEM,
    CANDIDATE_WAL_LIMIT,
    CANDIDATE_WAL_IO,
    CANDIDATE_WAL_CORRUPT,
    CANDIDATE_WAL_UNSUPPORTED,
    CANDIDATE_WAL_EXISTS,
    CANDIDATE_WAL_BUSY,
    CANDIDATE_WAL_FORKED,
    CANDIDATE_WAL_POISONED,
    CANDIDATE_WAL_TAIL,
    CANDIDATE_WAL_STOPPED,
    CANDIDATE_WAL_STALE
} candidate_wal_status_t;

typedef struct {
    uint16_t type;
    uint64_t lsn;
    uint64_t transaction_id;
    uint64_t previous_lsn;
    uint64_t owner_epoch;
    const uint8_t *payload;
    size_t payload_bytes;
} candidate_wal_record_t;

typedef struct {
    ssize_t (*pread)(int, void *, size_t, off_t);
    ssize_t (*pwrite)(int, const void *, size_t, off_t);
    int (*fsync)(int);
    int (*ftruncate)(int, off_t);
} candidate_wal_io_t;

typedef struct {
    void *context;
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *, size_t);
} candidate_wal_memory_t;

typedef struct {
    uint8_t uuid[16];
    uint64_t owner_epoch;
    uint64_t max_bytes;
    const candidate_wal_io_t *io;
    const candidate_wal_memory_t *memory;
} candidate_wal_options_t;

typedef struct {
    uint64_t records;
    uint64_t valid_end_lsn;
    uint64_t durable_end_lsn;
    uint64_t highest_owner_epoch;
    uint64_t tail_bytes;
    size_t resident_bytes;
    unsigned segments;
    bool has_tail;
} candidate_wal_info_t;

typedef struct candidate_wal candidate_wal_t;
typedef bool (*candidate_wal_visitor_t)(void *, const candidate_wal_record_t *);

/* 내부 비교 형식이며 기존 DB·공개 ABI에 연결하지 않고 거래 commit 여부도 판정하지 않는다. */
candidate_wal_status_t candidate_wal_encode(const uint8_t uuid[16], const candidate_wal_record_t *record,
                                          uint8_t *out, size_t capacity, size_t *written);
/* payload는 input을 빌리며 정확한 frame 한 개만 받는다. */
candidate_wal_status_t candidate_wal_decode(const uint8_t uuid[16], const uint8_t *input, size_t size,
                                          candidate_wal_record_t *out);
candidate_wal_status_t candidate_wal_create(const char *prefix, const candidate_wal_options_t *options,
                                          candidate_wal_t **out);
/* 열기는 파일을 바꾸지 않으며 불완전 마지막 tail도 info에 남긴다. */
candidate_wal_status_t candidate_wal_open(const char *prefix, const candidate_wal_options_t *options,
                                        candidate_wal_t **out);
/* 단일 호출자 계약이며 visitor의 빌린 payload는 그 callback 안에서만 유효하다. */
/* visitor는 유효 prefix만 관측하므로 전체 scan 성공 전 외부 효과를 확정하면 안 된다. */
candidate_wal_status_t candidate_wal_scan(candidate_wal_t *wal, candidate_wal_visitor_t visitor,
                                        void *context, candidate_wal_info_t *out);
/* append가 LSN·owner epoch를 부여하며 입력 payload는 WAL 내부 buffer와 겹치면 안 된다. */
candidate_wal_status_t candidate_wal_append(candidate_wal_t *wal, const candidate_wal_record_t *record,
                                          uint64_t *end_lsn);
/* append는 내구 성공이 아니며 force 실패 뒤 handle은 poison이다. */
candidate_wal_status_t candidate_wal_force(candidate_wal_t *wal, uint64_t through_end_lsn);
/* 전체 scan을 통과한 마지막 불완전 tail만 명시적으로 자르고 동기화한다. */
candidate_wal_status_t candidate_wal_repair_tail(candidate_wal_t *wal);
candidate_wal_info_t candidate_wal_info(const candidate_wal_t *wal);
/* force 없이 닫으며 fork 자식의 close도 부모 open description을 명시적으로 unlock하지 않는다. */
candidate_wal_status_t candidate_wal_close(candidate_wal_t **wal);

#endif
