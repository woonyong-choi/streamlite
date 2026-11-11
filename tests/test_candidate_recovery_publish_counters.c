#include "test_candidate_recovery_publish_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t group_base;

static uint64_t emit_offset_group(fx_t *fx, fx_txn_t *t, fx_image_t *images, unsigned image_count,
    const fx_event_t *events, unsigned event_count, bool complete, bool write_data)
{
    if (fx->group_id < group_base) fx->group_id = group_base;
    return fx_group(fx,t,images,image_count,events,event_count,complete,write_data);
}

#define fx_group emit_offset_group
#include "test_candidate_recovery_publish_scenario.c"
#undef fx_group

unsigned checks;

enum { K_WAL, K_DATA, K_CONTROL, K_KINDS };
static struct {
    ino_t data, control;
    unsigned writes[K_KINDS], syncs, truncates;
    bool fail_control_write, fail_wal_after_data;
} io;

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }
static int kind_of(int fd) { ino_t i = inode_of(fd); return i == io.data ? K_DATA : i == io.control ? K_CONTROL : K_WAL; }
static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset) { return pread(fd,p,n,offset); }

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    int k = kind_of(fd);
    bool should_fail = (k == K_CONTROL && io.fail_control_write) || (k == K_WAL && io.fail_wal_after_data && io.writes[K_DATA] > 0);
    if (should_fail) { errno = EIO; return -1; }
    io.writes[k]++;
    return pwrite(fd,p,n,offset);
}

static int hook_fsync(int fd) { io.syncs++; return fsync(fd); }
static int hook_truncate(int fd, off_t size) { io.truncates++; return ftruncate(fd,size); }
static const candidate_wal_io_t hooks = {hook_pread,hook_pwrite,hook_fsync,hook_truncate};

static unsigned io_writes(void) { return io.writes[K_WAL] + io.writes[K_DATA] + io.writes[K_CONTROL] + io.truncates; }

static const char *name_of(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

typedef enum { FAULT_NONE, FAULT_CONTROL_WRITE, FAULT_STAGE_WAL } fault_t;

static candidate_wal_status_t recover(const fx_t *fx, fault_t fault, candidate_recovery_t **out)
{
    memset(&io,0,sizeof(io));
    struct stat st; CHECK(stat(fx->data,&st) == 0); io.data = st.st_ino; CHECK(stat(fx->control,&st) == 0); io.control = st.st_ino;
    io.fail_control_write = fault == FAULT_CONTROL_WRITE; io.fail_wal_after_data = fault == FAULT_STAGE_WAL;
    candidate_recovery_options_t o = fx_options(fx,8); o.io = &hooks;
    return candidate_recovery_recover(&o,out);
}

typedef struct { uint8_t control[32], data[32], wal[8][32]; unsigned segments; } snapshot_t;

static snapshot_t take_snapshot(const fx_t *fx)
{
    snapshot_t s = {0}; fx_sha256_file(fx->control,s.control); fx_sha256_file(fx->data,s.data);
    for (unsigned id = 0; id < 8; id++) {
        char name[300]; CHECK(snprintf(name,sizeof(name),"%s.wal.%016" PRIx64,fx->wal,(uint64_t)id) > 0);
        if (access(name,F_OK) != 0) break;
        fx_sha256_file(name,s.wal[id]); s.segments++;
    }
    return s;
}

static bool is_same(const snapshot_t *a, const snapshot_t *b) { return memcmp(a,b,sizeof(*a)) == 0; }

typedef struct {
    uint64_t from, end, max_group, groups, group_ids[4];
    unsigned types[32], records;
} history_t;

static uint64_t read_id(const uint8_t *p) { uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

static bool count_record(void *context, const candidate_wal_record_t *r)
{
    history_t *h = context;
    bool is_group = r->type == 3 && r->payload_bytes == 16;
    if (is_group && read_id(r->payload) > h->max_group) h->max_group = read_id(r->payload);
    if (r->lsn < h->from) return true;
    h->records++; if (r->type < 32) h->types[r->type]++;
    if (is_group && h->groups < 4) h->group_ids[h->groups] = read_id(r->payload);
    if (is_group) h->groups++;
    return true;
}

static history_t read_history(const fx_t *fx, uint64_t from)
{
    candidate_wal_options_t options = {.owner_epoch = 8}; memcpy(options.uuid,fx_uuid,16);
    candidate_wal_t *wal = NULL; OK(candidate_wal_open(fx->wal,&options,&wal));
    history_t h = {.from = from}; candidate_wal_info_t info;
    OK(candidate_wal_scan(wal,count_record,&h,&info)); OK(candidate_wal_close(&wal));
    CHECK(!info.has_tail); h.end = info.valid_end_lsn;
    return h;
}

static void print_history(const char *tag, const history_t *h)
{
    printf("HISTORY %s from=%" PRIu64 " end=%" PRIu64 " max_group=%" PRIu64 " records=%u groups=%" PRIu64 " begin=%u end=%u abort=%u"
        " checkpoint=%u/%u/%u",tag,h->from,h->end,h->max_group,h->records,h->groups,h->types[3],h->types[8],h->types[11],
        h->types[12],h->types[13],h->types[14]);
    for (uint64_t i = 0; i < h->groups && i < 4; i++) printf(" id=%" PRIu64,h->group_ids[i]);
    printf("\n");
}

static bool has_only_terminal_records(const history_t *h)
{
    return h->records == h->types[11];
}

static candidate_wal_status_t open_strict(const fx_t *fx)
{
    candidate_recovery_options_t o = fx_options(fx,8); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = candidate_recovery_open(&o,&r);
    if (r != NULL) OK(candidate_recovery_close(&r));
    return status;
}

static candidate_wal_status_t accept_anchor(void *context, const candidate_control_state_t *anchor)
{
    (void)context; (void)anchor; return CANDIDATE_WAL_OK;
}

static candidate_control_info_t control_slots(const fx_t *fx)
{
    candidate_control_options_t o = {.backend = CANDIDATE_CONTROL_WAL,.verify = accept_anchor}; memcpy(o.uuid,fx_uuid,16);
    candidate_control_t *control = NULL; OK(candidate_control_open(fx->control,&o,&control));
    candidate_control_info_t info = candidate_control_info(control); OK(candidate_control_close(&control));
    return info;
}

static void print_slots(const char *tag, const fx_t *fx)
{
    candidate_control_info_t info = control_slots(fx);
    printf("SLOTS %s mask=%u",tag,info.valid_mask);
    for (unsigned i = 0; i < 2; i++) if ((info.valid_mask & (1u << i)) != 0)
        printf(" slot%u=generation:%" PRIu64 ",checkpoint:%" PRIu64 ",owner:%" PRIu64 ",root:%" PRIu64,i,info.slots[i].generation,
            info.slots[i].checkpoint_id,info.slots[i].owner_epoch,info.slots[i].outcome_root.id);
    printf("\n");
}

typedef struct { const candidate_control_state_t *state; uint64_t generation; } slot_t;

static void write_control(const fx_t *fx, const slot_t *slots, unsigned count)
{
    static uint8_t bytes[CANDIDATE_CONTROL_FILE_BYTES];
    memset(bytes,0,sizeof(bytes));
    for (unsigned i = 0; i < count; i++) {
        candidate_control_state_t s = *slots[i].state; s.generation = slots[i].generation; s.slot = (unsigned)((s.generation - 1) % 2);
        OK(candidate_control_encode(&s,bytes + s.slot * CANDIDATE_CONTROL_BYTES,CANDIDATE_CONTROL_BYTES));
    }
    int fd = open(fx->control,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(fd >= 0);
    CHECK(pwrite(fd,bytes,sizeof(bytes),0) == (ssize_t)sizeof(bytes)); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

static void print_case(const char *tag, candidate_wal_status_t status, candidate_recovery_t *r)
{
    printf("CASE %s status=%s writes=%u wal=%u data=%u control=%u truncates=%u syncs=%u",tag,name_of(status),io_writes(),
        io.writes[K_WAL],io.writes[K_DATA],io.writes[K_CONTROL],io.truncates,io.syncs);
    if (r != NULL) {
        candidate_recovery_publication_t p = candidate_recovery_publication(r); candidate_recovery_info_t i = candidate_recovery_info(r);
        printf(" owner=%" PRIu64 " checkpoint=%" PRIu64 " root=%" PRIu64 " groups=%u reserved=%" PRIu64 " alloc=%u aborts=%u",
            i.anchor.owner_epoch,i.anchor.checkpoint_id,i.anchor.outcome_root.id,p.system_groups,p.reserved_pages,p.allocation_pages,
            p.repair.terminals.appended_aborts);
    }
    printf("\n"); fflush(stdout);
}

static candidate_wal_status_t recover_case(const char *tag, const fx_t *fx, fault_t fault, candidate_recovery_publication_t *receipt)
{
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(fx,fault,&r);
    print_case(tag,status,r);
    CHECK((status == CANDIDATE_WAL_OK) == (r != NULL));
    if (r != NULL && receipt != NULL) *receipt = candidate_recovery_publication(r);
    if (r != NULL) OK(candidate_recovery_close(&r));
    return status;
}

static void expect_settled(const char *tag, const fx_t *fx)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"%s-reentry",tag) > 0);
    snapshot_t before = take_snapshot(fx);
    CHECK(recover_case(name,fx,FAULT_NONE,NULL) == CANDIDATE_WAL_OK); CHECK(io_writes() == 0);
    snapshot_t after = take_snapshot(fx); CHECK(is_same(&before,&after));
    candidate_wal_status_t strict = open_strict(fx);
    printf("CASE %s-strict-open status=%s\n",tag,name_of(strict)); CHECK(strict == CANDIDATE_WAL_OK);
}

static void expect_rejected(const char *tag, const fx_t *fx, const snapshot_t *input, candidate_wal_status_t strict_input)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"%s-retry",tag) > 0);
    snapshot_t before = take_snapshot(fx);
    CHECK(recover_case(name,fx,FAULT_NONE,NULL) == CANDIDATE_WAL_LIMIT); CHECK(io_writes() == 0);
    snapshot_t after = take_snapshot(fx); CHECK(is_same(&before,&after));
    candidate_wal_status_t strict = open_strict(fx);
    printf("CASE %s-strict-open status=%s input=%s unchanged_input=%d\n",tag,name_of(strict),name_of(strict_input),is_same(input,&after));
    CHECK(strict == strict_input);
}

static built_t build_empty(const char *tag, uint64_t last_group)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"task23s-%s-%ld",tag,(long)getpid()) > 0);
    group_base = last_group == 0 ? 0 : last_group - 1;
    scen_t s = {.name = name}; built_t b = scen_build(&s);
    group_base = 0;
    CHECK(last_group == 0 || b.fx->group_id == last_group);
    printf("FIXTURE %s wal=%s control=%s data=%s last_group=%" PRIu64 "\n",tag,b.fx->wal,b.fx->control,b.fx->data,b.fx->group_id);
    return b;
}

static void recover_exhausted_group_ids_returns_limit(const char *tag, uint64_t last_group)
{
    built_t b = build_empty(tag,last_group); scen_finish(&b);
    snapshot_t input = take_snapshot(b.fx); history_t h = read_history(b.fx,UINT64_MAX); CHECK(h.max_group == last_group);
    candidate_wal_status_t strict_input = open_strict(b.fx);

    candidate_wal_status_t status = recover_case(tag,b.fx,FAULT_NONE,NULL);

    snapshot_t after = take_snapshot(b.fx); h = read_history(b.fx,h.end); print_history(tag,&h);
    CHECK(status == CANDIDATE_WAL_LIMIT && io_writes() == 0 && is_same(&input,&after) && h.records == 0);
    expect_rejected(tag,b.fx,&input,strict_input);
    scen_free(&b);
}

static void recover_remaining_group_ids_publishes(const char *tag, uint64_t last_group)
{
    built_t b = build_empty(tag,last_group); scen_finish(&b);
    history_t h = read_history(b.fx,UINT64_MAX); uint64_t last = h.max_group; candidate_recovery_publication_t p = {0};

    candidate_wal_status_t status = recover_case(tag,b.fx,FAULT_NONE,&p);

    h = read_history(b.fx,h.end); print_history(tag,&h);
    CHECK(status == CANDIDATE_WAL_OK && p.system_groups == 2 && h.groups == 2);
    CHECK(h.group_ids[0] == last + 1 && h.group_ids[1] == last + 2 && h.max_group == last + 2);
    expect_settled(tag,b.fx);
    scen_free(&b);
}

static void recover_staged_retry_needs_no_group_id(void)
{
    built_t b = build_empty("gid-zero-need",UINT64_MAX - 2); scen_finish(&b);
    history_t h = read_history(b.fx,UINT64_MAX); snapshot_t input = take_snapshot(b.fx);
    CHECK(recover_case("gid-zero-need-interrupted",b.fx,FAULT_CONTROL_WRITE,NULL) == CANDIDATE_WAL_IO);
    snapshot_t interrupted = take_snapshot(b.fx); history_t staged = read_history(b.fx,h.end); print_history("gid-zero-need-interrupted",&staged);
    CHECK(memcmp(interrupted.control,input.control,32) == 0 && staged.max_group == UINT64_MAX && staged.groups == 2);
    candidate_recovery_publication_t p = {0};

    candidate_wal_status_t status = recover_case("gid-zero-need",b.fx,FAULT_NONE,&p);

    history_t retry = read_history(b.fx,staged.end); print_history("gid-zero-need",&retry);
    CHECK(status == CANDIDATE_WAL_OK && p.system_groups == 0 && retry.groups == 0 && retry.max_group == UINT64_MAX);
    CHECK(retry.types[12] == 1 && retry.types[14] == 1 && io.writes[K_DATA] == 0 && io.writes[K_CONTROL] == 1);
    expect_settled("gid-zero-need",b.fx);
    scen_free(&b);
}

static void recover_reserved_retry_needs_one_group_id(void)
{
    built_t b = build_empty("gid-one-need",UINT64_MAX - 2); scen_finish(&b);
    history_t h = read_history(b.fx,UINT64_MAX);
    CHECK(recover_case("gid-one-need-interrupted",b.fx,FAULT_STAGE_WAL,NULL) == CANDIDATE_WAL_IO);
    history_t reserved = read_history(b.fx,h.end); print_history("gid-one-need-interrupted",&reserved);
    CHECK(reserved.groups == 1 && reserved.group_ids[0] == UINT64_MAX - 1 && reserved.types[12] == 0);
    candidate_recovery_publication_t p = {0};

    candidate_wal_status_t status = recover_case("gid-one-need",b.fx,FAULT_NONE,&p);

    history_t retry = read_history(b.fx,reserved.end); print_history("gid-one-need",&retry);
    CHECK(status == CANDIDATE_WAL_OK && p.system_groups == 1 && p.reserved_pages == 0);
    CHECK(retry.groups == 1 && retry.group_ids[0] == UINT64_MAX && retry.max_group == UINT64_MAX);
    expect_settled("gid-one-need",b.fx);
    scen_free(&b);
}

static void recover_single_anchor_generation(const char *tag, uint64_t generation, candidate_wal_status_t expected)
{
    built_t b = build_empty(tag,0);
    slot_t slot = {&b.anchor,generation}; write_control(b.fx,&slot,1); fx_close(b.fx);
    print_slots("input",b.fx);
    snapshot_t input = take_snapshot(b.fx); history_t h = read_history(b.fx,UINT64_MAX);
    candidate_wal_status_t strict_input = open_strict(b.fx);

    candidate_wal_status_t status = recover_case(tag,b.fx,FAULT_NONE,NULL);

    snapshot_t after = take_snapshot(b.fx); history_t appended = read_history(b.fx,h.end); print_history(tag,&appended); print_slots("after",b.fx);
    CHECK(status == expected);
    if (expected == CANDIDATE_WAL_LIMIT) {
        CHECK(io_writes() == 0 && is_same(&input,&after) && appended.records == 0);
        expect_rejected(tag,b.fx,&input,strict_input);
    } else {
        candidate_control_info_t info = control_slots(b.fx);
        CHECK(info.valid_mask == 3 && info.slots[0].generation == UINT64_MAX);
        CHECK(info.slots[0].owner_epoch == 8 && info.slots[1].generation == generation && info.slots[1].owner_epoch == 7);
        expect_settled(tag,b.fx);
    }
    scen_free(&b);
}

static const req_t requests[] = {
    {REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_COMMIT,STORE_ADMITTED,1,9000,false,false},
    {REQ_COMMIT,STORE_NONE,1,70000,false,false},{REQ_COMMIT,STORE_FINAL,1,0,false,true},
    {REQ_ABORT,STORE_FINAL,1,0,false,false},{REQ_ABORT,STORE_FINAL,1,30,true,false},
    {REQ_ABORT,STORE_ADMITTED,1,20,false,false},{REQ_UNFINISHED,STORE_ADMITTED,1,50,false,false},
    {REQ_UNFINISHED,STORE_NONE,1,0,false,false},{REQ_COMMIT,STORE_ADMITTED,1,8008,false,false}};

static built_t build_fallback(const char *tag, candidate_control_state_t anchors[2])
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"task23s-%s-%ld",tag,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = sizeof(requests) / sizeof(requests[0]),.req = requests,.ballast = 2,.extra_empty_roots = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx;
    uint64_t extra = b.alloc_count - 2;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 5);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0);
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = b.anchor.transaction_highwater,
        .page_highwater = b.anchor.page_highwater,.visible_csn = fx->highest_csn + 2,.retained_bytes = CANDIDATE_PAGE_BYTES};
    candidate_page_location_t l = fx_location(extra,1,end); OK(candidate_catalog_encode_root(&l,&root,image.page,CANDIDATE_PAGE_BYTES));
    image.id = extra; image.generation = 1; image.previous = fx->pages[extra].lsn;
    fx_group(fx,&t,&image,1,NULL,0,true,true); fx_commit(fx,&t);
    anchors[0] = b.anchor; anchors[1] = b.anchor;
    anchors[1].checkpoint_id = 2; anchors[1].outcome_root = (candidate_page_ref_t){extra,1}; anchors[1].visible_csn = fx->highest_csn;
    const fx_txn_t *active[2] = {&b.txn[7],&b.txn[8]};
    fx_checkpoint(fx,&anchors[1],active,2);
    printf("FIXTURE %s wal=%s control=%s data=%s extra_root=%" PRIu64 "\n",tag,fx->wal,fx->control,fx->data,extra);
    return b;
}

static void recover_fallback_anchor_generation(const char *tag, uint64_t high_generation, candidate_wal_status_t expected)
{
    candidate_control_state_t anchors[2]; built_t b = build_fallback(tag,anchors);
    slot_t slots[2] = {{&anchors[0],high_generation - 1},{&anchors[1],high_generation}};
    write_control(b.fx,slots,2); fx_close(b.fx);
    print_slots("input",b.fx);
    candidate_control_info_t input_slots = control_slots(b.fx); CHECK(input_slots.valid_mask == 3);
    snapshot_t input = take_snapshot(b.fx); history_t h = read_history(b.fx,UINT64_MAX);
    candidate_wal_status_t strict_input = open_strict(b.fx);
    unsigned low_slot = (unsigned)((high_generation - 2) % 2);

    candidate_recovery_publication_t p = {0};
    candidate_wal_status_t status = recover_case(tag,b.fx,FAULT_NONE,&p);

    snapshot_t after = take_snapshot(b.fx); history_t appended = read_history(b.fx,h.end); print_history(tag,&appended); print_slots("after",b.fx);
    CHECK(status == expected);
    if (expected == CANDIDATE_WAL_LIMIT) {
        CHECK(io.writes[K_DATA] == 0 && io.writes[K_CONTROL] == 0 && io.truncates == 0);
        CHECK(has_only_terminal_records(&appended) && appended.types[11] == 2 && appended.groups == 0);
        CHECK(memcmp(input.control,after.control,32) == 0 && memcmp(input.data,after.data,32) == 0);
        expect_rejected(tag,b.fx,&after,strict_input);
    } else {
        candidate_control_info_t info = control_slots(b.fx);
        CHECK(info.slots[1 - low_slot].generation == UINT64_MAX && info.slots[1 - low_slot].owner_epoch == 8);
        CHECK(info.slots[1 - low_slot].checkpoint_id == 3 && info.slots[1 - low_slot].outcome_root.id == b.root_id);
        CHECK(info.slots[low_slot].generation == high_generation - 1 && info.slots[low_slot].checkpoint_id == 1);
        expect_settled(tag,b.fx);
    }
    scen_free(&b);
}

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); CHECK(argc == 2);
    const char *c = argv[1];
    if (strcmp(c,"gid-default") == 0) recover_remaining_group_ids_publishes(c,0);
    else if (strcmp(c,"gid-max-2") == 0) recover_remaining_group_ids_publishes(c,UINT64_MAX - 2);
    else if (strcmp(c,"gid-max-1") == 0) recover_exhausted_group_ids_returns_limit(c,UINT64_MAX - 1);
    else if (strcmp(c,"gid-max") == 0) recover_exhausted_group_ids_returns_limit(c,UINT64_MAX);
    else if (strcmp(c,"gid-zero-need") == 0) recover_staged_retry_needs_no_group_id();
    else if (strcmp(c,"gid-one-need") == 0) recover_reserved_retry_needs_one_group_id();
    else if (strcmp(c,"gen-max") == 0) recover_single_anchor_generation(c,UINT64_MAX,CANDIDATE_WAL_LIMIT);
    else if (strcmp(c,"gen-max-1") == 0) recover_single_anchor_generation(c,UINT64_MAX - 1,CANDIDATE_WAL_OK);
    else if (strcmp(c,"gen-max-1-fallback") == 0) recover_fallback_anchor_generation(c,UINT64_MAX - 1,CANDIDATE_WAL_LIMIT);
    else if (strcmp(c,"gen-max-2-fallback") == 0) recover_fallback_anchor_generation(c,UINT64_MAX - 2,CANDIDATE_WAL_OK);
    else CHECK(false);
    printf("PASS candidate recovery publication counters case=%s checks=%u\n",c,checks);
    return 0;
}
