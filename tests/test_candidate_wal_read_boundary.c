#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_wal.h"
#include "../src/storage/candidate_wal_internal.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
#define SEGMENT CANDIDATE_WAL_SEGMENT_BYTES
#define FRAMES 4u
#define PAYLOAD_BYTES (CANDIDATE_WAL_RECORD_BYTES - 96u)
#define PROBE_BEFORE_END 72u

static unsigned checks;

typedef struct {
    candidate_wal_t *wal;
    uint8_t uuid[16];
    uint8_t *payload;
    uint64_t frames[FRAMES];
    char prefix[256];
} boundary_t;
typedef struct { const char *name; bool is_pinned; } case_t;

static const case_t CASES[] = {{"unpinned",false},{"pinned",true}};

static const char *status_name(candidate_wal_status_t status)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

static void open_boundary(boundary_t *b, const char *name)
{
    memset(b,0,sizeof(*b));
    int n = snprintf(b->prefix,sizeof(b->prefix),"build/candidate-wal-read-boundary-%ld-%s",(long)getpid(),name);
    CHECK(n > 0 && n < (int)sizeof(b->prefix));
    candidate_wal_options_t o = {.owner_epoch = 7};
    for (unsigned i = 0; i < 16; i++) o.uuid[i] = (uint8_t)(i + 1);
    memcpy(b->uuid,o.uuid,16);
    OK(candidate_wal_create(b->prefix,&o,&b->wal));
    b->payload = malloc(PAYLOAD_BYTES); CHECK(b->payload != NULL);
    for (size_t i = 0; i < PAYLOAD_BYTES; i++) b->payload[i] = (uint8_t)(i * 31 + 7);
    for (unsigned i = 0; i < FRAMES; i++) {
        candidate_wal_record_t r = {.type = 2,.transaction_id = i + 1,.payload = b->payload,.payload_bytes = PAYLOAD_BYTES};
        uint64_t end = 0; OK(candidate_wal_append(b->wal,&r,&end));
        b->frames[i] = end - CANDIDATE_WAL_RECORD_BYTES;
    }
    OK(candidate_wal_force(b->wal,candidate_wal_info(b->wal).valid_end_lsn));
}

static void expect_layout(const boundary_t *b)
{
    candidate_wal_info_t info = candidate_wal_info(b->wal);
    uint64_t padding = b->frames[FRAMES - 2] + CANDIDATE_WAL_RECORD_BYTES;
    printf("LAYOUT prefix=%s segments=%u records=%llu end=%llu frames=%llu,%llu,%llu,%llu padding=%llu..%llu\n",b->prefix,
        info.segments,(unsigned long long)info.records,(unsigned long long)info.valid_end_lsn,(unsigned long long)b->frames[0],
        (unsigned long long)b->frames[1],(unsigned long long)b->frames[2],(unsigned long long)b->frames[3],
        (unsigned long long)padding,(unsigned long long)SEGMENT);
    CHECK(info.segments == 2 && info.records == FRAMES && !info.has_tail);
    CHECK(b->frames[0] == CANDIDATE_WAL_HEADER_BYTES && b->frames[FRAMES - 1] == SEGMENT + CANDIDATE_WAL_HEADER_BYTES);
    CHECK(info.valid_end_lsn == b->frames[FRAMES - 1] + CANDIDATE_WAL_RECORD_BYTES && padding < SEGMENT - PROBE_BEFORE_END);
}

static void expect_frame(boundary_t *b, unsigned frame)
{
    candidate_wal_record_t r;
    candidate_wal_status_t status = candidate_wal_read_at(b->wal,b->frames[frame],&r);
    bool is_same = status == CANDIDATE_WAL_OK && r.lsn == b->frames[frame] && r.type == 2 && r.transaction_id == frame + 1;
    is_same = is_same && r.payload_bytes == PAYLOAD_BYTES && memcmp(r.payload,b->payload,PAYLOAD_BYTES) == 0;
    printf("FRAME lsn=%llu status=%s same=%d\n",(unsigned long long)b->frames[frame],status_name(status),is_same);
    OK(status); CHECK(is_same);
}

static void run_case(const case_t *k)
{
    boundary_t b; open_boundary(&b,k->name);
    if (k->is_pinned) OK(candidate_wal_pin(b.wal));
    expect_layout(&b);
    expect_frame(&b,FRAMES - 2); expect_frame(&b,FRAMES - 1); OK(candidate_wal_check_uuid(b.wal,b.uuid));
    uint64_t probe = SEGMENT - PROBE_BEFORE_END; candidate_wal_record_t r;
    candidate_wal_status_t first = candidate_wal_read_at(b.wal,probe,&r);
    candidate_wal_status_t next = candidate_wal_read_at(b.wal,b.frames[0],&r);
    candidate_wal_status_t handle = candidate_wal_check_uuid(b.wal,b.uuid);
    printf("PROBE %s source=%s end=%llu lsn=%llu offset=%llu before_segment_end=%u first=%s next=%s handle=%s "
        "expected=IO/POISONED basis=legacy-raw-read-status-not-disk-fault\n",k->name,b.prefix,
        (unsigned long long)candidate_wal_info(b.wal).valid_end_lsn,(unsigned long long)probe,(unsigned long long)(probe % SEGMENT),
        PROBE_BEFORE_END,status_name(first),status_name(next),status_name(handle));
    CHECK(first == CANDIDATE_WAL_IO && next == CANDIDATE_WAL_POISONED && handle == CANDIDATE_WAL_POISONED);
    OK(candidate_wal_close(&b.wal));
    free(b.payload);
}

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90);
    const char *selected = argc > 1 ? argv[1] : "all";
    bool is_all = strcmp(selected,"all") == 0, has_match = false;
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        if (!is_all && strcmp(selected,CASES[i].name) != 0) continue;
        printf("CASE %s\n",CASES[i].name); run_case(&CASES[i]); has_match = true;
    }
    CHECK(has_match);
    printf("PASS candidate wal read boundary case=%s checks=%u; invalid-address raw read status only, not disk fault, power loss "
        "or speed\n",selected,checks);
    return 0;
}
