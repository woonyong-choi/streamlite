#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "storage/candidate_control.h"
#include "storage/checksum.h"

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1); } } while (0)
#define OK(x) CHECK((x)==CANDIDATE_WAL_OK)
static unsigned checks, serial, writes, syncs, reads, interrupts, short_bytes, fail_sync_at;
static unsigned read_fail_at, read_zero_at, sync_interrupts;
static bool write_zero, write_oversize;
static long cut = -1;
static uint64_t rejected_checkpoint;
static candidate_wal_status_t rejected_status = CANDIDATE_WAL_CORRUPT;
static candidate_control_t *active;
static bool reenter, crash_verify, crash_write, crash_sync;
static const uint8_t uuid[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

static void write_le(uint8_t *p,uint64_t v,unsigned n) { for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(i*8)); }
static void resign(uint8_t *p) { memset(p+176,0,4);write_le(p+176,crc32c(0,p,8192),4); }
static void path_name(char p[PATH_MAX]) { CHECK(snprintf(p,PATH_MAX,"build/candidate-control-%ld-%u.ctrl",(long)getpid(),serial++)<PATH_MAX); }
static candidate_control_state_t state(uint64_t checkpoint)
{
    candidate_control_state_t s={.backend=1,.generation=1,.owner_epoch=7,.checkpoint_id=checkpoint,
        .checkpoint_begin=64+checkpoint*1024,.checkpoint_end=1024+checkpoint*1024,.redo_begin=64,
        .transaction_highwater=9,.page_highwater=133,.request_epoch=1,.retention_floor=1,.visible_csn=1,
        .outcome_root={132,1},.allocation_root={133,1},.catalog_root={1,1},.gc_horizon=1};memcpy(s.uuid,uuid,16);return s;
}
static candidate_wal_status_t verify(void *context,const candidate_control_state_t *s)
{
    (void)context;
    if(crash_verify && s->checkpoint_id==2)_exit(0);
    if(reenter && active!=NULL) {
        candidate_control_t *copy=active;CHECK(candidate_control_publish(active,s)==CANDIDATE_WAL_INVALID);
        CHECK(candidate_control_close(&copy)==CANDIDATE_WAL_INVALID && copy==active);
    }
    return s->checkpoint_id==rejected_checkpoint?rejected_status:CANDIDATE_WAL_OK;
}
static ssize_t read_hook(int fd,void *p,size_t n,off_t offset)
{
    reads++;if(reads==read_fail_at){errno=EIO;return -1;}if(reads==read_zero_at)return 0;
    if(interrupts>0){interrupts--;errno=EINTR;return -1;}
    if(short_bytes>0 && n>short_bytes)n=short_bytes;return pread(fd,p,n,offset);
}
static ssize_t write_hook(int fd,const void *p,size_t n,off_t offset)
{
    writes++;if(interrupts>0){interrupts--;errno=EINTR;return -1;}
    if(write_zero)return 0;if(write_oversize)return (ssize_t)n+1;
    if(cut==0){errno=EIO;return -1;}if(cut>0 && n>(size_t)cut)n=(size_t)cut;
    if(short_bytes>0 && n>short_bytes)n=short_bytes;
    ssize_t result=pwrite(fd,p,n,offset);if(result>0 && cut>0)cut-=result;if(result>0 && crash_write)_exit(0);return result;
}
static int sync_hook(int fd)
{
    syncs++;if(sync_interrupts>0){sync_interrupts--;errno=EINTR;return -1;}
    if(syncs==fail_sync_at){errno=EIO;return -1;}int result=fsync(fd);if(result==0 && crash_sync)_exit(0);return result;
}
static candidate_wal_io_t io={read_hook,write_hook,sync_hook,ftruncate};
static candidate_control_options_t options(void)
{
    candidate_control_options_t o={.backend=1,.io=&io,.verify=verify};memcpy(o.uuid,uuid,16);return o;
}
static void reset_hooks(void) { writes=syncs=reads=interrupts=short_bytes=fail_sync_at=read_fail_at=read_zero_at=sync_interrupts=0;cut=-1;rejected_checkpoint=0;rejected_status=CANDIDATE_WAL_CORRUPT;reenter=crash_verify=crash_write=crash_sync=write_zero=write_oversize=false;active=NULL; }
static void read_file(const char *path,uint8_t p[16384])
{
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);CHECK(fd>=0);CHECK(read(fd,p,16384)==16384);CHECK(close(fd)==0);
}
static void codec(void)
{
    uint8_t p[8192],bad[8192],encoded[8192];candidate_control_state_t s=state(1),decoded;
    OK(candidate_control_encode(&s,p,8192));OK(candidate_control_decode(uuid,p,8192,&decoded));
    OK(candidate_control_encode(&decoded,encoded,8192));CHECK(memcmp(p,encoded,8192)==0);
    for(unsigned i=0;i<8192;i++){CHECK(candidate_control_decode(uuid,p,i,&decoded)!=CANDIDATE_WAL_OK);memcpy(bad,p,8192);bad[i]^=1;CHECK(candidate_control_decode(uuid,bad,8192,&decoded)!=CANDIDATE_WAL_OK);}
    const unsigned offsets[]={0,4,6,8,24,28,32,40,48,56,64,72,88,96,104,120,128,136,144,160,168,180,184,8191};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memcpy(bad,p,8192);unsigned at=offsets[i];
        if(at==28 || at>=180)bad[at]=1;else if(at==0 || at==4 || at==6 || at==8 || at==24)bad[at]^=0x80;
        else if(at==160)memset(bad+at,0,8);else if(at==168)write_le(bad+at,2,8);else memset(bad+at,0,8);
        resign(bad);CHECK(candidate_control_decode(uuid,bad,8192,&decoded)!=CANDIDATE_WAL_OK);
    }
    memset(encoded,0xaa,8192);s.slot=1;CHECK(candidate_control_encode(&s,encoded,8192)==CANDIDATE_WAL_INVALID && encoded[0]==0xaa);
    s=state(1);s.backend=2;s.checkpoint_begin=s.checkpoint_end=s.redo_begin=0;
    OK(candidate_control_encode(&s,p,8192));OK(candidate_control_decode(uuid,p,8192,&decoded));CHECK(decoded.backend==2);
    s=state(1);s.request_epoch=UINT64_MAX;s.retention_floor=UINT64_MAX-3;s.generation=UINT64_MAX;
    OK(candidate_control_encode(&s,p,8192));s.retention_floor--;CHECK(candidate_control_encode(&s,p,8192)==CANDIDATE_WAL_INVALID);
}
static void lifecycle(void)
{
    char path[PATH_MAX];path_name(path);candidate_control_options_t o=options();candidate_control_t *c=NULL,*other=NULL;
    candidate_control_options_t invalid=o;invalid.verify=NULL;CHECK(candidate_control_create(path,&invalid,&c)==CANDIDATE_WAL_INVALID && c==NULL);
    CHECK(candidate_control_create(path,&o,NULL)==CANDIDATE_WAL_INVALID);OK(candidate_control_create(path,&o,&c));
    CHECK(candidate_control_info(c).selected==-1);CHECK(candidate_control_create(path,&o,&other)==CANDIDATE_WAL_EXISTS && other==NULL);
    CHECK(candidate_control_open(path,&o,&other)==CANDIDATE_WAL_BUSY && other==NULL);
    candidate_control_state_t s=state(1);active=c;reenter=true;OK(candidate_control_publish(c,&s));reenter=false;
    candidate_control_info_t info=candidate_control_info(c);CHECK(info.selected==0 && info.valid_mask==1 && info.slots[0].generation==1);
    printf("MEMORY control requested=%zu; excludes stack/allocator/RSS\n",info.resident_bytes);
    uint8_t before[16384],after[16384];read_file(path,before);s=state(2);s.transaction_highwater=8;writes=0;
    CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_STALE && writes==0);
    s=state(2);rejected_checkpoint=2;writes=0;CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_CORRUPT && writes==0);rejected_checkpoint=0;
    interrupts=2;short_bytes=17;OK(candidate_control_publish(c,&s));short_bytes=0;read_file(path,after);CHECK(memcmp(before,after,8192)==0);
    s=state(3);s.redo_begin=1088;OK(candidate_control_publish(c,&s));info=candidate_control_info(c);CHECK(info.selected==0 && info.redo_floor==64);
    OK(candidate_control_close(&c));interrupts=2;short_bytes=17;OK(candidate_control_open(path,&o,&c));short_bytes=0;
    CHECK(candidate_control_info(c).slots[candidate_control_info(c).selected].checkpoint_id==3);OK(candidate_control_close(&c));
    rejected_checkpoint=3;const candidate_wal_status_t errors[]={CANDIDATE_WAL_IO,CANDIDATE_WAL_NOMEM,CANDIDATE_WAL_LIMIT,CANDIDATE_WAL_UNSUPPORTED};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++){rejected_status=errors[i];CHECK(candidate_control_open(path,&o,&c)==errors[i] && c==NULL);}
    rejected_status=CANDIDATE_WAL_CORRUPT;OK(candidate_control_open(path,&o,&c));CHECK(candidate_control_info(c).selected==1);
    read_file(path,before);s=state(4);s.redo_begin=1088;OK(candidate_control_publish(c,&s));read_file(path,after);
    CHECK(memcmp(before+8192,after+8192,8192)==0);CHECK(candidate_control_info(c).slots[0].generation==5);
    rejected_checkpoint=0;OK(candidate_control_close(&c));OK(candidate_control_open(path,&o,&c));CHECK(candidate_control_info(c).slots[0].generation==5);OK(candidate_control_close(&c));
    printf("FIXTURE control %s\n",path);
}
static void torn(void)
{
    const unsigned cuts[]={0,1,4,8,16,32,64,128,175,176,179,180,192,4096,8191,8192};
    for(unsigned i=0;i<sizeof(cuts)/sizeof(cuts[0]);i++){
        reset_hooks();char path[PATH_MAX];path_name(path);candidate_control_options_t o=options();candidate_control_t *c=NULL;
        OK(candidate_control_create(path,&o,&c));candidate_control_state_t s=state(1);OK(candidate_control_publish(c,&s));
        uint8_t before[16384],after[16384];read_file(path,before);cut=cuts[i];syncs=0;fail_sync_at=1;s=state(2);
        CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_IO);CHECK(candidate_control_info(c).is_poisoned);
        CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_POISONED);read_file(path,after);CHECK(memcmp(before,after,8192)==0);
        OK(candidate_control_close(&c));reset_hooks();OK(candidate_control_open(path,&o,&c));
        candidate_control_info_t info=candidate_control_info(c);CHECK(info.slots[info.selected].generation==(cuts[i]<180?1:2));OK(candidate_control_close(&c));
        printf("TORN bytes=%u observed_generation=%u path=%s\n",cuts[i],cuts[i]<180?1u:2u,path);
    }
}
static void namespace_and_fork(void)
{
    reset_hooks();char path[PATH_MAX],saved[PATH_MAX],moved[PATH_MAX];path_name(path);CHECK(getcwd(saved,sizeof(saved))!=NULL);
    candidate_control_options_t o=options();candidate_control_t *c=NULL;OK(candidate_control_create(path,&o,&c));candidate_control_state_t s=state(1);
    CHECK(chdir("/")==0);OK(candidate_control_publish(c,&s));CHECK(chdir(saved)==0);
    pid_t child=fork();CHECK(child>=0);
    if(child==0){CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_FORKED);OK(candidate_control_close(&c));CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_BUSY);_exit(0);}
    int status;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    CHECK(snprintf(moved,sizeof(moved),"%s.moved",path)<(int)sizeof(moved));CHECK(rename(path,moved)==0);
    uint8_t bytes[16384];read_file(moved,bytes);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);CHECK(fd>=0);CHECK(write(fd,bytes,16384)==16384);CHECK(close(fd)==0);
    writes=0;s=state(2);CHECK(candidate_control_publish(c,&s)==CANDIDATE_WAL_IO && writes==0);OK(candidate_control_close(&c));
    uint8_t after[16384];read_file(path,after);CHECK(memcmp(bytes,after,16384)==0);
    for(unsigned boundary=0;boundary<3;boundary++) {
        path_name(path);OK(candidate_control_create(path,&o,&c));s=state(1);OK(candidate_control_publish(c,&s));OK(candidate_control_close(&c));
        child=fork();CHECK(child>=0);
        if(child==0){OK(candidate_control_open(path,&o,&c));crash_verify=boundary==0;crash_write=boundary==1;crash_sync=boundary==2;s=state(2);candidate_control_publish(c,&s);_exit(9);}
        CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);OK(candidate_control_open(path,&o,&c));
        candidate_control_info_t info=candidate_control_info(c);CHECK(info.slots[info.selected].generation==(boundary==0?1:2));CHECK(info.slots[0].generation==1);OK(candidate_control_close(&c));
        printf("CRASH boundary=%u observed_generation=%u path=%s\n",boundary,boundary==0?1u:2u,path);
    }
    path_name(path);int pipefd[2];CHECK(pipe(pipefd)==0);child=fork();CHECK(child>=0);
    if(child==0){close(pipefd[0]);OK(candidate_control_create(path,&o,&c));s=state(1);OK(candidate_control_publish(c,&s));CHECK(write(pipefd[1],"A",1)==1);for(;;)pause();}
    close(pipefd[1]);char ack=0;CHECK(read(pipefd[0],&ack,1)==1 && ack=='A');CHECK(kill(child,SIGKILL)==0);CHECK(waitpid(child,&status,0)==child && WIFSIGNALED(status));CHECK(close(pipefd[0])==0);
    OK(candidate_control_open(path,&o,&c));CHECK(candidate_control_info(c).slots[0].generation==1);OK(candidate_control_close(&c));
}
typedef struct { size_t live;bool fail;candidate_control_options_t *mutate; } memory_t;
static void *allocate(void *context,size_t size) { memory_t *m=context;if(m->fail)return NULL;void *p=malloc(size);if(p!=NULL){m->live+=size;if(m->mutate!=NULL)memset(m->mutate,0,sizeof(*m->mutate));}return p; }
static void release(void *context,void *p,size_t size) { memory_t *m=context;CHECK(m->live>=size);m->live-=size;free(p); }
static void ownership_and_limits(void)
{
    reset_hooks();char path[PATH_MAX];path_name(path);memory_t m={.fail=true};candidate_wal_memory_t memory={&m,allocate,release};candidate_control_options_t o=options();o.memory=&memory;candidate_control_t *c=NULL;
    CHECK(candidate_control_create(path,&o,&c)==CANDIDATE_WAL_NOMEM && c==NULL && m.live==0);m.fail=false;OK(candidate_control_create(path,&o,&c));CHECK(m.live==candidate_control_info(c).resident_bytes);
    OK(candidate_control_close(&c));CHECK(m.live==0);CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_CORRUPT && c==NULL && m.live==0);
    uint8_t bytes[16384];candidate_control_state_t low=state(1),high=state(2);low.generation=UINT64_MAX-1;low.slot=1;high.generation=UINT64_MAX;high.slot=0;
    OK(candidate_control_encode(&high,bytes,8192));OK(candidate_control_encode(&low,bytes+8192,8192));int fd=open(path,O_WRONLY|O_CLOEXEC);CHECK(fd>=0);CHECK(pwrite(fd,bytes,16384,0)==16384);CHECK(fsync(fd)==0 && close(fd)==0);
    OK(candidate_control_open(path,&o,&c));candidate_control_state_t next=state(3);writes=0;CHECK(candidate_control_publish(c,&next)==CANDIDATE_WAL_LIMIT && writes==0);OK(candidate_control_close(&c));CHECK(m.live==0);
    path_name(path);m.mutate=&o;OK(candidate_control_create(path,&o,&c));CHECK(o.verify==NULL);next=state(1);OK(candidate_control_publish(c,&next));OK(candidate_control_close(&c));CHECK(m.live==0);
}
static void write_fixture(char path[PATH_MAX],const uint8_t *bytes,size_t size)
{
    path_name(path);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);CHECK(fd>=0);
    CHECK(size==0 || write(fd,bytes,size)==(ssize_t)size);CHECK(fsync(fd)==0 && close(fd)==0);
}
static void slots_and_failures(void)
{
    reset_hooks();uint8_t good[16385]={0},bad[16385];candidate_control_state_t low=state(1),high=state(2);high.generation=2;high.slot=1;
    OK(candidate_control_encode(&low,good,8192));OK(candidate_control_encode(&high,good+8192,8192));
    char path[PATH_MAX];candidate_control_options_t o=options();candidate_control_t *c=NULL;
    const unsigned sizes[]={0,1,180,8191,8192,8193,8372,12288,16383,16384,16385};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        write_fixture(path,good,sizes[i]);candidate_wal_status_t s=candidate_control_open(path,&o,&c);
        if(sizes[i]<8192 || sizes[i]>16384)CHECK(s==CANDIDATE_WAL_CORRUPT && c==NULL);
        else { CHECK(s==CANDIDATE_WAL_OK);candidate_control_info_t info=candidate_control_info(c);CHECK(info.slots[info.selected].generation==(sizes[i]==16384?2:1));OK(candidate_control_close(&c)); }
    }
    for(unsigned mode=0;mode<8;mode++) {
        memcpy(bad,good,16384);candidate_wal_status_t expected=CANDIDATE_WAL_UNSUPPORTED;
        if(mode==0){bad[8192+4]=2;resign(bad+8192);}
        if(mode==1){bad[8192+8]^=1;resign(bad+8192);}
        if(mode==2){candidate_control_state_t other=high;other.backend=2;other.checkpoint_begin=other.checkpoint_end=other.redo_begin=0;OK(candidate_control_encode(&other,bad+8192,8192));}
        if(mode==3){memcpy(bad,good+8192,8192);memcpy(bad+8192,good,8192);expected=CANDIDATE_WAL_CORRUPT;}
        if(mode==4){write_le(bad+8192+80,8,8);resign(bad+8192);expected=CANDIDATE_WAL_CORRUPT;}
        if(mode==5){bad[8192+200]^=1;expected=CANDIDATE_WAL_OK;}
        if(mode==6){bad[200]^=1;bad[8192+200]^=1;expected=CANDIDATE_WAL_CORRUPT;}
        if(mode==7){bad[8192+180]=1;resign(bad+8192);}
        write_fixture(path,bad,16384);CHECK(candidate_control_open(path,&o,&c)==expected);
        if(c!=NULL){CHECK(candidate_control_info(c).selected==0);OK(candidate_control_close(&c));}
    }
    write_fixture(path,good,16384);read_fail_at=2;reads=0;CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_IO && c==NULL);reset_hooks();
    read_zero_at=2;CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_IO && c==NULL);reset_hooks();
    for(unsigned mode=0;mode<3;mode++) {
        OK(candidate_control_open(path,&o,&c));write_zero=mode==0;write_oversize=mode==1;interrupts=mode==2?99:0;
        candidate_control_state_t next=state(3);CHECK(candidate_control_publish(c,&next)==CANDIDATE_WAL_IO && candidate_control_info(c).is_poisoned);OK(candidate_control_close(&c));reset_hooks();
    }
    OK(candidate_control_open(path,&o,&c));sync_interrupts=2;candidate_control_state_t next=state(3);OK(candidate_control_publish(c,&next));OK(candidate_control_close(&c));reset_hooks();
    for(unsigned i=1;i<=2;i++){path_name(path);fail_sync_at=i;CHECK(candidate_control_create(path,&o,&c)==CANDIDATE_WAL_IO && c==NULL);reset_hooks();CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_CORRUPT && c==NULL);}
    path_name(path);o.backend=2;OK(candidate_control_create(path,&o,&c));next=state(1);next.backend=2;next.checkpoint_begin=next.checkpoint_end=next.redo_begin=0;
    OK(candidate_control_publish(c,&next));OK(candidate_control_close(&c));OK(candidate_control_open(path,&o,&c));CHECK(candidate_control_info(c).redo_floor==0);OK(candidate_control_close(&c));
    o.backend=1;CHECK(candidate_control_open(path,&o,&c)==CANDIDATE_WAL_UNSUPPORTED && c==NULL);
    printf("FIXTURE cow %s\n",path);
    memcpy(bad,good,16384);high.checkpoint_id=UINT64_MAX;OK(candidate_control_encode(&high,bad+8192,8192));write_fixture(path,bad,16384);
    OK(candidate_control_open(path,&o,&c));next=state(3);writes=0;CHECK(candidate_control_publish(c,&next)==CANDIDATE_WAL_LIMIT && writes==0);OK(candidate_control_close(&c));
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);alarm(90);codec();reset_hooks();lifecycle();torn();namespace_and_fork();ownership_and_limits();slots_and_failures();
    OK(candidate_control_close(&active));CHECK(candidate_control_close(NULL)==CANDIDATE_WAL_INVALID);
    printf("PASS candidate control checks=%u; synthetic dependency verifier only, no checkpoint/WAL/data recovery activation\n",checks);return 0;
}
