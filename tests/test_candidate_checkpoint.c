#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_checkpoint.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#c); exit(1); } } while (0)
#define OK(c) CHECK((c)==CANDIDATE_WAL_OK)
static unsigned checks, serial;
static unsigned checkpoint_transaction_id, verify_calls;
static uint64_t fail_read_at;
static candidate_wal_options_t options = {.uuid={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},.owner_epoch=7};
typedef struct { uint64_t ends[128]; unsigned count; } positions_t;
typedef struct {
    unsigned calls;
    bool fail, clear_memory;
    size_t live, peak;
    candidate_control_state_t *mutate;
    candidate_wal_memory_t *mutate_memory;
} memory_t;
typedef struct { candidate_wal_t *wal; memory_t memory; } verifier_t;

static void put(uint8_t *p,uint64_t n,unsigned size) { for(unsigned i=0;i<size;i++)p[i]=(uint8_t)(n>>(8*i)); }
static void prefix(char *out,unsigned mode) { CHECK(snprintf(out,256,"build/candidate-checkpoint-%ld-%u-%u",(long)getpid(),mode,++serial)>0); }
static uint64_t start(candidate_wal_t *wal,size_t size)
{
    candidate_wal_info_t info=candidate_wal_info(wal); uint64_t end=info.valid_end_lsn;
    if(((96+size+7)&~(uint64_t)7)>CANDIDATE_WAL_SEGMENT_BYTES-end%CANDIDATE_WAL_SEGMENT_BYTES)
        end=(uint64_t)info.segments*CANDIDATE_WAL_SEGMENT_BYTES+64;
    return end;
}
static uint64_t append(candidate_wal_t *wal,unsigned type,uint64_t previous,const uint8_t *body,size_t size,positions_t *pos)
{
    candidate_wal_record_t r={.type=(uint16_t)type,.previous_lsn=previous,.payload=body,.payload_bytes=size};
    if(type>=12&&type<=14)r.transaction_id=checkpoint_transaction_id;
    uint64_t position=start(wal,size);
    uint64_t end;OK(candidate_wal_append(wal,&r,&end));
    CHECK(end==position+((96+size+7)&~(uint64_t)7));
    if(pos!=NULL){CHECK(pos->count<128);pos->ends[pos->count++]=end;}
    return end;
}
static void *allocate(void *context,size_t size)
{
    memory_t *m=context;m->calls++;
    if(m->mutate!=NULL){memset(m->mutate,0,sizeof(*m->mutate));m->mutate=NULL;}
    if(m->mutate_memory!=NULL){memset(m->mutate_memory,0,sizeof(*m->mutate_memory));m->mutate_memory=NULL;}
    if(m->fail)return NULL;
    void *p=malloc(size);if(p!=NULL){m->live+=size;if(m->peak<m->live)m->peak=m->live;}return p;
}
static void release(void *context,void *p,size_t size) { memory_t *m=context;CHECK(m->live>=size);m->live-=size;free(p); }
static ssize_t fault_pread(int fd,void *out,size_t size,off_t offset)
{
    if(fail_read_at>0&&(uint64_t)offset>=fail_read_at){errno=EIO;return -1;}return pread(fd,out,size,offset);
}

static void mutate_payload(uint8_t *body,size_t *size,unsigned type,unsigned mode,unsigned ordinal,uint64_t begin)
{
    if(type==12){
        if(mode==1)body[0]='X';
        if(mode==2)body[8]^=1;
        if(mode==3)body[156]=1;
        if(mode==4)put(body+48,9000,8);
        if(mode==5)put(body+144,8193,4);
        if(mode==26)put(body+4,2,2);
        if(mode==27)put(body+152,97,4);
        if(mode==37)(*size)--;
    }else if(type==13){
        unsigned kind=body[6];
        if(mode==6)put(body+20,129,4);
        if(mode==7&&ordinal==0)put(body+16,1,4);
        if(mode==8&&ordinal==1)put(body+32,128,8);
        if(mode==9&&kind==1)put(body+40,0,8);
        if(mode==10&&kind==1)put(body+48,begin+8,8);
        if(mode==12&&kind==2&&ordinal==1)put(body+32,128,8);
        if(mode==13&&kind==2&&ordinal==1)put(body+64,2,8);
        if(mode==14&&kind==2)put(body+40,128,8);
        if(mode==20)(*size)--;
        if(mode==28)put(body+6,3,2);
        if(mode==29&&kind==2)body[84]=1;
        if(mode==30&&kind==2)put(body+80,4,4);
        if(mode==31&&kind==2)put(body+32,9000,8);
        if(mode==32&&kind==2)put(body+72,8,8);
        if(mode==33&&kind==2)put(body+56,begin+8,8);
        if(mode==34&&kind==2)put(body+64,9000,8);
        if(mode==35&&kind==2)put(body+48,begin,8);
        if(mode==36){put(body+20,0,4);*size=32;}
        if(mode==42)*size=31;
        if(mode==43){body[*size]=0;(*size)++;}
        if(mode==46)put(body+112,184,8);
        if(mode==47)put(body+168,312,8);
        if(mode==48)put(body+96,176,8);
        if(mode==49)put(body+112,432,8);
        if(mode==50)put(body+48,72,8);
    }else if(type==14){
        if(mode==16)body[32]^=1;
        if(mode==17)put(body+16,begin+8,8);
        if(mode==18)put(body+24,0,4);
        if(mode==19)body[28]=1;
        if(mode==41)(*size)--;
    }
}

static candidate_control_state_t write_checkpoint(candidate_wal_t *wal,unsigned dirty,unsigned transactions,
    unsigned mode,uint64_t id,positions_t *pos)
{
    uint64_t begin=start(wal,160);
    candidate_control_state_t a={.backend=1,.generation=1,.slot=0,.owner_epoch=7,.checkpoint_id=id,
        .checkpoint_begin=begin,.checkpoint_end=begin+4096,.redo_begin=mode==11||mode==45?72:64,
        .transaction_highwater=8192,.page_highwater=16384,.request_epoch=2,.retention_floor=1,
        .visible_csn=3,.gc_horizon=2,.outcome_root={1,1},.allocation_root={2,1},.catalog_root={3,1}};
    memcpy(a.uuid,options.uuid,16);
    candidate_checkpoint_summary_t summary={a,dirty,transactions};
    uint8_t bytes[CANDIDATE_CHECKPOINT_PAYLOAD],digest[32]={0};size_t size;
    checkpoint_transaction_id=mode==23?1:0;
    OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));mutate_payload(bytes,&size,12,mode,0,begin);
    uint64_t last=start(wal,size);append(wal,12,0,bytes,size,pos);OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    unsigned ordinal=0;
    for(unsigned kind=1;kind<=2;kind++){
        unsigned total=kind==1?dirty:transactions;
        for(unsigned at=0;at<total;at+=128){
            candidate_checkpoint_chunk_t c={.checkpoint_id=id,.kind=kind,.ordinal=ordinal,.count=total-at<128?total-at:128};
            for(unsigned i=0;i<c.count;i++){
                unsigned index=at+i;
                if(kind==1)c.entries.dirty[i]=(candidate_checkpoint_dirty_t){{index+1,1},64};
                else{
                    candidate_checkpoint_state_t state=index==0||mode==15?CANDIDATE_CHECKPOINT_ACTIVE:
                        (index==2?CANDIDATE_CHECKPOINT_ABORT:CANDIDATE_CHECKPOINT_COMMIT);
                    c.entries.transactions[i]=(candidate_checkpoint_transaction_t){index+1,64,state==CANDIDATE_CHECKPOINT_ACTIVE?64:176,
                        state==CANDIDATE_CHECKPOINT_ACTIVE?0:(state==CANDIDATE_CHECKPOINT_COMMIT?424:304),
                        state==CANDIDATE_CHECKPOINT_COMMIT?index+1:0,7,state};
                    if(mode==44&&index==1)c.entries.transactions[i].csn=4;
                    if(mode==44&&index==3)c.entries.transactions[i].csn=2;
                }
            }
            OK(candidate_checkpoint_encode_chunk(&c,bytes,sizeof(bytes),&size));mutate_payload(bytes,&size,13,mode,ordinal,begin);
            if(mode!=21){uint64_t current=start(wal,size);append(wal,13,mode==22?0:last,bytes,size,pos);last=current;}
            OK(candidate_checkpoint_digest(digest,bytes,size,digest));ordinal++;
            if(mode==38){uint8_t payload[16]={0};append(wal,2,0,payload,sizeof(payload),pos);}
        }
    }
    candidate_checkpoint_end_t finish={.checkpoint_id=id,.begin_lsn=begin,.chunks=ordinal};memcpy(finish.digest,digest,32);
    OK(candidate_checkpoint_encode_end(&finish,bytes,sizeof(bytes),&size));mutate_payload(bytes,&size,14,mode,ordinal,begin);
    a.checkpoint_end=append(wal,14,last,bytes,size,pos);
    checkpoint_transaction_id=0;
    if(mode==24)a.checkpoint_end+=8;
    if(mode==25)a.outcome_root.id=4;
    return a;
}

static candidate_control_state_t fixture(char path[256],unsigned dirty,unsigned transactions,unsigned mode,positions_t *pos)
{
    prefix(path,mode);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));
    uint8_t payload[512]={0};append(wal,2,0,payload,sizeof(payload),pos);
    if(mode==40){
        uint8_t *filler=calloc(1,980000);CHECK(filler!=NULL);
        for(unsigned i=0;i<4;i++)append(wal,2,0,filler,980000,NULL);
        free(filler);
    }
    candidate_control_state_t a=write_checkpoint(wal,dirty,transactions,mode,1,pos);
    append(wal,2,0,payload,16,pos);OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
    return a;
}
static candidate_wal_status_t load(const char *path,const candidate_control_state_t *a,memory_t *m,candidate_checkpoint_t **out)
{
    candidate_wal_t *wal=NULL;candidate_wal_status_t s=candidate_wal_open(path,&options,&wal);
    candidate_wal_memory_t mem={m,allocate,release};
    if(m!=NULL&&m->clear_memory)m->mutate_memory=&mem;
    if(s==CANDIDATE_WAL_OK)s=candidate_checkpoint_load(wal,a,m==NULL?NULL:&mem,out);
    OK(candidate_wal_close(&wal));return s;
}
static void copy_prefix(const char *from,const char *to,uint64_t size)
{
    for(uint64_t index=0,at=0;at<size;index++){
        char source[300],target[300];CHECK(snprintf(source,300,"%s.wal.%016llx",from,(unsigned long long)index)>0);
        CHECK(snprintf(target,300,"%s.wal.%016llx",to,(unsigned long long)index)>0);
        int in=open(source,O_RDONLY),out=open(target,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(in>=0&&out>=0);
        uint64_t n=size-at<CANDIDATE_WAL_SEGMENT_BYTES?size-at:CANDIDATE_WAL_SEGMENT_BYTES;
        uint8_t bytes[8192];
        for(uint64_t done=0;done<n;){size_t count=n-done<8192?(size_t)(n-done):8192;
            CHECK(read(in,bytes,count)==(ssize_t)count);CHECK(write(out,bytes,count)==(ssize_t)count);done+=count;}
        at+=n;CHECK(close(in)==0&&close(out)==0);
    }
}

static void roundtrip_and_cuts(void)
{
    char path[256];positions_t pos={0};candidate_control_state_t a=fixture(path,129,4,38,&pos);
    memory_t m={0};candidate_checkpoint_t *c=NULL;OK(load(path,&a,&m,&c));candidate_checkpoint_info_t info=candidate_checkpoint_info(c);
    CHECK(info.summary.dirty_count==129&&info.summary.transaction_count==4&&info.active_transactions==1);
    CHECK(m.calls==1&&m.live==info.resident_bytes);
    for(unsigned i=0;i<129;i++)CHECK(candidate_checkpoint_dirty_at(c,i)->ref.id==i+1);
    for(unsigned i=0;i<4;i++)CHECK(candidate_checkpoint_transaction_at(c,i)->id==i+1);
    CHECK(candidate_checkpoint_dirty_at(c,129)==NULL&&candidate_checkpoint_transaction_at(c,4)==NULL);
    printf("resident=%zu; transport/stack/allocator/RSS excluded\n",m.live);
    candidate_checkpoint_close(&c);CHECK(m.live==0);
    for(unsigned i=0;i<=pos.count;i++){
        char cut[256];prefix(cut,100+i);uint64_t n=i==0?64:pos.ends[i-1];copy_prefix(path,cut,n);
        candidate_wal_status_t expected=n>=a.checkpoint_end?CANDIDATE_WAL_OK:CANDIDATE_WAL_CORRUPT;
        CHECK(load(cut,&a,NULL,&c)==expected);candidate_checkpoint_close(&c);
    }
    for(unsigned n=1;n<=32;n++){
        char cut[256];prefix(cut,200+n);copy_prefix(path,cut,a.checkpoint_end-n);CHECK(load(cut,&a,NULL,&c)==CANDIDATE_WAL_CORRUPT&&c==NULL);
    }
    m=(memory_t){.fail=true};CHECK(load(path,&a,&m,&c)==CANDIDATE_WAL_NOMEM&&c==NULL&&m.live==0);
    candidate_control_state_t copy=a;m=(memory_t){.mutate=&copy,.clear_memory=true};OK(load(path,&copy,&m,&c));candidate_checkpoint_close(&c);CHECK(m.live==0&&copy.generation==0);
    candidate_wal_io_t io={fault_pread,pwrite,fsync,ftruncate};candidate_wal_options_t o=options;o.io=&io;
    candidate_wal_t *wal=NULL;OK(candidate_wal_open(path,&o,&wal));m=(memory_t){0};candidate_wal_memory_t memory={&m,allocate,release};
    fail_read_at=a.checkpoint_end;CHECK(candidate_checkpoint_load(wal,&a,&memory,&c)==CANDIDATE_WAL_IO&&c==NULL&&m.live==0);
    fail_read_at=0;OK(candidate_wal_close(&wal));
    printf("FIXTURE checkpoint %s begin=%llu end=%llu dirty=129 transactions=4\n",path,(unsigned long long)a.checkpoint_begin,(unsigned long long)a.checkpoint_end);
}

static void malformed(void)
{
    const unsigned modes[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,41,42,43,45,46,47,48,49,50};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++){
        unsigned mode=modes[i];unsigned dirty=mode==12||mode==13||mode==14||mode==15||mode>=29?0:129;
        unsigned tx=mode==12||mode==13?129:(mode==15?65:4);
        char path[256];candidate_control_state_t a=fixture(path,dirty,tx,mode,NULL);candidate_checkpoint_t *c=NULL;memory_t m={0};
        candidate_wal_status_t s=load(path,&a,&m,&c);
        candidate_wal_status_t expected=mode==1||mode==2||mode==3||mode==19||mode==26||mode==28||mode==29||mode==30?
            CANDIDATE_WAL_UNSUPPORTED:(mode==5||mode==6||mode==15?CANDIDATE_WAL_LIMIT:CANDIDATE_WAL_CORRUPT);
        if(s!=expected)fprintf(stderr,"mode=%u got=%u expected=%u\n",mode,s,expected);
        CHECK(s==expected&&c==NULL&&m.live==0);
    }
}

static candidate_wal_status_t verify(void *context,const candidate_control_state_t *anchor)
{
    verifier_t *v=context;verify_calls++;candidate_wal_memory_t memory={&v->memory,allocate,release};
    candidate_checkpoint_t *c=NULL;candidate_wal_status_t s=candidate_checkpoint_load(v->wal,anchor,&memory,&c);
    candidate_checkpoint_close(&c);CHECK(v->memory.live==0);return s;
}
static void control_selection(void)
{
    const unsigned modes[]={16,26,5,0};
    const candidate_wal_status_t expected[]={CANDIDATE_WAL_OK,CANDIDATE_WAL_UNSUPPORTED,CANDIDATE_WAL_LIMIT,CANDIDATE_WAL_NOMEM};
    for(unsigned n=0;n<4;n++){
        char path[256];prefix(path,600+n);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));
        uint8_t payload[512]={0};append(wal,2,0,payload,512,NULL);
        candidate_control_state_t first=write_checkpoint(wal,1,1,0,1,NULL);
        candidate_control_state_t second=write_checkpoint(wal,1,1,modes[n],2,NULL);second.generation=2;second.slot=1;
        OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));
        verifier_t v={.wal=wal};candidate_control_options_t o={.backend=1,.verify=verify,.context=&v};memcpy(o.uuid,options.uuid,16);
        char file[300];CHECK(snprintf(file,300,"%s.ctrl",path)>0);candidate_control_t *control=NULL;
        OK(candidate_control_create(file,&o,&control));OK(candidate_control_publish(control,&first));OK(candidate_control_close(&control));
        uint8_t bytes[8192];OK(candidate_control_encode(&second,bytes,sizeof(bytes)));
        int fd=open(file,O_RDWR);CHECK(fd>=0);CHECK(pwrite(fd,bytes,8192,8192)==8192);CHECK(fsync(fd)==0&&close(fd)==0);
        verify_calls=0;v.memory.fail=n==3;CHECK(candidate_control_open(file,&o,&control)==expected[n]);
        CHECK(verify_calls==(n==0?2u:1u));
        if(n==0){CHECK(candidate_control_info(control).selected==0);CHECK(candidate_control_info(control).valid_mask==3&&candidate_control_info(control).redo_floor==64);}
        else CHECK(control==NULL);
        OK(candidate_control_close(&control));OK(candidate_wal_close(&wal));
        printf("FIXTURE control %s prefix=%s first_begin=%llu first_end=%llu second_begin=%llu second_end=%llu mode=%u\n",file,path,
            (unsigned long long)first.checkpoint_begin,(unsigned long long)first.checkpoint_end,
            (unsigned long long)second.checkpoint_begin,(unsigned long long)second.checkpoint_end,modes[n]);
    }
}

static void maximum_and_codec(void)
{
    char path[256];candidate_control_state_t a=fixture(path,8192,4096,40,NULL);candidate_checkpoint_t *c=NULL;
    OK(load(path,&a,NULL,&c));candidate_checkpoint_info_t info=candidate_checkpoint_info(c);
    CHECK(info.summary.dirty_count==8192&&info.summary.transaction_count==4096);
    CHECK(candidate_checkpoint_dirty_at(c,8191)->ref.id==8192&&candidate_checkpoint_transaction_at(c,4095)->id==4096);
    CHECK(info.transport.segments==2);candidate_checkpoint_close(&c);
    printf("FIXTURE maximum %s begin=%llu end=%llu dirty=8192 transactions=4096\n",path,(unsigned long long)a.checkpoint_begin,(unsigned long long)a.checkpoint_end);
    candidate_checkpoint_summary_t s={.anchor=a,.dirty_count=8193};uint8_t bytes[7200],old[7200];memset(bytes,0xa5,sizeof(bytes));memcpy(old,bytes,sizeof(bytes));size_t written=99;
    CHECK(candidate_checkpoint_encode_begin(&s,bytes,sizeof(bytes),&written)==CANDIDATE_WAL_LIMIT&&written==99&&!memcmp(bytes,old,sizeof(bytes)));
    s.dirty_count=0;s.transaction_count=4097;CHECK(candidate_checkpoint_encode_begin(&s,bytes,sizeof(bytes),&written)==CANDIDATE_WAL_LIMIT);
    s.transaction_count=0;for(unsigned i=0;i<160;i++)CHECK(candidate_checkpoint_encode_begin(&s,bytes,i,&written)==CANDIDATE_WAL_LIMIT&&written==99&&!memcmp(bytes,old,sizeof(bytes)));
    candidate_checkpoint_chunk_t chunk={.checkpoint_id=1,.kind=1,.count=1,.entries.dirty={{{1,1},64}}};
    for(unsigned i=0;i<56;i++)CHECK(candidate_checkpoint_encode_chunk(&chunk,bytes,i,&written)==CANDIDATE_WAL_LIMIT&&written==99&&!memcmp(bytes,old,sizeof(bytes)));
    candidate_checkpoint_end_t end={.checkpoint_id=1,.begin_lsn=64};
    for(unsigned i=0;i<64;i++)CHECK(candidate_checkpoint_encode_end(&end,bytes,i,&written)==CANDIDATE_WAL_LIMIT&&written==99&&!memcmp(bytes,old,sizeof(bytes)));
    char empty[256];candidate_control_state_t e=fixture(empty,0,0,0,NULL);OK(load(empty,&e,NULL,&c));CHECK(candidate_checkpoint_info(c).summary.transaction_count==0);candidate_checkpoint_close(&c);
    char reordered[256];e=fixture(reordered,0,4,44,NULL);OK(load(reordered,&e,NULL,&c));
    CHECK(candidate_checkpoint_transaction_at(c,1)->csn==4&&candidate_checkpoint_transaction_at(c,3)->csn==2);candidate_checkpoint_close(&c);
}

int main(void)
{
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);
    roundtrip_and_cuts();malformed();control_selection();maximum_and_codec();
    printf("PASS checkpoint checks=%u; anchored snapshot only, no data/terminal corroboration or recovery activation\n",checks);return 0;
}
