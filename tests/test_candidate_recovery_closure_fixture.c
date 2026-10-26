#include "test_candidate_recovery_closure_fixture.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_outcome_rebuild.h"
#include "../src/storage/candidate_wal_witness_state_internal.h"

#define PLAIN_FILL_BYTES 240u
#define DELTA_FILL_MIN 624u
#define DELTA_FILL_MAX 8744u
#define DELTA_FILL_BASE 616u

cf_io_t cf_io;
static char cf_prefix[256];
static fx_image_t cf_images[64];

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t read_le(const uint8_t *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static uint64_t segment_of(int fd)
{
    struct stat held; CHECK(fstat(fd,&held) == 0);
    for (uint64_t id = 0; id < CANDIDATE_WAL_MAX_BYTES / CANDIDATE_WAL_SEGMENT_BYTES; id++) {
        char path[320]; struct stat st;
        CHECK(snprintf(path,sizeof(path),"%s.wal.%016" PRIx64,cf_prefix,id) > 0);
        if (stat(path,&st) != 0) break;
        if (st.st_ino == held.st_ino && st.st_dev == held.st_dev) return id;
    }
    CHECK(false);
    return 0;
}

static bool fails_read(uint64_t lsn)
{
    if (cf_io.fail_from == 0 || lsn < cf_io.fail_from) return false;
    cf_io.fail_from = 0; cf_io.failed++; cf_io.reads++;
    return true;
}

static void fire_hook(void)
{
    cf_hook_t hook = cf_io.on_read; cf_io.on_read = NULL;
    hook(cf_io.context);
}

static void run_hook(uint64_t lsn)
{
    if (cf_io.on_read == NULL || cf_io.hook_call != 0 || lsn < cf_io.hook_from) return;
    fire_hook();
}

static void run_call_hook(void)
{
    if (cf_io.on_read == NULL || cf_io.hook_call == 0 || cf_io.reads + 1 != cf_io.hook_call) return;
    fire_hook();
}

static ssize_t cf_pread(int fd, void *p, size_t n, off_t offset)
{
    if (!cf_io.is_armed) return pread(fd,p,n,offset);
    uint64_t lsn = segment_of(fd) * CANDIDATE_WAL_SEGMENT_BYTES + (uint64_t)offset;
    if (lsn < cf_io.deny_before) { cf_io.denied++; errno = EACCES; return -1; }
    if (cf_io.reads == 0 || lsn < cf_io.lowest_lsn) cf_io.lowest_lsn = lsn;
    if (fails_read(lsn)) { errno = EIO; return -1; }
    run_call_hook();
    ssize_t r = pread(fd,p,n,offset);
    int saved = errno;
    cf_io.reads++; if (r > 0) cf_io.read_bytes += (uint64_t)r;
    run_hook(lsn);
    errno = saved;
    return r;
}

static const candidate_wal_io_t cf_hooks = {cf_pread,pwrite,fsync,ftruncate};

void cf_arm(uint64_t deny_before) { cf_io = (cf_io_t){.deny_before = deny_before,.is_armed = true}; }

void cf_disarm(void) { cf_io.is_armed = false; }

static fx_t *fx_of(const cf_t *c) { return c->f.built.fx; }

static void open_wal(cf_t *c, uint64_t owner)
{
    OK(candidate_wal_close(&c->f.wal));
    candidate_wal_options_t o = {.owner_epoch = owner,.io = &cf_hooks}; memcpy(o.uuid,fx_uuid,16);
    OK(candidate_wal_open(fx_of(c)->wal,&o,&c->f.wal));
    OK(candidate_wal_force(c->f.wal,candidate_wal_info(c->f.wal).valid_end_lsn));
    CHECK(snprintf(cf_prefix,sizeof(cf_prefix),"%s",fx_of(c)->wal) > 0);
}

void cf_reown(cf_t *c, uint64_t owner)
{
    open_wal(c,owner);
    fx_of(c)->w = c->f.wal; fx_of(c)->owner = owner;
}

static bool count_record(void *context, const candidate_wal_record_t *r)
{
    cf_tally_t *t = context;
    bool is_start = r->transaction_id != 0 && r->previous_lsn == 0 && (r->type == 1 || r->type == 2);
    if (is_start) { t->transactions++; if (r->transaction_id > t->max_started) t->max_started = r->transaction_id; }
    if (r->type == 1 && r->payload_bytes == 64) { CHECK(read_le(r->payload,8) < CF_EPOCHS); t->admitted[read_le(r->payload,8)]++; }
    if (r->type == 10) t->commits++;
    if (r->type == 11) t->aborts++;
    if (r->type == 8) { t->groups++; if (r->transaction_id == 0) t->system_groups++; }
    if (r->type == 6 || r->type == 7 || r->type == 15 || r->type == 16) t->events++;
    return true;
}

static void read_page(const cf_t *c, uint64_t id, uint8_t page[CANDIDATE_PAGE_BYTES], candidate_page_info_t *info)
{
    off_t offset = (off_t)((id - 1) * CANDIDATE_PAGE_BYTES);
    CHECK(pread(c->f.data_fd,page,CANDIDATE_PAGE_BYTES,offset) == (ssize_t)CANDIDATE_PAGE_BYTES);
    OK(candidate_page_decode_header(fx_uuid,page,CANDIDATE_PAGE_BYTES,info));
}

static void locate_pages(cf_t *c)
{
    const built_t *b = &c->f.built;
    c->delta_page = b->alloc_count; c->churn_first = c->delta_page - CF_CHURN_PAGES; c->epoch_root = c->churn_first - 1;
    uint8_t page[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    read_page(c,c->epoch_root,page,&info); CHECK(info.type == 4 && read_le(page + 70,2) == 2);
    read_page(c,1,page,&info); candidate_allocation_root_t root;
    OK(candidate_allocation_decode_root(fx_uuid,page,sizeof(page),&root));
    CHECK(root.count == b->alloc_count && root.nodes[0].id == 2 && root.nodes[0].generation == 1);
    read_page(c,c->delta_page,page,&info); memcpy(c->delta_body,page + 64,sizeof(c->delta_body));
}

void cf_boot(cf_t *c, const char *tag, candidate_recovery_view_mode_t mode)
{
    static const scen_t spec = {.requests = 3,.req = vf_clean,.stored_visible_ahead = true,.ballast = CF_CHURN_PAGES + 2,
        .txn_highwater = CF_TRANSACTION_HIGHWATER};
    memset(c,0,sizeof(*c));
    vf_boot(&c->f,tag,&spec,false);
    c->mode = mode; c->next_id = CF_FIRST_ID;
    open_wal(c,c->f.owner); locate_pages(c);
    const candidate_control_state_t *a = &c->f.anchor;
    c->epoch = a->request_epoch; c->floor = a->retention_floor;
    c->transaction_highwater = a->transaction_highwater; c->page_highwater = a->page_highwater;
    for (unsigned i = 0; i < CF_CHURN_PAGES; i++) c->generation[i] = 1;
    candidate_wal_info_t info; OK(candidate_wal_scan(c->f.wal,count_record,&c->tally,&info));
    fx_t *fx = fx_of(c); fx->w = c->f.wal; fx->data_fd = c->f.data_fd; fx->owner = c->f.owner;
    printf("CLOSURE_FIXTURE %s mode=%s pages=%u churn=%llu..%llu delta=%llu epoch_root=%llu wal_end=%llu txns=%llu "
        "groups=%llu events=%llu\n",tag,mode == CANDIDATE_RECOVERY_VIEW_RESOLVED ? "resolved" : "preview",c->f.built.alloc_count,
        (unsigned long long)c->churn_first,(unsigned long long)(c->delta_page - 1),(unsigned long long)c->delta_page,
        (unsigned long long)c->epoch_root,(unsigned long long)info.valid_end_lsn,(unsigned long long)c->tally.transactions,
        (unsigned long long)c->tally.groups,(unsigned long long)c->tally.events);
}

void cf_free(cf_t *c)
{
    fx_t *fx = fx_of(c); fx->w = NULL; fx->data_fd = -1;
    free(c->ledger); c->ledger = NULL; c->ledger_count = c->ledger_capacity = 0;
    vf_free(&c->f);
}

candidate_recovery_view_request_t cf_request(cf_t *c)
{
    return vf_request(&c->f,c->mode,c->mode == CANDIDATE_RECOVERY_VIEW_PREVIEW ? 8 : 0);
}

candidate_wal_status_t cf_open(cf_t *c, candidate_recovery_view_t **out)
{
    candidate_recovery_view_request_t request = cf_request(c);
    return candidate_recovery_view_open(c->f.wal,&request,out);
}

candidate_wal_status_t cf_continue(cf_t *c, const candidate_recovery_view_t *source, candidate_recovery_view_t **out)
{
    candidate_recovery_view_request_t request = cf_request(c);
    return candidate_recovery_view_continue(c->f.wal,&request,source,out);
}

static candidate_wal_record_t append(cf_t *c, uint16_t type, uint64_t txn, uint64_t previous, const uint8_t *body,
    size_t size)
{
    fx_t *fx = fx_of(c);
    uint64_t start, expected = fx_place(fx_position(fx),size,&start), end;
    candidate_wal_record_t r = {.type = type,.lsn = start,.transaction_id = txn,.previous_lsn = previous,.owner_epoch = fx->owner,
        .payload = body,.payload_bytes = size};
    OK(candidate_wal_append(fx->w,&r,&end)); CHECK(end == expected);
    return r;
}

static candidate_wal_record_t chain(cf_t *c, fx_txn_t *t, uint16_t type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t r = append(c,type,t->id,t->last,body,size);
    if (t->records == 0) t->first = r.lsn;
    t->last = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r);
    return r;
}

static void count_start(cf_t *c, uint64_t id)
{
    c->tally.transactions++;
    if (id > c->tally.max_started) c->tally.max_started = id;
}

static void ledger_admit(cf_t *c, const fx_txn_t *t)
{
    if (c->ledger_count == c->ledger_capacity) {
        c->ledger_capacity = c->ledger_capacity == 0 ? 1024 : c->ledger_capacity * 2;
        c->ledger = realloc(c->ledger,c->ledger_capacity * sizeof(*c->ledger)); CHECK(c->ledger != NULL);
    }
    cf_admission_t *a = &c->ledger[c->ledger_count++];
    *a = (cf_admission_t){.id = t->id,.epoch = t->epoch,.limit = t->limit,.owner = fx_of(c)->owner};
    memcpy(a->nonce,t->nonce,16); memcpy(a->operation,t->operation,32);
}

static void ledger_end(cf_t *c, const fx_txn_t *t)
{
    if (!t->has_admission) { c->last_plain = t->id; return; }
    cf_admission_t *a = NULL;
    for (uint32_t i = c->ledger_count; a == NULL && i > 0; i--) if (c->ledger[i - 1].id == t->id) a = &c->ledger[i - 1];
    CHECK(a != NULL);
    a->is_terminal = true; a->terminal_end = t->terminal_end; a->result_bytes = t->result_bytes; a->chunks = t->chunks;
    a->result = t->result; memcpy(a->result_digest,t->result_digest,32);
}

void cf_begin(cf_t *c, fx_txn_t *t, uint64_t id)
{
    memset(t,0,sizeof(*t)); fx_begin(fx_of(c),t,id); count_start(c,id);
}

void cf_admit(cf_t *c, fx_txn_t *t, uint64_t id, uint64_t limit, const uint8_t *nonce)
{
    CHECK(c->epoch < CF_EPOCHS);
    if (nonce == NULL) fx_admit(fx_of(c),t,id,c->epoch,limit);
    else {
        memset(t,0,sizeof(*t)); t->id = id; t->epoch = c->epoch; t->limit = limit; t->has_admission = true;
        memcpy(t->nonce,nonce,16);
        for (unsigned i = 0; i < 32; i++) t->operation[i] = (uint8_t)(id * 13 + i * 7 + 1);
        OK(candidate_digest_sha256(NULL,0,t->result_digest));
        uint8_t body[64] = {0}; write_le(body,c->epoch,8); memcpy(body + 8,nonce,16); memcpy(body + 24,t->operation,32);
        write_le(body + 56,limit,8); chain(c,t,1,body,sizeof(body));
    }
    fx_begin(fx_of(c),t,id); count_start(c,id); c->tally.admitted[c->epoch]++;
    ledger_admit(c,t);
}

void cf_end(cf_t *c, fx_txn_t *t, bool is_commit)
{
    if (is_commit) { fx_commit(fx_of(c),t); c->tally.commits++; }
    else { fx_abort(fx_of(c),t); c->tally.aborts++; }
    ledger_end(c,t);
}

void cf_delta(cf_t *c, fx_txn_t *t, uint32_t size)
{
    fx_delta_group(fx_of(c),t,c->delta_page,64,c->delta_body,size); c->tally.groups++;
}

void cf_chunk(cf_t *c, fx_txn_t *t, const cf_chunk_t *k)
{
    uint8_t body[112] = {0}; write_le(body,t->epoch,8); memcpy(body + 8,t->nonce,16);
    write_le(body + 24,k->index,4); write_le(body + 28,k->count,4); write_le(body + 32,k->total,8); memcpy(body + 40,k->digest,32);
    write_le(body + 72,k->ref.page.id,8); write_le(body + 80,k->ref.page.generation,8); write_le(body + 88,k->ref.slot,4);
    write_le(body + 92,k->ref.slot_generation,4); write_le(body + 96,k->offset,8); write_le(body + 104,k->length,4);
    chain(c,t,9,body,sizeof(body));
}

void cf_group_open(cf_t *c, fx_txn_t *t, cf_group_t *g)
{
    fx_t *fx = fx_of(c); const fx_page_t *page = &fx->pages[c->delta_page];
    g->id = ++fx->group_id;
    uint8_t begin[16] = {0}; write_le(begin,g->id,8); write_le(begin + 8,1,4); write_le(begin + 12,1,4);
    candidate_wal_record_t r = chain(c,t,3,begin,sizeof(begin)); g->crc = candidate_wal_witness_crc(0,&r);
    uint8_t delta[56] = {0}; write_le(delta,g->id,8); write_le(delta + 8,c->delta_page,8); write_le(delta + 16,page->generation,8);
    write_le(delta + 24,page->lsn,8); write_le(delta + 32,1,4); write_le(delta + 40,64,4); write_le(delta + 44,8,4);
    memcpy(delta + 48,c->delta_body,8);
    r = chain(c,t,5,delta,sizeof(delta)); g->crc = candidate_wal_witness_crc(g->crc,&r);
}

void cf_group_close(cf_t *c, fx_txn_t *t, const cf_group_t *g)
{
    CHECK(t->groups < FX_GROUP_DEPENDENCIES);
    uint8_t finish[24] = {0}; write_le(finish,g->id,8); write_le(finish + 8,1,4); write_le(finish + 12,1,4);
    write_le(finish + 16,g->crc,4);
    uint64_t start, end = fx_place(fx_position(fx_of(c)),sizeof(finish),&start);
    chain(c,t,8,finish,sizeof(finish));
    t->group_ids[t->groups] = g->id; t->group_ends[t->groups] = end; t->groups++;
    fx_of(c)->pages[c->delta_page].lsn = end; c->tally.groups++;
}

void cf_append_raw(cf_t *c, uint16_t type, uint64_t transaction_id, const uint8_t *body, size_t size)
{
    append(c,type,transaction_id,0,body,size);
}

uint64_t cf_unit(cf_t *c, bool is_admitted, bool is_commit)
{
    fx_txn_t t; uint64_t id = c->next_id++;
    if (is_admitted) cf_admit(c,&t,id,64,NULL);
    else cf_begin(c,&t,id);
    cf_delta(c,&t,8);
    cf_end(c,&t,is_commit);
    return id;
}

cf_chunk_t cf_carry_chunk(const cf_carried_t *k, uint32_t index)
{
    uint64_t offset = (uint64_t)index * 65536, rest = CF_CARRIED_BYTES - offset;
    cf_chunk_t chunk = {.ref = k->ref,.total = CF_CARRIED_BYTES,.offset = offset,.length = rest < 65536 ? rest : 65536,
        .index = index,.count = 2};
    memcpy(chunk.digest,k->digest,32);
    return chunk;
}

void cf_carry_start(cf_t *c, cf_carried_t *k)
{
    memset(k,0,sizeof(*k));
    uint8_t *body = malloc(CF_CARRIED_BYTES); CHECK(body != NULL); fx_payload(77,CF_CARRIED_BYTES,body);
    OK(candidate_digest_sha256(body,CF_CARRIED_BYTES,k->digest)); free(body);
    k->ref = (candidate_version_ref_t){{c->delta_page,1},0,1};
    cf_admit(c,&k->t,c->next_id++,CF_CARRIED_BYTES + 16,NULL);
    cf_delta(c,&k->t,8);
    cf_chunk_t chunk = cf_carry_chunk(k,0); cf_chunk(c,&k->t,&chunk);
    k->is_active = true;
}

void cf_carry_last(cf_t *c, cf_carried_t *k)
{
    cf_chunk_t chunk = cf_carry_chunk(k,1); cf_chunk(c,&k->t,&chunk);
    k->t.result = k->ref; k->t.result_bytes = CF_CARRIED_BYTES; k->t.chunks = 2; memcpy(k->t.result_digest,k->digest,32);
}

void cf_carry_abort(cf_t *c, cf_carried_t *k)
{
    cf_carry_last(c,k); cf_end(c,&k->t,false);
    k->is_active = false;
}

uint64_t cf_room(const cf_t *c)
{
    uint64_t used = fx_position(fx_of(c)) % CANDIDATE_WAL_SEGMENT_BYTES;
    return used == 0 ? 0 : CANDIDATE_WAL_SEGMENT_BYTES - used;
}

static uint32_t churn_images(const cf_t *c) { return c->is_churn_free ? 2 + CF_CHURN_PAGES : 2; }

uint64_t cf_churn_bytes(const cf_t *c)
{
    const fx_t *fx = fx_of(c);
    return fx_group_end(fx,churn_images(c),CF_CHURN_PAGES) - fx_position(fx);
}

static void encode_allocation(cf_t *c, uint64_t end, uint64_t page_highwater, fx_image_t *root, fx_image_t *node)
{
    fx_t *fx = fx_of(c); const candidate_page_ref_t nodes[1] = {{2,1}};
    root->id = 1; root->generation = 1; root->previous = fx->pages[1].lsn;
    fx_encode_alloc_root(fx,1,end,fx->owner,page_highwater,1,nodes,root->page);
    if (node == NULL) return;
    node->id = 2; node->generation = 1; node->previous = fx->pages[2].lsn;
    fx_encode_alloc_node(fx,0,2,end,node->page);
}

static void transition(cf_t *c, uint32_t i, uint64_t end, fx_event_t *event)
{
    uint64_t id = c->churn_first + i, generation = c->generation[i];
    candidate_allocation_entry_t *e = fx_alloc_find(fx_of(c),id); CHECK(e != NULL && e->ref.generation == generation);
    e->transition_lsn = end;
    if (!c->is_churn_free) {
        *event = (fx_event_t){7,id,generation,generation,1};
        e->state = CANDIDATE_ALLOCATION_FREE; e->release_horizon = 1;
        return;
    }
    *event = (fx_event_t){6,id,generation,generation + 1,0};
    e->state = CANDIDATE_ALLOCATION_LIVE; e->release_horizon = 0; e->ref.generation = generation + 1;
    c->generation[i] = generation + 1;
    fx_image_t *image = &cf_images[2 + i];
    image->id = id; image->generation = generation + 1; image->previous = 0;
    candidate_page_location_t l = fx_location(id,generation + 1,end);
    OK(candidate_page_encode_versions(&l,NULL,0,image->page,CANDIDATE_PAGE_BYTES));
}

void cf_churn(cf_t *c)
{
    static fx_event_t events[CF_CHURN_PAGES];
    fx_t *fx = fx_of(c); uint32_t images = churn_images(c);
    uint64_t end = fx_group_end(fx,images,CF_CHURN_PAGES);
    for (uint32_t i = 0; i < CF_CHURN_PAGES; i++) transition(c,i,end,&events[i]);
    encode_allocation(c,end,c->page_highwater,&cf_images[0],&cf_images[1]);
    CHECK(fx_group(fx,NULL,cf_images,images,events,CF_CHURN_PAGES,true,true) == end);
    c->is_churn_free = !c->is_churn_free; c->last_churn_end = end;
    c->tally.groups++; c->tally.system_groups++; c->tally.events += CF_CHURN_PAGES;
}

static void encode_epoch_root(cf_t *c, uint64_t end, uint64_t epoch, uint64_t floor, fx_image_t *image)
{
    fx_t *fx = fx_of(c);
    candidate_catalog_root_t root = {.owner_epoch = fx->owner,.current_epoch = epoch,.retention_floor = floor,
        .transaction_highwater = c->transaction_highwater,.page_highwater = c->page_highwater,.visible_csn = fx->highest_csn,
        .retained_bytes = CANDIDATE_PAGE_BYTES};
    candidate_page_location_t l = fx_location(c->epoch_root,1,end);
    image->id = c->epoch_root; image->generation = 1; image->previous = fx->pages[c->epoch_root].lsn;
    OK(candidate_catalog_encode_root(&l,&root,image->page,CANDIDATE_PAGE_BYTES));
}

void cf_epoch(cf_t *c, uint64_t next_floor)
{
    fx_t *fx = fx_of(c);
    uint64_t end = fx_group_end(fx,1,1);
    encode_epoch_root(c,end,c->epoch + 1,next_floor,&cf_images[0]);
    fx_event_t event = {15,c->epoch,c->epoch + 1,c->floor,next_floor};
    CHECK(fx_group(fx,NULL,cf_images,1,&event,1,true,true) == end);
    c->epoch++; c->floor = next_floor;
    c->tally.groups++; c->tally.system_groups++; c->tally.events++;
}

void cf_reserve(cf_t *c, uint64_t previous_transaction, uint64_t next_transaction, uint64_t next_page)
{
    fx_t *fx = fx_of(c);
    uint64_t end = fx_group_end(fx,2,1), previous_page = c->page_highwater;
    c->transaction_highwater = next_transaction; c->page_highwater = next_page;
    encode_epoch_root(c,end,c->epoch,c->floor,&cf_images[0]);
    encode_allocation(c,end,next_page,&cf_images[1],NULL);
    fx_event_t event = {16,previous_transaction,next_transaction,previous_page,next_page};
    CHECK(fx_group(fx,NULL,cf_images,2,&event,1,true,true) == end);
    c->last_reserve_end = end;
    c->tally.groups++; c->tally.system_groups++; c->tally.events++;
}

void cf_filler(cf_t *c, uint64_t bytes)
{
    CHECK(bytes == PLAIN_FILL_BYTES || (bytes >= DELTA_FILL_MIN && bytes <= DELTA_FILL_MAX && bytes % 8 == 0));
    uint64_t end = fx_position(fx_of(c)) + bytes;
    fx_txn_t t; cf_begin(c,&t,c->next_id++);
    if (bytes != PLAIN_FILL_BYTES) cf_delta(c,&t,(uint32_t)(bytes - DELTA_FILL_BASE));
    cf_end(c,&t,false);
    CHECK(fx_position(fx_of(c)) == end);
}

void cf_pad_to(cf_t *c, uint64_t room)
{
    CHECK(room % 8 == 0 && (room == 0 || cf_room(c) >= room));
    while (cf_room(c) - room > DELTA_FILL_MAX)
        cf_filler(c,cf_room(c) - room - DELTA_FILL_MAX >= DELTA_FILL_MIN ? DELTA_FILL_MAX : PLAIN_FILL_BYTES);
    if (cf_room(c) > room) cf_filler(c,cf_room(c) - room);
    CHECK(cf_room(c) == room);
}

void cf_pad(cf_t *c) { cf_pad_to(c,0); }

void cf_cross_segment(cf_t *c)
{
    unsigned segments = candidate_wal_info(c->f.wal).segments;
    while (candidate_wal_info(c->f.wal).segments == segments) {
        fx_txn_t t; cf_begin(c,&t,c->next_id++); cf_delta(c,&t,CANDIDATE_PAGE_BYTES - 64); cf_end(c,&t,false);
    }
}

static void restamp(cf_t *c, uint64_t id, uint64_t lsn)
{
    uint8_t page[CANDIDATE_PAGE_BYTES], out[CANDIDATE_PAGE_BYTES]; candidate_page_info_t info;
    read_page(c,id,page,&info);
    if (info.location.lsn == lsn) return;
    candidate_page_location_t l = info.location; l.lsn = lsn;
    OK(candidate_page_encode_body(&l,info.type,page + 64,info.body_bytes,out,sizeof(out)));
    CHECK(memcmp(out + 64,page + 64,CANDIDATE_PAGE_BYTES - 64) == 0);
    CHECK(pwrite(c->f.data_fd,out,sizeof(out),(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)) == (ssize_t)sizeof(out));
    CHECK(fsync(c->f.data_fd) == 0);
}

void cf_flush(cf_t *c)
{
    OK(candidate_wal_force(c->f.wal,candidate_wal_info(c->f.wal).valid_end_lsn));
    restamp(c,c->delta_page,fx_of(c)->pages[c->delta_page].lsn);
}

uint64_t cf_window_admissions(const cf_t *c)
{
    uint64_t total = 0;
    for (uint64_t e = c->floor; e < CF_EPOCHS; e++) total += c->tally.admitted[e];
    return total;
}

uint64_t cf_suffix_records(const candidate_wal_info_t *before, const candidate_wal_info_t *after)
{
    return after->records - before->records;
}

uint64_t cf_suffix_bytes(const candidate_wal_info_t *before, const candidate_wal_info_t *after)
{
    return after->valid_end_lsn - before->valid_end_lsn;
}

typedef struct { uint64_t from, to, records, frame_bytes; } frame_sum_t;

static bool sum_frames(void *context, const candidate_wal_record_t *r)
{
    frame_sum_t *s = context;
    if (r->lsn < s->from || r->lsn >= s->to) return true;
    s->records++; s->frame_bytes += (96 + r->payload_bytes + 7) & ~(uint64_t)7;
    return true;
}

cf_suffix_t cf_suffix_of(cf_t *c, const candidate_wal_info_t *before, const candidate_wal_info_t *after)
{
    CHECK(!cf_io.is_armed && before->segments <= after->segments);
    frame_sum_t sum = {.from = before->valid_end_lsn,.to = after->valid_end_lsn};
    candidate_wal_info_t info; OK(candidate_wal_scan(c->f.wal,sum_frames,&sum,&info));
    uint64_t total = cf_suffix_bytes(before,after);
    cf_suffix_t s = {.records = sum.records,.frame_bytes = sum.frame_bytes,
        .header_bytes = (uint64_t)(after->segments - before->segments) * CANDIDATE_WAL_HEADER_BYTES};
    CHECK(s.frame_bytes + s.header_bytes <= total);
    s.padding_bytes = total - s.frame_bytes - s.header_bytes;
    return s;
}

cf_suffix_t cf_expect_reads(const char *tag, cf_t *c, const candidate_wal_info_t *before, const candidate_wal_info_t *after)
{
    cf_io_t io = cf_io;
    cf_suffix_t s = cf_suffix_of(c,before,after);
    uint64_t expected = s.records * CANDIDATE_WAL_RECORD_HEADER_BYTES + s.frame_bytes + s.header_bytes + s.padding_bytes;
    bool is_empty = before->valid_end_lsn == after->valid_end_lsn;
    printf("SUFFIX_READS %s records=%llu frame_bytes=%llu header_bytes=%llu padding_bytes=%llu expected_read_bytes=%llu reads=%llu "
        "read_bytes=%llu lowest=%llu cut=%llu end=%llu denied=%llu basis=frame-head80-plus-frame-padding-header\n",tag,
        (unsigned long long)s.records,(unsigned long long)s.frame_bytes,(unsigned long long)s.header_bytes,
        (unsigned long long)s.padding_bytes,(unsigned long long)expected,(unsigned long long)io.reads,
        (unsigned long long)io.read_bytes,(unsigned long long)io.lowest_lsn,(unsigned long long)before->valid_end_lsn,
        (unsigned long long)after->valid_end_lsn,(unsigned long long)io.denied);
    CHECK(io.denied == 0 && io.failed == 0 && s.records == cf_suffix_records(before,after) && io.read_bytes == expected);
    CHECK(is_empty ? io.reads == 0 : io.lowest_lsn == before->valid_end_lsn);
    return s;
}

bool cf_semantic_of(const candidate_recovery_view_t *v, uint64_t id, candidate_semantic_transaction_t *out)
{
    const candidate_wal_witness_t *w = candidate_recovery_view_witness(v); uint32_t index;
    return candidate_witness_index(w,id,&index) && candidate_witness_transaction_at(w,index,out);
}

bool cf_admission_of(const candidate_recovery_view_t *v, uint64_t id, candidate_semantic_admission_t *out)
{
    candidate_semantic_admission_t a;
    for (uint32_t i = 0; candidate_admissions_at(v->admissions,i,&a); i++)
        if (a.transaction_id == id) { if (out != NULL) *out = a; return true; }
    return false;
}

void cf_expect_history(const cf_t *c, const candidate_recovery_closure_t *k)
{
    const candidate_recovery_history_t *h = &k->history; const cf_tally_t *t = &c->tally;
    printf("HISTORY closure_txns=%llu/%llu commits=%llu/%llu aborts=%llu/%llu groups=%llu/%llu system_groups=%llu/%llu "
        "events=%llu/%llu max_started=%llu/%llu tombstones=%u/%llu\n",(unsigned long long)h->transactions,
        (unsigned long long)t->transactions,(unsigned long long)h->commits,(unsigned long long)t->commits,
        (unsigned long long)h->aborts,(unsigned long long)t->aborts,(unsigned long long)h->groups,(unsigned long long)t->groups,
        (unsigned long long)h->system_groups,(unsigned long long)t->system_groups,(unsigned long long)h->events,
        (unsigned long long)t->events,(unsigned long long)h->max_started_id,(unsigned long long)t->max_started,k->tombstones,
        (unsigned long long)cf_window_admissions(c));
    CHECK(h->transactions == t->transactions && h->commits == t->commits && h->aborts == t->aborts);
    CHECK(h->groups == t->groups && h->system_groups == t->system_groups && h->events == t->events);
    CHECK(h->max_started_id == t->max_started && k->tombstones == cf_window_admissions(c));
}

static bool is_same_ref(candidate_version_ref_t a, candidate_version_ref_t b)
{
    return a.page.id == b.page.id && a.page.generation == b.page.generation && a.slot == b.slot &&
        a.slot_generation == b.slot_generation;
}

static void expect_admission(const candidate_recovery_view_t *v, const cf_admission_t *a, bool is_retained)
{
    candidate_semantic_transaction_t s = {0}; candidate_semantic_admission_t m = {0};
    bool has_transaction = cf_semantic_of(v,a->id,&s), has_admission = cf_admission_of(v,a->id,&m);
    if (!is_retained) { CHECK(!has_transaction && !has_admission); return; }
    const candidate_witness_transaction_t *t = &s.value;
    CHECK(has_transaction && has_admission && t->has_admission && t->request_epoch == a->epoch);
    CHECK(memcmp(t->request_nonce,a->nonce,16) == 0 && memcmp(t->operation_digest,a->operation,32) == 0);
    CHECK(s.owner_epoch == a->owner && s.result_limit == a->limit && m.owner_epoch == a->owner && m.result_limit == a->limit);
    if (!a->is_terminal) { CHECK(t->state == CANDIDATE_WITNESS_UNFINISHED); return; }
    CHECK(t->state == CANDIDATE_WITNESS_ABORT && t->csn == 0 && t->terminal_end_lsn == a->terminal_end);
    CHECK(t->result_bytes == a->result_bytes && t->result_chunks == a->chunks && s.chunk_bytes == a->result_bytes);
    CHECK(memcmp(t->result_digest,a->result_digest,32) == 0 && m.has_result == (a->chunks > 0));
    if (m.has_result) CHECK(is_same_ref(m.result,a->result));
}

static uint32_t catalog_count(const cf_t *c, char state)
{
    uint32_t count = 0;
    for (unsigned i = 0; i < c->f.built.spec.requests; i++) count += c->f.built.spec.req[i].state == state;
    return count;
}

void cf_expect_resident(const cf_t *c, const candidate_recovery_view_t *v, uint32_t plain_active)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v);
    uint32_t requests = c->f.built.spec.requests, retained = 0, active = 0;
    for (uint32_t i = 0; i < c->ledger_count; i++) {
        const cf_admission_t *a = &c->ledger[i];
        bool is_retained = a->epoch >= c->floor || !a->is_terminal;
        expect_admission(v,a,is_retained);
        retained += is_retained; active += !a->is_terminal;
    }
    uint32_t aborts = catalog_count(c,REQ_ABORT) + retained - active;
    printf("RESIDENT txns=%u/%u admissions=%u/%u unfinished=%u/%u aborts=%u/%u commits=%u/%u ledger=%u retained=%u "
        "epoch=%llu floor=%llu\n",k.transactions,requests + retained + plain_active,k.admissions,requests + retained,k.unfinished,
        active + plain_active,k.aborts,aborts,k.commits,catalog_count(c,REQ_COMMIT),c->ledger_count,retained,
        (unsigned long long)c->epoch,(unsigned long long)c->floor);
    CHECK(k.transactions == requests + retained + plain_active && k.admissions == requests + retained);
    CHECK(k.unfinished == active + plain_active && k.aborts == aborts && k.commits == catalog_count(c,REQ_COMMIT));
    if (c->last_plain != 0) CHECK(candidate_wal_witness_find(candidate_recovery_view_witness(v),c->last_plain) == NULL);
}

static bool is_projected_abort(const cf_admission_t *a, const candidate_outcome_t *e)
{
    uint8_t empty[32]; OK(candidate_digest_sha256(NULL,0,empty));
    bool is_request = e->epoch == a->epoch && memcmp(e->nonce,a->nonce,16) == 0 && memcmp(e->operation_digest,a->operation,32) == 0;
    bool is_abort = e->state == CANDIDATE_OUTCOME_ABORTED && e->csn == 0 && e->terminal_end_lsn == a->terminal_end;
    bool is_admission = e->declared_result_limit == a->limit && e->admitting_owner_epoch == a->owner;
    bool is_empty = e->result.page.id == 0 && e->result_bytes == 0 && e->result_chunks == 0 && memcmp(e->result_digest,empty,32) == 0;
    return a->is_terminal && a->chunks == 0 && is_request && is_abort && is_admission && is_empty;
}

static const candidate_outcome_t *rebuilt_of(const candidate_outcome_rebuild_t *rebuild, uint32_t count, uint64_t id)
{
    for (uint32_t n = 0; n < count; n++)
        if (candidate_outcome_rebuild_at(rebuild,n)->transaction_id == id) return candidate_outcome_rebuild_at(rebuild,n);
    return NULL;
}

void cf_expect_rebuild(cf_t *c)
{
    candidate_recovery_view_request_t r = cf_request(c);
    candidate_outcome_rebuild_options_t o = {.source = {.catalog = r.load,.checkpoint = &c->f.anchor},.owner_epoch = c->f.owner + 1};
    candidate_outcome_rebuild_t *rebuild = NULL;
    candidate_wal_status_t status = candidate_outcome_rebuild_load(c->f.wal,&o,&rebuild);
    candidate_outcome_rebuild_info_t info = candidate_outcome_rebuild_info(rebuild);
    uint32_t expected = 0, matched = 0;
    for (uint32_t i = 0; i < c->ledger_count; i++) {
        if (c->ledger[i].epoch < c->floor) continue;
        const candidate_outcome_t *e = rebuilt_of(rebuild,info.count,c->ledger[i].id);
        expected++; matched += e != NULL && is_projected_abort(&c->ledger[i],e);
    }
    printf("REBUILD status=%s entries=%u catalog=%u ledger_window=%u projected_aborts=%u epoch=%llu floor=%llu "
        "scope=below-legacy-4096\n",vf_status(status),info.count,c->f.built.spec.requests,expected,matched,
        (unsigned long long)info.current_epoch,(unsigned long long)info.retention_floor);
    candidate_outcome_rebuild_close(&rebuild);
    OK(status);
    CHECK(expected > 0 && matched == expected && info.count == c->f.built.spec.requests + expected);
}

void cf_expect_budget(const char *tag, const cf_t *c, size_t floor_bytes)
{
    struct rlimit stack; CHECK(getrlimit(RLIMIT_STACK,&stack) == 0);
    bool is_bounded = stack.rlim_cur != RLIM_INFINITY;
    cf_budget_t b = {.allocator_peak = c->f.memory.peak,.wal_resident = candidate_wal_info(c->f.wal).resident_bytes,
        .stack_bound = is_bounded ? (size_t)stack.rlim_cur : 0};
    b.total = b.allocator_peak + b.wal_resident + b.stack_bound;
    printf("BUDGET %s allocator_peak=%zu wal_resident=%zu stack_rlimit=%zu total=%zu limit=%u floor=%zu stack_bounded=%d "
        "basis=requested-bytes-not-rss\n",tag,b.allocator_peak,b.wal_resident,b.stack_bound,b.total,CF_ENVELOPE_BYTES,
        floor_bytes,is_bounded);
    CHECK(is_bounded && floor_bytes <= b.allocator_peak && b.total < CF_ENVELOPE_BYTES);
}

static candidate_page_result_t *read_body(cf_t *c, const candidate_outcome_t *e)
{
    if (e->result.page.id == 0) return NULL;
    candidate_result_read_t r = {.ref = e->result,.transaction_id = e->transaction_id,.result_bytes = e->result_bytes,
        .load = vf_load,.context = &c->f,.memory = &c->f.allocator};
    memcpy(r.uuid,fx_uuid,16); memcpy(r.digest,e->result_digest,32);
    candidate_page_result_t *body = NULL; OK(candidate_page_read_result(&r,&body));
    return body;
}

void cf_expect_outcomes(cf_t *c, const candidate_recovery_view_t *v)
{
    for (unsigned n = 0; n < c->f.built.spec.requests; n++) {
        candidate_catalog_query_t q = scen_query(&c->f.built,n); const candidate_outcome_t *e = NULL;
        candidate_catalog_lookup_t lookup = candidate_recovery_view_lookup(v,&q,&e);
        vf_expect_answer(&c->f,n,lookup,e);
        candidate_page_result_t *body = read_body(c,e); vf_expect_body(&c->f,n,body); candidate_page_result_close(&body);
    }
}

void cf_print(const char *tag, const cf_t *c, const candidate_recovery_view_t *v)
{
    candidate_recovery_closure_t k = candidate_recovery_view_closure(v); candidate_recovery_view_info_t i = candidate_recovery_view_info(v);
    printf("CLOSURE %s history_txns=%llu history_groups=%llu history_events=%llu resident_txns=%u unfinished=%u commits=%u "
        "aborts=%u groups=%u system_groups=%u events=%u admissions=%u intervals=%u tombstones=%u identity=%zu resident=%zu "
        "cut_end=%llu records=%llu segments=%u owner=%llu epoch=%llu floor=%llu live=%u free=%u versions=%llu\n",tag,
        (unsigned long long)k.history.transactions,(unsigned long long)k.history.groups,(unsigned long long)k.history.events,
        k.transactions,k.unfinished,k.commits,k.aborts,k.groups,k.system_groups,k.events,k.admissions,k.started_intervals,
        k.tombstones,k.identity_bytes,i.resident_bytes,(unsigned long long)i.cut.valid_end_lsn,(unsigned long long)i.cut.records,
        i.cut.segments,(unsigned long long)i.cut.highest_owner_epoch,(unsigned long long)c->epoch,(unsigned long long)c->floor,
        i.live_pages,i.free_pages,(unsigned long long)i.versions);
}
