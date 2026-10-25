#define main original_system_test_main
#include "test_candidate_wal_system.c"
#undef main

#include <sys/wait.h>

#include "storage/candidate_checkpoint.h"
#include "storage/candidate_redo.h"
#include "../src/storage/candidate_wal_witness_internal.h"

typedef struct {
    char wal[256];
    candidate_control_state_t anchor;
    positions_t positions;
    uint64_t group_ends[5], bad_lsn;
} redo_fixture_t;
typedef struct {
    int fd;
    candidate_wal_t *wal;
    unsigned reads,writes,syncs,fail_read,fail_write,fail_sync,crash_write,crash_sync;
} target_t;
typedef struct {
    unsigned calls,fail_at;
    size_t live,peak;
    candidate_redo_options_t *options;
    candidate_wal_memory_t *memory;
    candidate_control_state_t *anchor;
    candidate_redo_info_t *out;
} redo_memory_t;
static unsigned scan_pass, fail_scan, mutate_scan;
static uint64_t fail_lsn, mutate_lsn;
static bool fail_force;
static unsigned semantic_pass;
static uint64_t read_le(const uint8_t *p,unsigned size)
{
    uint64_t value=0;for(unsigned i=0;i<size;i++)value|=(uint64_t)p[i]<<(8*i);return value;
}

static uint64_t add_images(candidate_wal_t *wal, transaction_t *t, unsigned group_id,
    unsigned first,unsigned count,unsigned generation,positions_t *positions)
{
    uint64_t end=predict_end(candidate_wal_info(wal),count,0);uint32_t crc=0;
    uint8_t begin[16]={0};put(begin,group_id,8);put(begin+8,count,4);put(begin+12,count,4);append(wal,t,3,begin,16,positions,&crc,false);
    for(unsigned i=0;i<count;i++){
        uint8_t body[8224]={0},value=(uint8_t)('a'+first+i-1);put(body,group_id,8);put(body+8,first+i,8);put(body+16,generation,8);
        candidate_page_location_t l=location(first+i,generation,end);
        if(first+i==1){candidate_page_version_t version={.transaction_id=t->id,.table_id=1,.row_id=1,.payload_bytes=1,.slot_generation=1,.inline_bytes=&value};
            OK(candidate_page_encode_versions(&l,&version,1,body+32,8192));}
        else OK(candidate_page_encode_overflow(&l,(candidate_page_ref_t){0},&value,1,body+32,8192));
        append(wal,t,4,body,sizeof(body),positions,&crc,false);
    }
    uint8_t finish[24]={0};put(finish,group_id,8);put(finish+8,count,4);put(finish+12,count,4);put(finish+16,crc,4);
    CHECK(append(wal,t,8,finish,24,positions,NULL,false)==end);return end;
}

static uint64_t add_deltas(candidate_wal_t *wal,transaction_t *t,uint64_t previous,unsigned mode,positions_t *positions)
{
    uint32_t crc=0;uint8_t begin[16]={0};put(begin,3,8);put(begin+8,2,4);put(begin+12,2,4);append(wal,t,3,begin,16,positions,&crc,false);
    for(unsigned i=0;i<2;i++){
        uint8_t body[49]={0};put(body,3,8);put(body+8,i+1,8);put(body+16,1,8);put(body+24,mode==1?previous+8:previous,8);
        put(body+32,1,4);put(body+40,i==0?184:88,4);put(body+44,1,4);body[48]=(uint8_t)('A'+i);
        if(mode==3&&i==1)put(body+40,64,4);
        append(wal,t,5,body,sizeof(body),positions,&crc,false);
    }
    uint8_t finish[24]={0};put(finish,3,8);put(finish+8,2,4);put(finish+12,2,4);put(finish+16,crc,4);
    return append(wal,t,8,finish,24,positions,NULL,false);
}

static uint64_t redo_terminal(candidate_wal_t *wal,transaction_t *t,unsigned csn,positions_t *positions,const uint64_t *ends)
{
    uint8_t body[184]={0};put(body,t->first,8);put(body+8,t->last,8);put(body+16,t->count,8);put(body+24,t->crc,4);
    if(csn==0)return append(wal,t,11,body,32,positions,NULL,false);
    unsigned count=csn==1?2:1;put(body+32,csn,8);put(body+148,count,4);
    for(unsigned i=0;i<count;i++){unsigned index=csn==1?i*2:4;put(body+152+i*16,index+1,8);put(body+160+i*16,ends[index],8);}
    return append(wal,t,10,body,152+count*16,positions,NULL,false);
}

static redo_fixture_t redo_fixture(unsigned mode,bool checkpoint)
{
    redo_fixture_t f={0};options.owner_epoch=7;
    CHECK(snprintf(f.wal,sizeof(f.wal),"build/candidate-redo-%ld-%u-%u",(long)getpid(),mode,++serial)>0);
    candidate_wal_t *wal=NULL;OK(candidate_wal_create(f.wal,&options,&wal));
    transaction_t a={.id=11},b={.id=12},c={.id=13},d={.id=14};uint8_t begin[16]={0};
    append(wal,&a,2,begin,16,&f.positions,NULL,false);append(wal,&b,2,begin,16,&f.positions,NULL,false);
    f.group_ends[0]=add_images(wal,&a,1,1,2,1,&f.positions);
    f.group_ends[1]=add_images(wal,&b,2,3,1,1,&f.positions);redo_terminal(wal,&b,0,&f.positions,f.group_ends);
    f.group_ends[2]=add_deltas(wal,&a,f.group_ends[0],mode,&f.positions);redo_terminal(wal,&a,1,&f.positions,f.group_ends);
    append(wal,&c,2,begin,16,&f.positions,NULL,false);f.group_ends[3]=add_images(wal,&c,4,4,1,1,&f.positions);
    append(wal,&d,2,begin,16,&f.positions,NULL,false);f.group_ends[4]=add_images(wal,&d,5,3,1,mode==2?3:2,&f.positions);redo_terminal(wal,&d,2,&f.positions,f.group_ends);
    if(checkpoint){
        f.anchor=(candidate_control_state_t){.backend=1,.generation=1,.owner_epoch=7,.checkpoint_id=1,
            .checkpoint_begin=record_start(candidate_wal_info(wal),160),.redo_begin=64,.transaction_highwater=100,
            .page_highwater=100,.request_epoch=1,.retention_floor=1,.visible_csn=2,.outcome_root={1,1},.allocation_root={2,1}};
        f.anchor.checkpoint_end=f.anchor.checkpoint_begin+4096;memcpy(f.anchor.uuid,options.uuid,16);
        candidate_checkpoint_summary_t summary={.anchor=f.anchor,.transaction_count=1};uint8_t body[7200],digest[32]={0};size_t size;transaction_t cp={0};
        OK(candidate_checkpoint_encode_begin(&summary,body,sizeof(body),&size));append(wal,&cp,12,body,size,&f.positions,NULL,false);OK(candidate_checkpoint_digest(digest,body,size,digest));
        candidate_checkpoint_chunk_t chunk={.checkpoint_id=1,.kind=2,.count=1};chunk.entries.transactions[0]=(candidate_checkpoint_transaction_t){13,c.first,c.last,0,0,7,CANDIDATE_CHECKPOINT_ACTIVE};
        OK(candidate_checkpoint_encode_chunk(&chunk,body,sizeof(body),&size));append(wal,&cp,13,body,size,&f.positions,NULL,false);OK(candidate_checkpoint_digest(digest,body,size,digest));
        candidate_checkpoint_end_t end={.checkpoint_id=1,.begin_lsn=f.anchor.checkpoint_begin,.chunks=1};memcpy(end.digest,digest,32);
        OK(candidate_checkpoint_encode_end(&end,body,sizeof(body),&size));f.anchor.checkpoint_end=append(wal,&cp,14,body,size,&f.positions,NULL,false);
    }
    uint8_t group[16]={0};put(group,6,8);put(group+8,1,4);put(group+12,1,4);append(wal,&c,3,group,16,&f.positions,NULL,false);
    uint8_t image[8224]={0},value='x';put(image,6,8);put(image+8,5,8);put(image+16,1,8);
    uint64_t image_end=record_start(candidate_wal_info(wal),sizeof(image))+8320;
    candidate_page_location_t l=location(5,1,image_end+120);OK(candidate_page_encode_overflow(&l,(candidate_page_ref_t){0},&value,1,image+32,8192));
    f.bad_lsn=record_start(candidate_wal_info(wal),sizeof(image));append(wal,&c,4,image,sizeof(image),&f.positions,NULL,false);
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
    if(mode==4){char file[320];CHECK(snprintf(file,sizeof(file),"%s.wal.0000000000000000",f.wal)>0);int fd=open(file,O_RDWR);CHECK(fd>=0);
        uint8_t frame[280];CHECK(pread(fd,frame,sizeof(frame),(off_t)f.group_ends[2])==(ssize_t)sizeof(frame));
        put(frame+80+148,0,4);put(frame+68,0,4);put(frame+68,crc32c(0,frame,sizeof(frame)),4);
        CHECK(pwrite(fd,frame,sizeof(frame),(off_t)f.group_ends[2])==(ssize_t)sizeof(frame));CHECK(close(fd)==0);}
    return f;
}

static int target_file(char path[300],unsigned mode)
{
    CHECK(snprintf(path,300,"build/candidate-redo-target-%ld-%u-%u.pages",(long)getpid(),mode,++serial)>0);
    int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);CHECK(fd>=0);return fd;
}

static candidate_wal_status_t read_page(void *context,uint64_t id,uint8_t bytes[8192],bool *present)
{
    target_t *t=context;t->reads++;if(t->reads==t->fail_read)return CANDIDATE_WAL_IO;
    memset(bytes,0,8192);ssize_t size=pread(t->fd,bytes,8192,(off_t)((id-1)*8192));
    if(size<0)return CANDIDATE_WAL_IO;*present=size>0;return CANDIDATE_WAL_OK;
}
static candidate_wal_status_t write_page(void *context,uint64_t id,const uint8_t bytes[8192])
{
    target_t *t=context;t->writes++;CHECK(candidate_wal_info(t->wal).durable_end_lsn>=read_le(bytes+40,8));
    if(t->writes==t->fail_write){CHECK(pwrite(t->fd,bytes,32,(off_t)((id-1)*8192))==32);return CANDIDATE_WAL_IO;}
    CHECK(pwrite(t->fd,bytes,8192,(off_t)((id-1)*8192))==8192);
    if(t->writes==t->crash_write)_exit(41);return CANDIDATE_WAL_OK;
}
static candidate_wal_status_t sync_pages(void *context)
{
    target_t *t=context;t->syncs++;
    if(t->fail_sync)return CANDIDATE_WAL_IO;
    if(t->crash_sync==1)_exit(42);
    CHECK(fsync(t->fd)==0);if(t->crash_sync==2)_exit(43);return CANDIDATE_WAL_OK;
}

static void *redo_allocate(void *context,size_t size)
{
    redo_memory_t *m=context;m->calls++;CHECK(m->out->wal_end_lsn==0);
    if(m->options){memset(m->options,0,sizeof(*m->options));m->options=NULL;}
    if(m->memory){memset(m->memory,0,sizeof(*m->memory));m->memory=NULL;}
    if(m->anchor){memset(m->anchor,0,sizeof(*m->anchor));m->anchor=NULL;}
    if(m->calls==m->fail_at)return NULL;void *p=malloc(size);
    if(p){m->live+=size;if(m->peak<m->live)m->peak=m->live;}return p;
}
static void redo_release(void *context,void *p,size_t size)
{
    redo_memory_t *m=context;CHECK(m->out->wal_end_lsn==0&&m->live>=size);m->live-=size;free(p);
}
static ssize_t redo_pread(int fd,void *bytes,size_t size,off_t at)
{
    if(at==64&&size==80)scan_pass++;
    if(fail_scan!=0&&scan_pass==fail_scan&&(uint64_t)at>=fail_lsn){errno=EIO;return -1;}
    ssize_t got=pread(fd,bytes,size,at);
    if(semantic_pass!=0&&scan_pass==semantic_pass&&size==280&&got==280){uint8_t *p=bytes;put(p+80+148,2,4);put(p+68,0,4);put(p+68,crc32c(0,p,size),4);}
    if(scan_pass==mutate_scan&&(uint64_t)at==mutate_lsn&&size==8320&&got==8320){
        uint8_t *p=bytes;p[32+80+88]^=1;put(p+32+80+52,0,4);put(p+32+80+52,crc32c(0,p+112,8192),4);
        put(p+68,0,4);put(p+68,crc32c(0,p,size),4);
    }
    return got;
}
static int redo_fsync(int fd){if(fail_force){errno=EIO;return -1;}return fsync(fd);}

static candidate_wal_status_t run_redo(const redo_fixture_t *f,target_t *target,redo_memory_t *m,candidate_redo_info_t *info)
{
    candidate_wal_io_t io={redo_pread,pwrite,redo_fsync,ftruncate};candidate_wal_options_t wo=options;wo.io=&io;
    unsigned saved_fail=fail_scan,saved_mutate=mutate_scan,saved_semantic=semantic_pass;bool saved_force=fail_force;
    fail_scan=0;mutate_scan=0;fail_force=false;semantic_pass=0;
    candidate_wal_t *wal=NULL;OK(candidate_wal_open(f->wal,&wo,&wal));target->wal=wal;scan_pass=0;
    fail_scan=saved_fail;mutate_scan=saved_mutate;fail_force=saved_force;semantic_pass=saved_semantic;
    candidate_redo_options_t o={.checkpoint=f->anchor.checkpoint_id?&f->anchor:NULL,.target={target,read_page,write_page,sync_pages}};memcpy(o.uuid,options.uuid,16);
    candidate_wal_memory_t memory={m,redo_allocate,redo_release};
    if(m){m->out=info;o.memory=&memory;if(m->options)m->options=&o;if(m->memory)m->memory=&memory;}
    candidate_wal_status_t result=candidate_redo_execute(wal,&o,info);
    OK(candidate_wal_close(&wal));target->wal=NULL;return result;
}

static void check_target(target_t *target,const redo_fixture_t *f)
{
    for(unsigned id=1;id<=4;id++){
        uint8_t bytes[8192];CHECK(pread(target->fd,bytes,8192,(off_t)((id-1)*8192))==8192);
        candidate_page_info_t info;OK(candidate_page_decode(options.uuid,bytes,8192,&info));
        CHECK(info.location.ref.id==id&&info.location.ref.generation==(id==3?2u:1u));
        CHECK(info.location.lsn==f->group_ends[id<=2?2:(id==3?4:3)]);
        if(id==1){candidate_page_version_t value;OK(candidate_page_get_version(options.uuid,bytes,8192,(candidate_version_ref_t){{1,1},0,1},&value));CHECK(value.inline_bytes[0]=='A');}
        else CHECK(bytes[88]==(id==2?'B':(id==3?'c':'d')));
    }
    uint8_t byte;CHECK(pread(target->fd,&byte,1,4*8192)==0);
}

static void normal_and_bad_history(void)
{
    for(unsigned mode=0;mode<=3;mode++)for(unsigned cp=0;cp<=1;cp++){
        redo_fixture_t f=redo_fixture(mode,cp!=0);char path[300];target_t t={.fd=target_file(path,mode)};candidate_redo_info_t info;redo_memory_t m={0};
        candidate_wal_status_t status=run_redo(&f,&t,&m,&info);
        CHECK(status==(mode?CANDIDATE_WAL_CORRUPT:CANDIDATE_WAL_OK));CHECK(m.live==0);
        if(mode){CHECK(t.writes==(mode==3?3u:0u)&&t.syncs==0&&info.wal_end_lsn==0);}
        else {CHECK(info.groups==5&&info.pages_written==7&&info.pages_skipped==0&&info.distinct_pages==5);CHECK(info.workspace_bytes==m.peak&&m.peak<8*1024*1024);
            check_target(&t,&f);t.writes=0;OK(run_redo(&f,&t,NULL,&info));CHECK(t.writes==0&&info.pages_skipped==7);check_target(&t,&f);
            if(cp)printf("FIXTURE redo %s target=%s begin=%llu end=%llu wal_end=%llu workspace=%zu\n",f.wal,path,
                (unsigned long long)f.anchor.checkpoint_begin,(unsigned long long)f.anchor.checkpoint_end,(unsigned long long)info.wal_end_lsn,info.workspace_bytes);}
        CHECK(close(t.fd)==0);
    }
}

static void semantic_view_regression(void)
{
    redo_fixture_t f=redo_fixture(4,false);char path[300];target_t t={.fd=target_file(path,4)};candidate_redo_info_t info;
    for(unsigned pass=1;pass<=2;pass++){
        semantic_pass=pass;candidate_wal_status_t status=run_redo(&f,&t,NULL,&info);semantic_pass=0;
        printf("SEMANTIC_VIEW pass=%u status=%u writes=%u syncs=%u\n",pass,status,t.writes,t.syncs);
        CHECK(status==CANDIDATE_WAL_CORRUPT&&t.writes==0&&t.syncs==0);
    }
    CHECK(close(t.fd)==0);
}

static void faults_and_reexecution(void)
{
    redo_fixture_t f=redo_fixture(0,false);candidate_redo_info_t info;
    for(unsigned fault=1;fault<=12;fault++){
        char path[300];target_t t={.fd=target_file(path,fault)};int fd=t.fd;
        if(fault<=7)t.fail_write=fault;
        if(fault==8)t.fail_sync=1;
        if(fault>=9){const unsigned reads[]={1,6,8,13};t.fail_read=reads[fault-9];}
        CHECK(run_redo(&f,&t,NULL,&info)==CANDIDATE_WAL_IO&&info.wal_end_lsn==0);
        t=(target_t){.fd=fd};OK(run_redo(&f,&t,NULL,&info));check_target(&t,&f);CHECK(close(fd)==0);
    }
    for(unsigned phase=1;phase<=3;phase++)for(unsigned kind=0;kind<=1;kind++){
        char path[300];target_t t={.fd=target_file(path,phase+20)};int fd=t.fd;
        fail_lsn=f.bad_lsn;mutate_lsn=f.bad_lsn;
        if(kind)mutate_scan=phase;else fail_scan=phase;
        CHECK(run_redo(&f,&t,NULL,&info)==(kind?CANDIDATE_WAL_CORRUPT:CANDIDATE_WAL_IO));
        fail_scan=0;mutate_scan=0;CHECK(info.wal_end_lsn==0&&t.syncs==0&&t.writes==(phase==3?7u:0u));
        t=(target_t){.fd=fd};OK(run_redo(&f,&t,NULL,&info));check_target(&t,&f);CHECK(close(fd)==0);
    }
    char path[300];target_t t={.fd=target_file(path,30)};fail_force=true;
    CHECK(run_redo(&f,&t,NULL,&info)==CANDIDATE_WAL_IO&&info.wal_end_lsn==0&&t.writes==0);fail_force=false;
    OK(run_redo(&f,&t,NULL,&info));check_target(&t,&f);CHECK(close(t.fd)==0);
}

static void target_pages_and_oom(void)
{
    redo_fixture_t f=redo_fixture(0,true);candidate_redo_info_t info;
    char initial[300];target_t original={.fd=target_file(initial,40)};OK(run_redo(&f,&original,NULL,&info));uint8_t pages[4*8192];CHECK(pread(original.fd,pages,sizeof(pages),0)==(ssize_t)sizeof(pages));
    const unsigned offsets[]={0,16,20,24,32,40,48,52,64,72,80,88,184,8191};
    for(unsigned trial=0;trial<sizeof(offsets)/sizeof(offsets[0])+6;trial++){
        char path[300];target_t t={.fd=target_file(path,41)};uint8_t bytes[sizeof(pages)];memcpy(bytes,pages,sizeof(bytes));
        bool torn=trial<sizeof(offsets)/sizeof(offsets[0]);unsigned mode=torn?0:trial-(unsigned)(sizeof(offsets)/sizeof(offsets[0]));
        if(torn)bytes[offsets[trial]]^=1;
        else {if(mode==0)put(bytes+40,f.group_ends[3],8);if(mode==1)put(bytes+40,f.group_ends[2]+8,8);
            if(mode==2)put(bytes+32,2,8);if(mode==3)bytes[0]^=1;if(mode==4)put(bytes+20,2,2);if(mode==5)put(bytes+24,2,8);resign(bytes);}
        CHECK(pwrite(t.fd,bytes,sizeof(bytes),0)==(ssize_t)sizeof(bytes));candidate_wal_status_t status=run_redo(&f,&t,NULL,&info);
        CHECK(status==(torn?CANDIDATE_WAL_OK:((mode==3||mode==4)?CANDIDATE_WAL_UNSUPPORTED:CANDIDATE_WAL_CORRUPT)));
        if(torn){check_target(&t,&f);}else CHECK(t.writes==0&&t.syncs==0&&info.wal_end_lsn==0);
        CHECK(close(t.fd)==0);
    }
    for(unsigned fail=1;fail<=5;fail++){
        char path[300];target_t t={.fd=target_file(path,42)};redo_memory_t m={.fail_at=fail};
        CHECK(run_redo(&f,&t,&m,&info)==CANDIDATE_WAL_NOMEM&&m.live==0&&t.writes==0&&info.wal_end_lsn==0);CHECK(close(t.fd)==0);
    }
    char path[300];target_t t={.fd=target_file(path,43)};candidate_control_state_t saved=f.anchor;
    redo_memory_t m={.options=(candidate_redo_options_t *)1,.memory=(candidate_wal_memory_t *)1,.anchor=&f.anchor};
    OK(run_redo(&f,&t,&m,&info));CHECK(m.live==0&&m.peak==info.workspace_bytes&&f.anchor.checkpoint_id==0);f.anchor=saved;check_target(&t,&f);
    CHECK(close(t.fd)==0&&close(original.fd)==0);
}

static void prefixes_and_processes(void)
{
    redo_fixture_t f=redo_fixture(0,false);candidate_redo_info_t info;
    for(unsigned i=0;i<=f.positions.count;i++){
        char cut[256],path[300];CHECK(snprintf(cut,sizeof(cut),"build/candidate-redo-prefix-%ld-%u-%u",(long)getpid(),i,++serial)>0);
        uint64_t end=i?f.positions.ends[i-1]:64;copy_prefix(f.wal,cut,end);redo_fixture_t prefix=f;strcpy(prefix.wal,cut);
        target_t t={.fd=target_file(path,50)};OK(run_redo(&prefix,&t,NULL,&info));CHECK(info.wal_end_lsn==end);CHECK(close(t.fd)==0);
    }
    char tail[256],path[300];CHECK(snprintf(tail,sizeof(tail),"build/candidate-redo-tail-%ld-%u",(long)getpid(),++serial)>0);
    copy_prefix(f.wal,tail,f.positions.ends[f.positions.count-1]-1);redo_fixture_t truncated=f;strcpy(truncated.wal,tail);target_t target={.fd=target_file(path,51)};
    CHECK(run_redo(&truncated,&target,NULL,&info)==CANDIDATE_WAL_TAIL&&info.wal_end_lsn==0&&target.writes==0);CHECK(close(target.fd)==0);
    for(unsigned point=0;point<5;point++){
        target_t t={.fd=target_file(path,52)};pid_t pid=fork();CHECK(pid>=0);
        if(pid==0){alarm(10);if(point<3){const unsigned writes[]={1,4,7};t.crash_write=writes[point];}else t.crash_sync=point-2;
            candidate_wal_status_t status=run_redo(&f,&t,NULL,&info);_exit(status==CANDIDATE_WAL_OK?44:45);}
        int status;CHECK(waitpid(pid,&status,0)==pid&&WIFEXITED(status));CHECK(WEXITSTATUS(status)==(point<3?41:(point==3?42:43)));
        OK(run_redo(&f,&t,NULL,&info));check_target(&t,&f);CHECK(close(t.fd)==0);
    }
}

static candidate_wal_status_t load_restored(void *context,candidate_page_ref_t ref,uint8_t bytes[8192])
{
    bool present=false;candidate_wal_status_t status=read_page(context,ref.id,bytes,&present);
    return status==CANDIDATE_WAL_OK&&!present?CANDIDATE_WAL_IO:status;
}

static void metadata_and_capacity(void)
{
    redo_fixture_t f={0};CHECK(snprintf(f.wal,sizeof(f.wal),"build/candidate-redo-metadata-%ld-%u",(long)getpid(),++serial)>0);
    candidate_wal_t *wal=NULL;OK(candidate_wal_create(f.wal,&options,&wal));transaction_t system={0};uint64_t end=group(&wal,f.wal,&system,1,0,1,1,0,NULL);
    OK(candidate_wal_force(wal,end));OK(candidate_wal_close(&wal));char path[300];target_t t={.fd=target_file(path,60)};candidate_redo_info_t info;
    OK(run_redo(&f,&t,NULL,&info));CHECK(info.pages_written==4);
    candidate_catalog_load_t catalog={.root={3,1},.load=load_restored,.context=&t};memcpy(catalog.uuid,options.uuid,16);candidate_catalog_t *c=NULL;OK(candidate_catalog_load(&catalog,&c));CHECK(candidate_catalog_root(c)->current_epoch==1);candidate_catalog_close(&c);
    candidate_allocation_load_t allocation={.root={1,1},.load=load_restored,.context=&t};memcpy(allocation.uuid,options.uuid,16);candidate_allocation_t *a=NULL;OK(candidate_allocation_load(&allocation,&a));CHECK(candidate_allocation_info(a).count==4);candidate_allocation_close(&a);
    printf("FIXTURE metadata %s target=%s end=%llu\n",f.wal,path,(unsigned long long)end);CHECK(close(t.fd)==0);
    f=(redo_fixture_t){0};CHECK(snprintf(f.wal,sizeof(f.wal),"build/candidate-redo-capacity-%ld-%u",(long)getpid(),++serial)>0);
    OK(candidate_wal_create(f.wal,&options,&wal));transaction_t user={.id=21};uint8_t begin[16]={0};append(wal,&user,2,begin,16,NULL,NULL,false);
    for(unsigned i=0;i<520;i+=64)add_images(wal,&user,i/64+1,i+1,520-i<64?520-i:64,1,NULL);
    OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));CHECK(candidate_wal_info(wal).segments==2);OK(candidate_wal_close(&wal));
    t=(target_t){.fd=target_file(path,61)};redo_memory_t memory={0};OK(run_redo(&f,&t,&memory,&info));CHECK(info.pages_written==520&&info.groups==9&&info.distinct_pages==520&&memory.peak==info.workspace_bytes&&memory.live==0);
    OK(run_redo(&f,&t,NULL,&info));CHECK(info.pages_written==0&&info.pages_skipped==520);
    printf("FIXTURE capacity %s target=%s end=%llu pages=520 groups=9\n",f.wal,path,(unsigned long long)info.wal_end_lsn);CHECK(close(t.fd)==0);
}

static bool stop_observing(void *context,const candidate_wal_record_t *r)
{
    unsigned *count=context;(void)r;(*count)++;return false;
}

static void observer_stop(void)
{
    for(unsigned cp=0;cp<=1;cp++){
        redo_fixture_t f=redo_fixture(0,cp!=0);candidate_wal_t *wal=NULL;OK(candidate_wal_open(f.wal,&options,&wal));candidate_witness_options_t o={.enable_system_groups=true};memcpy(o.uuid,options.uuid,16);
        unsigned count=0;candidate_wal_witness_t *w=NULL;
        CHECK(candidate_witness_analyze_observed(wal,&o,cp?&f.anchor:NULL,stop_observing,&count,&w)==CANDIDATE_WAL_STOPPED&&count==1&&w==NULL);OK(candidate_wal_close(&wal));
    }
}

static void missing_base_and_invalid_options(void)
{
    redo_fixture_t f={0};CHECK(snprintf(f.wal,sizeof(f.wal),"build/candidate-redo-no-base-%ld-%u",(long)getpid(),++serial)>0);
    candidate_wal_t *wal=NULL;OK(candidate_wal_create(f.wal,&options,&wal));transaction_t t={.id=1};uint8_t begin[16]={0};
    append(wal,&t,2,begin,16,NULL,NULL,false);add_deltas(wal,&t,64,0,NULL);OK(candidate_wal_force(wal,candidate_wal_info(wal).valid_end_lsn));OK(candidate_wal_close(&wal));
    char path[300];target_t target={.fd=target_file(path,70)};candidate_redo_info_t info;
    CHECK(run_redo(&f,&target,NULL,&info)==CANDIDATE_WAL_CORRUPT&&target.writes==0&&info.wal_end_lsn==0);
    OK(candidate_wal_open(f.wal,&options,&wal));candidate_redo_options_t o={.target={&target,read_page,write_page,sync_pages}};memcpy(o.uuid,options.uuid,16);
    CHECK(candidate_redo_execute(NULL,&o,&info)==CANDIDATE_WAL_INVALID&&info.wal_end_lsn==0);
    CHECK(candidate_redo_execute(wal,NULL,&info)==CANDIDATE_WAL_INVALID&&info.wal_end_lsn==0);
    CHECK(candidate_redo_execute(wal,&o,NULL)==CANDIDATE_WAL_INVALID);
    o.target.read=NULL;CHECK(candidate_redo_execute(wal,&o,&info)==CANDIDATE_WAL_INVALID);
    OK(candidate_wal_close(&wal));CHECK(close(target.fd)==0);
}

int main(void)
{
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);normal_and_bad_history();semantic_view_regression();
    faults_and_reexecution();target_pages_and_oom();prefixes_and_processes();metadata_and_capacity();observer_stop();missing_base_and_invalid_options();
    printf("PASS physical redo checks=%u; no root selection, outcome resolution or recovery activation\n",checks);return 0;
}
