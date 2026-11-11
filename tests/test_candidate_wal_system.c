#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage/candidate_allocation.h"
#include "storage/candidate_catalog.h"
#include "storage/candidate_wal_witness.h"
#include "storage/checksum.h"

#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#c); exit(1); } } while (0)
#define OK(c) CHECK((c) == CANDIDATE_WAL_OK)
static unsigned checks, serial;
static uint64_t fail_read_lsn;
static candidate_wal_options_t options = {.uuid={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},.owner_epoch=7};
typedef struct { uint64_t id, first, last, count; uint32_t crc; } transaction_t;
typedef struct { uint64_t ends[128]; unsigned count; } positions_t;
typedef struct { unsigned calls, fail_at; size_t live, peak; candidate_witness_options_t *mutate; } memory_t;

static void put(uint8_t *p, uint64_t v, unsigned n) { for (unsigned i=0;i<n;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void prefix(char *p, unsigned mode) { CHECK(snprintf(p,256,"build/candidate-system-%ld-%u-%u",(long)getpid(),mode,++serial)>0); }
static void resign(uint8_t *p) { memset(p+52,0,4); put(p+52,crc32c(0,p,8192),4); }

static uint64_t record_start(candidate_wal_info_t info, size_t body)
{
    uint64_t total=(96+body+7)&~(uint64_t)7;
    uint64_t offset=info.valid_end_lsn-(uint64_t)(info.segments-1)*CANDIDATE_WAL_SEGMENT_BYTES;
    return total>CANDIDATE_WAL_SEGMENT_BYTES-offset?
        (uint64_t)info.segments*CANDIDATE_WAL_SEGMENT_BYTES+64:info.valid_end_lsn;
}

static uint64_t predict_end(candidate_wal_info_t info, unsigned pages, unsigned events)
{
    for(unsigned i=0;i<pages+events+2;i++){
        size_t size=i==0?16:(i<=pages?8224:(i<=pages+events?40:24));
        uint64_t start=record_start(info,size);
        if(start!=info.valid_end_lsn)info.segments++;
        info.valid_end_lsn=start+((96+size+7)&~(uint64_t)7);
    }
    return info.valid_end_lsn;
}

static uint64_t append(candidate_wal_t *wal, transaction_t *t, unsigned type, const uint8_t *body,
    size_t bytes, positions_t *positions, uint32_t *group_crc, bool bad_previous)
{
    candidate_wal_record_t r = {.type=(uint16_t)type,.lsn=record_start(candidate_wal_info(wal),bytes),
        .transaction_id=t->id,.previous_lsn=bad_previous?0:t->last,.owner_epoch=options.owner_epoch,.payload=body,.payload_bytes=bytes};
    uint64_t end; OK(candidate_wal_append(wal,&r,&end)); CHECK(end==r.lsn+((96+bytes+7)&~(uint64_t)7));
    if (positions != NULL) { CHECK(positions->count<128); positions->ends[positions->count++]=end; }
    if (t->count==0) t->first=r.lsn;
    t->last=r.lsn; t->count++; t->crc=candidate_wal_witness_crc(t->crc,&r);
    if (group_crc!=NULL) *group_crc=candidate_wal_witness_crc(*group_crc,&r);
    return end;
}

static candidate_page_location_t location(unsigned id, uint64_t generation, uint64_t lsn)
{
    candidate_page_location_t l={.ref={id,generation},.lsn=lsn}; memcpy(l.uuid,options.uuid,16); return l;
}

static uint64_t group(candidate_wal_t **wal, const char *path, transaction_t *t, uint64_t id,
    unsigned kind, uint64_t generation, unsigned count, unsigned mode, positions_t *positions)
{
    if (t->id==0) *t=(transaction_t){0};
    unsigned pages=kind==15?1:2;
    if (kind==0) pages=3+count;
    if (kind==6) pages+=count;
    if (mode==4) pages--;
    if (mode==10) pages--;
    unsigned events=kind==0?count+1:(kind==6||kind==7?count:1);
    if (mode==19) events++;
    uint64_t end=predict_end(candidate_wal_info(*wal),pages,events);
    uint8_t images[64][8192]; unsigned image_count=0;
    candidate_catalog_root_t catalog={.owner_epoch=mode==11?8:7,.current_epoch=(kind==15||kind==16)?2:1,
        .retention_floor=(kind==15||kind==16)?2:1,.transaction_highwater=kind==16?200:100,
        .page_highwater=kind==16?200:100,.retained_bytes=8192};
    candidate_allocation_root_t allocation={.owner_epoch=7,.page_highwater=kind==16?200:100,.gc_horizon=5,.count=3+count,.nodes={{2,1}}};
    if(mode==34){catalog.current_epoch=6;catalog.retention_floor=6;}
    candidate_page_location_t l;
    if (kind==0||kind==15||kind==16) {
        l=location(3,1,end); OK(candidate_catalog_encode_root(&l,&catalog,images[image_count++],8192));
    }
    if (kind!=15 && mode!=10) {
        l=location(1,1,end); OK(candidate_allocation_encode_root(&l,&allocation,images[image_count++],8192));
    }
    if (kind==0||kind==6||kind==7) {
        candidate_allocation_node_t node={.count=3+count};
        for (unsigned i=0;i<3;i++) node.entries[i]=(candidate_allocation_entry_t){.ref={i+1,1},.state=CANDIDATE_ALLOCATION_LIVE};
        for (unsigned i=0;i<count;i++) node.entries[i+3]=(candidate_allocation_entry_t){.ref={10+i,generation},
            .state=kind==7?CANDIDATE_ALLOCATION_FREE:CANDIDATE_ALLOCATION_LIVE,
            .release_horizon=kind==7?5:0,.transition_lsn=end};
        l=location(2,1,end); OK(candidate_allocation_encode_node(&l,&node,images[image_count],8192));
        if (mode==6) put(images[image_count]+96+3*40+8,2,8);
        if (mode==7) { put(images[image_count]+96+3*40+16,2,4); put(images[image_count]+96+3*40+24,5,8); }
        if (mode==8) put(images[image_count]+72,1,4);
        if (mode==9) put(images[image_count]+96+3*40+32,0,8);
        if (mode==31) images[image_count][80]=1;
        resign(images[image_count++]);
    }
    if ((kind==0||kind==6)&&mode!=4) {
        for (unsigned i=0;i<count;i++) {
            l=location(10+i,generation,end); uint8_t value=0x51;
            OK(candidate_page_encode_overflow(&l,(candidate_page_ref_t){0},&value,1,images[image_count],8192));
            if (mode==32) put(images[image_count]+40,end+8,8);
            if (mode==33) put(images[image_count]+80,0,4);
            resign(images[image_count++]);
        }
    }
    CHECK(image_count==pages);
    uint8_t begin[16]={0}; put(begin,id,8); put(begin+8,mode==15?pages+1:pages,4); put(begin+12,pages+events,4);
    uint32_t crc=0; append(*wal,t,3,begin,16,positions,&crc,false);
    for (unsigned i=0;i<pages;i++) {
        uint8_t body[8224]={0}; put(body,id,8); memcpy(body+8,images[i]+24,16);
        if (mode==5 && i==pages-1) put(body+24,64,8);
        memcpy(body+32,images[i],8192);
        append(*wal,t,4,body,sizeof(body),positions,&crc,mode==12&&i==0);
    }
    for (unsigned i=0;i<events;i++) {
        unsigned type=kind==0?(i==0?16:6):kind;
        uint8_t body[40]={0}; put(body,id,8);
        if (type==16) {
            put(body+8,kind==0?0:(mode==21?90:100),8); put(body+16,kind==0?100:200,8);
            put(body+24,kind==0?0:100,8); put(body+32,kind==0?100:200,8);
            if (mode==1) put(body+8,101,8);
            if (mode==2) put(body+32,101,8);
        } else if (type==15) {
            put(body+8,1,8);put(body+16,mode==25?6:2,8);put(body+24,1,8);put(body+32,2,8);
            if(mode==34){put(body+8,6,8);put(body+16,6,8);put(body+32,6,8);}
        } else {
            unsigned index=kind==0?i-1:i; if (mode==19&&i==events-1) index=0;
            put(body+8,10+index,8);put(body+16,type==6?generation-1:generation,8);put(body+24,generation,8);put(body+32,type==7?5:0,8);
            if (mode==3) put(body+24,2,8);
            if (mode==24) {put(body+16,UINT64_MAX,8);put(body+24,UINT64_MAX,8);}
            if (mode==30) put(body+32,6,8);
        }
        if (mode==13 && i==events-1) { OK(candidate_wal_close(wal));options.owner_epoch=8;OK(candidate_wal_open(path,&options,wal)); }
        append(*wal,t,type,body,40,positions,&crc,false);
    }
    uint8_t finish[24]={0};put(finish,id,8);put(finish+8,pages,4);put(finish+12,pages+events,4);put(finish+16,mode==14?crc^1:crc,4);
    uint64_t actual=append(*wal,t,8,finish,24,positions,NULL,false);CHECK(actual==end);return end;
}

static void terminal(candidate_wal_t *wal, transaction_t *t, positions_t *positions)
{
    uint8_t body[32]={0};put(body,t->first,8);put(body+8,t->last,8);put(body+16,t->count,8);put(body+24,t->crc,4);
    append(wal,t,11,body,32,positions,NULL,false);
}

static void *allocate(void *context,size_t n)
{
    memory_t *m=context;m->calls++;
    if(m->mutate!=NULL){memset(m->mutate,0,sizeof(*m->mutate));m->mutate=NULL;}
    if(m->calls==m->fail_at)return NULL;
    void *p=malloc(n);if(p!=NULL){m->live+=n;if(m->peak<m->live)m->peak=m->live;}return p;
}
static void release(void *context,void *p,size_t n){memory_t *m=context;CHECK(m->live>=n);m->live-=n;free(p);}

static ssize_t fault_pread(int fd,void *out,size_t n,off_t offset)
{
    if(fail_read_lsn>0&&(uint64_t)offset>=fail_read_lsn){errno=EIO;return -1;}
    return pread(fd,out,n,offset);
}

static candidate_wal_status_t analyze(const char *p,bool enabled,memory_t *m,candidate_wal_witness_t **out)
{
    candidate_wal_t *wal=NULL;candidate_wal_status_t status=candidate_wal_open(p,&options,&wal);
    candidate_witness_options_t o={.enable_system_groups=enabled};memcpy(o.uuid,options.uuid,16);
    candidate_wal_memory_t memory={m,allocate,release};if(m!=NULL){o.memory=&memory;if(m->mutate!=NULL)m->mutate=&o;}
    if(status==CANDIDATE_WAL_OK)status=candidate_wal_witness_analyze(wal,&o,out);
    OK(candidate_wal_close(&wal));return status;
}

static void copy_prefix(const char *from,const char *to,uint64_t size)
{
    for(uint64_t segment=0,at=0;at<size;segment++){
        char source[300],target[300];
        CHECK(snprintf(source,sizeof(source),"%s.wal.%016llx",from,(unsigned long long)segment)>0);
        CHECK(snprintf(target,sizeof(target),"%s.wal.%016llx",to,(unsigned long long)segment)>0);
        int in=open(source,O_RDONLY),out=open(target,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(in>=0&&out>=0);
        uint64_t end=size-at<CANDIDATE_WAL_SEGMENT_BYTES?size:at+CANDIDATE_WAL_SEGMENT_BYTES;
        uint8_t bytes[8192];
        while(at<end){size_t n=end-at<8192?(size_t)(end-at):8192;CHECK(read(in,bytes,n)==(ssize_t)n);CHECK(write(out,bytes,n)==(ssize_t)n);at+=n;}
        CHECK(close(in)==0&&close(out)==0);
    }
}

static void complete_and_cuts(char path[256])
{
    prefix(path,0);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));transaction_t t={0};positions_t pos={0};
    group(&wal,path,&t,1,0,1,1,0,&pos);group(&wal,path,&t,2,7,1,1,0,&pos);group(&wal,path,&t,3,6,2,1,0,&pos);
    group(&wal,path,&t,4,15,2,1,0,&pos);group(&wal,path,&t,5,16,2,1,0,&pos);
    transaction_t user={.id=11};uint8_t begin[16]={0};append(wal,&user,2,begin,16,&pos,NULL,false);terminal(wal,&user,&pos);
    uint8_t pending[16]={0};put(pending,6,8);put(pending+8,1,4);put(pending+12,2,4);t=(transaction_t){0};
    uint64_t end=append(wal,&t,3,pending,16,&pos,NULL,false);OK(candidate_wal_force(wal,end));OK(candidate_wal_close(&wal));
    memory_t memory={0};candidate_wal_witness_t *w=NULL;OK(analyze(path,true,&memory,&w));
    candidate_witness_system_info_t info=candidate_wal_witness_system_info(w);
    CHECK(info.events==6&&info.system_groups==5&&info.transaction_highwater==200&&info.page_highwater==200&&info.current_epoch==2&&info.retention_floor==2);
    CHECK(candidate_wal_witness_info(w).aborts==1&&candidate_wal_witness_info(w).complete_groups==5&&candidate_wal_witness_info(w).incomplete_group_lsn==pos.ends[pos.count-2]);
    CHECK(memory.live==candidate_wal_witness_info(w).resident_bytes);printf("resident=%zu; allocator/stack/loader/RSS excluded\n",memory.live);
    for(unsigned i=0;i<6;i++){const candidate_witness_system_event_t *e=candidate_wal_witness_system_at(w,i);CHECK(e!=NULL&&e->end_lsn>64&&e->owner_epoch==7);}
    CHECK(candidate_wal_witness_system_at(w,6)==NULL);candidate_wal_witness_close(&w);CHECK(memory.live==0);
    CHECK(analyze(path,false,NULL,&w)==CANDIDATE_WAL_UNSUPPORTED&&w==NULL);
    for(unsigned i=0;i<=pos.count;i++){
        char cut[256];prefix(cut,100+i);copy_prefix(path,cut,i==0?64:pos.ends[i-1]);OK(analyze(cut,true,NULL,&w));
        candidate_witness_system_info_t seen=candidate_wal_witness_system_info(w);
        unsigned expected=(i>=8?2:0)+(i>=13?1:0)+(i>=19?1:0)+(i>=23?1:0)+(i>=28?1:0);
        CHECK(seen.events==expected);candidate_wal_witness_close(&w);
    }
    for(unsigned fail=1;fail<=2;fail++){memory=(memory_t){.fail_at=fail};CHECK(analyze(path,true,&memory,&w)==CANDIDATE_WAL_NOMEM&&w==NULL&&memory.live==0);}
    memory=(memory_t){.mutate=(candidate_witness_options_t *)1};OK(analyze(path,true,&memory,&w));candidate_wal_witness_close(&w);CHECK(memory.live==0);
    candidate_wal_io_t io={fault_pread,pwrite,fsync,ftruncate};candidate_wal_options_t o=options;o.io=&io;
    candidate_wal_t *reading=NULL;OK(candidate_wal_open(path,&o,&reading));
    memory=(memory_t){0};candidate_wal_memory_t allocator={&memory,allocate,release};
    candidate_witness_options_t wo={.memory=&allocator,.enable_system_groups=true};memcpy(wo.uuid,options.uuid,16);
    fail_read_lsn=pos.ends[27];CHECK(candidate_wal_witness_analyze(reading,&wo,&w)==CANDIDATE_WAL_IO&&w==NULL&&memory.live==0);
    fail_read_lsn=0;OK(candidate_wal_close(&reading));
    printf("FIXTURE system %s records=%u events=6 groups=5\n",path,pos.count);
}

static void malformed(void)
{
    const unsigned modes[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,19,24,31,32,33,20,21,22,23,25,27,28,30};
    for(unsigned n=0;n<sizeof(modes)/sizeof(modes[0]);n++){
        unsigned mode=modes[n];options.owner_epoch=7;char path[256];prefix(path,mode);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));transaction_t t={0};
        bool advanced=mode==20||mode==21||mode==22||mode==23||mode==25||mode==27||mode==28||mode==30;
        group(&wal,path,&t,1,0,1,1,advanced?0:mode,NULL);
        if(mode==20){transaction_t user={.id=11};uint8_t admit[64]={0};put(admit,1,8);append(wal,&user,1,admit,64,NULL,NULL,false);group(&wal,path,&t,2,15,1,1,0,NULL);}
        if(mode==21)group(&wal,path,&t,2,16,1,1,mode,NULL);
        if(mode==22)group(&wal,path,&t,2,6,2,1,0,NULL);
        if(mode==23)group(&wal,path,&t,2,7,2,1,0,NULL);
        if(mode==25)group(&wal,path,&t,2,15,1,1,mode,NULL);
        if(mode==27||mode==28){transaction_t user={.id=mode==28?201:11};uint8_t admit[64]={0};put(admit,3,8);append(wal,&user,1,admit,64,NULL,NULL,false);}
        if(mode==30)group(&wal,path,&t,2,7,1,1,mode,NULL);
        OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
        candidate_wal_witness_t *w=NULL;candidate_wal_status_t status=analyze(path,true,NULL,&w);
        candidate_wal_status_t expected=mode==24?CANDIDATE_WAL_LIMIT:(mode==31?CANDIDATE_WAL_UNSUPPORTED:CANDIDATE_WAL_CORRUPT);
        if(status!=expected)fprintf(stderr,"mode=%u got=%u expected=%u\n",mode,status,expected);
        CHECK(status==expected&&w==NULL);
    }
    options.owner_epoch=7;
}

static void history_limit(void)
{
    char path[256];prefix(path,4096);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));transaction_t t={0};
    group(&wal,path,&t,1,0,1,1,0,NULL);
    uint64_t complete=0;
    for(unsigned i=0;i<67;i++){
        uint64_t end=group(&wal,path,&t,i+2,i%2==0?7:6,(i+3)/2,62,0,NULL);
        if(i==65)complete=end;
    }
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
    candidate_wal_witness_t *w=NULL;CHECK(analyze(path,true,NULL,&w)==CANDIDATE_WAL_LIMIT&&w==NULL);
    char cut[256];prefix(cut,4094);copy_prefix(path,cut,complete);OK(analyze(cut,true,NULL,&w));
    CHECK(candidate_wal_witness_system_info(w).events==4094&&candidate_wal_witness_system_info(w).system_groups==67);
    candidate_wal_witness_close(&w);printf("FIXTURE limit %s complete=%llu events=4094\n",path,(unsigned long long)complete);
}

static void abort_and_protocol(void)
{
    for(unsigned mode=0;mode<14;mode++){
        char path[256];prefix(path,5000+mode);candidate_wal_t *wal=NULL;OK(candidate_wal_create(path,&options,&wal));
        transaction_t system={0},user={.id=11};
        uint8_t begin[16]={0};
        if(mode>=11){
            uint8_t checkpoint[16]={0};append(wal,&system,mode+1,checkpoint,16,NULL,NULL,false);
        }else if(mode==10){
            group(&wal,path,&system,1,15,1,1,34,NULL);
        }else if(mode>=8){
            group(&wal,path,&system,1,0,1,1,0,NULL);group(&wal,path,&system,2,7,1,mode==8?64:65,0,NULL);
        }else if(mode<=1){
            group(&wal,path,&system,1,0,1,1,0,NULL);
            if(mode==0){
                group(&wal,path,&system,2,7,1,1,0,NULL);append(wal,&user,2,begin,16,NULL,NULL,false);
                group(&wal,path,&user,3,6,2,1,0,NULL);terminal(wal,&user,NULL);
            }else{
                uint8_t admit[64]={0};put(admit,1,8);append(wal,&user,1,admit,64,NULL,NULL,false);terminal(wal,&user,NULL);
                group(&wal,path,&system,2,15,1,1,0,NULL);
            }
        }else if(mode==2||mode==3){
            uint8_t event[40]={0};append(wal,&system,mode==2?15:16,event,40,NULL,NULL,false);
        }else if(mode==4){
            uint8_t start[16]={0};put(start,1,8);put(start+8,1,4);put(start+12,1,4);append(wal,&system,3,start,16,NULL,NULL,false);
            uint8_t delta[49]={0};put(delta,1,8);put(delta+8,10,8);put(delta+16,1,8);put(delta+32,1,4);put(delta+40,64,4);put(delta+44,1,4);delta[48]=1;
            append(wal,&system,5,delta,49,NULL,NULL,false);
        }else if(mode==5||mode==6){
            uint8_t start[16]={0};put(start,1,8);put(start+8,mode==5?65:1,4);put(start+12,mode==5?65:129,4);append(wal,&system,3,start,16,NULL,NULL,false);
        }else{
            append(wal,&user,2,begin,16,NULL,NULL,false);group(&wal,path,&system,1,0,1,1,0,NULL);
        }
        OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
        candidate_wal_witness_t *w=NULL;candidate_wal_status_t status=analyze(path,true,NULL,&w);
        candidate_wal_status_t expected=mode<=1||mode==8?CANDIDATE_WAL_OK:(mode==4||mode>=11?CANDIDATE_WAL_UNSUPPORTED:(mode==5||mode==6||mode==9?CANDIDATE_WAL_LIMIT:CANDIDATE_WAL_CORRUPT));
        if(status!=expected)fprintf(stderr,"protocol=%u got=%u expected=%u\n",mode,status,expected);
        CHECK(status==expected);
        if(mode==0)CHECK(candidate_wal_witness_system_info(w).events==4&&candidate_wal_witness_info(w).aborts==1&&candidate_wal_witness_info(w).complete_groups==3);
        if(mode==1)CHECK(candidate_wal_witness_system_info(w).retention_floor==2&&candidate_wal_witness_info(w).aborts==1);
        if(mode==8)CHECK(candidate_wal_witness_system_info(w).events==66);
        candidate_wal_witness_close(&w);
    }
}

int main(void)
{
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);char path[256];complete_and_cuts(path);malformed();history_limit();abort_and_protocol();
    printf("PASS WAL system checks=%u; observed-prefix witness only, no recovery activation/ID issuance\n",checks);return 0;
}
