#define main original_system_test_main
#include "test_candidate_wal_system.c"
#undef main

#include "storage/candidate_checkpoint.h"

typedef struct {
    candidate_control_state_t anchor;
    positions_t positions;
    candidate_checkpoint_dirty_t dirty[4];
    candidate_checkpoint_transaction_t transactions[3];
    unsigned dirty_count, transaction_count;
} fixture_t;
typedef struct {
    unsigned calls, fail_at;
    size_t live, peak;
    candidate_witness_options_t *options;
    candidate_control_state_t *anchor;
    candidate_wal_memory_t *memory;
    candidate_wal_witness_t **output;
} checkpoint_memory_t;
static unsigned read_pass, read_fault;
static uint64_t read_checkpoint, read_after;

static void checkpoint_prefix(char *out,unsigned mode)
{
    CHECK(snprintf(out,256,"build/candidate-checkpoint-witness-%ld-%u-%u",(long)getpid(),mode,++serial)>0);
}

static uint64_t genesis(candidate_wal_t *wal,positions_t *pos)
{
    uint64_t end=predict_end(candidate_wal_info(wal),4,5);uint8_t images[4][8192];
    const unsigned ids[]={1,2,3,10};
    candidate_allocation_root_t root={.owner_epoch=7,.page_highwater=100,.count=4,.nodes={{2,1}}};
    candidate_allocation_node_t node={.count=4};
    for(unsigned i=0;i<4;i++)node.entries[i]=(candidate_allocation_entry_t){{ids[i],1},CANDIDATE_ALLOCATION_LIVE,0,end};
    candidate_catalog_root_t catalog={.owner_epoch=7,.current_epoch=1,.retention_floor=1,
        .transaction_highwater=100,.page_highwater=100,.retained_bytes=8192};
    candidate_page_location_t l=location(1,1,end);OK(candidate_allocation_encode_root(&l,&root,images[0],8192));
    l=location(2,1,end);OK(candidate_allocation_encode_node(&l,&node,images[1],8192));
    l=location(3,1,end);OK(candidate_catalog_encode_root(&l,&catalog,images[2],8192));
    l=location(10,1,end);uint8_t value=0x51;OK(candidate_page_encode_overflow(&l,(candidate_page_ref_t){0},&value,1,images[3],8192));
    transaction_t t={0};uint32_t crc=0;uint8_t begin[16]={0};put(begin,1,8);put(begin+8,4,4);put(begin+12,9,4);
    append(wal,&t,3,begin,16,pos,&crc,false);
    for(unsigned i=0;i<4;i++){uint8_t body[8224]={0};put(body,1,8);put(body+8,ids[i],8);put(body+16,1,8);memcpy(body+32,images[i],8192);append(wal,&t,4,body,sizeof(body),pos,&crc,false);}
    uint8_t reserve[40]={0};put(reserve,1,8);put(reserve+16,100,8);put(reserve+32,100,8);append(wal,&t,16,reserve,40,pos,&crc,false);
    for(unsigned i=0;i<4;i++){uint8_t body[40]={0};put(body,1,8);put(body+8,ids[i],8);put(body+24,1,8);append(wal,&t,6,body,40,pos,&crc,false);}
    uint8_t finish[24]={0};put(finish,1,8);put(finish+8,4,4);put(finish+12,9,4);put(finish+16,crc,4);
    CHECK(append(wal,&t,8,finish,24,pos,NULL,false)==end);OK(candidate_wal_force(wal,end));return end;
}

static uint64_t page_group(candidate_wal_t *wal,transaction_t *t,uint64_t gid,uint64_t first,unsigned count,
    uint64_t previous,unsigned mode,positions_t *pos)
{
    uint64_t end=predict_end(candidate_wal_info(wal),count,0);uint32_t crc=0;
    uint8_t begin[16]={0};put(begin,gid,8);put(begin+8,count,4);put(begin+12,count,4);append(wal,t,3,begin,16,pos,&crc,false);
    for(unsigned i=0;i<count;i++){
        uint8_t body[8224]={0},value=0x52;candidate_page_location_t l=location((unsigned)first+i,1,end);
        put(body,gid,8);put(body+8,first+i,8);put(body+16,1,8);put(body+24,previous,8);
        OK(candidate_page_encode_overflow(&l,(candidate_page_ref_t){0},&value,1,body+32,8192));
        append(wal,t,4,body,sizeof(body),pos,&crc,false);
    }
    uint8_t finish[24]={0};put(finish,gid,8);put(finish+8,count,4);put(finish+12,count,4);put(finish+16,mode==29?crc^1:crc,4);
    CHECK(append(wal,t,8,finish,24,pos,NULL,false)==end);return end;
}

static uint64_t finish_user(candidate_wal_t *wal,transaction_t *t,uint64_t csn,uint64_t group_end,positions_t *pos,bool bad)
{
    uint8_t body[168]={0};put(body,t->first,8);put(body+8,t->last,8);put(body+16,t->count,8);put(body+24,bad?t->crc^1:t->crc,4);
    size_t size=32;unsigned type=11;
    if(csn>0){type=10;size=group_end>0?168:152;put(body+32,csn,8);if(group_end>0){put(body+148,1,4);put(body+152,2,8);put(body+160,group_end,8);}}
    return append(wal,t,type,body,size,pos,NULL,false);
}

static candidate_checkpoint_transaction_t state(transaction_t t,uint64_t csn,uint64_t end)
{
    return (candidate_checkpoint_transaction_t){t.id,t.first,t.last,end,csn,7,
        end==0?CANDIDATE_CHECKPOINT_ACTIVE:(csn>0?CANDIDATE_CHECKPOINT_COMMIT:CANDIDATE_CHECKPOINT_ABORT)};
}

static void metadata_group(candidate_wal_t *wal,uint64_t gid,uint64_t old_epoch,bool reserve,positions_t *pos)
{
    unsigned events=reserve?2:1;uint64_t end=predict_end(candidate_wal_info(wal),2,events);
    candidate_catalog_root_t catalog={.owner_epoch=options.owner_epoch,.current_epoch=reserve?3:2,.retention_floor=reserve?3:1,
        .transaction_highwater=reserve?200:100,.page_highwater=reserve?200:100,.retained_bytes=8192};
    candidate_allocation_root_t allocation={.owner_epoch=options.owner_epoch,.page_highwater=reserve?200:100,.count=4,.nodes={{2,1}}};
    uint8_t images[2][8192];candidate_page_location_t l=location(3,1,end);OK(candidate_catalog_encode_root(&l,&catalog,images[0],8192));
    l=location(1,1,end);OK(candidate_allocation_encode_root(&l,&allocation,images[1],8192));
    transaction_t t={0};uint32_t crc=0;uint8_t begin[16]={0};put(begin,gid,8);put(begin+8,2,4);put(begin+12,2+events,4);append(wal,&t,3,begin,16,pos,&crc,false);
    for(unsigned i=0;i<2;i++){uint8_t body[8224]={0};put(body,gid,8);memcpy(body+8,images[i]+24,16);memcpy(body+32,images[i],8192);append(wal,&t,4,body,sizeof(body),pos,&crc,false);}
    if(reserve){uint8_t body[40]={0};put(body,gid,8);put(body+8,100,8);put(body+16,200,8);put(body+24,100,8);put(body+32,200,8);append(wal,&t,16,body,40,pos,&crc,false);}
    uint8_t epoch[40]={0};put(epoch,gid,8);put(epoch+8,old_epoch,8);put(epoch+16,reserve?3:2,8);put(epoch+24,1,8);put(epoch+32,reserve?3:1,8);append(wal,&t,15,epoch,40,pos,&crc,false);
    uint8_t finish[24]={0};put(finish,gid,8);put(finish+8,2,4);put(finish+12,2+events,4);put(finish+16,crc,4);CHECK(append(wal,&t,8,finish,24,pos,NULL,false)==end);
}

static candidate_control_state_t anchor_at(candidate_wal_t *wal,uint64_t id)
{
    uint64_t begin=record_start(candidate_wal_info(wal),160);
    candidate_control_state_t a={.backend=1,.generation=1,.owner_epoch=options.owner_epoch,.checkpoint_id=id,
        .checkpoint_begin=begin,.checkpoint_end=begin+4096,.redo_begin=64,.transaction_highwater=100,
        .page_highwater=100,.request_epoch=1,.retention_floor=1,.outcome_root={3,1},.allocation_root={1,1}};
    memcpy(a.uuid,options.uuid,16);return a;
}

static void empty_checkpoint(candidate_wal_t *wal,uint64_t id,transaction_t *t,bool finish)
{
    candidate_checkpoint_summary_t s={.anchor=anchor_at(wal,id)};uint8_t bytes[160],digest[32]={0};size_t size;
    OK(candidate_checkpoint_encode_begin(&s,bytes,sizeof(bytes),&size));append(wal,t,12,bytes,size,NULL,NULL,false);
    if(finish){OK(candidate_checkpoint_digest(digest,bytes,size,digest));candidate_checkpoint_end_t end={.checkpoint_id=id,.begin_lsn=s.anchor.checkpoint_begin};
        memcpy(end.digest,digest,32);OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));append(wal,t,14,bytes,size,NULL,NULL,false);}
}

static void alter_fixture(fixture_t *f,unsigned mode,uint64_t group_begin)
{
    candidate_checkpoint_transaction_t *a=&f->transactions[0];
    if(mode==1)f->transactions[2].id=14;
    if(mode==2){f->transactions[1]=f->transactions[2];f->transaction_count=2;}
    if(mode==3){a->state=CANDIDATE_CHECKPOINT_ABORT;a->csn=0;a->terminal_end_lsn=a->last_lsn+128;}
    if(mode==4)a->csn=2;
    if(mode==5)a->first_lsn+=8;
    if(mode==6){a->last_lsn-=16;a->terminal_end_lsn-=16;}
    if(mode==7)a->terminal_end_lsn+=16;
    if(mode==8)a->owner_epoch=6;
    if(mode==9)f->transactions[1].last_lsn+=112;
    if(mode==10)f->dirty[3].ref.generation=2;
    if(mode==11)f->dirty[0].rec_lsn+=112;
    if(mode==12)f->dirty[3].ref.id=11;
    if(mode==13)f->dirty[0].rec_lsn=group_begin;
    if(mode==14){a->state=CANDIDATE_CHECKPOINT_ACTIVE;a->csn=0;a->terminal_end_lsn=0;}
    if(mode==15){f->transactions[0]=f->transactions[1];f->transaction_count=1;}
    if(mode==16)f->anchor.transaction_highwater=101;
    if(mode==17)f->anchor.page_highwater=101;
    if(mode==18)f->anchor.request_epoch=2;
    if(mode==19)f->anchor.retention_floor=2;
    if(mode==20)f->anchor.visible_csn=2;
    if(mode==25)f->transactions[2].owner_epoch=8;
    if(mode==30)f->transaction_count=2;
}

static fixture_t fixture(char path[256],unsigned mode)
{
    options.owner_epoch=7;checkpoint_prefix(path,mode);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));
    fixture_t f={.dirty_count=4,.transaction_count=3};positions_t *pos=&f.positions;uint64_t initial=genesis(wal,pos);
    if(mode==26){transaction_t unused={0};empty_checkpoint(wal,1,&unused,true);}
    transaction_t a={.id=11},b={.id=12},c={.id=13},d={.id=14};uint8_t begin[16]={0};
    append(wal,&a,2,begin,16,pos,NULL,false);append(wal,&b,2,begin,16,pos,NULL,false);
    uint64_t group_begin=record_start(candidate_wal_info(wal),16),group_end=page_group(wal,&a,2,10,1,initial,mode,pos);
    uint64_t a_end=finish_user(wal,&a,1,group_end,pos,false);OK(candidate_wal_force(wal,a_end));
    uint8_t admit[64]={0};put(admit,1,8);admit[8]=1;append(wal,&c,1,admit,64,pos,NULL,false);
    if(mode==24||mode==25){OK(candidate_wal_close(&wal));options.owner_epoch=8;OK(candidate_wal_open(path,&options,&wal));}
    uint64_t c_end=0;if(mode!=30&&mode!=31)c_end=finish_user(wal,&c,0,0,pos,false);
    if(mode==19)metadata_group(wal,3,1,false,pos);
    if(mode==21){uint8_t group_start[16]={0};put(group_start,3,8);put(group_start+8,1,4);put(group_start+12,1,4);append(wal,&b,3,group_start,16,pos,NULL,false);}
    f.anchor=anchor_at(wal,mode==26?2:1);if(mode==19)f.anchor.request_epoch=2;
    f.transactions[0]=state(a,1,a_end);f.transactions[1]=state(b,0,0);f.transactions[2]=state(c,0,c_end);
    const unsigned ids[]={1,2,3,10};for(unsigned i=0;i<4;i++)f.dirty[i]=(candidate_checkpoint_dirty_t){{ids[i],1},64};
    alter_fixture(&f,mode,group_begin);candidate_checkpoint_summary_t summary={f.anchor,f.dirty_count,f.transaction_count};
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD],digest[32]={0};size_t size;transaction_t cp={0};
    OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));append(wal,&cp,12,bytes,size,pos,NULL,false);OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    for(unsigned kind=1;kind<=2;kind++){
        candidate_checkpoint_chunk_t chunk={.checkpoint_id=f.anchor.checkpoint_id,.kind=kind,.ordinal=kind-1,.count=kind==1?f.dirty_count:f.transaction_count};
        if(kind==1)memcpy(chunk.entries.dirty,f.dirty,f.dirty_count*sizeof(f.dirty[0]));else memcpy(chunk.entries.transactions,f.transactions,f.transaction_count*sizeof(f.transactions[0]));
        OK(candidate_checkpoint_encode_chunk(&chunk,bytes,sizeof(bytes),&size));append(wal,&cp,13,bytes,size,pos,NULL,false);OK(candidate_checkpoint_digest(digest,bytes,size,digest));
        if(kind==1&&mode!=21){
            bool death=mode==24||mode==25;uint64_t b_end=finish_user(wal,&b,death?0:2,0,pos,false);OK(candidate_wal_force(wal,b_end));
            if(c_end==0)finish_user(wal,&c,0,0,pos,false);
            append(wal,&d,2,begin,16,pos,NULL,false);metadata_group(wal,mode==19?4:3,mode==19?2:1,true,pos);
        }
    }
    candidate_checkpoint_end_t end={.checkpoint_id=f.anchor.checkpoint_id,.begin_lsn=f.anchor.checkpoint_begin,.chunks=2};memcpy(end.digest,digest,32);
    OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));f.anchor.checkpoint_end=append(wal,&cp,14,bytes,size,pos,NULL,false);
    if(mode==23)finish_user(wal,&d,0,0,pos,true);
    if(mode==26){transaction_t unused={0};empty_checkpoint(wal,3,&unused,false);}
    if(mode==27)empty_checkpoint(wal,3,&d,false);
    if(mode==28){uint8_t group_start[16]={0};put(group_start,4,8);put(group_start+8,1,4);put(group_start+12,1,4);append(wal,&d,3,group_start,16,pos,NULL,false);}
    if(mode!=21&&mode!=28){transaction_t later={.id=15};append(wal,&later,2,begin,16,pos,NULL,false);finish_user(wal,&later,0,0,pos,false);}
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));return f;
}

static void *checkpoint_allocate(void *context,size_t size)
{
    checkpoint_memory_t *m=context;m->calls++;if(m->output!=NULL)CHECK(*m->output==NULL);
    if(m->options!=NULL){memset(m->options,0,sizeof(*m->options));m->options=NULL;}
    if(m->anchor!=NULL){memset(m->anchor,0,sizeof(*m->anchor));m->anchor=NULL;}
    if(m->memory!=NULL){memset(m->memory,0,sizeof(*m->memory));m->memory=NULL;}
    if(m->calls==m->fail_at)return NULL;void *p=malloc(size);if(p!=NULL){m->live+=size;if(m->peak<m->live)m->peak=m->live;}return p;
}
static void checkpoint_release(void *context,void *p,size_t size)
{
    checkpoint_memory_t *m=context;if(m->output!=NULL)CHECK(*m->output==NULL);CHECK(m->live>=size);m->live-=size;free(p);
}
static ssize_t checkpoint_pread(int fd,void *out,size_t size,off_t offset)
{
    if((uint64_t)offset==read_checkpoint&&size==80)read_pass++;
    if(read_fault==1&&read_pass==2&&(uint64_t)offset>=read_after){errno=EIO;return -1;}
    ssize_t n=pread(fd,out,size,offset);
    if(read_fault==2&&read_pass==2&&(uint64_t)offset==read_checkpoint&&size==256&&n==256){
        uint8_t *p=out;put(p+80+48,101,8);put(p+68,0,4);put(p+68,crc32c(0,p,size),4);
    }
    if((read_fault==3||read_fault==4)&&read_pass==2&&size>80&&n==(ssize_t)size){
        uint8_t *p=out;unsigned type=(unsigned)p[6]|((unsigned)p[7]<<8);
        if(type==(read_fault==3?13u:14u)){p[111]^=1;put(p+68,0,4);put(p+68,crc32c(0,p,size),4);}
    }
    return n;
}

static candidate_wal_status_t analyze_checkpoint(const char *path,candidate_control_state_t *anchor,
    checkpoint_memory_t *m,candidate_wal_witness_t **out)
{
    candidate_wal_io_t io={checkpoint_pread,pwrite,fsync,ftruncate};candidate_wal_options_t wo=options;wo.io=&io;
    candidate_wal_t *wal=NULL;OK(candidate_wal_open(path,&wo,&wal));read_pass=0;
    candidate_witness_options_t o={.enable_system_groups=true};memcpy(o.uuid,options.uuid,16);
    candidate_wal_memory_t memory={m,checkpoint_allocate,checkpoint_release};
    if(m!=NULL){o.memory=&memory;m->output=out;if(m->options!=NULL)m->options=&o;if(m->memory!=NULL)m->memory=&memory;}
    candidate_wal_status_t s=candidate_wal_witness_analyze_checkpoint(wal,&o,anchor,out);
    OK(candidate_wal_close(&wal));return s;
}

static void functional(void)
{
    for(unsigned mode=0;mode<=31;mode++){
        char path[256];fixture_t f=fixture(path,mode);candidate_wal_t *wal=NULL;OK(candidate_wal_open(path,&options,&wal));
        candidate_checkpoint_t *raw=NULL;OK(candidate_checkpoint_load(wal,&f.anchor,NULL,&raw));candidate_checkpoint_close(&raw);OK(candidate_wal_close(&wal));
        bool good=mode==0||mode==15||mode==22||mode==24||mode==26||mode==28||mode==31;
        candidate_wal_witness_t *w=NULL;checkpoint_memory_t m={0};candidate_wal_status_t s=analyze_checkpoint(path,&f.anchor,&m,&w);
        if(s!=(good?CANDIDATE_WAL_OK:CANDIDATE_WAL_CORRUPT))fprintf(stderr,"mode=%u got=%u good=%u\n",mode,s,good);
        CHECK(s==(good?CANDIDATE_WAL_OK:CANDIDATE_WAL_CORRUPT));
        if(good){candidate_witness_info_t info=candidate_wal_witness_info(w);CHECK(info.highest_csn==(mode==24?1u:2u)&&info.unfinished==1);
            CHECK(candidate_wal_witness_system_info(w).transaction_highwater==200&&candidate_wal_witness_system_info(w).current_epoch==3);
            CHECK(m.live==1681736&&m.peak==2108056);if(mode==28)CHECK(info.incomplete_group_lsn>=f.anchor.checkpoint_end);
            if(mode==0)printf("FIXTURE history %s begin=%llu end=%llu a_first=%llu a_terminal=%llu a_end=%llu b_begin=%llu c_admit=%llu c_abort=%llu c_end=%llu\n",path,
                (unsigned long long)f.anchor.checkpoint_begin,(unsigned long long)f.anchor.checkpoint_end,
                (unsigned long long)f.transactions[0].first_lsn,(unsigned long long)f.transactions[0].last_lsn,(unsigned long long)f.transactions[0].terminal_end_lsn,
                (unsigned long long)f.transactions[1].first_lsn,(unsigned long long)f.transactions[2].first_lsn,(unsigned long long)f.transactions[2].last_lsn,(unsigned long long)f.transactions[2].terminal_end_lsn);
        }else CHECK(w==NULL);
        candidate_wal_witness_close(&w);CHECK(m.live==0);
    }
}

static void cuts_and_failures(void)
{
    char path[256];fixture_t f=fixture(path,0);candidate_wal_witness_t *w=NULL;
    for(unsigned i=0;i<=f.positions.count;i++){
        char cut[256];checkpoint_prefix(cut,100+i);uint64_t end=i==0?64:f.positions.ends[i-1];copy_prefix(path,cut,end);
        candidate_wal_status_t expected=end>=f.anchor.checkpoint_end?CANDIDATE_WAL_OK:CANDIDATE_WAL_CORRUPT;
        CHECK(analyze_checkpoint(cut,&f.anchor,NULL,&w)==expected);candidate_wal_witness_close(&w);
    }
    for(unsigned fail=1;fail<=3;fail++){checkpoint_memory_t m={.fail_at=fail};CHECK(analyze_checkpoint(path,&f.anchor,&m,&w)==CANDIDATE_WAL_NOMEM&&w==NULL&&m.live==0);}
    checkpoint_memory_t m={.options=(candidate_witness_options_t *)1,.anchor=&f.anchor,.memory=(candidate_wal_memory_t *)1};candidate_control_state_t saved=f.anchor;
    OK(analyze_checkpoint(path,&f.anchor,&m,&w));candidate_wal_witness_close(&w);CHECK(m.live==0&&f.anchor.checkpoint_id==0);f.anchor=saved;
    read_checkpoint=f.anchor.checkpoint_begin;read_after=f.anchor.checkpoint_end;
    for(unsigned fault=1;fault<=4;fault++){read_fault=fault;m=(checkpoint_memory_t){0};
        CHECK(analyze_checkpoint(path,&f.anchor,&m,&w)==(fault==1?CANDIDATE_WAL_IO:CANDIDATE_WAL_CORRUPT)&&w==NULL&&m.live==0&&m.peak==2108056);read_fault=0;}
    candidate_wal_t *wal=NULL;OK(candidate_wal_open(path,&options,&wal));candidate_witness_options_t o={0};memcpy(o.uuid,options.uuid,16);
    CHECK(candidate_wal_witness_analyze_checkpoint(wal,&o,&f.anchor,&w)==CANDIDATE_WAL_INVALID&&w==NULL);
    o.enable_system_groups=true;o.uuid[0]++;
    CHECK(candidate_wal_witness_analyze_checkpoint(wal,&o,&f.anchor,&w)==CANDIDATE_WAL_UNSUPPORTED&&w==NULL);
    CHECK(candidate_wal_witness_analyze_checkpoint(wal,NULL,&f.anchor,&w)==CANDIDATE_WAL_INVALID&&w==NULL);
    CHECK(candidate_wal_witness_analyze_checkpoint(wal,&o,NULL,&w)==CANDIDATE_WAL_INVALID&&w==NULL);
    CHECK(candidate_wal_witness_analyze_checkpoint(wal,&o,&f.anchor,NULL)==CANDIDATE_WAL_INVALID);
    OK(candidate_wal_close(&wal));
}

static void capacity(unsigned total,unsigned active,unsigned pages)
{
    char path[256];checkpoint_prefix(path,1000+total+active);options.owner_epoch=7;
    candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));
    candidate_checkpoint_transaction_t *states=calloc(total,sizeof(*states));
    candidate_checkpoint_dirty_t *dirty=calloc(pages?pages:1,sizeof(*dirty));CHECK(states!=NULL&&dirty!=NULL);
    uint8_t begin[16]={0};transaction_t current={0};
    for(unsigned i=0;i<total;i++){
        current=(transaction_t){.id=i+1};append(wal,&current,2,begin,16,NULL,NULL,false);
        uint64_t end=0;if(i<total-active)end=finish_user(wal,&current,0,0,NULL,false);
        states[i]=state(current,0,end);
    }
    for(unsigned i=0;i<pages;i+=64){
        uint64_t start=record_start(candidate_wal_info(wal),16);page_group(wal,&current,i/64+1,i+1,64,0,0,NULL);
        for(unsigned j=0;j<64;j++)dirty[i+j]=(candidate_checkpoint_dirty_t){{i+j+1,1},start};
    }
    if(pages>0)states[total-1]=state(current,0,0);
    candidate_checkpoint_summary_t summary={.anchor=anchor_at(wal,1),.dirty_count=pages,.transaction_count=total>4096?4096:total};
    summary.anchor.transaction_highwater=total;summary.anchor.page_highwater=pages?pages:100;
    summary.anchor.outcome_root=(candidate_page_ref_t){2,1};
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD],digest[32]={0};size_t size;transaction_t cp={0};unsigned ordinal=0;
    OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));append(wal,&cp,12,bytes,size,NULL,NULL,false);OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    for(unsigned kind=1;kind<=2;kind++){
        unsigned count=kind==1?pages:summary.transaction_count;
        for(unsigned start=0;start<count;start+=128){
            candidate_checkpoint_chunk_t chunk={.checkpoint_id=1,.kind=kind,.ordinal=ordinal++,.count=count-start<128?count-start:128};
            for(unsigned j=0;j<chunk.count;j++){
                if(kind==1)chunk.entries.dirty[j]=dirty[start+j];else chunk.entries.transactions[j]=states[start+j];
            }
            OK(candidate_checkpoint_encode_chunk(&chunk,bytes,sizeof(bytes),&size));append(wal,&cp,13,bytes,size,NULL,NULL,false);OK(candidate_checkpoint_digest(digest,bytes,size,digest));
        }
    }
    candidate_checkpoint_end_t end={.checkpoint_id=1,.begin_lsn=summary.anchor.checkpoint_begin,.chunks=ordinal};memcpy(end.digest,digest,32);
    OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));summary.anchor.checkpoint_end=append(wal,&cp,14,bytes,size,NULL,NULL,false);
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
    candidate_wal_witness_t *w=NULL;checkpoint_memory_t memory={0};
    candidate_wal_status_t status=analyze_checkpoint(path,&summary.anchor,&memory,&w);
    CHECK(status==(total>4096||active>64?CANDIDATE_WAL_LIMIT:CANDIDATE_WAL_OK));
    if(status==CANDIDATE_WAL_OK){candidate_witness_info_t info=candidate_wal_witness_info(w);
        CHECK(info.transactions==total&&info.unfinished==active&&info.complete_groups==pages/64);
        CHECK(memory.live==1681736&&memory.peak==2108056);}
    else CHECK(w==NULL);
    printf("CAPACITY %s transactions=%u active=%u dirty=%u chunks=%u begin=%llu end=%llu status=%u\n",path,total,active,pages,ordinal,
        (unsigned long long)summary.anchor.checkpoint_begin,(unsigned long long)summary.anchor.checkpoint_end,status);
    candidate_wal_witness_close(&w);CHECK(memory.live==0);free(states);free(dirty);
}

int main(int argc,char **argv)
{
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);
    if(argc==2&&strcmp(argv[1],"max")==0){capacity(4096,1,8192);capacity(4097,0,0);capacity(64,64,0);capacity(65,65,0);}
    else {CHECK(argc==1);functional();cuts_and_failures();}
    printf("PASS checkpoint history checks=%u; retained-prefix corroboration only, no data redo/recovery activation\n",checks);return 0;
}
