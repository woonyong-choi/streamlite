#include "test_candidate_recovery_publish_fixture.h"

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_wal_witness.h"
#include "../src/storage/candidate_wal_internal.h"

unsigned checks;

enum { V_ALL = 1, V_FULL, V_RANDOM_FULL, V_RANDOM_BOTH };
typedef struct { uint64_t offset; uint8_t *bytes; size_t size; int mode; } region_t;
typedef struct {
    ino_t data, control, wal0;
    unsigned wal_writes, data_writes, control_writes, truncates, scans, write_scan, data_write_scan;
    unsigned active_scan, from_release, to_release, releases, hits, random_hits, big_count, fsyncs;
    unsigned first_hit_scan, release_scan, mutated;
    uint8_t *mutate;
    size_t mutate_bytes;
    size_t marker, big[32];
    region_t regions[4];
    unsigned region_count;
    uint64_t last;
    bool armed, always;
} hook_t;
static hook_t h;

static ino_t inode_of(int fd) { struct stat st; return fstat(fd,&st) == 0 ? st.st_ino : 0; }

static bool view_b(void)
{
    if (h.marker != 0) return h.releases >= h.from_release && h.releases < h.to_release;
    if (h.active_scan != 0) return h.scans == h.active_scan;
    return h.always;
}

static void overlay(const region_t *g, uint8_t *p, size_t n, uint64_t at)
{
    uint64_t lo = at > g->offset ? at : g->offset, hi = at + n < g->offset + g->size ? at + n : g->offset + g->size;
    if (lo < hi) { memcpy(p + (lo - at),g->bytes + (lo - g->offset),(size_t)(hi - lo)); if (h.hits++ == 0) h.first_hit_scan = h.scans; }
}

static ssize_t hook_pread(int fd, void *p, size_t n, off_t offset)
{
    ssize_t got = pread(fd,p,n,offset);
    if (got <= 0 || inode_of(fd) != h.wal0) return got;
    uint64_t at = (uint64_t)offset;
    if (at == 0 && n == 64) { h.scans++; h.last = 0; }
    bool is_back = at < h.last;
    if (at > h.last) h.last = at;
    for (unsigned i = 0; i < h.region_count; i++) if (at == h.regions[i].offset && n == 80 && is_back) h.random_hits++;
    if (!view_b()) return got;
    for (unsigned i = 0; i < h.region_count; i++) {
        const region_t *g = &h.regions[i];
        bool is_full = at == g->offset && (size_t)got == g->size;
        if (at == g->offset && n == 80 && is_back) h.armed = true;
        if (g->mode == V_ALL) overlay(g,p,(size_t)got,at);
        else if (g->mode == V_FULL && is_full) overlay(g,p,(size_t)got,at);
        else if (g->mode == V_RANDOM_FULL && is_full && h.armed) { overlay(g,p,(size_t)got,at); h.armed = false; }
        else if (g->mode == V_RANDOM_BOTH && h.armed && at == g->offset) { overlay(g,p,(size_t)got,at); if (is_full) h.armed = false; }
    }
    return got;
}

static ssize_t hook_pwrite(int fd, const void *p, size_t n, off_t offset)
{
    ino_t inode = inode_of(fd);
    if (h.wal_writes + h.data_writes + h.control_writes == 0) h.write_scan = h.scans;
    if (inode == h.data) { if (h.data_writes++ == 0) h.data_write_scan = h.scans; }
    else if (inode == h.control) h.control_writes++;
    else h.wal_writes++;
    if (h.mutate == NULL || inode != h.wal0) return pwrite(fd,p,n,offset);
    ssize_t got = pwrite(fd,p,n,offset); memset(h.mutate,0xb2,h.mutate_bytes); h.mutate = NULL; h.mutated++;
    return got;
}

static int hook_fsync(int fd) { h.fsyncs++; return fsync(fd); }
static int hook_truncate(int fd, off_t size) { h.truncates++; return ftruncate(fd,size); }
static const candidate_wal_io_t hooks = {hook_pread,hook_pwrite,hook_fsync,hook_truncate};

static void *allocate(void *context, size_t size)
{
    (void)context;
    if (size >= (1u << 20) && h.big_count < 32) h.big[h.big_count++] = size;
    return malloc(size);
}

static void release(void *context, void *p, size_t size)
{
    (void)context;
    if (h.marker != 0 && size == h.marker) { if (h.releases == 0) h.release_scan = h.scans; h.releases++; }
    free(p);
}
static const candidate_wal_memory_t memory = {NULL,allocate,release};

static void segment_path(const char *wal, unsigned id, char out[300])
{
    CHECK(snprintf(out,300,"%s.wal.%016" PRIx64,wal,(uint64_t)id) > 0);
}

static void hook_reset(const fx_t *fx)
{
    memset(&h,0,sizeof(h)); struct stat st;
    CHECK(stat(fx->data,&st) == 0); h.data = st.st_ino;
    CHECK(stat(fx->control,&st) == 0); h.control = st.st_ino;
    char name[300]; segment_path(fx->wal,0,name); CHECK(stat(name,&st) == 0); h.wal0 = st.st_ino;
}

static void hook_regions(const region_t *regions, unsigned count)
{
    CHECK(count <= 4); memcpy(h.regions,regions,count * sizeof(regions[0])); h.region_count = count;
}

static unsigned writes(void) { return h.wal_writes + h.data_writes + h.control_writes + h.truncates; }

static void print_digest(const char *label, const uint8_t *bytes, size_t size)
{
    uint8_t d[32]; OK(candidate_digest_sha256(bytes,size,d)); printf(" %s=",label);
    for (unsigned i = 0; i < 32; i++) printf("%02x",d[i]);
}

static void keep_bytes(const char *path, const uint8_t *bytes, size_t size)
{
    int fd = open(path,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(fd >= 0);
    CHECK(pwrite(fd,bytes,size,0) == (ssize_t)size); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

static candidate_wal_status_t recover(const fx_t *fx, uint64_t owner, candidate_recovery_t **out)
{
    candidate_recovery_options_t o = fx_options(fx,owner); o.io = &hooks; o.memory = &memory;
    return candidate_recovery_recover(&o,out);
}

static const char *name_of(candidate_wal_status_t s)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

typedef struct { uint8_t control[32], data[32], wal[64][32]; unsigned segments; } snapshot_t;

static snapshot_t take(const fx_t *fx)
{
    snapshot_t s = {0}; fx_sha256_file(fx->control,s.control); fx_sha256_file(fx->data,s.data);
    for (unsigned id = 0; id < 64; id++) {
        char name[300]; segment_path(fx->wal,id,name);
        if (access(name,F_OK) != 0) break;
        fx_sha256_file(name,s.wal[id]); s.segments++;
    }
    return s;
}

static bool same(const snapshot_t *a, const snapshot_t *b) { return memcmp(a,b,sizeof(*a)) == 0; }

static void report(const char *tag, candidate_wal_status_t s, const snapshot_t *before, const fx_t *fx)
{
    snapshot_t after = take(fx);
    printf("CASE %s status=%s writes=%u wal=%u data=%u control=%u truncates=%u fsyncs=%u unchanged=%d scans=%u hits=%u random=%u\n",
        tag,name_of(s),writes(),h.wal_writes,h.data_writes,h.control_writes,h.truncates,h.fsyncs,same(before,&after),h.scans,h.hits,
        h.random_hits);
    fflush(stdout);
}

static size_t calibrate_marker(void)
{
    size_t marker = 0;
    printf("CALIBRATE big=");
    for (unsigned i = 0; i < h.big_count; i++) {
        printf("%s%zu",i == 0 ? "" : ",",h.big[i]);
        if (h.big[i] > marker) marker = h.big[i];
    }
    printf(" marker=%zu scans=%u write_scan=%u data_write_scan=%u\n",marker,h.scans,h.write_scan,h.data_write_scan);
    fflush(stdout);
    CHECK(marker >= (4u << 20));
    return marker;
}

typedef struct { candidate_wal_record_t r; uint8_t *payload; } rec_t;
typedef struct { rec_t *recs; unsigned count, capacity; uint64_t from, to; } capture_t;

static bool capture_visit(void *context, const candidate_wal_record_t *r)
{
    capture_t *c = context;
    if (r->lsn < c->from || r->lsn >= c->to) return true;
    CHECK(c->count < c->capacity);
    rec_t *e = &c->recs[c->count++]; e->r = *r;
    e->payload = malloc(r->payload_bytes + 1); CHECK(e->payload != NULL);
    memcpy(e->payload,r->payload,r->payload_bytes); e->r.payload = e->payload;
    return true;
}

static capture_t capture_open(candidate_wal_t *w, uint64_t from, uint64_t to)
{
    static rec_t recs[512]; capture_t c = {recs,0,512,from,to}; candidate_wal_info_t info;
    OK(candidate_wal_scan(w,capture_visit,&c,&info)); return c;
}

static capture_t capture_file(const fx_t *fx, uint64_t from)
{
    candidate_wal_options_t o = {.owner_epoch = 9}; memcpy(o.uuid,fx_uuid,16);
    candidate_wal_t *w = NULL; OK(candidate_wal_open(fx->wal,&o,&w));
    capture_t c = capture_open(w,from,UINT64_MAX); OK(candidate_wal_close(&w)); return c;
}

static region_t frame_region(const candidate_wal_record_t *r, int mode)
{
    region_t g = {.offset = r->lsn % CANDIDATE_WAL_SEGMENT_BYTES,.mode = mode};
    g.bytes = malloc(CANDIDATE_WAL_RECORD_BYTES); CHECK(g.bytes != NULL);
    OK(candidate_wal_encode(fx_uuid,r,g.bytes,CANDIDATE_WAL_RECORD_BYTES,&g.size));
    return g;
}

/* group [begin,end)의 target page image를 바꾸고 PHYS_END group CRC만 다시 계산한 B frame 두 개를 만든다. */
static unsigned spoof_page(candidate_wal_t *w, uint64_t begin, uint64_t end, uint64_t target, const uint8_t *page,
    int mode, region_t out[2])
{
    capture_t c = capture_open(w,begin,end);
    CHECK(c.count >= 3 && c.recs[0].r.type == 3 && c.recs[c.count - 1].r.type == 8);
    uint32_t crc = 0; unsigned n = 0;
    for (unsigned i = 0; i < c.count; i++) {
        candidate_wal_record_t *r = &c.recs[i].r;
        if (r->lsn == target) {
            CHECK(r->type == 4 && r->payload_bytes == 32 + CANDIDATE_PAGE_BYTES);
            memcpy(c.recs[i].payload + 32,page,CANDIDATE_PAGE_BYTES); out[n++] = frame_region(r,mode);
        }
        if (r->type != 8) { crc = candidate_wal_witness_crc(crc,r); continue; }
        for (unsigned k = 0; k < 4; k++) c.recs[i].payload[16 + k] = (uint8_t)(crc >> (8 * k));
        out[n++] = frame_region(r,mode);
    }
    CHECK(n == 2);
    return n;
}

static void dump_view(const fx_t *fx, const region_t *regions, unsigned count)
{
    char source[300], target[320]; segment_path(fx->wal,0,source);
    CHECK(snprintf(target,sizeof(target),"%s-viewb.wal.%016" PRIx64,fx->wal,(uint64_t)0) > 0);
    uint64_t size = fx_file_bytes(source); uint8_t *bytes = malloc(size); CHECK(bytes != NULL);
    int in = open(source,O_RDONLY); CHECK(in >= 0); CHECK(pread(in,bytes,size,0) == (ssize_t)size); CHECK(close(in) == 0);
    for (unsigned i = 0; i < count; i++) {
        CHECK(regions[i].offset + regions[i].size <= size); memcpy(bytes + regions[i].offset,regions[i].bytes,regions[i].size);
    }
    int out = open(target,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(out >= 0);
    CHECK(pwrite(out,bytes,size,0) == (ssize_t)size); CHECK(fsync(out) == 0); CHECK(close(out) == 0); free(bytes);
    printf("FIXTURE wal=%s viewb=%s-viewb control=%s data=%s regions=%u",fx->wal,fx->wal,fx->control,fx->data,count);
    for (unsigned i = 0; i < count; i++) printf(" lsn%u=%" PRIu64,i,regions[i].offset);
    printf("\n"); fflush(stdout);
}

static void name_case(char *out, size_t size, const char *tag)
{
    CHECK(snprintf(out,size,"t23ps-%s-%ld",tag,(long)getpid()) > 0);
}

static void read_page(const fx_t *fx, uint64_t id, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    int fd = open(fx->data,O_RDONLY); CHECK(fd >= 0); memset(out,0,CANDIDATE_PAGE_BYTES);
    CHECK(pread(fd,out,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)) >= 0); CHECK(close(fd) == 0);
}

typedef struct { uint64_t wrong; unsigned visits, wrong_visits; } seen_t;
static bool seen_visit(void *context, const candidate_wal_record_t *r)
{
    seen_t *s = context; s->visits++; if (r->lsn == s->wrong) s->wrong_visits++; return true;
}

/* 80B header는 A이고 full frame 재읽기만 같은 길이의 다른 LSN frame B다. */
static void case_scan_lsn(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"scanlsn");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s);
    uint64_t lsn = b.txn[0].last; capture_t c = capture_open(b.fx->w,lsn,lsn + 1); CHECK(c.count == 1 && c.recs[0].r.type == 10);
    candidate_wal_record_t r = c.recs[0].r; r.lsn = lsn + 8; region_t g = frame_region(&r,V_FULL); g.offset = lsn % CANDIDATE_WAL_SEGMENT_BYTES;
    scen_finish(&b); dump_view(b.fx,&g,1);
    hook_reset(b.fx); hook_regions(&g,1);
    candidate_wal_options_t o = {.owner_epoch = 8,.io = &hooks}; memcpy(o.uuid,fx_uuid,16);
    candidate_wal_t *w = NULL; OK(candidate_wal_open(b.fx->wal,&o,&w));
    h.always = true; seen_t seen = {.wrong = lsn + 8}; candidate_wal_info_t info;
    candidate_wal_status_t status = candidate_wal_scan(w,seen_visit,&seen,&info);
    printf("CASE scan-lsn wal status=%s visits=%u wrong_lsn_visits=%u\n",name_of(status),seen.visits,seen.wrong_visits);
    candidate_wal_close(&w);
    snapshot_t before = take(b.fx); hook_reset(b.fx); hook_regions(&g,1); h.always = true;
    candidate_recovery_t *rec = NULL; candidate_wal_status_t recovered = recover(b.fx,8,&rec);
    report("scan-lsn-recover",recovered,&before,b.fx);
    snapshot_t after = take(b.fx);
    CHECK(status == CANDIDATE_WAL_CORRUPT && seen.wrong_visits == 0);
    CHECK(recovered != CANDIDATE_WAL_OK && rec == NULL && writes() == 0 && same(&before,&after));
    scen_free(&b);
}

/* page X: G1 image와 G2 delta가 data에 없어 preview가 delta와 그 이전 image를 read-at으로 읽는다. */
static void encode_ballast(uint64_t id, uint64_t lsn, uint8_t nonce, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_catalog_node_t node = {.count = 1};
    node.entries[0] = (candidate_outcome_t){.epoch = 1,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,.admitting_owner_epoch = 7};
    memset(node.entries[0].nonce,nonce,16);
    candidate_page_location_t l = fx_location(id,1,lsn); OK(candidate_catalog_encode_node(&l,&node,out,CANDIDATE_PAGE_BYTES));
}

static void case_delta(bool predecessor)
{
    static req_t req[1] = {{REQ_COMMIT,STORE_ADMITTED,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),predecessor ? "pred" : "readlsn");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    uint64_t begin, image_lsn, at = fx_place(fx_position(fx),16,&begin); fx_place(at,32 + CANDIDATE_PAGE_BYTES,&image_lsn);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0);
    encode_ballast(x,end,0x11,image.page); image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    fx_group(fx,&t,&image,1,NULL,0,true,false);
    uint8_t second[CANDIDATE_PAGE_BYTES]; encode_ballast(x,end,0x22,second);
    uint32_t body = (uint32_t)(second[48] | second[49] << 8 | second[50] << 16 | (uint32_t)second[51] << 24);
    uint64_t delta_lsn; at = fx_place(fx_position(fx),16,&begin); fx_place(at,48 + body,&delta_lsn);
    fx_delta_group(fx,&t,x,64,second + 64,body); fx_commit(fx,&t);
    region_t g;
    if (predecessor) {
        capture_t c = capture_open(fx->w,image_lsn,image_lsn + 1); CHECK(c.count == 1);
        encode_ballast(x,end,0x33,c.recs[0].payload + 32); g = frame_region(&c.recs[0].r,V_RANDOM_BOTH);
    } else {
        capture_t c = capture_open(fx->w,delta_lsn,delta_lsn + 1); CHECK(c.count == 1 && c.recs[0].r.type == 5);
        candidate_wal_record_t r = c.recs[0].r; r.lsn = delta_lsn + 8; g = frame_region(&r,V_RANDOM_FULL);
        g.offset = delta_lsn % CANDIDATE_WAL_SEGMENT_BYTES;
    }
    scen_finish(&b); dump_view(fx,&g,1);
    snapshot_t before = take(fx); hook_reset(fx); hook_regions(&g,1); h.always = true;
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(fx,8,&r);
    const char *tag = predecessor ? "pred-spoof" : "read-lsn";
    report(tag,status,&before,fx); unsigned hits = h.hits;
    snapshot_t after = take(fx);
    CHECK(status == CANDIDATE_WAL_CORRUPT && r == NULL && writes() == 0 && h.fsyncs == 0 && same(&before,&after) && hits > 0);
    hook_reset(fx); status = recover(fx,8,&r);
    printf("CASE %s-clean status=%s\n",tag,name_of(status)); fflush(stdout);
    CHECK(status == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    uint8_t page[CANDIDATE_PAGE_BYTES]; read_page(fx,x,page); CHECK(memcmp(page + 64,second + 64,body) == 0);
    scen_free(&b);
}

/* 선택 scan은 A, 첫 수리 의미 scan부터는 B: B root는 allocation/PHYS가 없는 ref4를 가리킨다. */
static void case_selection_repair(void)
{
    char name[64]; name_case(name,sizeof(name),"selrepair");
    scen_t s = {.name = name,.system_bootstrap = true,.page_highwater = 8,.unapplied_last = true};
    built_t b = scen_build(&s); fx_t *fx = b.fx;
    capture_t c = capture_open(fx->w,64,b.last_group_end); uint64_t begin = 0, target = 0;
    for (unsigned i = 0; i < c.count; i++) {
        const candidate_wal_record_t *r = &c.recs[i].r;
        if (r->type == 3) begin = r->lsn;
        if (r->type == 4 && c.recs[i].payload[8] == b.root_id && r->lsn > begin) target = r->lsn;
    }
    CHECK(begin != 0 && target > begin);
    capture_t t = capture_open(fx->w,target,target + 1);
    candidate_catalog_root_t root; OK(candidate_catalog_decode_root(fx_uuid,t.recs[0].payload + 32,CANDIDATE_PAGE_BYTES,&root));
    CHECK(root.count == 0 && root.page_highwater >= 4);
    root.count = 1; root.pages[0] = (candidate_page_ref_t){4,1}; root.retained_bytes = 2 * CANDIDATE_PAGE_BYTES;
    uint8_t page[CANDIDATE_PAGE_BYTES]; candidate_page_location_t l = fx_location(b.root_id,1,b.last_group_end);
    OK(candidate_catalog_encode_root(&l,&root,page,sizeof(page)));
    region_t g[2]; spoof_page(fx->w,begin,b.last_group_end,target,page,V_ALL,g);
    fx_txn_t open = {0}; fx_begin(fx,&open,1);
    scen_finish(&b); dump_view(fx,g,2);
    fx_t cal, spoof; fx_copy_set(fx,"cal",&cal); fx_copy_set(fx,"spoof",&spoof);
    candidate_recovery_t *r = NULL; hook_reset(&cal); candidate_wal_status_t status = recover(&cal,8,&r);
    printf("CASE selection-repair-calibrate status=%s\n",name_of(status)); size_t marker = calibrate_marker();
    CHECK(status == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    snapshot_t before = take(&spoof); hook_reset(&spoof); hook_regions(g,2);
    h.marker = marker; h.from_release = 1; h.to_release = UINT_MAX;
    status = recover(&spoof,8,&r); report("selection-repair",status,&before,&spoof);
    snapshot_t after = take(&spoof); unsigned changed = writes() + h.fsyncs; bool unchanged = same(&before,&after);
    hook_reset(&spoof); candidate_wal_status_t retry = recover(&spoof,8,&r);
    printf("CASE selection-repair-retry status=%s\n",name_of(retry)); fflush(stdout);
    if (retry == CANDIDATE_WAL_OK) OK(candidate_recovery_close(&r));
    CHECK(status == CANDIDATE_WAL_CORRUPT && changed == 0 && unchanged);
    CHECK(retry == CANDIDATE_WAL_OK);
    scen_free(&b);
}

/* 높은 slot A는 visible 상한으로 CORRUPT, 낮은 slot은 B, 그 뒤 다시 A. data는 A라 B frame random read가 없다. */
static void case_cross_fallback(void)
{
    char name[64]; name_case(name,sizeof(name),"cross");
    scen_t s = {.name = name,.system_bootstrap = true,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx;
    candidate_control_state_t anchors[2] = {b.anchor,b.anchor};
    uint64_t bad = b.alloc_count, ph = b.anchor.page_highwater + 1;
    static fx_image_t images[2];
    fx_event_t reserve = {16,b.anchor.transaction_highwater,b.anchor.transaction_highwater,b.anchor.page_highwater,ph};
    uint64_t begin, target, at = fx_place(fx_position(fx),16,&begin); fx_place(at,32 + CANDIDATE_PAGE_BYTES,&target);
    uint64_t end = fx_group_end(fx,2,1);
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,.transaction_highwater = b.anchor.transaction_highwater,
        .page_highwater = ph,.visible_csn = 1,.retained_bytes = CANDIDATE_PAGE_BYTES};
    candidate_page_location_t l = fx_location(bad,1,end);
    OK(candidate_catalog_encode_root(&l,&root,images[0].page,CANDIDATE_PAGE_BYTES));
    images[0].id = bad; images[0].generation = 1; images[0].previous = fx->pages[bad].lsn;
    candidate_page_ref_t nodes[1] = {{2,1}};
    images[1].id = 1; images[1].generation = 1; images[1].previous = fx->pages[1].lsn;
    fx_encode_alloc_root(fx,1,end,7,ph,0,nodes,images[1].page);
    CHECK(fx_group(fx,NULL,images,2,&reserve,1,true,true) == end);
    anchors[1].checkpoint_id = 2; anchors[1].page_highwater = ph; anchors[1].outcome_root = (candidate_page_ref_t){bad,1};
    fx_checkpoint(fx,&anchors[1],NULL,0);
    root.retained_bytes = 2 * CANDIDATE_PAGE_BYTES; uint8_t page[CANDIDATE_PAGE_BYTES];
    OK(candidate_catalog_encode_root(&l,&root,page,sizeof(page)));
    region_t g[2]; spoof_page(fx->w,begin,end,target,page,V_ALL,g);
    fx_txn_t t = {0}; fx_begin(fx,&t,1);
    fx_control(fx,anchors,2); fx_close(fx); dump_view(fx,g,2);
    fx_t cal, spoof; fx_copy_set(fx,"cal",&cal); fx_copy_set(fx,"spoof",&spoof);
    candidate_recovery_t *r = NULL; hook_reset(&cal); candidate_wal_status_t status = recover(&cal,8,&r);
    printf("CASE cross-fallback-calibrate status=%s root=%" PRIu64 "\n",name_of(status),
        status == CANDIDATE_WAL_OK ? candidate_recovery_info(r).anchor.outcome_root.id : 0);
    size_t marker = calibrate_marker(); CHECK(status == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    snapshot_t before = take(&spoof); hook_reset(&spoof); hook_regions(g,2);
    h.marker = marker; h.from_release = 1; h.to_release = 2;
    status = recover(&spoof,8,&r); report("cross-fallback",status,&before,&spoof);
    snapshot_t after = take(&spoof); unsigned changed = writes() + h.fsyncs, random = h.random_hits; bool unchanged = same(&before,&after);
    if (r != NULL) OK(candidate_recovery_close(&r));
    hook_reset(&spoof); candidate_wal_status_t retry = recover(&spoof,8,&r);
    printf("CASE cross-fallback-retry status=%s\n",name_of(retry)); fflush(stdout);
    if (retry == CANDIDATE_WAL_OK) OK(candidate_recovery_close(&r));
    CHECK(random == 0);
    CHECK(status == CANDIDATE_WAL_CORRUPT && changed == 0 && unchanged && retry == CANDIDATE_WAL_OK);
    scen_free(&b);
}

/* 의미 scan·redo preflight는 A, redo 적용 scan만 B: 참조 없는 일반 version inline payload만 바뀐다. */
static void encode_version(uint64_t id, uint64_t lsn, uint64_t txn, uint8_t fill, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    uint8_t bytes[64]; memset(bytes,fill,sizeof(bytes));
    candidate_page_version_t v = {.transaction_id = txn,.table_id = 1,.row_id = 77,.payload_bytes = sizeof(bytes),.slot_generation = 1,
        .inline_bytes = bytes};
    candidate_page_location_t l = fx_location(id,1,lsn); OK(candidate_page_encode_versions(&l,&v,1,out,CANDIDATE_PAGE_BYTES));
}

static void case_redo_apply(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"redoapply");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    uint64_t begin, target, at = fx_place(fx_position(fx),16,&begin); fx_place(at,32 + CANDIDATE_PAGE_BYTES,&target);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0);
    encode_version(x,end,t.id,0x5a,image.page); image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    CHECK(fx_group(fx,&t,&image,1,NULL,0,true,false) == end); fx_commit(fx,&t);
    uint8_t page_a[CANDIDATE_PAGE_BYTES], page_b[CANDIDATE_PAGE_BYTES];
    memcpy(page_a,image.page,sizeof(page_a)); encode_version(x,end,t.id,0xa5,page_b);
    region_t g[2]; spoof_page(fx->w,begin,end,target,page_b,V_ALL,g);
    scen_finish(&b); dump_view(fx,g,2);
    fx_t cal, spoof; fx_copy_set(fx,"cal",&cal); fx_copy_set(fx,"spoof",&spoof);
    candidate_recovery_t *r = NULL; hook_reset(&cal); candidate_wal_status_t status = recover(&cal,8,&r);
    unsigned apply_scan = h.data_write_scan;
    printf("CASE redo-apply-calibrate status=%s apply_scan=%u\n",name_of(status),apply_scan); calibrate_marker();
    CHECK(status == CANDIDATE_WAL_OK && apply_scan != 0); OK(candidate_recovery_close(&r));
    uint8_t got[CANDIDATE_PAGE_BYTES]; read_page(&cal,x,got); CHECK(memcmp(got,page_a,sizeof(got)) == 0);
    snapshot_t before = take(&spoof); hook_reset(&spoof); hook_regions(g,2); h.active_scan = apply_scan;
    status = recover(&spoof,8,&r); report("redo-apply",status,&before,&spoof);
    unsigned data_writes = h.data_writes, first_fsyncs = h.fsyncs; if (r != NULL) OK(candidate_recovery_close(&r));
    char kept[320]; CHECK(snprintf(kept,sizeof(kept),"%s-after-spoof",spoof.data) > 0); fx_copy_file(spoof.data,kept);
    read_page(&spoof,x,got); bool first_is_b = memcmp(got,page_b,sizeof(got)) == 0;
    printf("CASE redo-apply-target page_is_a=%d page_is_b=%d kept=%s\n",memcmp(got,page_a,sizeof(got)) == 0,first_is_b,kept);
    hook_reset(&spoof); candidate_wal_status_t retry = recover(&spoof,8,&r);
    read_page(&spoof,x,got); bool is_a = memcmp(got,page_a,sizeof(got)) == 0, is_b = memcmp(got,page_b,sizeof(got)) == 0;
    printf("CASE redo-apply-retry status=%s page_is_a=%d page_is_b=%d data_writes_first=%u fsyncs_first=%u\n",name_of(retry),is_a,is_b,
        data_writes,first_fsyncs);
    fflush(stdout);
    if (retry == CANDIDATE_WAL_OK) OK(candidate_recovery_close(&r));
    CHECK(status == CANDIDATE_WAL_CORRUPT && !first_is_b && data_writes == 0);
    CHECK(retry == CANDIDATE_WAL_OK && is_a && !is_b);
    scen_free(&b);
}

static void check_answers(candidate_recovery_t *r, const built_t *b, uint64_t floor)
{
    for (unsigned i = 0; i < b->spec.requests; i++) {
        candidate_catalog_query_t q = scen_query(b,i); candidate_recovery_answer_t a;
        OK(candidate_recovery_query(r,&q,&a));
        const req_t *req = &b->spec.req[i];
        if (req->epoch < floor) { CHECK(a.lookup == CANDIDATE_CATALOG_OUTSIDE_RETENTION); continue; }
        CHECK(a.lookup == CANDIDATE_CATALOG_MATCH && a.outcome.transaction_id == b->txn[i].id);
        if (req->state == REQ_COMMIT) CHECK(a.outcome.state == CANDIDATE_OUTCOME_COMMITTED && a.outcome.result_bytes == req->bytes);
        else CHECK(a.outcome.state == CANDIDATE_OUTCOME_ABORTED);
        candidate_page_result_close(&a.body);
    }
}

/* 선택 root는 2/1, 최종 3/2. 앞 32개 nonempty root는 epoch1 항목으로 부적격이고 33번째만 적격이다. */
static void case_carrier(bool good)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,2,40,false,false}};
    char name[64]; name_case(name,sizeof(name),good ? "carrier33" : "carriernone");
    unsigned ballast = good ? 8123 : 8125;
    scen_t s = {.name = name,.requests = 1,.req = req,.root_epoch = 2,.root_floor = 1,.epoch_to = 3,.floor_to = 2,
        .free_epoch_root = true,.expired_roots = 32,.good_extra_root = good,.ballast = ballast};
    built_t b = scen_build(&s); scen_finish(&b);
    uint64_t carrier = b.alloc_count - ballast - 1;
    snapshot_t before = take(b.fx); hook_reset(b.fx);
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(b.fx,8,&r);
    const char *tag = good ? "carrier-33" : "carrier-none"; report(tag,status,&before,b.fx);
    if (!good) { CHECK(status == CANDIDATE_WAL_LIMIT && r == NULL && writes() == 0); scen_free(&b); return; }
    CHECK(status == CANDIDATE_WAL_OK && r != NULL);
    candidate_recovery_publication_t p = candidate_recovery_publication(r); candidate_recovery_extent_t e = candidate_recovery_publication_extent(r);
    check_answers(r,&b,2); OK(candidate_recovery_close(&r));
    printf("CASE carrier-33 extent data_bytes=%" PRIu64 " file=%" PRIu64 " maintenance=%" PRIu64 " allocated=%" PRIu64 " carrier=%" PRIu64 "\n",
        e.data_file_bytes,fx_file_bytes(b.fx->data),e.maintenance_pages,e.allocated_pages,e.carrier_pages);
    CHECK(e.data_file_bytes == fx_file_bytes(b.fx->data) && e.protected_pages == 8192 && e.carrier_pages == 0);
    CHECK(e.allocated_pages == p.reserved_pages + p.reused_reserved_pages && e.allocated_pages == p.allocation_pages);
    capture_t c = capture_file(b.fx,b.anchor.checkpoint_end); uint64_t image = 0;
    for (unsigned i = 0; i < c.count && image == 0; i++)
        if (c.recs[i].r.type == 4) image = c.recs[i].payload[8] | (uint64_t)c.recs[i].payload[9] << 8;
    printf("CASE carrier-33 protected=%" PRIu64 " new_nodes=%u reserved=%" PRIu64 " alloc_pages=%u reserve_root=%" PRIu64 " expected=%" PRIu64 "\n",
        p.protected_pages,p.new_nodes,p.reserved_pages,p.allocation_pages,image,carrier);
    fflush(stdout);
    CHECK(p.protected_pages == 8192 && p.new_nodes == 0 && image == carrier && image != b.root_id);
    candidate_recovery_options_t o = fx_options(b.fx,8); OK(candidate_recovery_open(&o,&r)); check_answers(r,&b,2); OK(candidate_recovery_close(&r));
    snapshot_t settled = take(b.fx); hook_reset(b.fx); CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK && writes() == 0);
    snapshot_t again = take(b.fx); CHECK(same(&settled,&again)); OK(candidate_recovery_close(&r));
    scen_free(&b);
}

/* 142개 할당 + 새 carrier 1 + 새 outcome node 67: 실제 첫 slice 143..202는 allocation node image 2개를 쓴다. */
typedef struct { unsigned count; size_t size[512]; uint16_t type[512]; } sizes_t;
typedef struct { const sizes_t *need, *plan; bool fit; uint64_t start; unsigned pads, groups; uint32_t base; } filler_t;

static bool fits(uint64_t position, const sizes_t *z)
{
    for (unsigned i = 0; i < z->count; i++) {
        uint64_t start; position = fx_place(position,z->size[i],&start);
        if (position > CANDIDATE_WAL_MAX_BYTES) return false;
    }
    return true;
}

/* 최종 checkpoint chunk는 거래 수에 따라 커지므로 실제 예상 거래 수로 type13 크기를 다시 계산한다. */
static void with_transactions(const sizes_t *in, uint32_t transactions, sizes_t *out)
{
    out->count = 0; bool done = false;
    for (unsigned i = 0; i < in->count; i++) {
        if (in->type[i] != 13) { CHECK(out->count < 512); out->size[out->count] = in->size[i]; out->type[out->count++] = in->type[i]; continue; }
        CHECK(!done || in->type[i - 1] == 13);
        if (done) continue;
        done = true;
        for (uint32_t c = 0; c * CANDIDATE_CHECKPOINT_ENTRIES < transactions; c++) {
            uint32_t left = transactions - c * CANDIDATE_CHECKPOINT_ENTRIES, n = left < CANDIDATE_CHECKPOINT_ENTRIES ? left : CANDIDATE_CHECKPOINT_ENTRIES;
            CHECK(out->count < 512); out->size[out->count] = 32 + (size_t)n * 56; out->type[out->count++] = 13;
        }
    }
    CHECK(done);
}

static uint32_t chunk_transactions(const sizes_t *z)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < z->count; i++) if (z->type[i] == 13) { CHECK((z->size[i] - 32) % 56 == 0); n += (uint32_t)((z->size[i] - 32) / 56); }
    return n;
}

static bool fits_with(uint64_t position, const sizes_t *z, uint32_t transactions)
{
    static sizes_t adjusted; with_transactions(z,transactions,&adjusted); return fits(position,&adjusted);
}

static uint64_t after_checkpoint(uint64_t position)
{
    uint64_t start; position = fx_place(position,32,&start); position = fx_place(position,160,&start);
    return fx_place(position,64,&start);
}

static void fill_cap(void *built, void *context)
{
    built_t *b = built; filler_t *f = context; fx_t *fx = b->fx;
    if (f->need == NULL) return;
    fx_txn_t t = {0}; fx_begin(fx,&t,b->spec.requests + 2);
    static fx_image_t images[64]; uint64_t first = b->alloc_count - 63, txn = b->spec.requests + 3;
    uint8_t probe[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    CHECK(pread(fx->data_fd,probe,sizeof(probe),(off_t)((first - 1) * CANDIDATE_PAGE_BYTES)) == (ssize_t)sizeof(probe));
    OK(candidate_page_decode_header(fx_uuid,probe,sizeof(probe),&info)); CHECK(info.type == 4 && probe[70] == 2);
    for (unsigned k = 64; k > 0; k = k == 64 ? 1 : 0) {
        while (true) {
            uint64_t end = fx_group_end(fx,k,0);
            if (!fits_with(after_checkpoint(end) + (k == 64 ? 600000 : 20000),f->need,f->base + 1 + 1022)) break;
            for (unsigned i = 0; i < k; i++) {
                uint64_t id = first + i;
                candidate_catalog_node_t node = {.count = 1};
                node.entries[0] = (candidate_outcome_t){.epoch = 1,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,.admitting_owner_epoch = 7};
                uint64_t unique = ((uint64_t)f->groups << 20) ^ id; memcpy(node.entries[0].nonce,&unique,8);
                candidate_page_location_t l = fx_location(id,1,end); OK(candidate_catalog_encode_node(&l,&node,images[i].page,CANDIDATE_PAGE_BYTES));
                images[i].id = id; images[i].generation = 1; images[i].previous = fx->pages[id].lsn;
            }
            fx_group(fx,&t,images,k,NULL,0,true,true); f->groups++;
        }
    }
    while (true) {
        uint64_t start, next = fx_place(fx_position(fx),16,&start); next = fx_place(next,32,&start);
        bool stop = f->fit ? !fits_with(after_checkpoint(next),f->need,f->base + 2 + f->pads) :
            !fits_with(after_checkpoint(fx_position(fx)),f->need,f->base + 1 + f->pads);
        if (stop) break;
        fx_txn_t pad = {0}; fx_begin(fx,&pad,txn++); fx_abort(fx,&pad); f->pads++;
    }
    fx_abort(fx,&t);
    f->start = after_checkpoint(fx_position(fx)) - 0;
}

static built_t build_cap(const char *tag, filler_t *f)
{
    static req_t req[3072];
    for (unsigned i = 0; i < 3072; i++) {
        req[i] = (req_t){REQ_COMMIT,STORE_NONE,1 + i / 1024,8 + i % 33,false,false};
        if (i % 97 == 5) req[i].state = REQ_ABORT;
    }
    char name[64]; name_case(name,sizeof(name),tag);
    scen_t s = {.name = name,.requests = 3072,.req = req,.root_epoch = 3,.root_floor = 1,.epoch_to = 4,.floor_to = 1,
        .free_epoch_root = true,.txn_highwater = 8192,.pad_alloc_to = 142,.before_checkpoint = fill_cap,.context = f};
    built_t b = scen_build(&s); scen_finish(&b); CHECK(b.alloc_count == 142); return b;
}

static void case_placement(const char *mode)
{
    filler_t none = {0}; built_t small = build_cap("placesmall",&none);
    candidate_recovery_t *r = NULL; hook_reset(small.fx); CHECK(recover(small.fx,8,&r) == CANDIDATE_WAL_OK);
    candidate_recovery_publication_t p = candidate_recovery_publication(r); OK(candidate_recovery_close(&r));
    capture_t c = capture_file(small.fx,small.anchor.checkpoint_end);
    static sizes_t need, plan; unsigned group = 0, images[8] = {0}; bool removed = false;
    for (unsigned i = 0; i < c.count; i++) {
        const candidate_wal_record_t *x = &c.recs[i].r;
        if (x->type == 3) group++;
        if (x->type == 4 && group <= 8) images[group - 1]++;
        need.size[need.count] = x->payload_bytes; need.type[need.count++] = x->type;
        if (x->type == 4 && group == 2 && !removed) { removed = true; continue; }
        plan.size[plan.count] = x->payload_bytes; plan.type[plan.count++] = x->type;
    }
    printf("CASE placement-shape records=%u groups=%u images=%u,%u,%u new_nodes=%u alloc_pages=%u reserved=%" PRIu64 "\n",
        need.count,group,images[0],images[1],images[2],p.new_nodes,p.allocation_pages,p.reserved_pages);
    fflush(stdout);
    uint32_t base = chunk_transactions(&need);
    printf("CASE placement-base transactions=%u\n",base);
    CHECK(group == 3 && images[0] == 3 && images[1] == 62 && images[2] == 11 && p.new_nodes == 67 && removed && base == 3073);
    scen_free(&small);
    bool fit = strcmp(mode,"fit") == 0;
    filler_t f = {.need = &need,.plan = &plan,.fit = fit,.base = base};
    built_t b = build_cap(fit ? "placefit" : "placecap",&f);
    uint32_t total = base + 1 + f.pads;
    bool plan_fits = fits_with(b.anchor.checkpoint_end,&plan,total), need_fits = fits_with(b.anchor.checkpoint_end,&need,total);
    bool next_fits = fits_with(b.anchor.checkpoint_end + 240,&need,total + 1);
    printf("CASE placement-%s start=%" PRIu64 " pads=%u groups=%u transactions=%u plan_fits=%d actual_fits=%d next_pad_fits=%d\n",mode,
        b.anchor.checkpoint_end,f.pads,f.groups,total,plan_fits,need_fits,next_fits);
    fflush(stdout);
    CHECK(f.pads <= 1022 && total <= CANDIDATE_CHECKPOINT_TRANSACTIONS);
    CHECK(fit ? need_fits : (plan_fits && !need_fits));
    snapshot_t before = take(b.fx); hook_reset(b.fx); candidate_wal_status_t status = recover(b.fx,8,&r);
    char tag[64]; CHECK(snprintf(tag,sizeof(tag),"placement-%s",mode) > 0); report(tag,status,&before,b.fx);
    snapshot_t after = take(b.fx);
    if (fit) {
        CHECK(status == CANDIDATE_WAL_OK); p = candidate_recovery_publication(r);
        printf("CASE placement-fit new_nodes=%u alloc_pages=%u groups=%u wal_bytes=%" PRIu64 "\n",p.new_nodes,p.allocation_pages,
            p.system_groups,p.wal_bytes_appended);
        check_answers(r,&b,1); OK(candidate_recovery_close(&r));
        static sizes_t expected; with_transactions(&need,total,&expected);
        capture_t emitted = capture_file(b.fx,b.anchor.checkpoint_end); unsigned mismatches = 0;
        for (unsigned i = 0; i < emitted.count && i < expected.count; i++)
            mismatches += emitted.recs[i].r.type != expected.type[i] || emitted.recs[i].r.payload_bytes != expected.size[i];
        printf("CASE placement-fit emitted=%u expected=%u mismatches=%u\n",emitted.count,expected.count,mismatches); fflush(stdout);
        CHECK(emitted.count == expected.count && mismatches == 0 && p.new_nodes == 67);
    } else CHECK(status == CANDIDATE_WAL_LIMIT && r == NULL && writes() == 0 && same(&before,&after));
    scen_free(&b);
}

/* checkpoint 뒤 마지막 system group이 REQUEST_EPOCH(type15)까지 쓰고 PHYS_END 없이 끝난다. */
static void case_tail_epoch(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"tail15");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    uint64_t begin; fx_place(fx_position(fx),16,&begin);
    static fx_image_t image; encode_ballast(x,fx_group_end(fx,1,1),0x44,image.page);
    image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    fx_event_t epoch = {15,1,2,1,1};
    fx_group(fx,NULL,&image,1,&epoch,1,false,false);
    scen_finish(&b); fx_t kept; fx_copy_set(fx,"before",&kept);
    printf("FIXTURE wal=%s before=%s control=%s data=%s\n",fx->wal,kept.wal,kept.control,kept.data); fflush(stdout);
    snapshot_t before = take(fx); hook_reset(fx); candidate_recovery_t *r = NULL;
    candidate_wal_status_t status = recover(fx,8,&r); report("tail-15",status,&before,fx);
    CHECK(status == CANDIDATE_WAL_OK);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    printf("CASE tail-15 discarded=%" PRIu64 " begin=%" PRIu64 "\n",p.repair.discarded_group_lsn,begin); fflush(stdout);
    CHECK(p.repair.discarded_group_lsn == begin); check_answers(r,&b,1); OK(candidate_recovery_close(&r));
    snapshot_t settled = take(fx); hook_reset(fx); CHECK(recover(fx,8,&r) == CANDIDATE_WAL_OK && writes() == 0);
    snapshot_t again = take(fx); CHECK(same(&settled,&again)); OK(candidate_recovery_close(&r));
    scen_free(&b);
}

static void nonce_range(uint64_t x, uint64_t lsn, uint32_t *offset)
{
    uint8_t a[CANDIDATE_PAGE_BYTES], b[CANDIDATE_PAGE_BYTES]; encode_ballast(x,lsn,0x11,a); encode_ballast(x,lsn,0x22,b);
    uint32_t lo = 0, hi = 0;
    for (uint32_t i = 64; i < CANDIDATE_PAGE_BYTES; i++) if (a[i] != b[i]) { if (lo == 0) lo = i; hi = i; }
    printf("CASE nonce-range lo=%u hi=%u\n",lo,hi); CHECK(lo != 0 && hi - lo + 1 == 16); *offset = lo;
}

/* G1 image, G2/G3는 서로 다른 nonce 절반만 고치는 delta다. B는 read-at으로만 보이는 G2 patch이며 G3가 가리지 않는다. */
static void case_delta_chain(void)
{
    static req_t req[1] = {{REQ_COMMIT,STORE_ADMITTED,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"chain");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0); uint32_t at;
    encode_ballast(x,end,0x11,image.page); image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    nonce_range(x,end,&at);
    fx_group(fx,&t,&image,1,NULL,0,true,false);
    uint8_t half2[8], half3[8], spoofed[8]; memset(half2,0x22,8); memset(half3,0x33,8); memset(spoofed,0x44,8);
    uint64_t begin, delta2, next = fx_place(fx_position(fx),16,&begin); fx_place(next,48 + 8,&delta2);
    fx_delta_group(fx,&t,x,at,half2,8);
    fx_delta_group(fx,&t,x,at + 8,half3,8); fx_commit(fx,&t);
    capture_t c = capture_open(fx->w,delta2,delta2 + 1); CHECK(c.count == 1 && c.recs[0].r.type == 5 && c.recs[0].r.payload_bytes == 56);
    memcpy(c.recs[0].payload + 48,spoofed,8); region_t g = frame_region(&c.recs[0].r,V_RANDOM_BOTH);
    scen_finish(&b); dump_view(fx,&g,1);
    snapshot_t before = take(fx); hook_reset(fx); hook_regions(&g,1); h.always = true;
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(fx,8,&r);
    report("delta-chain",status,&before,fx); unsigned hits = h.hits, random = h.random_hits, effects = writes() + h.fsyncs;
    snapshot_t after = take(fx);
    CHECK(status == CANDIDATE_WAL_CORRUPT && r == NULL && effects == 0 && same(&before,&after) && hits > 0 && random > 0);
    hook_reset(fx); status = recover(fx,8,&r);
    printf("CASE delta-chain-clean status=%s\n",name_of(status)); fflush(stdout);
    CHECK(status == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    uint8_t page[CANDIDATE_PAGE_BYTES]; read_page(fx,x,page);
    CHECK(memcmp(page + at,half2,8) == 0 && memcmp(page + at + 8,half3,8) == 0);
    scen_free(&b);
}

/* A: DELTA group 1개. B: BEGIN records2, 같은 DELTA, 136B type16 event, END를 136 뒤로 옮겨 count/CRC/previous를 다시 쓴다. */
static void case_moved_end(void)
{
    static req_t req[1] = {{REQ_COMMIT,STORE_ADMITTED,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"movedend");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    static fx_image_t image; uint64_t base_end = fx_group_end(fx,1,0); uint32_t at;
    encode_ballast(x,base_end,0x11,image.page); image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    nonce_range(x,base_end,&at);
    CHECK(fx_group(fx,&t,&image,1,NULL,0,true,true) == base_end);
    uint8_t half[8]; memset(half,0x22,8);
    uint64_t begin_lsn, delta_lsn, end_lsn, next = fx_place(fx_position(fx),16,&begin_lsn);
    next = fx_place(next,48 + 8,&delta_lsn); fx_place(next,24,&end_lsn);
    uint64_t group = fx->group_id + 1, a_end = fx_delta_group(fx,&t,x,at,half,8); fx_commit(fx,&t);
    CHECK(a_end == end_lsn + 120);
    capture_t c = capture_open(fx->w,begin_lsn,delta_lsn + 1); CHECK(c.count == 2 && c.recs[0].r.type == 3 && c.recs[1].r.type == 5);
    candidate_wal_record_t begin_b = c.recs[0].r; uint8_t begin_body[16]; memcpy(begin_body,c.recs[0].payload,16);
    begin_body[12] = 2; begin_b.payload = begin_body;
    candidate_wal_record_t delta = c.recs[1].r;
    uint8_t event_body[40] = {0}; for (unsigned k = 0; k < 8; k++) event_body[k] = (uint8_t)(group >> (8 * k));
    event_body[8] = 9; event_body[16] = 10; event_body[24] = 11; event_body[32] = 12;
    candidate_wal_record_t event = {.type = 16,.lsn = end_lsn,.transaction_id = t.id,.previous_lsn = delta_lsn,.owner_epoch = delta.owner_epoch,
        .payload = event_body,.payload_bytes = sizeof(event_body)};
    uint32_t crc = candidate_wal_witness_crc(0,&begin_b); crc = candidate_wal_witness_crc(crc,&delta); crc = candidate_wal_witness_crc(crc,&event);
    uint8_t end_body[24] = {0}; for (unsigned k = 0; k < 8; k++) end_body[k] = (uint8_t)(group >> (8 * k));
    end_body[8] = 1; end_body[12] = 2; for (unsigned k = 0; k < 4; k++) end_body[16 + k] = (uint8_t)(crc >> (8 * k));
    candidate_wal_record_t end_b = {.type = 8,.lsn = end_lsn + 136,.transaction_id = t.id,.previous_lsn = end_lsn,.owner_epoch = delta.owner_epoch,
        .payload = end_body,.payload_bytes = sizeof(end_body)};
    region_t g[3] = {frame_region(&begin_b,V_ALL),frame_region(&event,V_ALL),frame_region(&end_b,V_ALL)};
    CHECK(g[1].size == 136 && g[2].size == 120);
    scen_finish(&b); dump_view(fx,g,3);
    printf("FIXTURE moved-end begin=%" PRIu64 " delta=%" PRIu64 " a_end=%" PRIu64 " b_end=%" PRIu64 "\n",begin_lsn,delta_lsn,a_end,a_end + 136);
    fx_t cal, spoof; fx_copy_set(fx,"cal",&cal); fx_copy_set(fx,"spoof",&spoof);
    candidate_recovery_t *r = NULL; hook_reset(&cal); candidate_wal_status_t status = recover(&cal,8,&r);
    unsigned apply_scan = h.data_write_scan;
    printf("CASE moved-end-calibrate status=%s apply_scan=%u\n",name_of(status),apply_scan); fflush(stdout);
    CHECK(status == CANDIDATE_WAL_OK && apply_scan != 0); OK(candidate_recovery_close(&r));
    uint8_t got[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info; read_page(&cal,x,got);
    OK(candidate_page_decode_header(fx_uuid,got,sizeof(got),&info)); CHECK(info.location.lsn == a_end);
    snapshot_t before = take(&spoof); hook_reset(&spoof); hook_regions(g,3); h.active_scan = apply_scan;
    status = recover(&spoof,8,&r); report("moved-end",status,&before,&spoof);
    unsigned hits = h.hits, first_data = h.data_writes; if (r != NULL) OK(candidate_recovery_close(&r));
    char kept[320]; CHECK(snprintf(kept,sizeof(kept),"%s-after-spoof",spoof.data) > 0); fx_copy_file(spoof.data,kept);
    read_page(&spoof,x,got); OK(candidate_page_decode_header(fx_uuid,got,sizeof(got),&info)); uint64_t first_lsn = info.location.lsn;
    bool base_kept = memcmp(got,image.page,sizeof(got)) == 0;
    printf("CASE moved-end-target page_lsn=%" PRIu64 " is_b_end=%d base_kept=%d data_writes=%u kept=%s\n",first_lsn,first_lsn == a_end + 136,
        base_kept,first_data,kept);
    hook_reset(&spoof); candidate_wal_status_t retry = recover(&spoof,8,&r);
    read_page(&spoof,x,got); OK(candidate_page_decode_header(fx_uuid,got,sizeof(got),&info));
    printf("CASE moved-end-retry status=%s page_lsn=%" PRIu64 "\n",name_of(retry),info.location.lsn); fflush(stdout);
    if (retry == CANDIDATE_WAL_OK) OK(candidate_recovery_close(&r));
    CHECK(hits > 0 && status == CANDIDATE_WAL_CORRUPT && first_lsn != a_end + 136 && first_data == 0 && base_kept);
    CHECK(retry == CANDIDATE_WAL_OK && info.location.lsn == a_end && memcmp(got + at,half,8) == 0);
    scen_free(&b);
}

static void case_append_mutate(bool alternate)
{
    char name[64], wal[300], path[340], segment[300]; name_case(name,sizeof(name),alternate ? "appendb" : "appenda");
    CHECK(snprintf(wal,sizeof(wal),"build/%s",name) > 0);
    candidate_wal_options_t o = {.owner_epoch = 7,.io = &hooks}; memcpy(o.uuid,fx_uuid,16);
    candidate_wal_t *w = NULL; OK(candidate_wal_create(wal,&o,&w));
    memset(&h,0,sizeof(h)); segment_path(wal,0,segment); struct stat st; CHECK(stat(segment,&st) == 0); h.wal0 = st.st_ino;
    uint8_t first[16] = {1}; uint64_t end;
    candidate_wal_record_t r = {.type = 2,.transaction_id = 1,.payload = first,.payload_bytes = sizeof(first)};
    OK(candidate_wal_append(w,&r,&end)); OK(candidate_wal_force(w,end)); OK(candidate_wal_pin(w));
    static uint8_t payload[64], copy_a[64], frame_a[256], frame_b[256], physical[256];
    memset(payload,0xa1,sizeof(payload)); memcpy(copy_a,payload,sizeof(copy_a));
    uint64_t lsn = end; candidate_wal_record_t a = {.type = 2,.transaction_id = 2,.payload = payload,.payload_bytes = sizeof(payload)};
    h.mutate = payload; h.mutate_bytes = sizeof(payload);
    candidate_wal_status_t appended = candidate_wal_append(w,&a,&end);
    candidate_wal_record_t encoded = a; encoded.lsn = lsn; encoded.owner_epoch = 7; encoded.payload = copy_a; size_t size_a, size_b;
    OK(candidate_wal_encode(fx_uuid,&encoded,frame_a,sizeof(frame_a),&size_a));
    encoded.payload = payload; OK(candidate_wal_encode(fx_uuid,&encoded,frame_b,sizeof(frame_b),&size_b));
    int fd = open(segment,O_RDONLY); CHECK(fd >= 0);
    CHECK(pread(fd,physical,size_a,(off_t)(lsn % CANDIDATE_WAL_SEGMENT_BYTES)) == (ssize_t)size_a); CHECK(close(fd) == 0);
    CHECK(snprintf(path,sizeof(path),"%s-frame-a",wal) > 0); keep_bytes(path,frame_a,size_a);
    CHECK(snprintf(path,sizeof(path),"%s-frame-b",wal) > 0); keep_bytes(path,frame_b,size_b);
    printf("FIXTURE wal=%s lsn=%" PRIu64 " end=%" PRIu64 " frame_bytes=%zu",wal,lsn,end,size_a);
    print_digest("frame_a",frame_a,size_a); print_digest("frame_b",frame_b,size_b); print_digest("physical",physical,size_a);
    printf("\n"); fflush(stdout);
    CHECK(appended == CANDIDATE_WAL_OK && h.mutated == 1 && payload[0] == 0xb2 && size_a == size_b);
    CHECK(memcmp(physical,frame_a,size_a) == 0 && memcmp(frame_a,frame_b,size_a) != 0);
    region_t g = {.offset = lsn % CANDIDATE_WAL_SEGMENT_BYTES,.bytes = frame_b,.size = size_b,.mode = V_ALL};
    if (alternate) { hook_regions(&g,1); h.always = true; }
    candidate_wal_info_t info; candidate_wal_status_t scanned = candidate_wal_scan(w,NULL,NULL,&info);
    candidate_wal_status_t again = candidate_wal_scan(w,NULL,NULL,&info);
    const char *tag = alternate ? "append-mutate-b" : "append-mutate-a";
    printf("CASE %s scan=%s again=%s hits=%u\n",tag,name_of(scanned),name_of(again),h.hits); fflush(stdout);
    candidate_wal_close(&w);
    if (alternate) CHECK(scanned == CANDIDATE_WAL_CORRUPT && again == CANDIDATE_WAL_CORRUPT && h.hits > 0);
    else CHECK(scanned == CANDIDATE_WAL_OK && again == CANDIDATE_WAL_OK);
}

/* B는 같은 id/generation/endLSN에서 참조 없는 version payload, PHYS_END group CRC와 COMMIT chain CRC를 바꾼다. */
static unsigned spoof_txn(candidate_wal_t *w, const fx_txn_t *t, uint64_t target, const uint8_t *page, region_t out[3])
{
    capture_t c = capture_open(w,t->first,t->terminal_end);
    uint32_t group = 0, chain = 0; unsigned n = 0;
    for (unsigned i = 0; i < c.count; i++) {
        candidate_wal_record_t *r = &c.recs[i].r; uint8_t *payload = c.recs[i].payload; CHECK(r->transaction_id == t->id);
        if (r->lsn == target) {
            CHECK(r->type == 4 && r->payload_bytes == 32 + CANDIDATE_PAGE_BYTES);
            memcpy(payload + 32,page,CANDIDATE_PAGE_BYTES); out[n++] = frame_region(r,V_ALL);
        }
        if (r->type == 3) group = 0;
        if (r->type == 8) {
            for (unsigned k = 0; k < 4; k++) payload[16 + k] = (uint8_t)(group >> (8 * k));
            out[n++] = frame_region(r,V_ALL);
        } else if (r->type != 10) group = candidate_wal_witness_crc(group,r);
        if (r->type == 10) {
            for (unsigned k = 0; k < 4; k++) payload[24 + k] = (uint8_t)(chain >> (8 * k));
            out[n++] = frame_region(r,V_ALL);
        } else chain = candidate_wal_witness_crc(chain,r);
    }
    CHECK(n == 3 && c.recs[c.count - 1].r.type == 10);
    return n;
}

static candidate_wal_status_t strict_open(const fx_t *fx, uint64_t owner, candidate_recovery_t **out)
{
    candidate_recovery_options_t o = fx_options(fx,owner); o.io = &hooks; o.memory = &memory;
    return candidate_recovery_open(&o,out);
}

static void case_strict_open(void)
{
    req_t req[1] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false}};
    char name[64]; name_case(name,sizeof(name),"strictopen");
    scen_t s = {.name = name,.requests = 1,.req = req,.ballast = 1};
    built_t b = scen_build(&s); fx_t *fx = b.fx; uint64_t x = b.alloc_count;
    fx_txn_t t = {0}; fx_begin(fx,&t,b.spec.requests + 2);
    uint64_t begin, target, at = fx_place(fx_position(fx),16,&begin); fx_place(at,32 + CANDIDATE_PAGE_BYTES,&target);
    static fx_image_t image; uint64_t end = fx_group_end(fx,1,0);
    encode_version(x,end,t.id,0x5a,image.page); image.id = x; image.generation = 1; image.previous = fx->pages[x].lsn;
    CHECK(fx_group(fx,&t,&image,1,NULL,0,true,true) == end); fx_commit(fx,&t);
    uint8_t page_a[CANDIDATE_PAGE_BYTES], page_b[CANDIDATE_PAGE_BYTES], got[CANDIDATE_PAGE_BYTES];
    memcpy(page_a,image.page,sizeof(page_a)); encode_version(x,end,t.id,0xa5,page_b);
    region_t g[3]; spoof_txn(fx->w,&t,target,page_b,g);
    scen_finish(&b); dump_view(fx,g,3);
    candidate_recovery_t *r = NULL; hook_reset(fx); candidate_wal_status_t status = recover(fx,8,&r);
    printf("CASE strict-open-publish status=%s writes=%u\n",name_of(status),writes()); fflush(stdout);
    CHECK(status == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    fx_t cal, spoof; fx_copy_set(fx,"cal",&cal); fx_copy_set(fx,"spoof",&spoof);
    hook_reset(&cal); status = strict_open(&cal,8,&r);
    printf("CASE strict-open-calibrate status=%s writes=%u\n",name_of(status),writes()); size_t marker = calibrate_marker();
    CHECK(status == CANDIDATE_WAL_OK && writes() == 0); OK(candidate_recovery_close(&r));
    snapshot_t before = take(&spoof); hook_reset(&spoof); hook_regions(g,3);
    h.marker = marker; h.from_release = 1; h.to_release = UINT_MAX;
    status = strict_open(&spoof,8,&r); report("strict-open",status,&before,&spoof);
    bool ready = r != NULL; unsigned hits = h.hits, changed = writes();
    printf("CASE strict-open-phase release_scan=%u first_hit_scan=%u releases=%u ready=%d\n",h.release_scan,h.first_hit_scan,
        h.releases,ready);
    fflush(stdout);
    if (r != NULL) OK(candidate_recovery_close(&r));
    snapshot_t after = take(&spoof); bool unchanged = same(&before,&after);
    hook_reset(&spoof); candidate_wal_status_t retry = strict_open(&spoof,8,&r);
    printf("CASE strict-open-retry status=%s\n",name_of(retry)); fflush(stdout);
    if (retry == CANDIDATE_WAL_OK) { check_answers(r,&b,1); OK(candidate_recovery_close(&r)); }
    read_page(&spoof,x,got);
    CHECK(hits > 0 && changed == 0 && unchanged && memcmp(got,page_a,sizeof(got)) == 0);
    CHECK(status == CANDIDATE_WAL_CORRUPT && !ready && retry == CANDIDATE_WAL_OK);
    scen_free(&b);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *c = argv[1];
    if (strcmp(c,"scan-lsn") == 0) case_scan_lsn();
    else if (strcmp(c,"read-lsn") == 0) case_delta(false);
    else if (strcmp(c,"pred-spoof") == 0) case_delta(true);
    else if (strcmp(c,"selection-repair") == 0) case_selection_repair();
    else if (strcmp(c,"cross-fallback") == 0) case_cross_fallback();
    else if (strcmp(c,"redo-apply") == 0) case_redo_apply();
    else if (strcmp(c,"carrier-33") == 0) case_carrier(true);
    else if (strcmp(c,"carrier-none") == 0) case_carrier(false);
    else if (strcmp(c,"placement-cap") == 0) case_placement("cap");
    else if (strcmp(c,"placement-fit") == 0) case_placement("fit");
    else if (strcmp(c,"tail-15") == 0) case_tail_epoch();
    else if (strcmp(c,"append-mutate-a") == 0) case_append_mutate(false);
    else if (strcmp(c,"append-mutate-b") == 0) case_append_mutate(true);
    else if (strcmp(c,"strict-open") == 0) case_strict_open();
    else if (strcmp(c,"delta-chain") == 0) case_delta_chain();
    else if (strcmp(c,"moved-end") == 0) case_moved_end();
    else CHECK(false);
    printf("PASS %s checks=%u\n",c,checks);
    return 0;
}
