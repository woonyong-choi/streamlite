#ifndef STORAGE_CANDIDATE_DIGEST_H
#define STORAGE_CANDIDATE_DIGEST_H

#include "storage/candidate_wal.h"

/* 내부 본문 digest이며 인증·FIPS 검증 모듈을 제공하지 않는다. 실패 시 out은 그대로다. */
candidate_wal_status_t candidate_digest_sha256(const uint8_t *bytes, size_t size, uint8_t out[32]);

#endif
