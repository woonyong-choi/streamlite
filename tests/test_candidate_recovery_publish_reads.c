#include "test_candidate_recovery_publish_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/storage/candidate_wal_internal.h"

#define EXPECT(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __func__, __LINE__, #c); fflush(stdout); } } while (0)
#define EXIT_CALIBRATION 2
#define EXIT_EXPECTATION 4

unsigned checks;
static unsigned failures;
static const char *place = "";
static bool has_handle;

typedef enum { KIND_IMAGE, KIND_DELTA, KIND_INVALID_ROOT } kind_t;
typedef enum {
    M_NONE, M_HEADER, M_CRC, M_TRAILER, M_TOTAL, M_VERSION, M_UUID, M_IO_HEADER, M_IO_FRAME, M_INVALID_ARGS
} malform_t;

typedef struct { uint64_t offset, lsn; size_t size; uint16_t type; unsigned hits, mismatches; } site_t;
typedef struct {
    ino_t data, control, wal0;
    unsigned wal_writes, data_writes, control_writes, truncates, syncs, injected;
    unsigned allocations, first_hit_allocation, fail_allocation, failed_allocations;
    size_t failed_size;
    site_t sites[2];
    unsigned site_count;
    uint64_t last, injected_total;
    malform_t malform;
    bool is_armed, is_frame_armed;
} hook_t;
static hook_t h;

typedef struct {
    built_t b;
    fx_t *fx;
    uint64_t older_root, newer_root, valid_end;
    site_t sites[2];
    unsigned site_count;
    uint8_t stale_page[CANDIDATE_PAGE_BYTES], valid_page[CANDIDATE_PAGE_BYTES];
} fixture_t;

typedef struct { uint8_t control[32], data[32], wal[32]; } snapshot_t;

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t v = 0; for (unsigned i = 0; i < size; i++) v |= (uint64_t)p[i] << (8 * i); return v;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }

static site_t *site_at(uint64_t offset)
{
    for (unsigned i = 0; i < h.site_count; i++) if (h.sites[i].offset == offset) return &h.sites[i];
    return NULL;
}

static void inject_header(int fd, uint8_t *p, uint64_t at)
{
    struct stat st; CHECK(fstat(fd,&st) == 0);
    switch (h.malform) {
    case M_HEADER: p[0] ^= 0x01; break;
    case M_TOTAL: {
        uint64_t total = (uint64_t)st.st_size - at + 8;
        write_le(p + 8,total,4); write_le(p + 12,total - 96,4); h.injected_total = total; break;
    }
    case M_VERSION: write_le(p + 4,2,2); break;
    case M_UUID: p[40] ^= 0x01; break;
    default: return;
    }
    h.is_armed = false; h.injected++;
}

static void inject_frame(uint8_t *p, size_t size)
{
    if (h.malform == M_CRC) p[80] ^= 0x5a;
    else p[size - 12] ^= 0x01;
    h.is_frame_armed = false; h.injected++;
}

static void observe_header(site_t *s, const uint8_t *p)
{
    s->hits++;
    if (read_le(p + 6,2) != s->type || read_le(p + 16,8) != s->lsn) s->mismatches++;
    if (h.first_hit_allocation == 0) h.first_hit_allocation = h.allocations;
}

static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset)
{
    if (inode_of(fd) != h.wal0) return pread(fd,p,n,offset);
    uint64_t at = (uint64_t)offset;
    if (at == 0 && n == 64) h.last = 0;
    bool is_back = at < h.last;
    if (at > h.last) h.last = at;
    site_t *s = site_at(at);
    bool is_target = s != NULL && s == &h.sites[0], is_header = s != NULL && n == 80 && is_back;
    bool is_frame = is_target && h.is_frame_armed && n == s->size;
    if (is_target && is_header && h.is_armed && h.malform == M_IO_HEADER) { h.is_armed = false; h.injected++; errno = EIO; return -1; }
    if (is_frame && h.malform == M_IO_FRAME) { h.is_frame_armed = false; h.injected++; errno = EIO; return -1; }
    ssize_t got = pread(fd,p,n,offset);
    if (got <= 0 || s == NULL) return got;
    if (is_header && (size_t)got == 80) {
        observe_header(s,p);
        if (!is_target || !h.is_armed) return got;
        if (h.malform == M_CRC || h.malform == M_TRAILER || h.malform == M_IO_FRAME) { h.is_armed = false; h.is_frame_armed = true; }
        else inject_header(fd,p,at);
    } else if (is_frame && (size_t)got == s->size) {
        inject_frame(p,s->size);
    }
    return got;
}

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    ino_t inode = inode_of(fd);
    if (inode == h.data) h.data_writes++; else if (inode == h.control) h.control_writes++; else h.wal_writes++;
    return pwrite(fd,p,n,offset);
}

static int hook_fsync(int fd) { h.syncs++; return fsync(fd); }
static int hook_truncate(int fd, off_t size) { h.truncates++; return ftruncate(fd,size); }
static const candidate_wal_io_t hooks = {hook_pread,hook_pwrite,hook_fsync,hook_truncate};

static void *allocate(void *context, size_t size)
{
    (void)context;
    if (h.fail_allocation != 0 && ++h.allocations == h.fail_allocation) { h.failed_allocations++; h.failed_size = size; return NULL; }
    if (h.fail_allocation == 0) h.allocations++;
    return malloc(size);
}

static void release(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }
static const candidate_wal_memory_t memory = {NULL,allocate,release};

static void wal_path(const char *wal, char out[300])
{
    CHECK(snprintf(out,300,"%s.wal.%016" PRIx64,wal,(uint64_t)0) > 0);
}

static void hook_reset(const fx_t *fx, const fixture_t *f, malform_t malform)
{
    uint64_t last = h.last; memset(&h,0,sizeof(h)); h.last = last; struct stat st;
    CHECK(stat(fx->data,&st) == 0); h.data = st.st_ino;
    CHECK(stat(fx->control,&st) == 0); h.control = st.st_ino;
    char name[300]; wal_path(fx->wal,name); CHECK(stat(name,&st) == 0); h.wal0 = st.st_ino;
    memcpy(h.sites,f->sites,sizeof(h.sites)); h.site_count = f->site_count;
    for (unsigned i = 0; i < h.site_count; i++) { h.sites[i].hits = 0; h.sites[i].mismatches = 0; }
    h.malform = malform; h.is_armed = malform != M_NONE;
}

static unsigned effects(void) { return h.wal_writes + h.data_writes + h.control_writes + h.truncates + h.syncs; }

static const char *name_of(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

static void hex(const uint8_t d[32], char out[65]) { for (unsigned i = 0; i < 32; i++) sprintf(out + 2 * i,"%02x",d[i]); }

static snapshot_t take(const fx_t *fx)
{
    snapshot_t s; char name[300]; wal_path(fx->wal,name);
    fx_sha256_file(fx->control,s.control); fx_sha256_file(fx->data,s.data); fx_sha256_file(name,s.wal);
    return s;
}

static bool same(const snapshot_t *a, const snapshot_t *b) { return memcmp(a,b,sizeof(*a)) == 0; }

static void print_snapshot(const char *tag, const fx_t *fx)
{
    snapshot_t s = take(fx); char c[65], d[65], w[65]; hex(s.control,c); hex(s.data,d); hex(s.wal,w);
    printf("FILES %s prefix=%s control=%s data=%s wal0=%s\n",tag,fx->wal,c,d,w); fflush(stdout);
}

static void print_hook(const char *tag, candidate_wal_status_t status, uint64_t root)
{
    printf("CASE %s status=%s root=%" PRIu64 " wal_writes=%u data_writes=%u control_writes=%u truncates=%u syncs=%u injected=%u",
        tag,name_of(status),root,h.wal_writes,h.data_writes,h.control_writes,h.truncates,h.syncs,h.injected);
    for (unsigned i = 0; i < h.site_count; i++)
        printf(" site%u_lsn=%" PRIu64 " site%u_type=%u site%u_hits=%u site%u_mismatches=%u",i,h.sites[i].lsn,i,h.sites[i].type,i,
            h.sites[i].hits,i,h.sites[i].mismatches);
    printf(" first_hit_allocation=%u failed_allocations=%u failed_size=%zu injected_total=%" PRIu64 "\n",h.first_hit_allocation,
        h.failed_allocations,h.failed_size,h.injected_total);
    fflush(stdout);
}

static candidate_wal_status_t recover(const fx_t *fx, uint64_t *root)
{
    candidate_recovery_options_t o = fx_options(fx,8); o.io = &hooks; o.memory = &memory;
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = candidate_recovery_recover(&o,&r);
    *root = 0; has_handle = r != NULL;
    if (r != NULL) { *root = candidate_recovery_info(r).anchor.outcome_root.id; OK(candidate_recovery_close(&r)); }
    return status;
}

typedef struct { site_t *sites; unsigned count; } capture_t;

static bool capture_visit(void *context, const candidate_wal_record_t *r)
{
    capture_t *c = context;
    for (unsigned i = 0; i < c->count; i++) {
        if (c->sites[i].lsn != r->lsn) continue;
        c->sites[i].type = r->type; c->sites[i].size = (96 + r->payload_bytes + 7) & ~(size_t)7;
    }
    return true;
}

static void encode_root(const fixture_t *f, uint64_t lsn, uint64_t visible, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    const candidate_control_state_t *a = &f->b.anchor;
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = a->transaction_highwater,
        .page_highwater = a->page_highwater + 1,.visible_csn = visible,.retained_bytes = CANDIDATE_PAGE_BYTES};
    candidate_page_location_t l = fx_location(f->newer_root,1,lsn);
    OK(candidate_catalog_encode_root(&l,&root,out,CANDIDATE_PAGE_BYTES));
}

static void diff_range(const uint8_t *a, const uint8_t *b, uint32_t *offset, uint32_t *size)
{
    uint32_t lo = 0, hi = 0;
    for (uint32_t i = 64; i < CANDIDATE_PAGE_BYTES; i++) if (a[i] != b[i]) { if (lo == 0) lo = i; hi = i; }
    CHECK(lo != 0); *offset = lo; *size = hi - lo + 1;
}

static void append_newer_root(fixture_t *f, kind_t kind)
{
    fx_t *fx = f->fx; const candidate_control_state_t *a = &f->b.anchor;
    static fx_image_t images[2];
    uint64_t ph = a->page_highwater + 1;
    fx_event_t reserve = {16,a->transaction_highwater,a->transaction_highwater,a->page_highwater,ph};
    uint64_t begin, image_lsn, at = fx_place(fx_position(fx),16,&begin); fx_place(at,32 + CANDIDATE_PAGE_BYTES,&image_lsn);
    uint64_t end = fx_group_end(fx,2,1);
    uint64_t visible = kind == KIND_IMAGE ? a->visible_csn : a->visible_csn + 1;
    encode_root(f,end,visible,images[0].page);
    images[0].id = f->newer_root; images[0].generation = 1; images[0].previous = fx->pages[f->newer_root].lsn;
    candidate_page_ref_t nodes[1] = {{2,1}};
    images[1].id = 1; images[1].generation = 1; images[1].previous = fx->pages[1].lsn;
    fx_encode_alloc_root(fx,1,end,7,ph,0,nodes,images[1].page);
    CHECK(fx_group(fx,NULL,images,2,&reserve,1,true,false) == end);
    memcpy(f->stale_page,images[0].page,CANDIDATE_PAGE_BYTES); encode_root(f,end,a->visible_csn,f->valid_page);
    f->sites[0] = (site_t){.lsn = image_lsn}; f->site_count = 1;
}

static void append_root_delta(fixture_t *f, fx_txn_t *t)
{
    fx_t *fx = f->fx; uint32_t offset, size; diff_range(f->stale_page,f->valid_page,&offset,&size);
    fx_begin(fx,t,1);
    uint64_t begin, delta_lsn, at = fx_place(fx_position(fx),16,&begin); fx_place(at,48 + size,&delta_lsn);
    fx_delta_group(fx,t,f->newer_root,offset,f->valid_page + offset,size); fx_commit(fx,t);
    f->sites[1] = f->sites[0]; f->sites[0] = (site_t){.lsn = delta_lsn}; f->site_count = 2;
    printf("DELTA offset=%u size=%u\n",offset,size);
}

static void build_fixture(fixture_t *f, const char *tag, kind_t kind)
{
    char name[200]; CHECK(snprintf(name,sizeof(name),"%st23t-%s-%ld",place,tag,(long)getpid()) > 0);
    scen_t s = {.name = name,.system_bootstrap = true,.ballast = 1};
    memset(f,0,sizeof(*f)); f->b = scen_build(&s); f->fx = f->b.fx;
    f->older_root = f->b.root_id; f->newer_root = f->b.alloc_count;
    append_newer_root(f,kind);
    candidate_control_state_t anchors[2] = {f->b.anchor,f->b.anchor};
    anchors[1].checkpoint_id = 2; anchors[1].page_highwater = f->b.anchor.page_highwater + 1;
    anchors[1].outcome_root = (candidate_page_ref_t){f->newer_root,1};
    fx_checkpoint(f->fx,&anchors[1],NULL,0);
    fx_txn_t delta = {0}, open = {0};
    if (kind == KIND_DELTA) append_root_delta(f,&delta);
    fx_begin(f->fx,&open,kind == KIND_DELTA ? 2 : 1);
    capture_t c = {f->sites,f->site_count}; candidate_wal_info_t info; OK(candidate_wal_scan(f->fx->w,capture_visit,&c,&info));
    fx_control(f->fx,anchors,2); fx_close(f->fx);
    char wal[300]; wal_path(f->fx->wal,wal); f->valid_end = fx_file_bytes(wal);
    for (unsigned i = 0; i < f->site_count; i++) { CHECK(f->sites[i].size > 0); f->sites[i].offset = f->sites[i].lsn % CANDIDATE_WAL_SEGMENT_BYTES; }
    printf("FIXTURE kind=%d prefix=%s older_root=%" PRIu64 " newer_root=%" PRIu64 " valid_end=%" PRIu64,kind,f->fx->wal,f->older_root,
        f->newer_root,f->valid_end);
    for (unsigned i = 0; i < f->site_count; i++)
        printf(" site%u_lsn=%" PRIu64 " site%u_type=%u site%u_bytes=%zu",i,f->sites[i].lsn,i,f->sites[i].type,i,f->sites[i].size);
    printf("\n"); fflush(stdout);
}

static bool calibrate(const fixture_t *f, fx_t *cal, unsigned *first_hit_allocation)
{
    fx_copy_set(f->fx,"cal",cal); print_snapshot("cal-before",cal);
    hook_reset(cal,f,M_NONE); uint64_t root; candidate_wal_status_t status = recover(cal,&root);
    print_hook("calibrate",status,root); *first_hit_allocation = h.first_hit_allocation;
    bool is_read = true;
    for (unsigned i = 0; i < h.site_count; i++) is_read = is_read && h.sites[i].hits > 0 && h.sites[i].mismatches == 0;
    printf("CALIBRATE status=%s root=%" PRIu64 " read=%d\n",name_of(status),root,is_read);
    print_snapshot("cal-after",cal);
    return status == CANDIDATE_WAL_OK && root == f->newer_root && is_read;
}

static void expect_stable(const fx_t *set, const fixture_t *f, const char *tag)
{
    hook_reset(set,f,M_NONE); uint64_t root; candidate_wal_status_t status = recover(set,&root);
    print_hook(tag,status,root);
    EXPECT(status == CANDIDATE_WAL_OK); EXPECT(root == f->newer_root);
}

static int run_recovery(kind_t kind, malform_t malform, bool fails_allocation, const char *tag)
{
    fixture_t f; build_fixture(&f,tag,kind);
    fx_t cal, attempt, pre, post; unsigned first_hit_allocation;
    if (!calibrate(&f,&cal,&first_hit_allocation)) { printf("CALIBRATION-FAILED %s\n",tag); scen_free(&f.b); return EXIT_CALIBRATION; }
    if (malform == M_NONE && !fails_allocation) { scen_free(&f.b); return 0; }
    fx_copy_set(f.fx,"attempt",&attempt); fx_copy_set(&attempt,"pre",&pre);
    print_snapshot("attempt-pre",&pre);
    snapshot_t before = take(&attempt); hook_reset(&attempt,&f,malform);
    if (fails_allocation) h.fail_allocation = first_hit_allocation;
    uint64_t root; candidate_wal_status_t status = recover(&attempt,&root);
    print_hook(tag,status,root);
    hook_t observed = h; bool observed_handle = has_handle; unsigned observed_effects = effects();
    fx_copy_set(&attempt,"post",&post); print_snapshot("attempt-post",&post);
    snapshot_t after = take(&attempt);
    EXPECT(status == (fails_allocation ? CANDIDATE_WAL_NOMEM : CANDIDATE_WAL_CORRUPT)); EXPECT(!observed_handle);
    EXPECT(observed_effects == 0); EXPECT(same(&before,&after));
    if (fails_allocation) EXPECT(observed.failed_allocations == 1);
    else EXPECT(observed.injected == 1 && observed.sites[0].hits > 0 && observed.sites[0].mismatches == 0);
    expect_stable(&attempt,&f,"stable-after-attempt");
    scen_free(&f.b);
    return failures == 0 ? 0 : EXIT_EXPECTATION;
}

static int run_fallback(void)
{
    fixture_t f; build_fixture(&f,"fallback",KIND_INVALID_ROOT);
    fx_t set; fx_copy_set(f.fx,"fallback",&set); print_snapshot("fallback-before",&set);
    hook_reset(&set,&f,M_NONE); uint64_t root; candidate_wal_status_t status = recover(&set,&root);
    print_hook("dependency-fallback",status,root); print_snapshot("fallback-after",&set);
    EXPECT(status == CANDIDATE_WAL_OK); EXPECT(root == f.older_root);
    EXPECT(h.sites[0].hits > 0 && h.sites[0].mismatches == 0);
    scen_free(&f.b);
    return failures == 0 ? 0 : EXIT_EXPECTATION;
}

typedef struct { const char *name; malform_t malform; bool is_pinned; candidate_wal_status_t first, then; } wal_case_t;

static candidate_wal_t *open_wal(const fx_t *set, bool is_pinned)
{
    candidate_wal_options_t o = {.owner_epoch = 8,.io = &hooks}; memcpy(o.uuid,fx_uuid,16);
    candidate_wal_t *w = NULL; OK(candidate_wal_open(set->wal,&o,&w));
    if (is_pinned) OK(candidate_wal_pin(w));
    return w;
}

static candidate_wal_status_t read_target(candidate_wal_t *w, const fixture_t *f)
{
    candidate_wal_record_t r; candidate_wal_status_t status = candidate_wal_read_at(w,f->sites[0].lsn,&r);
    if (status == CANDIDATE_WAL_OK && (r.lsn != f->sites[0].lsn || r.type != f->sites[0].type)) return CANDIDATE_WAL_EXISTS;
    return status;
}

static void expect_invalid_args(candidate_wal_t *w, const fixture_t *f)
{
    candidate_wal_record_t r; uint64_t lsn = f->sites[0].lsn;
    candidate_wal_status_t got[4] = {candidate_wal_read_at(w,lsn,NULL),candidate_wal_read_at(w,lsn + 4,&r),
        candidate_wal_read_at(w,8,&r),candidate_wal_read_at(w,f->valid_end,&r)};
    for (unsigned i = 0; i < 4; i++) { printf("CASE invalid-arg%u status=%s\n",i,name_of(got[i])); EXPECT(got[i] == CANDIDATE_WAL_INVALID); }
}

static void expect_followups(candidate_wal_t *w, const fixture_t *f, const wal_case_t *c)
{
    candidate_wal_info_t info; uint8_t body[16] = {0}; uint64_t end = 0;
    candidate_wal_record_t record = {.type = 2,.transaction_id = 4000,.payload = body,.payload_bytes = sizeof(body)};
    candidate_wal_status_t read = read_target(w,f), scan = candidate_wal_scan(w,NULL,NULL,&info);
    candidate_wal_status_t append = CANDIDATE_WAL_OK, force = CANDIDATE_WAL_OK;
    bool is_rejected = c->then != CANDIDATE_WAL_OK;
    if (is_rejected) { append = candidate_wal_append(w,&record,&end); force = candidate_wal_force(w,f->valid_end); }
    printf("CASE followup read=%s scan=%s append=%s force=%s effects=%u\n",name_of(read),name_of(scan),
        is_rejected ? name_of(append) : "skipped",is_rejected ? name_of(force) : "skipped",effects());
    fflush(stdout);
    EXPECT(read == c->then); EXPECT(scan == c->then); EXPECT(append == c->then || !is_rejected);
    EXPECT(force == c->then || !is_rejected); EXPECT(effects() == 0);
}

static int run_wal(const wal_case_t *c)
{
    fixture_t f; build_fixture(&f,c->name,KIND_IMAGE);
    fx_t set; fx_copy_set(f.fx,"wal",&set); print_snapshot("wal-before",&set);
    hook_reset(&set,&f,M_NONE); candidate_wal_t *w = open_wal(&set,c->is_pinned);
    uint64_t end = candidate_wal_info(w).valid_end_lsn; candidate_wal_status_t stable = read_target(w,&f);
    printf("CASE %s-stable status=%s valid_end=%" PRIu64 " file_end=%" PRIu64 "\n",c->name,name_of(stable),end,f.valid_end);
    if (stable != CANDIDATE_WAL_OK || end != f.valid_end) { printf("CALIBRATION-FAILED %s\n",c->name); return EXIT_CALIBRATION; }
    snapshot_t before = take(&set);
    hook_reset(&set,&f,c->malform);
    if (c->malform == M_INVALID_ARGS) expect_invalid_args(w,&f);
    else { candidate_wal_status_t first = read_target(w,&f); print_hook(c->name,first,0); EXPECT(first == c->first); EXPECT(h.injected == 1); }
    hook_reset(&set,&f,M_NONE); expect_followups(w,&f,c);
    printf("CASE close status=%s\n",name_of(candidate_wal_close(&w)));
    snapshot_t after = take(&set); EXPECT(same(&before,&after)); print_snapshot("wal-after",&set);
    hook_reset(&set,&f,M_NONE); w = open_wal(&set,true); candidate_wal_status_t reopened = read_target(w,&f);
    printf("CASE reopen status=%s\n",name_of(reopened)); EXPECT(reopened == CANDIDATE_WAL_OK); OK(candidate_wal_close(&w));
    scen_free(&f.b);
    return failures == 0 ? 0 : EXIT_EXPECTATION;
}

static const wal_case_t wal_cases[] = {
    {"wal-header-pinned",M_HEADER,true,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
    {"wal-header-unpinned",M_HEADER,false,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_POISONED},
    {"wal-crc-pinned",M_CRC,true,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
    {"wal-crc-unpinned",M_CRC,false,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_POISONED},
    {"wal-trailer-pinned",M_TRAILER,true,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
    {"wal-total-pinned",M_TOTAL,true,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_CORRUPT},
    {"wal-total-unpinned",M_TOTAL,false,CANDIDATE_WAL_CORRUPT,CANDIDATE_WAL_POISONED},
    {"wal-version-pinned",M_VERSION,true,CANDIDATE_WAL_UNSUPPORTED,CANDIDATE_WAL_OK},
    {"wal-uuid-pinned",M_UUID,true,CANDIDATE_WAL_UNSUPPORTED,CANDIDATE_WAL_OK},
    {"wal-io-header-pinned",M_IO_HEADER,true,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED},
    {"wal-io-frame-pinned",M_IO_FRAME,true,CANDIDATE_WAL_IO,CANDIDATE_WAL_POISONED},
    {"wal-invalid-pinned",M_INVALID_ARGS,true,CANDIDATE_WAL_INVALID,CANDIDATE_WAL_OK},
};

typedef struct { const char *name; kind_t kind; malform_t malform; bool fails_allocation; } recovery_case_t;

static const recovery_case_t recovery_cases[] = {
    {"image-calibrate",KIND_IMAGE,M_NONE,false},
    {"delta-calibrate",KIND_DELTA,M_NONE,false},
    {"image-header",KIND_IMAGE,M_HEADER,false},
    {"image-crc",KIND_IMAGE,M_CRC,false},
    {"image-total",KIND_IMAGE,M_TOTAL,false},
    {"delta-header",KIND_DELTA,M_HEADER,false},
    {"delta-crc",KIND_DELTA,M_CRC,false},
    {"delta-total",KIND_DELTA,M_TOTAL,false},
    {"image-nomem",KIND_IMAGE,M_NONE,true},
};

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
    const char *c = argv[1]; int code = -1;
    if (argc == 3) place = argv[2];
    for (unsigned i = 0; i < sizeof(recovery_cases) / sizeof(recovery_cases[0]); i++) {
        const recovery_case_t *k = &recovery_cases[i];
        if (strcmp(c,k->name) == 0) code = run_recovery(k->kind,k->malform,k->fails_allocation,k->name);
    }
    for (unsigned i = 0; i < sizeof(wal_cases) / sizeof(wal_cases[0]); i++) if (strcmp(c,wal_cases[i].name) == 0) code = run_wal(&wal_cases[i]);
    if (strcmp(c,"dependency-fallback") == 0) code = run_fallback();
    CHECK(code >= 0);
    printf("%s %s checks=%u failures=%u\n",code == 0 ? "PASS" : "FAILED",c,checks,failures);
    return code;
}
