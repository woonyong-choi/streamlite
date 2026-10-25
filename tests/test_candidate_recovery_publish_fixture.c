#include "test_candidate_recovery_publish_fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/candidate_digest.h"
#include "storage/candidate_wal_witness.h"

const uint8_t fx_uuid[16] = {0x51,0x7a,0x23,0x10,0x44,0x05,0x96,0x07,0x28,0x39,0x4a,0x5b,0x6c,0x7d,0x8e,0x9f};

static void write_le(uint8_t *p, uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t frame(size_t payload) { return (96 + payload + 7) & ~(uint64_t)7; }

uint64_t fx_place(uint64_t position, size_t payload_bytes, uint64_t *start)
{
    uint64_t total = frame(payload_bytes), base = position / CANDIDATE_WAL_SEGMENT_BYTES * CANDIDATE_WAL_SEGMENT_BYTES;
    if (position % CANDIDATE_WAL_SEGMENT_BYTES == 0) base = position - CANDIDATE_WAL_SEGMENT_BYTES;
    if (total > base + CANDIDATE_WAL_SEGMENT_BYTES - position) position = base + CANDIDATE_WAL_SEGMENT_BYTES + 64;
    *start = position;
    return position + total;
}

uint64_t fx_position(const fx_t *fx) { return candidate_wal_info(fx->w).valid_end_lsn; }

fx_t *fx_create(const char *name, uint64_t owner)
{
    fx_t *fx = calloc(1,sizeof(*fx)); CHECK(fx != NULL);
    CHECK(snprintf(fx->wal,sizeof(fx->wal),"build/%s",name) > 0);
    CHECK(snprintf(fx->control,sizeof(fx->control),"%s.control",fx->wal) > 0);
    CHECK(snprintf(fx->data,sizeof(fx->data),"%s.pages",fx->wal) > 0);
    candidate_wal_options_t options = {.owner_epoch = owner}; memcpy(options.uuid,fx_uuid,16);
    OK(candidate_wal_create(fx->wal,&options,&fx->w));
    fx->data_fd = open(fx->data,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(fx->data_fd >= 0);
    fx->owner = owner;
    return fx;
}

static fx_page_t *page_of(fx_t *fx, uint64_t id)
{
    CHECK(id != 0 && id < (1u << 24));
    if (id >= fx->page_slots) {
        uint64_t slots = fx->page_slots == 0 ? 1024 : fx->page_slots;
        while (slots <= id) slots *= 2;
        fx->pages = realloc(fx->pages,slots * sizeof(*fx->pages)); CHECK(fx->pages != NULL);
        memset(fx->pages + fx->page_slots,0,(slots - fx->page_slots) * sizeof(*fx->pages));
        fx->page_slots = slots;
    }
    return &fx->pages[id];
}

static candidate_wal_record_t append(fx_t *fx, uint16_t type, uint64_t txn, uint64_t previous, const uint8_t *body, size_t size)
{
    uint64_t start, expected = fx_place(fx_position(fx),size,&start), end;
    candidate_wal_record_t r = {.type = type,.lsn = start,.transaction_id = txn,.previous_lsn = previous,.owner_epoch = fx->owner,
        .payload = body,.payload_bytes = size};
    OK(candidate_wal_append(fx->w,&r,&end)); CHECK(end == expected);
    return r;
}

static void chain(fx_t *fx, fx_txn_t *t, uint16_t type, const uint8_t *body, size_t size)
{
    candidate_wal_record_t r = append(fx,type,t->id,t->last,body,size);
    if (t->records == 0) t->first = r.lsn;
    t->last = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r);
}

void fx_admit(fx_t *fx, fx_txn_t *t, uint64_t id, uint64_t epoch, uint64_t limit)
{
    memset(t,0,sizeof(*t)); t->id = id; t->epoch = epoch; t->limit = limit; t->has_admission = true;
    write_le(t->nonce,id * 7919 + epoch,8); write_le(t->nonce + 8,0xa5a5a5a5u ^ id,8);
    for (unsigned i = 0; i < 32; i++) t->operation[i] = (uint8_t)(id * 13 + i * 7 + 1);
    OK(candidate_digest_sha256(NULL,0,t->result_digest));
    uint8_t body[64] = {0}; write_le(body,epoch,8); memcpy(body + 8,t->nonce,16); memcpy(body + 24,t->operation,32);
    write_le(body + 56,limit,8);
    chain(fx,t,1,body,sizeof(body));
}

void fx_begin(fx_t *fx, fx_txn_t *t, uint64_t id)
{
    if (!t->has_admission) { memset(t,0,sizeof(*t)); t->id = id; }
    uint8_t body[16] = {0}; chain(fx,t,2,body,sizeof(body)); t->has_begin = true;
}

void fx_chunks(fx_t *fx, fx_txn_t *t, candidate_version_ref_t ref, const uint8_t *bytes, size_t size)
{
    CHECK(size > 0 && size <= t->limit);
    uint8_t digest[32]; OK(candidate_digest_sha256(bytes,size,digest));
    uint32_t count = (uint32_t)((size + 65535) / 65536);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t body[112] = {0}; write_le(body,t->epoch,8); memcpy(body + 8,t->nonce,16);
        write_le(body + 24,i,4); write_le(body + 28,count,4); write_le(body + 32,size,8); memcpy(body + 40,digest,32);
        write_le(body + 72,ref.page.id,8); write_le(body + 80,ref.page.generation,8); write_le(body + 88,ref.slot,4);
        write_le(body + 92,ref.slot_generation,4); write_le(body + 96,(uint64_t)i * 65536,8);
        write_le(body + 104,size - (uint64_t)i * 65536 < 65536 ? size - (uint64_t)i * 65536 : 65536,4);
        chain(fx,t,9,body,sizeof(body));
    }
    t->result = ref; t->result_bytes = size; t->chunks = count; memcpy(t->result_digest,digest,32);
}

void fx_commit(fx_t *fx, fx_txn_t *t)
{
    CHECK(t->groups <= FX_GROUP_DEPENDENCIES);
    size_t size = 152 + (size_t)t->groups * 16; uint8_t body[152 + FX_GROUP_DEPENDENCIES * 16] = {0};
    write_le(body,t->first,8); write_le(body + 8,t->last,8); write_le(body + 16,t->records,8); write_le(body + 24,t->crc,4);
    t->csn = ++fx->highest_csn; write_le(body + 32,t->csn,8);
    if (t->has_admission) {
        write_le(body + 40,t->epoch,8); memcpy(body + 48,t->nonce,16); memcpy(body + 64,t->operation,32);
        memcpy(body + 96,t->result_digest,32); write_le(body + 128,t->result_bytes,8); write_le(body + 136,t->chunks,4);
    }
    write_le(body + 148,t->groups,4);
    for (uint32_t i = 0; i < t->groups; i++) { write_le(body + 152 + i * 16,t->group_ids[i],8); write_le(body + 160 + i * 16,t->group_ends[i],8); }
    uint64_t start; t->terminal_end = fx_place(fx_position(fx),size,&start);
    chain(fx,t,10,body,size);
}

void fx_abort(fx_t *fx, fx_txn_t *t)
{
    uint8_t body[32] = {0};
    write_le(body,t->first,8); write_le(body + 8,t->last,8); write_le(body + 16,t->records,8); write_le(body + 24,t->crc,4);
    uint64_t start; t->terminal_end = fx_place(fx_position(fx),sizeof(body),&start);
    chain(fx,t,11,body,sizeof(body));
}

uint64_t fx_group_end(const fx_t *fx, unsigned images, unsigned events)
{
    uint64_t start, at = fx_place(fx_position(fx),16,&start);
    for (unsigned i = 0; i < images; i++) at = fx_place(at,32 + CANDIDATE_PAGE_BYTES,&start);
    for (unsigned i = 0; i < events; i++) at = fx_place(at,40,&start);
    return fx_place(at,24,&start);
}

candidate_page_location_t fx_location(uint64_t id, uint64_t generation, uint64_t lsn)
{
    candidate_page_location_t l = {.ref = {id,generation},.lsn = lsn}; memcpy(l.uuid,fx_uuid,16); return l;
}

static void write_page(fx_t *fx, uint64_t id, const uint8_t *page)
{
    CHECK(pwrite(fx->data_fd,page,CANDIDATE_PAGE_BYTES,(off_t)((id - 1) * CANDIDATE_PAGE_BYTES)) == CANDIDATE_PAGE_BYTES);
}

uint64_t fx_group(fx_t *fx, fx_txn_t *t, fx_image_t *images, unsigned image_count,
    const fx_event_t *events, unsigned event_count, bool complete, bool write_data)
{
    CHECK(image_count + event_count >= 1 && image_count <= 64 && image_count + event_count <= 128);
    uint64_t id = ++fx->group_id, end = fx_group_end(fx,image_count,event_count);
    uint64_t txn = t == NULL ? 0 : t->id;
    uint8_t begin[16] = {0}; write_le(begin,id,8); write_le(begin + 8,image_count,4); write_le(begin + 12,image_count + event_count,4);
    uint64_t previous = t == NULL ? 0 : t->last;
    candidate_wal_record_t r = append(fx,3,txn,previous,begin,sizeof(begin));
    uint32_t crc = candidate_wal_witness_crc(0,&r);
    if (t != NULL) { if (t->records == 0) t->first = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r); }
    previous = r.lsn;
    static uint8_t payload[32 + CANDIDATE_PAGE_BYTES];
    for (unsigned i = 0; i < image_count; i++) {
        fx_image_t *image = &images[i];
        write_le(payload,id,8); write_le(payload + 8,image->id,8); write_le(payload + 16,image->generation,8);
        write_le(payload + 24,image->previous,8); memcpy(payload + 32,image->page,CANDIDATE_PAGE_BYTES);
        r = append(fx,4,txn,previous,payload,sizeof(payload)); crc = candidate_wal_witness_crc(crc,&r); previous = r.lsn;
        if (t != NULL) { t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r); }
    }
    for (unsigned i = 0; i < event_count; i++) {
        uint8_t body[40] = {0}; write_le(body,id,8); write_le(body + 8,events[i].a,8); write_le(body + 16,events[i].b,8);
        write_le(body + 24,events[i].c,8); write_le(body + 32,events[i].d,8);
        r = append(fx,events[i].type,txn,previous,body,sizeof(body)); crc = candidate_wal_witness_crc(crc,&r); previous = r.lsn;
        if (t != NULL) { t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r); }
    }
    if (t != NULL) t->last = previous;
    if (!complete) return 0;
    uint8_t finish[24] = {0}; write_le(finish,id,8); write_le(finish + 8,image_count,4);
    write_le(finish + 12,image_count + event_count,4); write_le(finish + 16,crc,4);
    r = append(fx,8,txn,previous,finish,sizeof(finish)); CHECK(fx_position(fx) == end);
    if (t != NULL) {
        t->last = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r);
        if (t->groups < FX_GROUP_DEPENDENCIES) { t->group_ids[t->groups] = id; t->group_ends[t->groups] = end; }
        t->groups++;
    }
    for (unsigned i = 0; i < image_count; i++) {
        fx_page_t *p = page_of(fx,images[i].id); p->generation = images[i].generation; p->lsn = end;
        p->has_data = write_data;
        if (write_data) write_page(fx,images[i].id,images[i].page);
    }
    return end;
}

uint64_t fx_delta_group(fx_t *fx, fx_txn_t *t, uint64_t id, uint32_t offset, const uint8_t *bytes, uint32_t size)
{
    fx_page_t *page = page_of(fx,id); CHECK(page->generation != 0 && size > 0 && size <= CANDIDATE_PAGE_BYTES - 64);
    uint64_t group = ++fx->group_id, start, at = fx_place(fx_position(fx),16,&start);
    at = fx_place(at,48 + size,&start); uint64_t end = fx_place(at,24,&start);
    uint64_t txn = t == NULL ? 0 : t->id, previous = t == NULL ? 0 : t->last;
    uint8_t begin[16] = {0}; write_le(begin,group,8); write_le(begin + 8,1,4); write_le(begin + 12,1,4);
    candidate_wal_record_t r = append(fx,3,txn,previous,begin,sizeof(begin));
    uint32_t crc = candidate_wal_witness_crc(0,&r);
    if (t != NULL) { if (t->records == 0) t->first = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r); }
    static uint8_t delta[48 + CANDIDATE_PAGE_BYTES];
    write_le(delta,group,8); write_le(delta + 8,id,8); write_le(delta + 16,page->generation,8); write_le(delta + 24,page->lsn,8);
    write_le(delta + 32,1,4); write_le(delta + 36,0,4); write_le(delta + 40,offset,4); write_le(delta + 44,size,4);
    memcpy(delta + 48,bytes,size);
    r = append(fx,5,txn,r.lsn,delta,48 + size); crc = candidate_wal_witness_crc(crc,&r);
    if (t != NULL) { t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r); }
    uint8_t finish[24] = {0}; write_le(finish,group,8); write_le(finish + 8,1,4); write_le(finish + 12,1,4); write_le(finish + 16,crc,4);
    r = append(fx,8,txn,r.lsn,finish,sizeof(finish)); CHECK(fx_position(fx) == end);
    if (t != NULL) {
        t->last = r.lsn; t->records++; t->crc = candidate_wal_witness_crc(t->crc,&r);
        if (t->groups < FX_GROUP_DEPENDENCIES) { t->group_ids[t->groups] = group; t->group_ends[t->groups] = end; }
        t->groups++;
    }
    page->lsn = end; page->has_data = false;
    return end;
}

void fx_payload(uint64_t seed, size_t size, uint8_t *out)
{
    for (size_t i = 0; i < size; i++) out[i] = (uint8_t)((seed * 31 + i * 7 + (i >> 8)) % 251);
}

candidate_allocation_entry_t *fx_alloc_find(fx_t *fx, uint64_t id)
{
    for (uint32_t i = 0; i < fx->alloc_count; i++) if (fx->alloc[i].ref.id == id) return &fx->alloc[i];
    return NULL;
}

void fx_alloc_add(fx_t *fx, uint64_t id, uint64_t generation, candidate_allocation_state_t state,
    uint64_t horizon, uint64_t transition_lsn)
{
    CHECK(fx->alloc_count == 0 || fx->alloc[fx->alloc_count - 1].ref.id < id);
    if (fx->alloc_count == fx->alloc_capacity) {
        fx->alloc_capacity = fx->alloc_capacity == 0 ? 256 : fx->alloc_capacity * 2;
        fx->alloc = realloc(fx->alloc,fx->alloc_capacity * sizeof(*fx->alloc)); CHECK(fx->alloc != NULL);
    }
    fx->alloc[fx->alloc_count++] = (candidate_allocation_entry_t){{id,generation},state,horizon,transition_lsn};
}

void fx_encode_alloc_node(const fx_t *fx, uint32_t ordinal, uint64_t id, uint64_t lsn, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_allocation_node_t node = {.ordinal = ordinal};
    uint32_t start = ordinal * CANDIDATE_ALLOCATION_NODE_ENTRIES;
    CHECK(start < fx->alloc_count);
    node.count = fx->alloc_count - start < CANDIDATE_ALLOCATION_NODE_ENTRIES ? fx->alloc_count - start : CANDIDATE_ALLOCATION_NODE_ENTRIES;
    memcpy(node.entries,fx->alloc + start,node.count * sizeof(node.entries[0]));
    candidate_page_location_t l = fx_location(id,1,lsn);
    OK(candidate_allocation_encode_node(&l,&node,out,CANDIDATE_PAGE_BYTES));
}

void fx_encode_alloc_root(const fx_t *fx, uint64_t id, uint64_t lsn, uint64_t owner, uint64_t highwater,
    uint64_t gc, const candidate_page_ref_t *nodes, uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_allocation_root_t root = {.owner_epoch = owner,.page_highwater = highwater,.gc_horizon = gc,.count = fx->alloc_count};
    uint32_t count = (fx->alloc_count + CANDIDATE_ALLOCATION_NODE_ENTRIES - 1) / CANDIDATE_ALLOCATION_NODE_ENTRIES;
    memcpy(root.nodes,nodes,count * sizeof(nodes[0]));
    candidate_page_location_t l = fx_location(id,1,lsn);
    OK(candidate_allocation_encode_root(&l,&root,out,CANDIDATE_PAGE_BYTES));
}

void fx_checkpoint(fx_t *fx, candidate_control_state_t *a, const fx_txn_t *const *active, unsigned active_count)
{
    candidate_checkpoint_summary_t summary = {.anchor = *a,.transaction_count = active_count};
    summary.anchor.generation = 1; summary.anchor.slot = 0;
    summary.anchor.checkpoint_begin = a->redo_begin; summary.anchor.checkpoint_end = a->redo_begin + 4096;
    uint8_t body[CANDIDATE_CHECKPOINT_PAYLOAD], digest[32] = {0}; size_t size;
    OK(candidate_checkpoint_encode_begin(&summary,body,sizeof(body),&size));
    uint64_t start; fx_place(fx_position(fx),size,&start); a->checkpoint_begin = start;
    candidate_wal_record_t r = append(fx,12,0,0,body,size); uint64_t previous = r.lsn;
    OK(candidate_checkpoint_digest(digest,body,size,digest));
    unsigned chunks = (active_count + CANDIDATE_CHECKPOINT_ENTRIES - 1) / CANDIDATE_CHECKPOINT_ENTRIES;
    for (unsigned c = 0; c < chunks; c++) {
        candidate_checkpoint_chunk_t chunk = {.checkpoint_id = a->checkpoint_id,.kind = 2,.ordinal = c};
        chunk.count = active_count - c * 128 < 128 ? active_count - c * 128 : 128;
        for (unsigned i = 0; i < chunk.count; i++) {
            const fx_txn_t *t = active[c * 128 + i];
            chunk.entries.transactions[i] = (candidate_checkpoint_transaction_t){t->id,t->first,t->last,0,0,fx->owner,CANDIDATE_CHECKPOINT_ACTIVE};
        }
        OK(candidate_checkpoint_encode_chunk(&chunk,body,sizeof(body),&size));
        r = append(fx,13,0,previous,body,size); previous = r.lsn;
        OK(candidate_checkpoint_digest(digest,body,size,digest));
    }
    candidate_checkpoint_end_t last = {.checkpoint_id = a->checkpoint_id,.begin_lsn = a->checkpoint_begin,.chunks = chunks};
    memcpy(last.digest,digest,32);
    OK(candidate_checkpoint_encode_end(&last,body,sizeof(body),&size));
    append(fx,14,0,previous,body,size); a->checkpoint_end = fx_position(fx);
}

static candidate_wal_status_t accept(void *context, const candidate_control_state_t *anchor)
{
    (void)context; (void)anchor; return CANDIDATE_WAL_OK;
}

void fx_control(fx_t *fx, const candidate_control_state_t *anchors, unsigned count)
{
    candidate_control_options_t options = {.backend = CANDIDATE_CONTROL_WAL,.verify = accept}; memcpy(options.uuid,fx_uuid,16);
    candidate_control_t *control = NULL; OK(candidate_control_create(fx->control,&options,&control));
    for (unsigned i = 0; i < count; i++) OK(candidate_control_publish(control,&anchors[i]));
    OK(candidate_control_close(&control));
}

void fx_tail_garbage(fx_t *fx, size_t bytes)
{
    char name[300]; uint64_t end = fx_position(fx); unsigned segment = (unsigned)(end / CANDIDATE_WAL_SEGMENT_BYTES);
    CHECK(snprintf(name,sizeof(name),"%s.wal.%016" PRIx64,fx->wal,(uint64_t)segment) > 0);
    int fd = open(name,O_WRONLY); CHECK(fd >= 0);
    uint8_t body[16] = {0}, garbage[256]; size_t written;
    candidate_wal_record_t r = {.type = 2,.lsn = end,.transaction_id = 4000,.owner_epoch = fx->owner,.payload = body,.payload_bytes = sizeof(body)};
    OK(candidate_wal_encode(fx_uuid,&r,garbage,sizeof(garbage),&written)); CHECK(bytes < written);
    CHECK(pwrite(fd,garbage,bytes,(off_t)(end % CANDIDATE_WAL_SEGMENT_BYTES)) == (ssize_t)bytes);
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

void fx_close(fx_t *fx)
{
    if (fx->w != NULL) {
        OK(candidate_wal_force(fx->w,fx_position(fx))); OK(candidate_wal_close(&fx->w));
    }
    if (fx->data_fd >= 0) { CHECK(fsync(fx->data_fd) == 0); CHECK(close(fx->data_fd) == 0); fx->data_fd = -1; }
    int directory = open("build",O_RDONLY | O_DIRECTORY); CHECK(directory >= 0); CHECK(fsync(directory) == 0); CHECK(close(directory) == 0);
}

void fx_free(fx_t **out)
{
    fx_t *fx = *out; if (fx == NULL) return;
    if (fx->w != NULL || fx->data_fd >= 0) fx_close(fx);
    free(fx->pages); free(fx->alloc); free(fx); *out = NULL;
}

void fx_copy_file(const char *source, const char *target)
{
    int in = open(source,O_RDONLY); CHECK(in >= 0);
    int out = open(target,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(out >= 0);
    static uint8_t block[1 << 20]; ssize_t n; off_t at = 0;
    while ((n = pread(in,block,sizeof(block),at)) > 0) { CHECK(pwrite(out,block,(size_t)n,at) == n); at += n; }
    CHECK(n == 0); CHECK(fsync(out) == 0); CHECK(close(out) == 0); CHECK(close(in) == 0);
}

void fx_copy_set(const fx_t *fx, const char *tag, fx_t *out)
{
    memset(out,0,sizeof(*out)); out->data_fd = -1; out->owner = fx->owner;
    CHECK(snprintf(out->wal,sizeof(out->wal),"%s-%s",fx->wal,tag) > 0);
    CHECK(snprintf(out->control,sizeof(out->control),"%s.control",out->wal) > 0);
    CHECK(snprintf(out->data,sizeof(out->data),"%s.pages",out->wal) > 0);
    fx_copy_file(fx->control,out->control); fx_copy_file(fx->data,out->data);
    for (unsigned id = 0;; id++) {
        char source[300], target[300];
        CHECK(snprintf(source,sizeof(source),"%s.wal.%016" PRIx64,fx->wal,(uint64_t)id) > 0);
        CHECK(snprintf(target,sizeof(target),"%s.wal.%016" PRIx64,out->wal,(uint64_t)id) > 0);
        if (access(source,F_OK) != 0) break;
        fx_copy_file(source,target);
    }
    int directory = open("build",O_RDONLY | O_DIRECTORY); CHECK(directory >= 0); CHECK(fsync(directory) == 0); CHECK(close(directory) == 0);
}

uint64_t fx_file_bytes(const char *path)
{
    struct stat st; CHECK(stat(path,&st) == 0); return (uint64_t)st.st_size;
}

void fx_sha256_file(const char *path, uint8_t out[32])
{
    uint64_t size = fx_file_bytes(path); uint8_t *bytes = malloc(size + 1); CHECK(bytes != NULL);
    int fd = open(path,O_RDONLY); CHECK(fd >= 0);
    uint64_t at = 0; while (at < size) { ssize_t n = pread(fd,bytes + at,size - at,(off_t)at); CHECK(n > 0); at += (uint64_t)n; }
    CHECK(close(fd) == 0); OK(candidate_digest_sha256(bytes,size,out)); free(bytes);
}

candidate_recovery_options_t fx_options(const fx_t *fx, uint64_t owner)
{
    candidate_recovery_options_t o = {.owner_epoch = owner,.wal_prefix = fx->wal,.control_path = fx->control,.data_path = fx->data};
    memcpy(o.uuid,fx_uuid,16); return o;
}
