#ifndef STORAGE_CANDIDATE_CONTROL_INTERNAL_H
#define STORAGE_CANDIDATE_CONTROL_INTERNAL_H

#include "storage/candidate_control.h"

/* publish와 같은 접근·identity·generation·slot parity·codec·follows 판정만 하며 verifier·쓰기·fsync는 하지 않는다. */
candidate_wal_status_t candidate_control_check_publication(candidate_control_t *control, const candidate_control_state_t *next);

#endif
