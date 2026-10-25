#include "test_candidate_recovery_publish_fixture.h"

#include <string.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_wal_witness.h"

#define OVERFLOW_AT 3000u

typedef enum { P_ALLOC_ROOT = 1, P_ALLOC_NODE, P_ROOT, P_NODE, P_VERSION, P_OVERFLOW, P_EPOCH_ROOT,
    P_EXTRA_ROOT, P_BAD_ROOT, P_BAD_NODE, P_GOOD_ROOT, P_GOOD_NODE, P_BALLAST } kind_t;
typedef struct { kind_t kind; uint32_t index; uint64_t id; } plan_t;
typedef struct {
    uint64_t txn, bytes, seed;
    candidate_version_ref_t ref, previous;
    candidate_page_ref_t overflow;
    uint32_t overflow_pages, page;
} version_t;
typedef struct {
    const scen_t *s;
    built_t *b;
    plan_t *plan;
    uint32_t plan_count;
    version_t *versions;
    uint32_t version_count, page_count, *page_first, *page_count_of;
    uint64_t overflow_first, txh, ph, root_visible;
    candidate_outcome_t *stored;
    uint32_t stored_count;
    candidate_page_ref_t alloc_nodes[CANDIDATE_ALLOCATION_NODES];
} work_t;

static uint64_t limit_of(const req_t *r) { return r->bytes + 16; }

static bool wants_version(const req_t *r) { return r->has_version || r->bytes > 0 || (r->state == REQ_COMMIT && r->store == STORE_FINAL); }

static void plan_versions(work_t *w)
{
    const scen_t *s = w->s;
    w->versions = calloc(s->requests + 2,sizeof(*w->versions)); CHECK(w->versions != NULL);
    for (unsigned i = 0; i < s->requests; i++) {
        if (!wants_version(&s->req[i])) continue;
        version_t *v = &w->versions[w->version_count++];
        *v = (version_t){.txn = i + 1,.bytes = s->req[i].bytes,.seed = i + 1};
        v->overflow_pages = v->bytes > OVERFLOW_AT ? (uint32_t)((v->bytes + CANDIDATE_PAGE_OVERFLOW_BYTES - 1) / CANDIDATE_PAGE_OVERFLOW_BYTES) : 0;
    }
    uint32_t chained = w->version_count, previous = s->chain_b == 2 ? 2 : (s->chain_b == 1 ? 1 : 0);
    for (uint32_t i = 0; i < previous; i++) {
        version_t *v = &w->versions[w->version_count++];
        *v = (version_t){.txn = w->b->carrier_txn,.bytes = s->chain_b == 1 ? 9000 : 5000,.seed = 999983 + i};
        v->overflow_pages = s->chain_b == 1 ? 2 : 1;
    }
    w->page_first = calloc(w->version_count + 1,sizeof(uint32_t)); w->page_count_of = calloc(w->version_count + 1,sizeof(uint32_t));
    CHECK(w->page_first != NULL && w->page_count_of != NULL);
    size_t used = 0;
    for (uint32_t i = 0; i < w->version_count; i++) {
        version_t *v = &w->versions[i];
        size_t size = 8 + 104 + (v->overflow_pages == 0 ? v->bytes : 0);
        /* 연결 시험은 결과 page·이전 page를 따로 둔다. shared 모드는 두 결과와 두 이전 version이 각각 같은 page다. */
        bool own = (s->chain_b == 1 && (i == 0 || i == 1 || i == chained)) || (s->chain_b == 2 && (i == 0 || i == 2 || i == chained));
        bool fresh = w->page_count == 0 || used + size > CANDIDATE_PAGE_BYTES - 72 || w->page_count_of[w->page_count - 1] == 101 || own;
        if (fresh) { w->page_first[w->page_count] = i; w->page_count++; used = 0; }
        v->page = w->page_count - 1; w->page_count_of[v->page]++; used += size;
    }
}

static uint32_t plan_add(work_t *w, kind_t kind, uint32_t index)
{
    w->plan[w->plan_count] = (plan_t){kind,index,w->plan_count + 1}; return w->plan_count++;
}

static uint32_t stored_count(const work_t *w)
{
    uint32_t count = 0;
    for (unsigned i = 0; i < w->s->requests; i++) {
        const req_t *r = &w->s->req[i];
        if (r->store != STORE_NONE && r->epoch >= w->s->root_floor && r->epoch <= w->s->root_epoch) count++;
    }
    return count;
}

static void plan_pages(work_t *w)
{
    const scen_t *s = w->s; built_t *b = w->b;
    uint32_t nodes = (stored_count(w) + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    uint32_t overflow = 0; for (uint32_t i = 0; i < w->version_count; i++) overflow += w->versions[i].overflow_pages;
    uint32_t fixed = 1 + 1 + nodes + w->page_count + overflow + (s->epoch_to != 0) + s->extra_empty_roots +
        (s->invalid_extra_root ? 2 : 0) + 2 * s->expired_roots + (s->good_extra_root ? 2 : 0) + s->ballast;
    uint32_t ballast = s->ballast, a = 1;
    if (s->pad_alloc_to != 0) {
        for (a = 1; a < CANDIDATE_ALLOCATION_NODES; a++)
            if ((s->pad_alloc_to + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES == a) break;
        CHECK(s->pad_alloc_to >= fixed - s->ballast + a);
        ballast = s->pad_alloc_to - (fixed - s->ballast) - a;
    } else {
        while ((fixed + a + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES != a) a++;
    }
    uint32_t total = fixed - s->ballast + ballast + a;
    w->plan = calloc(total,sizeof(*w->plan)); CHECK(w->plan != NULL);
    plan_add(w,P_ALLOC_ROOT,0);
    for (uint32_t i = 0; i < a; i++) { uint32_t at = plan_add(w,P_ALLOC_NODE,i); w->alloc_nodes[i] = (candidate_page_ref_t){w->plan[at].id,1}; }
    b->root_id = w->plan[plan_add(w,P_ROOT,0)].id;
    for (uint32_t i = 0; i < nodes; i++) plan_add(w,P_NODE,i);
    for (uint32_t i = 0; i < w->page_count; i++) plan_add(w,P_VERSION,i);
    w->overflow_first = w->plan_count + 1;
    for (uint32_t i = 0; i < overflow; i++) plan_add(w,P_OVERFLOW,i);
    if (s->epoch_to != 0) b->epoch_root_id = w->plan[plan_add(w,P_EPOCH_ROOT,0)].id;
    for (uint32_t i = 0; i < s->extra_empty_roots; i++) plan_add(w,P_EXTRA_ROOT,i);
    if (s->invalid_extra_root) { plan_add(w,P_BAD_ROOT,0); plan_add(w,P_BAD_NODE,0); }
    for (uint32_t i = 0; i < s->expired_roots; i++) { plan_add(w,P_BAD_ROOT,i + 1); plan_add(w,P_BAD_NODE,i + 1); }
    if (s->good_extra_root) { plan_add(w,P_GOOD_ROOT,0); plan_add(w,P_GOOD_NODE,0); }
    for (uint32_t i = 0; i < ballast; i++) plan_add(w,P_BALLAST,i);
    CHECK(w->plan_count == total);
    b->alloc_root_id = 1; b->alloc_count = total; b->stored_nodes = nodes; b->version_pages = w->page_count;
    uint64_t next = w->overflow_first;
    for (uint32_t i = 0; i < w->version_count; i++) {
        version_t *v = &w->versions[i];
        uint64_t page_id = 0;
        for (uint32_t j = 0; j < w->plan_count; j++) if (w->plan[j].kind == P_VERSION && w->plan[j].index == v->page) page_id = w->plan[j].id;
        v->ref = (candidate_version_ref_t){{page_id,1},i - w->page_first[v->page],1};
        if (v->overflow_pages != 0) { v->overflow = (candidate_page_ref_t){next,1}; next += v->overflow_pages; }
    }
    if (s->chain_b == 1) w->versions[0].previous = w->versions[w->version_count - 1].ref;
    if (s->chain_b == 2) {
        w->versions[0].previous = w->versions[w->version_count - 2].ref; w->versions[1].previous = w->versions[w->version_count - 1].ref;
    }
    w->ph = s->page_highwater != 0 ? s->page_highwater : total;
    w->txh = s->txn_highwater != 0 ? s->txn_highwater : s->requests + 64;
}

static const version_t *version_of(const work_t *w, uint64_t txn)
{
    for (uint32_t i = 0; i < w->version_count; i++) if (w->versions[i].txn == txn) return &w->versions[i];
    return NULL;
}

static void emit_requests(work_t *w)
{
    const scen_t *s = w->s; built_t *b = w->b;
    for (unsigned i = 0; i < s->requests; i++) {
        const req_t *r = &s->req[i]; fx_txn_t *t = &b->txn[i];
        fx_admit(b->fx,t,i + 1,r->epoch,limit_of(r)); fx_begin(b->fx,t,i + 1);
        const version_t *v = version_of(w,i + 1);
        if (r->bytes > 0) {
            uint8_t *body = malloc(r->bytes); CHECK(body != NULL); fx_payload(v->seed,r->bytes,body);
            fx_chunks(b->fx,t,v->ref,body,r->bytes); free(body);
        } else if (v != NULL) t->result = v->ref;
        if (i + 1 == b->carrier_txn) continue;
        if (r->state == REQ_COMMIT) fx_commit(b->fx,t);
        else if (r->state == REQ_ABORT) fx_abort(b->fx,t);
    }
}

static candidate_outcome_t stored_entry(const work_t *w, unsigned i)
{
    const req_t *r = &w->s->req[i]; const fx_txn_t *t = &w->b->txn[i];
    candidate_outcome_t e = {.epoch = r->epoch,.transaction_id = t->id,.state = CANDIDATE_OUTCOME_ADMITTED,
        .declared_result_limit = t->limit,.admitting_owner_epoch = w->b->fx->owner};
    memcpy(e.nonce,t->nonce,16); memcpy(e.operation_digest,t->operation,32);
    if (r->store != STORE_FINAL) return e;
    if (r->state == REQ_UNFINISHED) {
        e.state = CANDIDATE_OUTCOME_COMMITTED; e.csn = w->b->fx->highest_csn; e.terminal_end_lsn = 4096 * 8;
        e.result = t->result; e.result_bytes = t->result_bytes; e.result_chunks = t->chunks; memcpy(e.result_digest,t->result_digest,32);
        if (r->retain) { e.state = CANDIDATE_OUTCOME_ABORTED; e.csn = 0; }
        if (e.state == CANDIDATE_OUTCOME_COMMITTED && e.csn == 0) e.csn = 1;
        return e;
    }
    e.state = r->state == REQ_COMMIT ? CANDIDATE_OUTCOME_COMMITTED : CANDIDATE_OUTCOME_ABORTED;
    e.csn = t->csn; e.terminal_end_lsn = t->terminal_end;
    if (r->state == REQ_ABORT && !r->retain) { OK(candidate_digest_sha256(NULL,0,e.result_digest)); return e; }
    e.result = t->result; e.result_bytes = t->result_bytes; e.result_chunks = t->chunks; memcpy(e.result_digest,t->result_digest,32);
    return e;
}

static void build_stored(work_t *w)
{
    w->stored = calloc(w->s->requests + 1,sizeof(*w->stored)); CHECK(w->stored != NULL);
    for (unsigned i = 0; i < w->s->requests; i++) {
        const req_t *r = &w->s->req[i];
        if (r->store == STORE_NONE || r->epoch < w->s->root_floor || r->epoch > w->s->root_epoch) continue;
        w->stored[w->stored_count++] = stored_entry(w,i);
    }
}

static uint64_t root_retained(const work_t *w)
{
    uint64_t bytes = (uint64_t)(w->b->stored_nodes + 1) * CANDIDATE_PAGE_BYTES;
    for (uint32_t i = 0; i < w->stored_count; i++) bytes += w->stored[i].result_bytes;
    return bytes + w->s->retained_extra;
}

static void encode_root(const work_t *w, uint64_t id, uint64_t lsn, uint64_t epoch, uint64_t floor, uint32_t count,
    const candidate_page_ref_t *pages, uint64_t retained, uint8_t *out)
{
    candidate_catalog_root_t root = {.owner_epoch = w->b->fx->owner,.current_epoch = epoch,.retention_floor = floor,
        .transaction_highwater = w->txh,.page_highwater = w->ph,.visible_csn = w->root_visible,.retained_bytes = retained,.count = count};
    uint32_t nodes = (count + CANDIDATE_CATALOG_NODE_ENTRIES - 1) / CANDIDATE_CATALOG_NODE_ENTRIES;
    if (nodes > 0) memcpy(root.pages,pages,nodes * sizeof(pages[0]));
    candidate_page_location_t l = fx_location(id,1,lsn);
    OK(candidate_catalog_encode_root(&l,&root,out,CANDIDATE_PAGE_BYTES));
}

static void encode_page(work_t *w, const plan_t *p, uint64_t lsn, uint8_t *out)
{
    const scen_t *s = w->s; built_t *b = w->b; candidate_page_location_t l = fx_location(p->id,1,lsn);
    switch (p->kind) {
    case P_ALLOC_ROOT: fx_encode_alloc_root(b->fx,p->id,lsn,b->fx->owner,w->ph,0,w->alloc_nodes,out); return;
    case P_ALLOC_NODE: fx_encode_alloc_node(b->fx,p->index,p->id,lsn,out); return;
    case P_ROOT: {
        candidate_page_ref_t pages[CANDIDATE_CATALOG_PAGES];
        for (uint32_t i = 0; i < b->stored_nodes; i++) pages[i] = (candidate_page_ref_t){p->id + 1 + i,1};
        encode_root(w,p->id,lsn,s->root_epoch,s->root_floor,w->stored_count,pages,root_retained(w),out); return;
    }
    case P_NODE: {
        candidate_catalog_node_t node = {.ordinal = p->index};
        uint32_t start = p->index * CANDIDATE_CATALOG_NODE_ENTRIES;
        node.count = w->stored_count - start < CANDIDATE_CATALOG_NODE_ENTRIES ? w->stored_count - start : CANDIDATE_CATALOG_NODE_ENTRIES;
        memcpy(node.entries,w->stored + start,node.count * sizeof(node.entries[0]));
        OK(candidate_catalog_encode_node(&l,&node,out,CANDIDATE_PAGE_BYTES)); return;
    }
    case P_VERSION: {
        candidate_page_version_t versions[CANDIDATE_PAGE_MAX_SLOTS]; uint8_t *bodies[CANDIDATE_PAGE_MAX_SLOTS] = {0};
        uint32_t first = w->page_first[p->index], count = w->page_count_of[p->index];
        for (uint32_t i = 0; i < count; i++) {
            const version_t *v = &w->versions[first + i];
            versions[i] = (candidate_page_version_t){.transaction_id = v->txn,.previous = v->previous,.table_id = 1,.row_id = v->txn,
                .payload_bytes = v->bytes,.slot_generation = 1,.overflow = v->overflow};
            if (v->overflow_pages == 0 && v->bytes > 0) {
                bodies[i] = malloc(v->bytes); CHECK(bodies[i] != NULL); fx_payload(v->seed,v->bytes,bodies[i]); versions[i].inline_bytes = bodies[i];
            }
        }
        OK(candidate_page_encode_versions(&l,versions,count,out,CANDIDATE_PAGE_BYTES));
        for (uint32_t i = 0; i < count; i++) free(bodies[i]);
        return;
    }
    case P_OVERFLOW: {
        uint64_t at = w->overflow_first;
        for (uint32_t i = 0; i < w->version_count; i++) {
            const version_t *v = &w->versions[i];
            if (v->overflow_pages == 0) continue;
            if (p->id >= at + v->overflow_pages) { at += v->overflow_pages; continue; }
            uint32_t part = (uint32_t)(p->id - at);
            uint8_t *body = malloc(v->bytes); CHECK(body != NULL); fx_payload(v->seed,v->bytes,body);
            size_t offset = (size_t)part * CANDIDATE_PAGE_OVERFLOW_BYTES, size = v->bytes - offset;
            if (size > CANDIDATE_PAGE_OVERFLOW_BYTES) size = CANDIDATE_PAGE_OVERFLOW_BYTES;
            candidate_page_ref_t next = part + 1 == v->overflow_pages ? (candidate_page_ref_t){0} : (candidate_page_ref_t){p->id + 1,1};
            OK(candidate_page_encode_overflow(&l,next,body + offset,size,out,CANDIDATE_PAGE_BYTES)); free(body); return;
        }
        CHECK(false); return;
    }
    case P_EPOCH_ROOT: encode_root(w,p->id,lsn,s->epoch_to,s->floor_to,0,NULL,CANDIDATE_PAGE_BYTES,out); return;
    case P_EXTRA_ROOT: encode_root(w,p->id,lsn,s->root_epoch,s->root_floor,0,NULL,CANDIDATE_PAGE_BYTES,out); return;
    case P_BAD_ROOT: case P_GOOD_ROOT: {
        candidate_page_ref_t node = {p->id + 1,1};
        encode_root(w,p->id,lsn,s->root_epoch,s->root_floor,1,&node,2 * CANDIDATE_PAGE_BYTES,out); return;
    }
    case P_BAD_NODE: case P_GOOD_NODE: case P_BALLAST: {
        candidate_catalog_node_t node = {.count = 1};
        uint64_t epoch = p->kind == P_GOOD_NODE ? s->floor_to : s->root_floor;
        node.entries[0] = (candidate_outcome_t){.epoch = epoch,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,
            .admitting_owner_epoch = b->fx->owner};
        uint64_t unique = p->id ^ 0x5d5d5d5d5d5dull; memcpy(node.entries[0].nonce,&unique,8);
        OK(candidate_catalog_encode_node(&l,&node,out,CANDIDATE_PAGE_BYTES)); return;
    }
    }
}

static bool is_root_kind(kind_t k) { return k == P_ROOT || k == P_EXTRA_ROOT || k == P_BAD_ROOT || k == P_GOOD_ROOT || k == P_EPOCH_ROOT; }

static void emit_bootstrap(work_t *w)
{
    built_t *b = w->b; fx_txn_t *carrier = NULL, own = {0};
    if (!w->s->system_bootstrap) {
        if (b->carrier_txn <= w->s->requests) carrier = &b->txn[b->carrier_txn - 1];
        else { fx_begin(b->fx,&own,b->carrier_txn); carrier = &own; }
    }
    static fx_image_t images[64];
    uint32_t *final = calloc(w->plan_count,sizeof(uint32_t)), final_count = 0;
    bool *done = calloc(w->plan_count,sizeof(bool)); CHECK(done != NULL && final != NULL);
    for (uint32_t i = 0; i < w->plan_count; i++) {
        kind_t k = w->plan[i].kind;
        if (k == P_EPOCH_ROOT) done[i] = true;
        if (k == P_ALLOC_ROOT || k == P_ROOT) { final[final_count++] = i; done[i] = true; }
    }
    for (uint32_t i = w->plan_count; i > 0 && final_count < 64; i--)
        if (!done[i - 1] && !is_root_kind(w->plan[i - 1].kind)) { final[final_count++] = i - 1; done[i - 1] = true; }
    if (w->s->system_bootstrap) for (uint32_t i = 0; i < w->plan_count; i++) CHECK(done[i]);
    while (true) {
        uint32_t count = 0, picks[64]; bool has_root = false;
        for (uint32_t i = 0; i < w->plan_count && count < 64; i++) {
            if (done[i]) continue;
            if (is_root_kind(w->plan[i].kind)) { if (has_root) continue; has_root = true; }
            picks[count++] = i;
        }
        if (count == 0) { memcpy(picks,final,final_count * sizeof(uint32_t)); count = final_count; final_count = 0; }
        if (count == 0) break;
        fx_event_t reserve = {16,0,w->txh,0,w->ph};
        bool is_final = final_count == 0;
        unsigned events = w->s->system_bootstrap ? 1 : 0;
        uint64_t end = fx_group_end(b->fx,count,events);
        for (uint32_t j = 0; j < count; j++) {
            const plan_t *p = &w->plan[picks[j]];
            images[j].id = p->id; images[j].generation = 1; images[j].previous = 0;
            encode_page(w,p,end,images[j].page); done[picks[j]] = true;
        }
        bool write = !(w->s->unapplied_last && is_final);
        b->last_group_end = fx_group(b->fx,carrier,images,count,&reserve,events,true,write);
        if (is_final) break;
    }
    free(done); free(final);
    if (carrier != NULL) fx_commit(b->fx,carrier);
}

static void emit_epoch(work_t *w)
{
    const scen_t *s = w->s; built_t *b = w->b;
    if (s->epoch_to == 0) return;
    static fx_image_t image;
    fx_event_t event = {15,s->root_epoch,s->epoch_to,s->root_floor,s->floor_to};
    uint64_t end = fx_group_end(b->fx,1,1);
    for (uint32_t i = 0; i < w->plan_count; i++) if (w->plan[i].kind == P_EPOCH_ROOT) {
        image.id = w->plan[i].id; image.generation = 1; image.previous = 0; encode_page(w,&w->plan[i],end,image.page);
    }
    fx_group(b->fx,NULL,&image,1,&event,1,true,true);
    if (!s->free_epoch_root) return;
    candidate_allocation_entry_t *e = fx_alloc_find(b->fx,b->epoch_root_id); CHECK(e != NULL);
    static fx_image_t pair[2];
    fx_event_t free_event = {7,b->epoch_root_id,1,1,1};
    end = fx_group_end(b->fx,2,1);
    e->state = CANDIDATE_ALLOCATION_FREE; e->release_horizon = 1; e->transition_lsn = end;
    uint32_t ordinal = (uint32_t)((e - b->fx->alloc) / CANDIDATE_ALLOCATION_NODE_ENTRIES);
    pair[0].id = 1; pair[0].generation = 1; pair[0].previous = b->fx->pages[1].lsn;
    fx_encode_alloc_root(b->fx,1,end,b->fx->owner,w->ph,1,w->alloc_nodes,pair[0].page);
    pair[1].id = w->alloc_nodes[ordinal].id; pair[1].generation = 1; pair[1].previous = b->fx->pages[pair[1].id].lsn;
    fx_encode_alloc_node(b->fx,ordinal,pair[1].id,end,pair[1].page);
    fx_group(b->fx,NULL,pair,2,&free_event,1,true,true);
}

built_t scen_build(const scen_t *s)
{
    built_t b = {.spec = *s}; work_t w = {.s = &b.spec,.b = &b};
    if (b.spec.root_epoch == 0) { b.spec.root_epoch = 1; b.spec.root_floor = 1; }
    b.fx = fx_create(s->name,7);
    b.txn = calloc(s->requests + 1,sizeof(*b.txn)); CHECK(b.txn != NULL);
    b.carrier_txn = s->system_bootstrap ? 0 : (s->requests < CANDIDATE_WITNESS_TRANSACTIONS ? s->requests + 1 : s->requests);
    plan_versions(&w); plan_pages(&w);
    for (uint32_t i = 0; i < w.plan_count; i++) fx_alloc_add(b.fx,w.plan[i].id,1,CANDIDATE_ALLOCATION_LIVE,0,0);
    emit_requests(&w);
    w.root_visible = b.fx->highest_csn;
    if (s->stored_visible_ahead) w.root_visible++;
    build_stored(&w);
    emit_bootstrap(&w);
    emit_epoch(&w);
    if (s->before_checkpoint != NULL) s->before_checkpoint(&b,s->context);
    candidate_control_state_t a = {.backend = CANDIDATE_CONTROL_WAL,.generation = 1,.owner_epoch = 7,.checkpoint_id = 1,.redo_begin = 64,
        .transaction_highwater = w.txh,.page_highwater = w.ph,.request_epoch = s->epoch_to ? s->epoch_to : b.spec.root_epoch,
        .retention_floor = s->epoch_to ? s->floor_to : b.spec.root_floor,.visible_csn = b.fx->highest_csn,
        .outcome_root = {b.root_id,1},.allocation_root = {1,1},.gc_horizon = s->free_epoch_root ? 1 : s->gc_horizon};
    memcpy(a.uuid,fx_uuid,16);
    const fx_txn_t *active[64]; unsigned active_count = 0;
    for (unsigned i = 0; i < s->requests; i++) if (s->req[i].state == REQ_UNFINISHED) { CHECK(active_count < 63); active[active_count++] = &b.txn[i]; }
    fx_checkpoint(b.fx,&a,active,active_count);
    b.anchor = a;
    free(w.plan); free(w.versions); free(w.page_first); free(w.page_count_of); free(w.stored);
    return b;
}

void scen_finish(built_t *b)
{
    fx_control(b->fx,&b->anchor,1); fx_close(b->fx);
}

candidate_catalog_query_t scen_query(const built_t *b, unsigned index)
{
    candidate_catalog_query_t q = {.epoch = b->txn[index].epoch}; memcpy(q.uuid,fx_uuid,16);
    memcpy(q.nonce,b->txn[index].nonce,16); memcpy(q.operation_digest,b->txn[index].operation,32);
    return q;
}

void scen_free(built_t *b)
{
    fx_free(&b->fx); free(b->txn); b->txn = NULL;
}
