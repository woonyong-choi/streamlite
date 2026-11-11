#ifndef TEST_CANDIDATE_BUFFER_CASES_H
#define TEST_CANDIDATE_BUFFER_CASES_H

#include "test_candidate_buffer_fixture.h"

typedef struct {
    uint64_t admitted, groups, written, wal_end, known;
    uint32_t dirty, resident, pinned, leases;
    size_t requested;
    bool is_fenced;
    uint8_t data[32];
} bf_state_t;

typedef struct {
    const char *name;
    void (*run)(void);
} bf_case_t;

bf_state_t bf_take(bf_t *b);
bool bf_same(const bf_state_t *a, const bf_state_t *b);

extern const bf_case_t BF_CORE_CASES[];
extern const bf_case_t BF_FLUSH_CASES[];
extern const bf_case_t BF_CHECKPOINT_CASES[];
extern const bf_case_t BF_REPAIR_CASES[];
extern const bf_case_t BF_SOURCE_CASES[];

#endif
