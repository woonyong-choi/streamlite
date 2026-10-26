#include "test_candidate_recovery_view_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "../src/storage/candidate_outcome_internal.h"

vf_io_t vf_io;
vf_t *vf_env;
const req_t vf_clean[3] = {{REQ_COMMIT,STORE_FINAL,1,40,false,false},{REQ_COMMIT,STORE_FINAL,1,9000,false,false},
    {REQ_ABORT,STORE_FINAL,1,0,false,false}};
const req_t vf_expired[4] = {{REQ_COMMIT,STORE_NONE,1,64,false,false},{REQ_COMMIT,STORE_FINAL,2,40,false,false},
    {REQ_COMMIT,STORE_FINAL,2,9000,false,false},{REQ_ABORT,STORE_FINAL,2,0,false,false}};

static void *vf_allocate(void *context, size_t size)
{
    vf_memory_t *m = context;
    if (++m->calls == m->fail_at) return NULL;
    if (m->mutate_request != NULL) { memset(m->mutate_request,0,sizeof(*m->mutate_request)); m->mutate_request = NULL; }
    if (m->mutate_memory != NULL) { memset(m->mutate_memory,0,sizeof(*m->mutate_memory)); m->mutate_memory = NULL; }
    void *p = malloc(size);
    if (p != NULL) { m->live += size; if (m->live > m->peak) m->peak = m->live; }
    return p;
}

static void vf_release(void *context, void *p, size_t size)
{
    vf_memory_t *m = context;
    CHECK(m->live >= size); m->live -= size; free(p);
}

static ssize_t vf_pread(int fd, void *p, size_t n, off_t offset)
{
    ssize_t r = pread(fd,p,n,offset);
    int saved = errno;
    vf_io.reads++;
    if (r > 0) vf_io.read_bytes += (uint64_t)r;
    vf_hook_t hook = vf_io.on_read;
    if (hook != NULL) { vf_io.on_read = NULL; hook(vf_env); }
    errno = saved;
    return r;
}

static int vf_fsync(int fd)
{
    vf_io.fsyncs++;
    if (!vf_io.should_fail_fsync) return fsync(fd);
    vf_io.fsync_failures++; errno = EIO;
    return -1;
}

static const candidate_wal_io_t vf_hooks = {vf_pread,pwrite,vf_fsync,ftruncate};

static candidate_wal_status_t accept(void *context, const candidate_control_state_t *anchor)
{
    (void)context; (void)anchor; return CANDIDATE_WAL_OK;
}

void vf_reset_memory(vf_t *f)
{
    f->allocator = (candidate_wal_memory_t){&f->memory,vf_allocate,vf_release};
    f->memory.calls = 0; f->memory.fail_at = 0; f->memory.peak = f->memory.live;
    f->memory.mutate_request = NULL; f->memory.mutate_memory = NULL;
}

void vf_boot(vf_t *f, const char *tag, const scen_t *spec, bool is_recovered)
{
    static unsigned serial;
    memset(f,0,sizeof(*f)); f->data_fd = -1; f->owner = 7;
    CHECK(snprintf(f->name,sizeof(f->name),"task24a2a-%s-%ld-%u",tag,(long)getpid(),++serial) > 0);
    scen_t s = *spec; s.name = f->name;
    f->built = scen_build(&s); scen_finish(&f->built);
    vf_reset_memory(f); vf_env = f;
    if (is_recovered) {
        candidate_recovery_options_t o = fx_options(f->built.fx,8); candidate_recovery_t *r = NULL;
        OK(candidate_recovery_recover(&o,&r)); OK(candidate_recovery_close(&r)); f->owner = 8;
    }
    vf_open(f);
    printf("FIXTURE %s prefix=%s recovered=%d outcome_root=%llu checkpoint=%llu owner=%llu wal_end=%llu\n",tag,f->built.fx->wal,
        is_recovered,(unsigned long long)f->anchor.outcome_root.id,(unsigned long long)f->anchor.checkpoint_id,
        (unsigned long long)f->anchor.owner_epoch,(unsigned long long)candidate_wal_info(f->wal).valid_end_lsn);
}

void vf_open(vf_t *f)
{
    candidate_control_options_t co = {.backend = CANDIDATE_CONTROL_WAL,.verify = accept}; memcpy(co.uuid,fx_uuid,16);
    candidate_control_t *control = NULL; OK(candidate_control_open(f->built.fx->control,&co,&control));
    candidate_control_info_t info = candidate_control_info(control); CHECK(info.selected >= 0);
    f->anchor = info.slots[info.selected]; OK(candidate_control_close(&control));
    candidate_wal_options_t wo = {.owner_epoch = f->owner,.io = &vf_hooks}; memcpy(wo.uuid,fx_uuid,16);
    OK(candidate_wal_open(f->built.fx->wal,&wo,&f->wal));
    OK(candidate_wal_force(f->wal,candidate_wal_info(f->wal).valid_end_lsn));
    f->data_fd = open(f->built.fx->data,O_RDWR); CHECK(f->data_fd >= 0);
}

void vf_close(vf_t *f)
{
    if (f->wal != NULL) OK(candidate_wal_close(&f->wal));
    if (f->data_fd >= 0) { CHECK(close(f->data_fd) == 0); f->data_fd = -1; }
}

void vf_free(vf_t *f)
{
    vf_close(f); scen_free(&f->built);
    if (vf_env == f) vf_env = NULL;
}

candidate_recovery_view_request_t vf_request(vf_t *f, candidate_recovery_view_mode_t mode, uint64_t owner)
{
    candidate_recovery_view_request_t r = {.anchor = f->anchor,.load = {.root = f->anchor.outcome_root,.load = vf_load,
        .context = f,.memory = &f->allocator},.mode = mode,.owner_epoch = owner};
    memcpy(r.load.uuid,fx_uuid,16);
    return r;
}

candidate_wal_status_t vf_load(void *context, candidate_page_ref_t ref, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    vf_t *f = context;
    CHECK(!f->is_loader_closed && ref.id != 0);
    f->loads++;
    if (f->on_load != NULL && f->loads == f->hook_at) { vf_hook_t hook = f->on_load; f->on_load = NULL; hook(f); }
    ssize_t n = pread(f->data_fd,out,CANDIDATE_PAGE_BYTES,(off_t)((ref.id - 1) * CANDIDATE_PAGE_BYTES));
    return n == (ssize_t)CANDIDATE_PAGE_BYTES ? CANDIDATE_WAL_OK : CANDIDATE_WAL_IO;
}

static candidate_wal_status_t validate_graph(vf_t *f, bool is_preview, const candidate_catalog_load_t *load,
    candidate_recovery_info_t *info)
{
    *info = (candidate_recovery_info_t){.anchor = f->anchor};
    return is_preview ? candidate_recovery_validate_preview(f->wal,&f->anchor,load,info) :
        candidate_recovery_validate(f->wal,&f->anchor,load,info);
}

candidate_wal_status_t vf_graph(vf_t *f, candidate_recovery_view_mode_t mode)
{
    candidate_recovery_view_request_t r = vf_request(f,mode,0); candidate_recovery_info_t info;
    return validate_graph(f,mode == CANDIDATE_RECOVERY_VIEW_PREVIEW,&r.load,&info);
}

candidate_wal_status_t vf_legacy(vf_t *f, candidate_recovery_view_mode_t mode, uint64_t owner,
    candidate_recovery_info_t *graph, candidate_outcome_witness_info_t *outcomes)
{
    candidate_recovery_view_request_t r = vf_request(f,mode,owner);
    candidate_recovery_info_t info;
    bool is_preview = mode == CANDIDATE_RECOVERY_VIEW_PREVIEW;
    candidate_wal_status_t status = validate_graph(f,is_preview,&r.load,&info);
    if (status != CANDIDATE_WAL_OK) return status;
    candidate_outcome_witness_options_t o = {.catalog = r.load,.checkpoint = &f->anchor};
    if (is_preview) status = candidate_outcome_preview(f->wal,&o,owner);
    else {
        candidate_outcome_witness_t *w = NULL;
        status = candidate_outcome_witness_load(f->wal,&o,&w);
        if (status == CANDIDATE_WAL_OK && outcomes != NULL) *outcomes = candidate_outcome_witness_info(w);
        candidate_outcome_witness_close(&w);
    }
    if (status == CANDIDATE_WAL_OK && graph != NULL) *graph = info;
    return status;
}

void vf_edit_versions(vf_t *f, uint64_t id, vf_edit_t edit, void *context)
{
    static uint8_t page[CANDIDATE_PAGE_BYTES], out[CANDIDATE_PAGE_BYTES];
    off_t offset = (off_t)((id - 1) * CANDIDATE_PAGE_BYTES);
    CHECK(pread(f->data_fd,page,sizeof(page),offset) == (ssize_t)sizeof(page));
    candidate_page_info_t info; OK(candidate_page_decode(fx_uuid,page,sizeof(page),&info)); CHECK(info.type == 1);
    candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS];
    for (uint32_t i = 0; i < info.slots; i++)
        OK(candidate_page_get_version(fx_uuid,page,sizeof(page),(candidate_version_ref_t){info.location.ref,i,1},&versions[i]));
    candidate_page_location_t location = info.location;
    edit(&location,versions,info.slots,context);
    OK(candidate_page_encode_versions(&location,versions,info.slots,out,sizeof(out)));
    CHECK(pwrite(f->data_fd,out,sizeof(out),offset) == (ssize_t)sizeof(out)); CHECK(fsync(f->data_fd) == 0);
}

candidate_wal_status_t vf_read_version(vf_t *f, candidate_version_ref_t ref, candidate_page_version_t *out)
{
    static uint8_t page[CANDIDATE_PAGE_BYTES];
    CHECK(ref.page.id != 0);
    CHECK(pread(f->data_fd,page,sizeof(page),(off_t)((ref.page.id - 1) * CANDIDATE_PAGE_BYTES)) == (ssize_t)sizeof(page));
    candidate_page_info_t info; candidate_wal_status_t status = candidate_page_decode(fx_uuid,page,sizeof(page),&info);
    if (status == CANDIDATE_WAL_OK) status = candidate_page_get_version(fx_uuid,page,sizeof(page),ref,out);
    return status;
}

void vf_expect_absent(vf_t *f, unsigned index)
{
    const fx_txn_t *t = &f->built.txn[index];
    candidate_page_version_t v; OK(vf_read_version(f,t->result,&v));
    const candidate_allocation_entry_t *page = fx_alloc_find(f->built.fx,t->result.page.id);
    candidate_recovery_view_request_t r = vf_request(f,CANDIDATE_RECOVERY_VIEW_RESOLVED,0); candidate_catalog_t *c = NULL;
    OK(candidate_catalog_load(&r.load,&c));
    const candidate_catalog_root_t *root = candidate_catalog_root(c);
    unsigned found = 0, retained = 0;
    for (uint32_t i = 0; i < root->count; i++) {
        uint64_t id = candidate_catalog_at(c,i)->transaction_id;
        for (unsigned n = 0; n < f->built.spec.requests; n++) if (id == f->built.txn[n].id) { if (n == index) found++; else retained++; }
    }
    candidate_catalog_query_t q = scen_query(&f->built,index); const candidate_outcome_t *e = (void *)1;
    candidate_catalog_lookup_t lookup = candidate_catalog_lookup(c,&q,&e);
    printf("ABSENT txn=%llu epoch=%llu floor=%llu ref=%llu/%u version_txn=%llu payload=%llu frozen=%llu page_state=%d entries=%u "
        "retained=%u found=%u lookup=%u\n",(unsigned long long)t->id,(unsigned long long)t->epoch,
        (unsigned long long)root->retention_floor,(unsigned long long)t->result.page.id,t->result.slot,
        (unsigned long long)v.transaction_id,(unsigned long long)v.payload_bytes,(unsigned long long)v.frozen_csn,
        page != NULL ? (int)page->state : -1,root->count,retained,found,lookup);
    CHECK(v.transaction_id == t->id && v.payload_bytes == t->result_bytes && page != NULL && page->state == CANDIDATE_ALLOCATION_LIVE);
    CHECK(found == 0 && retained == root->count && root->count == f->built.spec.requests - 1);
    CHECK(t->epoch < root->retention_floor && lookup == CANDIDATE_CATALOG_OUTSIDE_RETENTION && e == NULL);
    candidate_catalog_close(&c);
}

void vf_abort_resultless(vf_t *f, uint64_t id)
{
    static fx_txn_t t;
    fx_t *fx = f->built.fx; CHECK(fx->w == NULL);
    uint64_t owner = fx->owner;
    memset(&t,0,sizeof(t)); fx->w = f->wal; fx->owner = f->owner;
    fx_begin(fx,&t,id); fx_abort(fx,&t); fx->w = NULL; fx->owner = owner;
    printf("ABORTED transaction=%llu first=%llu last=%llu records=%llu terminal_end=%llu\n",(unsigned long long)t.id,
        (unsigned long long)t.first,(unsigned long long)t.last,(unsigned long long)t.records,(unsigned long long)t.terminal_end);
}

void vf_flip_wal_byte(vf_t *f, uint64_t offset)
{
    char path[320]; CHECK(snprintf(path,sizeof(path),"%s.wal.0000000000000000",f->built.fx->wal) > 0);
    CHECK(offset < CANDIDATE_WAL_SEGMENT_BYTES);
    int fd = open(path,O_RDWR); CHECK(fd >= 0);
    uint8_t before; CHECK(pread(fd,&before,1,(off_t)offset) == 1);
    uint8_t after = before ^ 0xff; CHECK(pwrite(fd,&after,1,(off_t)offset) == 1);
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
    printf("WAL_FLIP %s offset=%llu byte=0x%02x->0x%02x\n",path,(unsigned long long)offset,before,after);
}

void vf_mark_admitted(vf_t *f, unsigned index)
{
    static uint8_t page[CANDIDATE_PAGE_BYTES];
    static candidate_catalog_node_t node;
    uint64_t transaction = f->built.txn[index].id;
    CHECK(pread(f->data_fd,page,sizeof(page),(off_t)((f->anchor.outcome_root.id - 1) * CANDIDATE_PAGE_BYTES)) == (ssize_t)sizeof(page));
    candidate_catalog_root_t root; OK(candidate_catalog_decode_root(fx_uuid,page,sizeof(page),&root));
    unsigned nodes = (root.count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    for (unsigned n = 0; n < nodes; n++) {
        off_t offset = (off_t)((root.pages[n].id - 1) * CANDIDATE_PAGE_BYTES);
        CHECK(pread(f->data_fd,page,sizeof(page),offset) == (ssize_t)sizeof(page));
        candidate_page_info_t info; OK(candidate_page_decode_header(fx_uuid,page,sizeof(page),&info));
        OK(candidate_catalog_decode_node(fx_uuid,page,sizeof(page),&node));
        for (unsigned i = 0; i < node.count; i++) {
            candidate_outcome_t *e = &node.entries[i];
            if (e->transaction_id != transaction) continue;
            CHECK(e->state == CANDIDATE_OUTCOME_COMMITTED);
            e->state = CANDIDATE_OUTCOME_ADMITTED; e->csn = 0; e->terminal_end_lsn = 0; e->result = (candidate_version_ref_t){0};
            e->result_bytes = 0; e->result_chunks = 0; memset(e->result_digest,0,sizeof(e->result_digest));
            OK(candidate_catalog_encode_node(&info.location,&node,page,sizeof(page)));
            CHECK(pwrite(f->data_fd,page,sizeof(page),offset) == (ssize_t)sizeof(page)); CHECK(fsync(f->data_fd) == 0);
            printf("ADMITTED transaction=%llu node=%llu lsn=%llu\n",(unsigned long long)transaction,
                (unsigned long long)root.pages[n].id,(unsigned long long)info.location.lsn);
            return;
        }
    }
    CHECK(false);
}

void vf_expect_answer(const vf_t *f, unsigned index, candidate_catalog_lookup_t lookup, const candidate_outcome_t *outcome)
{
    const req_t *r = &f->built.spec.req[index]; const fx_txn_t *t = &f->built.txn[index];
    CHECK(lookup == CANDIDATE_CATALOG_MATCH && outcome != NULL && outcome->transaction_id == t->id);
    if (r->state == REQ_COMMIT) {
        CHECK(outcome->state == CANDIDATE_OUTCOME_COMMITTED && outcome->csn == t->csn && outcome->result_bytes == r->bytes);
        return;
    }
    CHECK(outcome->state == CANDIDATE_OUTCOME_ABORTED && outcome->csn == 0 && outcome->result.page.id == 0);
}

void vf_expect_body(const vf_t *f, unsigned index, const candidate_page_result_t *body)
{
    uint32_t bytes = f->built.spec.req[index].bytes;
    if (bytes == 0) { CHECK(body == NULL); return; }
    CHECK(candidate_page_result_size(body) == bytes);
    uint8_t *expected = malloc(bytes); CHECK(expected != NULL); fx_payload(index + 1,bytes,expected);
    CHECK(memcmp(candidate_page_result_bytes(body),expected,bytes) == 0); free(expected);
}

candidate_recovery_options_t vf_options(vf_t *f, uint64_t owner)
{
    candidate_recovery_options_t o = fx_options(f->built.fx,owner);
    o.memory = &f->allocator;
    return o;
}

const char *vf_status(candidate_wal_status_t status)
{
    static const char *names[] = {"OK","INVALID","NOMEM","LIMIT","IO","CORRUPT","UNSUPPORTED","EXISTS","BUSY","FORKED",
        "POISONED","TAIL","STOPPED","STALE"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "?";
}
