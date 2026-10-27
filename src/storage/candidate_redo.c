#include "storage/candidate_redo.h"

#include <stdlib.h>
#include <string.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_digest.h"
#include "storage/candidate_store.h"
#include "storage/candidate_wal_witness.h"
#include "storage/checksum.h"
#include "candidate_redo_state_internal.h"
#include "candidate_wal_internal.h"
#include "candidate_wal_witness_internal.h"

#define HISTORY_SLOTS CANDIDATE_REDO_HISTORY_SLOTS
#define DATA_PRESENT 1u
#define DATA_CORROBORATED 2u

typedef candidate_redo_history_t history_t;
typedef candidate_redo_source_t source_record_t;
typedef candidate_redo_stage_t staged_t;
typedef candidate_redo_fingerprint_t fingerprint_t;
typedef candidate_redo_workspace_t workspace_t;

static uint64_t read_le(const uint8_t *p, unsigned count)
{
    uint64_t value=0;
    for (unsigned i=0; i<count; i++) value|=(uint64_t)p[i]<<(8*i);
    return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned count)
{
    for (unsigned i=0; i<count; i++) p[i]=(uint8_t)(value>>(8*i));
}

uint32_t candidate_redo_page_crc(const uint8_t *p)
{
    const uint8_t zero[4]={0};
    uint32_t crc=crc32c(0,p,52);
    crc=crc32c(crc,zero,4);
    return crc32c(crc,p+56,CANDIDATE_PAGE_BYTES-56);
}

static candidate_wal_status_t check_page(const uint8_t uuid[16], const uint8_t *p, candidate_page_info_t *info)
{
    candidate_wal_status_t status=candidate_page_decode_header(uuid,p,8192,info);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (info->type==1 || info->type==2) return candidate_page_decode(uuid,p,8192,info);
    if (info->type==3) { candidate_store_node_t node; return candidate_store_decode(uuid,p,8192,&node); }
    unsigned kind=(unsigned)read_le(p+70,2);
    if (info->type==4) {
        if (kind==1) { candidate_catalog_root_t value; return candidate_catalog_decode_root(uuid,p,8192,&value); }
        if (kind==2) { candidate_catalog_node_t value; return candidate_catalog_decode_node(uuid,p,8192,&value); }
    }
    if (info->type==5) {
        if (kind==1) { candidate_allocation_root_t value; return candidate_allocation_decode_root(uuid,p,8192,&value); }
        if (kind==2) { candidate_allocation_node_t value; return candidate_allocation_decode_node(uuid,p,8192,&value); }
    }
    return CANDIDATE_WAL_UNSUPPORTED;
}

static bool hash_record(void *context, const candidate_wal_record_t *r)
{
    fingerprint_t *f=context;
    if (r->payload_bytes>CANDIDATE_WAL_RECORD_BYTES-80) { f->status=CANDIDATE_WAL_LIMIT; return false; }
    uint8_t *p=f->scratch;
    memcpy(p,f->digest,32); memset(p+32,0,48);
    write_le(p+32,r->type,2); write_le(p+40,r->lsn,8); write_le(p+48,r->transaction_id,8);
    write_le(p+56,r->previous_lsn,8); write_le(p+64,r->owner_epoch,8); write_le(p+72,r->payload_bytes,8);
    memcpy(p+80,r->payload,r->payload_bytes);
    f->status=candidate_digest_sha256(p,80+r->payload_bytes,f->digest);
    if (r->type==4 || r->type==5) f->page_records++;
    return f->status==CANDIDATE_WAL_OK;
}

static candidate_wal_status_t record_digest(const candidate_wal_record_t *r, uint8_t *scratch, uint8_t digest[32])
{
    if (r->payload_bytes>CANDIDATE_WAL_RECORD_BYTES-80) return CANDIDATE_WAL_LIMIT;
    memset(scratch,0,80);
    write_le(scratch,r->type,2); write_le(scratch+8,r->lsn,8); write_le(scratch+16,r->transaction_id,8);
    write_le(scratch+24,r->previous_lsn,8); write_le(scratch+32,r->owner_epoch,8); write_le(scratch+40,r->payload_bytes,8);
    memcpy(scratch+80,r->payload,r->payload_bytes);
    return candidate_digest_sha256(scratch,80+r->payload_bytes,digest);
}

static candidate_wal_status_t scan_status(candidate_wal_status_t status, const fingerprint_t *f)
{
    if (status==CANDIDATE_WAL_POISONED && f->status==CANDIDATE_WAL_CORRUPT) return CANDIDATE_WAL_CORRUPT;
    return status==CANDIDATE_WAL_STOPPED ? f->status : status;
}

static candidate_wal_status_t verify_source(candidate_wal_t *wal, const candidate_redo_options_t *options,
    uint8_t digest[32], bool allows_tail, uint64_t *page_records)
{
    const candidate_wal_memory_t *memory=options->memory;
    fingerprint_t f={0}; f.scratch=memory->allocate(memory->context,CANDIDATE_WAL_RECORD_BYTES);
    if (f.scratch==NULL) return CANDIDATE_WAL_NOMEM;
    candidate_witness_options_t request={.memory=memory,.enable_system_groups=true};
    memcpy(request.uuid,options->uuid,16); candidate_wal_witness_t *witness=NULL;
    candidate_wal_status_t status=candidate_witness_analyze_observed(wal,&request,options->checkpoint,hash_record,&f,&witness);
    status=scan_status(status,&f);
    if (status==CANDIDATE_WAL_OK && !allows_tail && candidate_wal_witness_info(witness).transport.has_tail) status=CANDIDATE_WAL_TAIL;
    if (status==CANDIDATE_WAL_OK) { memcpy(digest,f.digest,32); if (page_records!=NULL) *page_records=f.page_records; }
    candidate_wal_witness_close(&witness);
    memory->release(memory->context,f.scratch,CANDIDATE_WAL_RECORD_BYTES);
    return status;
}

static candidate_wal_status_t read_target(workspace_t *w, history_t *h)
{
    uint8_t page[8192]; bool is_present=false;
    candidate_wal_status_t status=w->options.target.read(w->options.target.context,h->id,page,&is_present);
    if (status!=CANDIDATE_WAL_OK || !is_present) return status;
    if (candidate_redo_page_crc(page)!=read_le(page+52,4)) return CANDIDATE_WAL_OK;
    candidate_page_info_t info;
    status=check_page(w->options.uuid,page,&info);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id!=h->id || info.location.lsn==0) return CANDIDATE_WAL_CORRUPT;
    h->data_generation=info.location.ref.generation; h->data_lsn=info.location.lsn; h->flags=DATA_PRESENT;
    return CANDIDATE_WAL_OK;
}

history_t *candidate_redo_find_history(workspace_t *w, uint64_t id)
{
    uint64_t hash=id*UINT64_C(11400714819323198485);
    unsigned slot=(unsigned)(hash^(hash>>32))&(HISTORY_SLOTS-1);
    while (w->history[slot].id!=0 && w->history[slot].id!=id) slot=(slot+1)&(HISTORY_SLOTS-1);
    return &w->history[slot];
}

static candidate_wal_status_t load_stage(workspace_t *w, staged_t *s)
{
    bool is_present=false; history_t *h=s->history;
    candidate_wal_status_t status=w->options.target.read(w->options.target.context,h->id,s->bytes,&is_present);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (h->flags & DATA_PRESENT) {
        candidate_page_info_t info;
        if (!is_present) return CANDIDATE_WAL_CORRUPT;
        status=check_page(w->options.uuid,s->bytes,&info);
        if (status!=CANDIDATE_WAL_OK) return status;
        if (info.location.ref.id!=h->id || info.location.ref.generation!=h->data_generation || info.location.lsn!=h->data_lsn)
            return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t apply_delta(staged_t *s, const candidate_wal_record_t *r)
{
    if (r->payload_bytes<40 || read_le(r->payload+36,4)!=0) return CANDIDATE_WAL_CORRUPT;
    unsigned count=(unsigned)read_le(r->payload+32,4);
    if (count==0 || count>128) return CANDIDATE_WAL_CORRUPT;
    size_t at=40, previous_end=64;
    for (unsigned i=0; i<count; i++) {
        if (r->payload_bytes-at<8) return CANDIDATE_WAL_CORRUPT;
        size_t offset=(size_t)read_le(r->payload+at,4), size=(size_t)read_le(r->payload+at+4,4); at+=8;
        if (offset<previous_end || offset>8192 || size==0) return CANDIDATE_WAL_CORRUPT;
        if (size>8192-offset || size>r->payload_bytes-at) return CANDIDATE_WAL_CORRUPT;
        memcpy(s->bytes+offset,r->payload+at,size); at+=size; previous_end=offset+size;
    }
    return at==r->payload_bytes ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static source_record_t *find_source(workspace_t *w, uint64_t lsn)
{
    uint64_t lo=0, hi=w->record_count;
    while (lo<hi) { uint64_t mid=lo+(hi-lo)/2; if (w->records[mid].lsn<lsn) lo=mid+1; else hi=mid; }
    return lo<w->record_count && w->records[lo].lsn==lsn ? &w->records[lo] : NULL;
}

static candidate_wal_status_t index_record(workspace_t *w, staged_t *s, const history_t *h, const candidate_wal_record_t *r)
{
    uint8_t digest[32];
    candidate_wal_status_t status=record_digest(r,w->scratch,digest);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (w->is_applying) {
        s->source=find_source(w,r->lsn);
        if (s->source!=NULL && memcmp(s->source->digest,digest,32)==0) return CANDIDATE_WAL_OK;
        candidate_wal_reject(w->wal); return CANDIDATE_WAL_CORRUPT;
    }
    if (w->record_count==w->record_capacity) return CANDIDATE_WAL_CORRUPT;
    s->source=&w->records[w->record_count++];
    *s->source=(source_record_t){.lsn=r->lsn,.previous_record=h->generation==s->generation ? h->last_lsn-h->record_back : 0};
    memcpy(s->source->digest,digest,32);
    return CANDIDATE_WAL_OK;
}

static uint64_t seed_of(const workspace_t *w, const history_t *h)
{
    return w->seeds==NULL ? 0 : w->seeds[h-w->history];
}

static bool is_before_seed(const workspace_t *w, const history_t *h)
{
    return h->id==0 || w->group_lsn<seed_of(w,h);
}

static candidate_wal_status_t stage_page(workspace_t *w, const candidate_wal_record_t *r)
{
    if (r->payload_bytes<32 || w->staged_count==64) return CANDIDATE_WAL_CORRUPT;
    if (r->type==4 && r->payload_bytes!=8224) return CANDIDATE_WAL_CORRUPT;
    uint64_t id=read_le(r->payload+8,8); if (id==0) return CANDIDATE_WAL_CORRUPT;
    history_t *h=candidate_redo_find_history(w,id);
    if (w->seeds!=NULL && is_before_seed(w,h)) { w->skipped_pages++; return CANDIDATE_WAL_OK; }
    if (h->id==0) {
        if (w->is_applying) return CANDIDATE_WAL_CORRUPT;
        if (w->info.distinct_pages==CANDIDATE_ALLOCATION_ENTRIES) return CANDIDATE_WAL_LIMIT;
        h->id=id; w->info.distinct_pages++;
        candidate_wal_status_t status=read_target(w,h); if (status!=CANDIDATE_WAL_OK) return status;
    }
    for (unsigned i=0; i<w->staged_count; i++) if (w->staged[i].history==h) return CANDIDATE_WAL_CORRUPT;
    staged_t *s=&w->staged[w->staged_count++];
    *s=(staged_t){.history=h,.generation=read_le(r->payload+16,8),.previous_lsn=read_le(r->payload+24,8),
        .record_lsn=r->lsn,.has_image=r->type==4};
    if (w->records!=NULL) { candidate_wal_status_t status=index_record(w,s,h,r); if (status!=CANDIDATE_WAL_OK) return status; }
    if (s->generation==0) return CANDIDATE_WAL_CORRUPT;
    if (w->is_applying) { candidate_wal_status_t status=load_stage(w,s); if (status!=CANDIDATE_WAL_OK) return status; }
    if (s->has_image) {
        memcpy(s->bytes,r->payload+32,8192); candidate_page_info_t info;
        return check_page(w->options.uuid,s->bytes,&info);
    }
    return apply_delta(s,r);
}

candidate_wal_status_t candidate_redo_check_link(uint64_t generation, uint64_t last_lsn, const candidate_redo_link_t *next)
{
    if (generation!=0 && generation==next->generation) return next->previous_lsn==last_lsn ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
    if (!next->is_image || next->previous_lsn!=0) return CANDIDATE_WAL_CORRUPT;
    bool is_next=generation==0 || (generation!=UINT64_MAX && next->generation==generation+1);
    return is_next ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_link(const workspace_t *w, const staged_t *s)
{
    const history_t *h=s->history;
    uint64_t seed=seed_of(w,h);
    if (seed==0 || h->generation!=0) {
        candidate_redo_link_t next={s->generation,s->previous_lsn,s->has_image};
        return candidate_redo_check_link(h->generation,h->last_lsn,&next);
    }
    return s->has_image && w->group_lsn==seed ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t check_history(const workspace_t *w, staged_t *s, uint64_t end)
{
    history_t *h=s->history;
    candidate_wal_status_t status=check_link(w,s);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (end-s->record_lsn>UINT32_MAX) return CANDIDATE_WAL_LIMIT;
    h->generation=s->generation; h->last_lsn=end; h->record_back=(uint32_t)(end-s->record_lsn);
    if (h->data_generation==s->generation && h->data_lsn==end) h->flags|=DATA_CORROBORATED;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t prepare_stage(workspace_t *w, staged_t *s, uint64_t end)
{
    history_t *h=s->history;
    if ((h->flags & DATA_PRESENT) && h->data_lsn>=end) { w->info.pages_skipped++; return CANDIDATE_WAL_OK; }
    if (!s->has_image) {
        if (!(h->flags & DATA_PRESENT) || h->data_generation!=s->generation || h->data_lsn!=s->previous_lsn)
            return CANDIDATE_WAL_CORRUPT;
    }
    write_le(s->bytes+40,end,8); write_le(s->bytes+52,candidate_redo_page_crc(s->bytes),4);
    candidate_page_info_t info;
    candidate_wal_status_t status=check_page(w->options.uuid,s->bytes,&info);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id!=h->id || info.location.ref.generation!=s->generation) return CANDIDATE_WAL_CORRUPT;
    s->should_write=true; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t write_stage(workspace_t *w, staged_t *s, uint64_t end)
{
    if (!s->should_write) return CANDIDATE_WAL_OK;
    history_t *h=s->history;
    candidate_wal_status_t status=w->options.target.write(w->options.target.context,h->id,s->bytes);
    if (status!=CANDIDATE_WAL_OK) return status;
    h->data_generation=s->generation; h->data_lsn=end; h->flags=DATA_PRESENT|DATA_CORROBORATED;
    w->info.pages_written++; return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t finish_group(workspace_t *w, const candidate_wal_record_t *r)
{
    uint32_t pages=w->staged_count+w->skipped_pages;
    if (r->payload_bytes!=24 || read_le(r->payload,8)!=w->group_id) return CANDIDATE_WAL_CORRUPT;
    if (pages!=w->expected_pages || w->group_records!=w->expected_records) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload+8,4)!=pages || read_le(r->payload+12,4)!=w->group_records) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload+16,4)!=w->group_crc || read_le(r->payload+20,4)!=0) return CANDIDATE_WAL_CORRUPT;
    uint64_t end=r->lsn+120;
    for (unsigned i=0; i<w->staged_count; i++) {
        staged_t *s=&w->staged[i];
        if (s->has_image && read_le(s->bytes+40,8)!=end) return CANDIDATE_WAL_CORRUPT;
        if (s->source!=NULL && !w->is_applying) s->source->group_end=end;
        if (s->source!=NULL && w->is_applying && s->source->group_end!=end) { candidate_wal_reject(w->wal); return CANDIDATE_WAL_CORRUPT; }
        candidate_wal_status_t status=w->is_applying ? prepare_stage(w,s,end) : check_history(w,s,end);
        if (status!=CANDIDATE_WAL_OK) return status;
    }
    if (w->is_applying) for (unsigned i=0; i<w->staged_count; i++) {
        candidate_wal_status_t status=write_stage(w,&w->staged[i],end);
        if (status!=CANDIDATE_WAL_OK) return status;
    }
    w->info.groups++; w->group_id=0; w->staged_count=0; return CANDIDATE_WAL_OK;
}

candidate_wal_status_t candidate_redo_visit_record(workspace_t *w, const candidate_wal_record_t *r)
{
    if (r->type==3) {
        if (w->group_id!=0 || r->payload_bytes!=16) return CANDIDATE_WAL_CORRUPT;
        w->group_id=read_le(r->payload,8); if (w->group_id==0) return CANDIDATE_WAL_CORRUPT;
        w->transaction_id=r->transaction_id; w->owner_epoch=r->owner_epoch;
        w->expected_pages=(uint32_t)read_le(r->payload+8,4); w->expected_records=(uint32_t)read_le(r->payload+12,4);
        w->staged_count=0; w->skipped_pages=0; w->group_records=0; w->group_crc=candidate_wal_witness_crc(0,r);
        w->last_lsn=r->lsn; w->group_lsn=r->lsn;
        return CANDIDATE_WAL_OK;
    }
    if (w->group_id==0) return CANDIDATE_WAL_OK;
    if (r->transaction_id!=w->transaction_id || r->owner_epoch!=w->owner_epoch || r->previous_lsn!=w->last_lsn)
        return CANDIDATE_WAL_CORRUPT;
    if (r->type==8) return finish_group(w,r);
    if (r->payload_bytes<8 || read_le(r->payload,8)!=w->group_id) return CANDIDATE_WAL_CORRUPT;
    if (r->type==4 || r->type==5) {
        candidate_wal_status_t status=stage_page(w,r); if (status!=CANDIDATE_WAL_OK) return status;
    } else if (r->type!=6 && r->type!=7 && r->type!=15 && r->type!=16) return CANDIDATE_WAL_CORRUPT;
    w->group_records++; w->group_crc=candidate_wal_witness_crc(w->group_crc,r); w->last_lsn=r->lsn;
    return CANDIDATE_WAL_OK;
}

static bool visit(void *context, const candidate_wal_record_t *r)
{
    workspace_t *w=context;
    if (!hash_record(&w->fingerprint,r)) return false;
    w->fingerprint.status=candidate_redo_visit_record(w,r);
    return w->fingerprint.status==CANDIDATE_WAL_OK;
}

static candidate_wal_status_t scan_pages(candidate_wal_t *wal, workspace_t *w, const uint8_t digest[32])
{
    w->fingerprint=(fingerprint_t){.scratch=w->scratch};
    w->group_id=0; w->staged_count=0; w->info.groups=0;
    if (!w->is_applying) w->record_count=0;
    candidate_wal_info_t info;
    candidate_wal_status_t status=scan_status(candidate_wal_scan(wal,visit,w,&info),&w->fingerprint);
    if (status!=CANDIDATE_WAL_OK) return status;
    if ((info.has_tail && !w->is_preview) || memcmp(digest,w->fingerprint.digest,32)!=0) return CANDIDATE_WAL_CORRUPT;
    w->info.wal_end_lsn=info.valid_end_lsn;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t check_targets(const workspace_t *w)
{
    for (unsigned i=0; i<HISTORY_SLOTS; i++) {
        unsigned flags=w->history[i].flags;
        if ((flags & DATA_PRESENT) && !(flags & DATA_CORROBORATED)) return CANDIDATE_WAL_CORRUPT;
    }
    return CANDIDATE_WAL_OK;
}

static void *allocate_default(void *context, size_t size) { (void)context; return malloc(size); }
static void release_default(void *context, void *p, size_t size) { (void)context; (void)size; free(p); }

candidate_wal_status_t candidate_redo_execute(candidate_wal_t *wal,
    const candidate_redo_options_t *options, candidate_redo_info_t *out)
{
    if (out==NULL) return CANDIDATE_WAL_INVALID;
    *out=(candidate_redo_info_t){0};
    if (wal==NULL || options==NULL) return CANDIDATE_WAL_INVALID;
    candidate_redo_options_t saved=*options;
    if (saved.target.read==NULL || saved.target.write==NULL || saved.target.sync==NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_state_t anchor;
    if (saved.checkpoint!=NULL) { anchor=*saved.checkpoint; saved.checkpoint=&anchor; }
    candidate_wal_memory_t memory={NULL,allocate_default,release_default};
    if (saved.memory!=NULL) memory=*saved.memory;
    if (memory.allocate==NULL || memory.release==NULL) return CANDIDATE_WAL_INVALID;
    saved.memory=&memory;
    uint8_t digest[32]; uint64_t page_records=0;
    candidate_wal_status_t status=verify_source(wal,&saved,digest,false,&page_records);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (page_records>(uint64_t)CANDIDATE_WITNESS_GROUPS*CANDIDATE_WITNESS_PAGES) return CANDIDATE_WAL_LIMIT;
    workspace_t *w=memory.allocate(memory.context,sizeof(*w));
    if (w==NULL) return CANDIDATE_WAL_NOMEM;
    memset(w,0,sizeof(*w)); w->options=saved; w->wal=wal; w->record_capacity=page_records;
    size_t index_bytes=(size_t)page_records*sizeof(*w->records);
    if (page_records>0) {
        w->records=memory.allocate(memory.context,index_bytes);
        if (w->records==NULL) { memory.release(memory.context,w,sizeof(*w)); return CANDIDATE_WAL_NOMEM; }
    }
    w->info.workspace_bytes=sizeof(*w)+index_bytes;
    status=scan_pages(wal,w,digest);
    if (status==CANDIDATE_WAL_OK) status=check_targets(w);
    if (status==CANDIDATE_WAL_OK) status=candidate_wal_force(wal,w->info.wal_end_lsn);
    if (status==CANDIDATE_WAL_OK) { w->is_applying=true; status=scan_pages(wal,w,digest); }
    if (status==CANDIDATE_WAL_OK) status=saved.target.sync(saved.target.context);
    candidate_redo_info_t result=w->info;
    if (w->records!=NULL) memory.release(memory.context,w->records,index_bytes);
    memory.release(memory.context,w,sizeof(*w));
    if (status==CANDIDATE_WAL_OK) *out=result;
    return status;
}

candidate_wal_status_t candidate_redo_preview_open(candidate_wal_t *wal,
    const candidate_redo_options_t *options, candidate_redo_preview_t **out)
{
    if (out==NULL) return CANDIDATE_WAL_INVALID;
    *out=NULL;
    if (wal==NULL || options==NULL) return CANDIDATE_WAL_INVALID;
    candidate_redo_options_t saved=*options;
    if (saved.target.read==NULL) return CANDIDATE_WAL_INVALID;
    candidate_control_state_t anchor;
    if (saved.checkpoint!=NULL) { anchor=*saved.checkpoint; saved.checkpoint=&anchor; }
    candidate_wal_memory_t memory={NULL,allocate_default,release_default};
    if (saved.memory!=NULL) memory=*saved.memory;
    if (memory.allocate==NULL || memory.release==NULL) return CANDIDATE_WAL_INVALID;
    saved.memory=&memory;
    uint8_t digest[32]; uint64_t page_records=0;
    candidate_wal_status_t status=verify_source(wal,&saved,digest,true,&page_records);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (page_records>(uint64_t)CANDIDATE_WITNESS_GROUPS*CANDIDATE_WITNESS_PAGES+CANDIDATE_WITNESS_PAGES) return CANDIDATE_WAL_LIMIT;
    candidate_redo_preview_t *p=memory.allocate(memory.context,sizeof(*p));
    if (p==NULL) return CANDIDATE_WAL_NOMEM;
    memset(p,0,sizeof(*p)); p->memory=memory; p->wal=wal;
    workspace_t *w=&p->workspace;
    w->options=saved; w->options.memory=&p->memory; w->options.checkpoint=NULL; w->wal=wal;
    w->is_preview=true; w->record_capacity=page_records;
    if (page_records>0) {
        w->records=memory.allocate(memory.context,(size_t)page_records*sizeof(*w->records));
        if (w->records==NULL) { candidate_redo_preview_close(&p); return CANDIDATE_WAL_NOMEM; }
    }
    w->info.workspace_bytes=sizeof(*p)+(size_t)page_records*sizeof(*w->records);
    status=scan_pages(wal,w,digest);
    if (status==CANDIDATE_WAL_OK) status=check_targets(w);
    if (status!=CANDIDATE_WAL_OK) { candidate_redo_preview_close(&p); return status; }
    *out=p; return CANDIDATE_WAL_OK;
}

static void fill_unknown(candidate_redo_preview_t *p, size_t offset, const uint8_t *bytes, size_t size)
{
    for (size_t i=0; i<size; i++) {
        if (p->known[offset+i]) continue;
        p->page[offset+i]=bytes[i]; p->known[offset+i]=1;
    }
}

static candidate_wal_status_t fill_delta(candidate_redo_preview_t *p, const candidate_wal_record_t *r)
{
    if (r->payload_bytes<40 || read_le(r->payload+36,4)!=0) return CANDIDATE_WAL_CORRUPT;
    unsigned count=(unsigned)read_le(r->payload+32,4);
    if (count==0 || count>128) return CANDIDATE_WAL_CORRUPT;
    size_t at=40, previous_end=64;
    for (unsigned i=0; i<count; i++) {
        if (r->payload_bytes-at<8) return CANDIDATE_WAL_CORRUPT;
        size_t offset=(size_t)read_le(r->payload+at,4), size=(size_t)read_le(r->payload+at+4,4); at+=8;
        if (offset<previous_end || offset>8192 || size==0) return CANDIDATE_WAL_CORRUPT;
        if (size>8192-offset || size>r->payload_bytes-at) return CANDIDATE_WAL_CORRUPT;
        fill_unknown(p,offset,r->payload+at,size); at+=size; previous_end=offset+size;
    }
    return at==r->payload_bytes ? CANDIDATE_WAL_OK : CANDIDATE_WAL_CORRUPT;
}

static candidate_wal_status_t read_page_record(candidate_redo_preview_t *p, const source_record_t *source, const history_t *h,
    uint64_t generation, candidate_wal_record_t *r)
{
    candidate_wal_status_t status=candidate_wal_read_at(p->wal,source->lsn,r);
    if (status!=CANDIDATE_WAL_OK) return status;
    uint8_t digest[32];
    status=record_digest(r,p->workspace.scratch,digest);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (r->lsn!=source->lsn || memcmp(digest,source->digest,32)!=0) { candidate_wal_reject(p->wal); return CANDIDATE_WAL_CORRUPT; }
    if ((r->type!=4 && r->type!=5) || r->payload_bytes<32) return CANDIDATE_WAL_CORRUPT;
    if (read_le(r->payload+8,8)!=h->id || read_le(r->payload+16,8)!=generation) return CANDIDATE_WAL_CORRUPT;
    if (r->type==4 && r->payload_bytes!=32+CANDIDATE_PAGE_BYTES) return CANDIDATE_WAL_CORRUPT;
    return CANDIDATE_WAL_OK;
}

static candidate_wal_status_t fill_from_data(candidate_redo_preview_t *p, const history_t *h)
{
    workspace_t *w=&p->workspace; bool is_present=false; uint8_t page[CANDIDATE_PAGE_BYTES];
    candidate_wal_status_t status=w->options.target.read(w->options.target.context,h->id,page,&is_present);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (!is_present || candidate_redo_page_crc(page)!=read_le(page+52,4)) return CANDIDATE_WAL_CORRUPT;
    candidate_page_info_t info;
    status=check_page(w->options.uuid,page,&info);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id!=h->id || info.location.ref.generation!=h->data_generation || info.location.lsn!=h->data_lsn)
        return CANDIDATE_WAL_CORRUPT;
    memcpy(p->page,page,64); fill_unknown(p,64,page+64,CANDIDATE_PAGE_BYTES-64);
    return CANDIDATE_WAL_OK;
}

static bool has_data_state(const history_t *h, uint64_t generation, uint64_t lsn)
{
    return (h->flags & DATA_CORROBORATED) && h->data_generation==generation && h->data_lsn==lsn;
}

static candidate_wal_status_t reconstruct(candidate_redo_preview_t *p, const history_t *h)
{
    memset(p->known,0,sizeof(p->known));
    if (has_data_state(h,h->generation,h->last_lsn)) return fill_from_data(p,h);
    const source_record_t *source=find_source(&p->workspace,h->last_lsn-h->record_back);
    for (uint64_t steps=0; source!=NULL && steps<=p->workspace.record_count; steps++) {
        candidate_wal_record_t r;
        candidate_wal_status_t status=read_page_record(p,source,h,h->generation,&r);
        if (status!=CANDIDATE_WAL_OK) return status;
        if (r.type==4) {
            memcpy(p->page,r.payload+32,64); fill_unknown(p,64,r.payload+32+64,CANDIDATE_PAGE_BYTES-64);
            return CANDIDATE_WAL_OK;
        }
        uint64_t previous=read_le(r.payload+24,8);
        status=fill_delta(p,&r);
        if (status!=CANDIDATE_WAL_OK) return status;
        if (previous==0) return CANDIDATE_WAL_CORRUPT;
        if (has_data_state(h,h->generation,previous)) return fill_from_data(p,h);
        if (source->previous_record==0 || source->previous_record>=source->lsn) return CANDIDATE_WAL_CORRUPT;
        source=find_source(&p->workspace,source->previous_record);
    }
    return CANDIDATE_WAL_CORRUPT;
}

candidate_wal_status_t candidate_redo_preview_load(void *context, candidate_page_ref_t ref,
    uint8_t out[CANDIDATE_PAGE_BYTES])
{
    candidate_redo_preview_t *p=context;
    if (p==NULL || out==NULL || ref.id==0) return CANDIDATE_WAL_INVALID;
    const history_t *h=candidate_redo_find_history(&p->workspace,ref.id);
    if (h->id==0 || h->generation==0) return CANDIDATE_WAL_CORRUPT;
    candidate_wal_status_t status=reconstruct(p,h);
    if (status!=CANDIDATE_WAL_OK) return status;
    write_le(p->page+40,h->last_lsn,8); write_le(p->page+52,candidate_redo_page_crc(p->page),4);
    candidate_page_info_t info;
    status=check_page(p->workspace.options.uuid,p->page,&info);
    if (status!=CANDIDATE_WAL_OK) return status;
    if (info.location.ref.id!=h->id || info.location.ref.generation!=h->generation) return CANDIDATE_WAL_CORRUPT;
    memcpy(out,p->page,CANDIDATE_PAGE_BYTES);
    return CANDIDATE_WAL_OK;
}

candidate_redo_info_t candidate_redo_preview_info(const candidate_redo_preview_t *p)
{
    return p==NULL ? (candidate_redo_info_t){0} : p->workspace.info;
}

void candidate_redo_preview_close(candidate_redo_preview_t **preview)
{
    if (preview==NULL || *preview==NULL) return;
    candidate_redo_preview_t *p=*preview; *preview=NULL;
    candidate_wal_memory_t memory=p->memory;
    if (p->workspace.records!=NULL)
        memory.release(memory.context,p->workspace.records,(size_t)p->workspace.record_capacity*sizeof(*p->workspace.records));
    if (p->workspace.seeds!=NULL) memory.release(memory.context,p->workspace.seeds,HISTORY_SLOTS*sizeof(*p->workspace.seeds));
    memory.release(memory.context,p,sizeof(*p));
}
