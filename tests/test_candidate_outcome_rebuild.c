#define CANDIDATE_ABORTED_RESULT_TEST_MAIN aborted_result_regression_main
#include "test_candidate_aborted_result.c"
#include "storage/candidate_outcome_rebuild.h"
#include "../src/storage/candidate_wal_witness_internal.h"

typedef struct { aborted_t *source; int fd; uint64_t root; unsigned nodes; candidate_page_ref_t refs[90]; } projection_t;
static candidate_outcome_rebuild_options_t *mutate_rebuild;

static candidate_wal_status_t fail_result(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    return ref.id == 1 ? CANDIDATE_WAL_IO : read_aborted(context,ref,bytes);
}

static void *allocate_rebuild(void *context, size_t size)
{
    if (mutate_rebuild != NULL) { memset(mutate_rebuild,0,sizeof(*mutate_rebuild)); mutate_rebuild = NULL; }
    return allocate(context,size);
}

static candidate_outcome_rebuild_options_t rebuild_options(aborted_t *a)
{
    return (candidate_outcome_rebuild_options_t){.source = aborted_options(a),.owner_epoch = 8};
}

static void save_bytes(int fd, const char *path)
{
    int out = open(path,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(out >= 0); uint8_t bytes[8192]; off_t at = 0; ssize_t n;
    while ((n = pread(fd,bytes,sizeof(bytes),at)) > 0) { CHECK(write(out,bytes,(size_t)n) == n); at += n; }
    CHECK(n == 0); CHECK(fsync(out) == 0); CHECK(close(out) == 0);
}

static void save_source(aborted_t *a, const char *tag)
{
    char path[320]; CHECK(snprintf(path,sizeof(path),"%s.%s",a->files.wal,tag) > 0); save_bytes(a->fd,path);
}

static candidate_wal_status_t read_projection(void *context, candidate_page_ref_t ref, uint8_t bytes[8192])
{
    projection_t *p = context;
    if (ref.id == p->root) return pread(p->fd,bytes,8192,0) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
    for (unsigned i = 0; i < p->nodes; i++)
        if (ref.id == p->refs[i].id) return pread(p->fd,bytes,8192,(off_t)(i + 1) * 8192) == 8192 ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
    return read_aborted(p->source,ref,bytes);
}

static void render_plan(aborted_t *a, candidate_outcome_rebuild_t *p, const char *tag, uint64_t first_node)
{
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(p);
    char path[320]; CHECK(snprintf(path,sizeof(path),"%s.%s",a->files.wal,tag) > 0);
    projection_t target = {.source = a,.fd = open(path,O_CREAT | O_EXCL | O_RDWR,0600),.root = a->files.anchor.outcome_root.id,.nodes = info.nodes};
    CHECK(target.fd >= 0); candidate_page_location_t l = {.ref = {target.root,1},.lsn = info.wal_end_lsn}; memcpy(l.uuid,uuid,16);
    for (unsigned i = 0; i < info.nodes; i++) target.refs[i] = (candidate_page_ref_t){first_node + i,1};
    uint8_t bytes[8192]; OK(candidate_outcome_rebuild_encode_root(p,&l,target.refs,info.nodes,info.minimum_retained_bytes,bytes,8192));
    CHECK(write(target.fd,bytes,8192) == 8192);
    for (unsigned i = 0; i < info.nodes; i++) {
        l.ref = target.refs[i]; OK(candidate_outcome_rebuild_encode_node(p,i,&l,bytes,8192)); CHECK(write(target.fd,bytes,8192) == 8192);
    }
    CHECK(fsync(target.fd) == 0);
    candidate_catalog_load_t load = {.root = {target.root,1},.load = read_projection,.context = &target}; memcpy(load.uuid,uuid,16);
    candidate_catalog_t *catalog = NULL; OK(candidate_catalog_load(&load,&catalog)); CHECK(candidate_catalog_root(catalog)->count == info.count);
    for (unsigned i = 0; i < info.count; i++) {
        const candidate_outcome_t *e = candidate_outcome_rebuild_at(p,i), *copy = candidate_catalog_at(catalog,i);
        CHECK(e != NULL && copy != NULL && e->epoch == copy->epoch && memcmp(e->nonce,copy->nonce,16) == 0);
        CHECK(e->transaction_id == copy->transaction_id && e->state == copy->state && e->csn == copy->csn);
        CHECK(e->terminal_end_lsn == copy->terminal_end_lsn && e->result_bytes == copy->result_bytes && e->result_chunks == copy->result_chunks);
        CHECK(memcmp(e->result_digest,copy->result_digest,32) == 0 && e->result.page.id == copy->result.page.id);
    }
    candidate_catalog_close(&catalog);
    memset(bytes,0x7b,sizeof(bytes)); l.ref = (candidate_page_ref_t){target.root,1};
    CHECK(candidate_outcome_rebuild_encode_root(p,&l,target.refs,info.nodes,info.minimum_retained_bytes - 1,bytes,8192) == CANDIDATE_WAL_LIMIT);
    for (unsigned i = 0; i < sizeof(bytes); i++) CHECK(bytes[i] == 0x7b);
    CHECK(candidate_outcome_rebuild_encode_node(p,info.nodes,&l,bytes,8192) == CANDIDATE_WAL_INVALID);
    CHECK(candidate_outcome_rebuild_at(p,info.count) == NULL); CHECK(close(target.fd) == 0);
    printf("REBUILD %s tag=%s count=%u nodes=%u bytes=%llu epoch=%llu floor=%llu hash=",a->files.wal,tag,info.count,info.nodes,
        (unsigned long long)info.result_bytes,(unsigned long long)info.current_epoch,(unsigned long long)info.retention_floor);
    for (unsigned i = 0; i < 32; i++) printf("%02x",info.wal_digest[i]);
    printf("\n");
}

static aborted_t committed_fixture(size_t bytes)
{
    aborted_t a = {.files = fixture(0,bytes),.bytes = bytes}; candidate_recovery_options_t o = options(&a.files); candidate_recovery_t *r = NULL;
    OK(candidate_recovery_open(&o,&r)); OK(candidate_recovery_close(&r)); candidate_wal_options_t wo = {.owner_epoch = 8}; memcpy(wo.uuid,uuid,16);
    OK(candidate_wal_open(a.files.wal,&wo,&a.wal)); OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn));
    a.fd = open(a.files.data,O_RDWR); CHECK(a.fd >= 0); return a;
}

static void change_source(aborted_t *a, unsigned mode)
{
    if (mode == 12) {
        uint8_t page[8192], value = 'Q'; candidate_page_version_t v; candidate_page_info_t info;
        CHECK(pread(a->fd,page,8192,0) == 8192); OK(candidate_page_get_version(uuid,page,8192,(candidate_version_ref_t){{1,1},0,7},&v));
        OK(candidate_page_decode_header(uuid,page,8192,&info)); v.inline_bytes = &value;
        OK(candidate_page_encode_versions(&info.location,&v,1,page,8192)); CHECK(pwrite(a->fd,page,8192,0) == 8192); CHECK(fsync(a->fd) == 0); return;
    }
    uint64_t id = a->files.anchor.outcome_root.id + (mode == 2 || mode == 11 ? 0 : 1);
    uint8_t bytes[8192]; CHECK(pread(a->fd,bytes,8192,(off_t)(id - 1) * 8192) == 8192); candidate_page_info_t info;
    OK(candidate_page_decode_header(uuid,bytes,8192,&info));
    if (mode == 2 || mode == 11) {
        candidate_catalog_root_t root; OK(candidate_catalog_decode_root(uuid,bytes,8192,&root));
        if (mode == 2) root.count = 0; else root.visible_csn++;
        OK(candidate_catalog_encode_root(&info.location,&root,bytes,8192));
    } else {
        candidate_catalog_node_t node; OK(candidate_catalog_decode_node(uuid,bytes,8192,&node)); candidate_outcome_t *e = &node.entries[0];
        switch (mode) {
        case 1: e->state = CANDIDATE_OUTCOME_ADMITTED; e->csn = e->terminal_end_lsn = e->result_bytes = e->result_chunks = 0;
            e->result = (candidate_version_ref_t){0}; memset(e->result_digest,0,32); break;
        case 3: e->terminal_end_lsn += 8; break;
        case 4: e->admitting_owner_epoch++; break;
        case 5: e->operation_digest[0] ^= 1; break;
        case 6: e->result.slot_generation++; break;
        case 7: e->result_digest[0] ^= 1; break;
        case 8: e->declared_result_limit++; break;
        case 9: e->nonce[0] ^= 1; break;
        case 10: e->state = CANDIDATE_OUTCOME_ABORTED; e->csn = 0; break;
        default: CHECK(false);
        }
        OK(candidate_catalog_encode_node(&info.location,&node,bytes,8192));
    }
    CHECK(pwrite(a->fd,bytes,8192,(off_t)(id - 1) * 8192) == 8192); CHECK(fsync(a->fd) == 0);
}

static void normal_rebuilds(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        aborted_t a = mode < 3 ? committed_fixture(mode == 0 ? 0 : mode == 1 ? 1 : 1048576) : aborted_fixture(mode == 5 ? 0 : 65537,mode == 5 ? 0 : 2,true);
        candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
        if (mode == 4) change_source(&a,1);
        save_source(&a,"source"); OK(candidate_outcome_rebuild_load(a.wal,&o,&p));
        CHECK(candidate_outcome_rebuild_info(p).count == 1);
        if (mode == 4) CHECK(candidate_outcome_rebuild_at(p,0)->result.page.id == 0);
        render_plan(&a,p,"projection",a.files.anchor.outcome_root.id + 1);
        candidate_outcome_t copy = *candidate_outcome_rebuild_at(p,0); close_aborted(&a);
        CHECK(candidate_outcome_rebuild_at(p,0)->transaction_id == copy.transaction_id);
        candidate_outcome_rebuild_close(&p); CHECK(p == NULL);
    }
    for (unsigned chunks = 0; chunks <= 2; chunks++) {
        aborted_t a = aborted_fixture(65537,chunks,false); change_source(&a,chunks == 2 ? 2 : 1); save_source(&a,"source");
        candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
        OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_at(p,0)->state == CANDIDATE_OUTCOME_ABORTED);
        render_plan(&a,p,"projection",a.files.anchor.outcome_root.id + 1); candidate_outcome_rebuild_close(&p); close_aborted(&a);
    }
    for (unsigned missing = 1; missing <= 2; missing++) {
        aborted_t a = committed_fixture(1); change_source(&a,missing); save_source(&a,"source");
        candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
        OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); render_plan(&a,p,"projection",a.files.anchor.outcome_root.id + 1);
        candidate_outcome_rebuild_close(&p); close_aborted(&a);
    }
}

static void rejected_rebuilds(void)
{
    for (unsigned mode = 1; mode <= 12; mode++) {
        aborted_t a = committed_fixture(mode <= 2 ? 0 : 1); change_source(&a,mode); save_source(&a,"source");
        candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = (void *)1;
        candidate_wal_status_t status = candidate_outcome_rebuild_load(a.wal,&o,&p);
        CHECK(status == (mode <= 2 ? CANDIDATE_WAL_STALE : CANDIDATE_WAL_CORRUPT) && p == NULL);
        printf("REBUILD_REJECT %s mode=%u status=%u\n",a.files.wal,mode,status); close_aborted(&a);
    }
    aborted_t a = committed_fixture(1); candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = (void *)1;
    OK(candidate_wal_close(&a.wal)); candidate_wal_options_t wo = {.owner_epoch = 8}; memcpy(wo.uuid,uuid,16); OK(candidate_wal_open(a.files.wal,&wo,&a.wal));
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_STALE && p == NULL);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); candidate_outcome_rebuild_close(&p);
    o.owner_epoch = 6; CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_STALE && p == NULL);
    o.owner_epoch = 8; o.source.catalog.load = fail_result;
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_IO && p == NULL);
    printf("REBUILD_FORCE %s\n",a.files.wal); close_aborted(&a);
}

static void memory_rebuilds(void)
{
    aborted_t a = committed_fixture(1048576); candidate_outcome_rebuild_options_t o = rebuild_options(&a); memory_t count = {0};
    candidate_wal_memory_t memory = {&count,allocate_rebuild,release}; o.source.catalog.memory = &memory;
    candidate_outcome_rebuild_t *p = NULL; OK(candidate_outcome_rebuild_load(a.wal,&o,&p));
    unsigned calls = count.calls; size_t peak = count.peak; CHECK(count.live == candidate_outcome_rebuild_info(p).resident_bytes);
    candidate_outcome_rebuild_close(&p); CHECK(count.live == 0);
    for (unsigned at = 1; at <= calls; at++) {
        count = (memory_t){.fail_at = at}; p = (void *)1;
        CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_NOMEM && p == NULL && count.live == 0);
    }
    count = (memory_t){.mutate_memory = &memory}; mutate_rebuild = &o;
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); candidate_outcome_rebuild_close(&p); CHECK(count.live == 0 && o.owner_epoch == 0);
    printf("REBUILD_MEMORY %s calls=%u peak=%zu; owned requests exclude stack/allocator/RSS\n",a.files.wal,calls,peak); close_aborted(&a);
}

#ifndef CANDIDATE_REBUILD_TEST_MAIN
#define CANDIDATE_REBUILD_TEST_MAIN main
#endif
int CANDIDATE_REBUILD_TEST_MAIN(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90); normal_rebuilds(); rejected_rebuilds(); memory_rebuilds();
    printf("PASS outcome reconstruction checks=%u; owned unpublished metadata, no DB activation\n",checks); return 0;
}
