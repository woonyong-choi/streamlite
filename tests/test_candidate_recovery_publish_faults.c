#define main gates_main
#include "test_candidate_recovery_publish_gates.c"
#undef main

/* segment 경계: 같은 기록 크기를 유지하는 거래 padding으로 게시 record 하나를 경계 직전·정확한 끝에 둔다. */
typedef struct { unsigned small, commits, groups; } pad_t;

static void pad_wal(void *built, void *context)
{
    built_t *b = built; const pad_t *pad = context; fx_t *fx = b->fx;
    uint64_t txn = b->spec.requests + 2;
    for (unsigned i = 0; i < pad->small; i++) {
        fx_txn_t t = {0}; fx_begin(fx,&t,txn++);
        if (i < pad->commits) fx_commit(fx,&t); else fx_abort(fx,&t);
    }
    fx_txn_t t = {0}; fx_begin(fx,&t,txn);
    static fx_image_t image; uint64_t id = b->alloc_count;
    for (unsigned g = 0; g < pad->groups; g++) {
        uint64_t end = fx_group_end(fx,1,0);
        candidate_catalog_node_t node = {.count = 1};
        node.entries[0] = (candidate_outcome_t){.epoch = 1,.transaction_id = 1,.state = CANDIDATE_OUTCOME_ADMITTED,.admitting_owner_epoch = 7};
        uint64_t unique = 0xabc000 + g; memcpy(node.entries[0].nonce,&unique,8);
        candidate_page_location_t l = fx_location(id,1,end); OK(candidate_catalog_encode_node(&l,&node,image.page,CANDIDATE_PAGE_BYTES));
        image.id = id; image.generation = 1; image.previous = fx->pages[id].lsn;
        fx_group(fx,&t,&image,1,NULL,0,true,true);
    }
    fx_abort(fx,&t);
}

#define PAD_SMALL 1100u
static const req_t segment_req[3] = {{REQ_COMMIT,STORE_NONE,1,40,false,false},{REQ_ABORT,STORE_ADMITTED,1,0,false,false},
    {REQ_COMMIT,STORE_FINAL,1,0,false,true}};

static built_t build_padded(const char *tag, pad_t *pad)
{
    char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-seg-%s-%ld",tag,(long)getpid()) > 0);
    scen_t s = {.name = name,.requests = 3,.req = segment_req,.ballast = 1,.txn_highwater = 4096,.before_checkpoint = pad_wal,.context = pad};
    built_t b = scen_build(&s); scen_finish(&b); return b;
}

typedef struct { uint64_t lsn[64], size[64]; uint16_t type[64]; unsigned count; uint64_t from; } records_t;
static bool collect(void *context, const candidate_wal_record_t *r)
{
    records_t *c = context;
    if (r->lsn < c->from) return true;
    CHECK(c->count < 64); c->lsn[c->count] = r->lsn; c->type[c->count] = r->type;
    c->size[c->count++] = (96 + r->payload_bytes + 7) & ~(uint64_t)7; return true;
}

static records_t published(const fx_t *fx, uint64_t from)
{
    records_t c = {.from = from}; candidate_wal_options_t o = {.owner_epoch = 8}; memcpy(o.uuid,fx_uuid,16);
    candidate_wal_t *w = NULL; OK(candidate_wal_open(fx->wal,&o,&w)); candidate_wal_info_t info;
    OK(candidate_wal_scan(w,collect,&c,&info)); OK(candidate_wal_close(&w)); return c;
}

static bool solve(uint64_t delta, pad_t *pad)
{
    for (unsigned c = 0; c < 2000; c++) {
        if ((uint64_t)c * 8552 > delta) break;
        uint64_t rest = delta - (uint64_t)c * 8552;
        if (rest % 120 != 0 || rest / 120 > PAD_SMALL) continue;
        *pad = (pad_t){PAD_SMALL,(unsigned)(rest / 120),c}; return true;
    }
    return false;
}

static void case_segments(void)
{
    const uint64_t boundary = CANDIDATE_WAL_SEGMENT_BYTES;
    pad_t pad = {PAD_SMALL,0,0}; built_t zero = build_padded("zero",&pad);
    uint64_t base = zero.anchor.checkpoint_end; scen_free(&zero);
    CHECK(solve(boundary - 300000 - base,&pad)); built_t ref = build_padded("ref",&pad);
    uint64_t end = ref.anchor.checkpoint_end; candidate_recovery_t *r = NULL;
    CHECK(recover(ref.fx,8,&r) == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    records_t pub = published(ref.fx,end); CHECK(pub.count > 8 && pub.lsn[0] == end);
    for (unsigned i = 1; i < pub.count; i++) CHECK(pub.lsn[i] == pub.lsn[i - 1] + pub.size[i - 1]);
    scen_free(&ref);
    unsigned variants = 0;
    for (unsigned i = 0; i < pub.count; i++) for (unsigned mode = 0; mode < 2; mode++) {
        uint64_t offset = pub.lsn[i] - end;
        /* mode0: record i가 경계 8B 전에 시작해 다음 segment로 간다. mode1: record i가 경계에서 정확히 끝난다. */
        uint64_t target = mode == 0 ? boundary - offset - 8 : boundary - offset - pub.size[i];
        CHECK(target > base); CHECK(solve(target - base,&pad));
        char tag[32]; CHECK(snprintf(tag,sizeof(tag),"%u-%u",i,mode) > 0);
        built_t b = build_padded(tag,&pad); CHECK(b.anchor.checkpoint_end == target);
        CHECK(recover(b.fx,8,&r) == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
        records_t got = published(b.fx,target);
        unsigned moved = mode == 0 ? i : i + 1;
        CHECK(got.count == pub.count);
        for (unsigned j = 0; j < got.count; j++) {
            CHECK(got.type[j] == pub.type[j] && got.size[j] == pub.size[j]);
            uint64_t expected = j < moved ? target + (pub.lsn[j] - end) : boundary + 64 + (pub.lsn[j] - pub.lsn[moved]);
            CHECK(got.lsn[j] == expected);
        }
        printf("SEGMENT record=%u type=%u mode=%u target=%" PRIu64 " moved_lsn=%" PRIu64 "\n",i,pub.type[i],mode,target,
            moved < got.count ? got.lsn[moved] : 0);
        settle(&b,8,1); scen_free(&b); variants++;
    }
    printf("CASE segments variants=%u records=%u\n",variants,pub.count);
}

static unsigned fault_iterations, reuse_retries;

static void verify_after(const built_t *base, const fx_t *copy, const char *tag)
{
    built_t b = *base; fx_t view = *copy; b.fx = &view;
    candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover(&view,8,&r);
    if (status != CANDIDATE_WAL_OK) printf("FAULT %s retry=%s\n",tag,name_of(status));
    CHECK(status == CANDIDATE_WAL_OK); receipt(tag,r);
    if (candidate_recovery_publication(r).reused_reserved_pages > 0) reuse_retries++;
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); CHECK(io.live == 0);
    check_slots(&view,&base->anchor,8); settle(&b,8,1); fault_iterations++;
}

static void run_child(const fx_t *copy, const faults_t *plan, int *code)
{
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        io_reset(copy); ino_t data = io.data, control = io.control; io = *plan; io.data = data; io.control = control;
        candidate_recovery_t *r = NULL; candidate_wal_status_t status = recover_with(copy,8,&r);
        _exit(status == CANDIDATE_WAL_OK ? 0 : 10 + (int)status);
    }
    int wstatus; CHECK(waitpid(child,&wstatus,0) == child); CHECK(WIFEXITED(wstatus)); *code = WEXITSTATUS(wstatus);
}

static faults_t plan_none(void)
{
    faults_t plan; memset(&plan,0,sizeof(plan)); plan.fail_kind = plan.fail_sync_kind = plan.stop_kind = plan.stop_sync_kind = -1;
    return plan;
}

static bool picked(const unsigned *list, unsigned count, unsigned k)
{
    if (list == NULL) return true;
    for (unsigned i = 0; i < count; i++) if (list[i] == k) return true;
    return false;
}

static void case_faults(const built_t *base, const char *which, unsigned stride, unsigned sync_stride, bool representative)
{
    fx_t ref; fx_copy_set(base->fx,"ref",&ref);
    candidate_recovery_t *r = NULL; CHECK(recover(&ref,8,&r) == CANDIDATE_WAL_OK); OK(candidate_recovery_close(&r));
    faults_t counts = io;
    printf("FAULT counts wal_writes=%u data_writes=%u control_writes=%u wal_syncs=%u data_syncs=%u control_syncs=%u truncates=%u allocs=%u\n",
        counts.writes[K_WAL],counts.writes[K_DATA],counts.writes[K_CONTROL],counts.syncs[K_WAL],counts.syncs[K_DATA],counts.syncs[K_CONTROL],
        counts.truncates,counts.allocs);
    unsigned serial = 0; char tag[64];
    for (int kind = 0; kind < K_KINDS; kind++) {
        unsigned w = counts.writes[kind], f = counts.syncs[kind];
        unsigned writes_at[3] = {1,w / 2 > 1 ? w / 2 : 1,w}, syncs_at[2] = {1,f};
        const unsigned *wl = representative ? writes_at : NULL, *sl = representative ? syncs_at : NULL;
        for (unsigned k = 1; k <= counts.writes[kind]; k += stride) for (unsigned torn = 0; torn < 2; torn++) {
            if (!picked(wl,3,k)) continue;
            CHECK(snprintf(tag,sizeof(tag),"%s-w%d-%u-%u-%u",which,kind,k,torn,serial++) > 0); fx_t copy; fx_copy_set(base->fx,tag,&copy);
            io_reset(&copy); io.fail_kind = kind; io.fail_write_at = k; io.torn = torn;
            candidate_wal_status_t status = recover_with(&copy,8,&r);
            CHECK(status != CANDIDATE_WAL_OK && r == NULL && io.live == 0);
            printf("FAULT %s write-error status=%s\n",tag,name_of(status)); verify_after(base,&copy,tag);
            if (torn == 0) continue;
            CHECK(snprintf(tag,sizeof(tag),"%s-s%d-%u-%u",which,kind,k,serial++) > 0); fx_copy_set(base->fx,tag,&copy);
            faults_t plan = plan_none(); plan.stop_kind = kind; plan.stop_write_at = k; int code; run_child(&copy,&plan,&code);
            CHECK(code == 78); printf("FAULT %s stop-write exit=%d\n",tag,code); verify_after(base,&copy,tag);
        }
        for (unsigned k = 1; k <= counts.syncs[kind]; k += sync_stride) {
            if (!picked(sl,2,k)) continue;
            CHECK(snprintf(tag,sizeof(tag),"%s-f%d-%u-%u",which,kind,k,serial++) > 0); fx_t copy; fx_copy_set(base->fx,tag,&copy);
            io_reset(&copy); io.fail_sync_kind = kind; io.fail_sync_at = k;
            candidate_wal_status_t status = recover_with(&copy,8,&r);
            CHECK(status != CANDIDATE_WAL_OK && r == NULL && io.live == 0);
            printf("FAULT %s sync-error status=%s\n",tag,name_of(status)); verify_after(base,&copy,tag);
            for (unsigned after = 0; after < 2; after++) {
                CHECK(snprintf(tag,sizeof(tag),"%s-p%d-%u-%u-%u",which,kind,k,after,serial++) > 0); fx_copy_set(base->fx,tag,&copy);
                faults_t plan = plan_none(); plan.stop_sync_kind = kind; plan.stop_sync_at = k; plan.stop_after_sync = after;
                int code; run_child(&copy,&plan,&code);
                CHECK(code == (after ? 80 : 79)); printf("FAULT %s stop-sync exit=%d\n",tag,code); verify_after(base,&copy,tag);
            }
        }
    }
    unsigned allocs_at[2] = {1,counts.allocs};
    for (unsigned k = 1; k <= counts.allocs; k += stride) {
        if (!picked(representative ? allocs_at : NULL,2,k)) continue;
        CHECK(snprintf(tag,sizeof(tag),"%s-m%u-%u",which,k,serial++) > 0); fx_t copy; fx_copy_set(base->fx,tag,&copy);
        io_reset(&copy); io.fail_alloc_at = k;
        candidate_wal_status_t status = recover_with(&copy,8,&r);
        if (status != CANDIDATE_WAL_NOMEM) printf("FAULT %s oom status=%s\n",tag,name_of(status));
        CHECK(status == CANDIDATE_WAL_NOMEM && r == NULL && io.live == 0); verify_after(base,&copy,tag);
    }
    for (unsigned mode = 0; mode < 2; mode++) {
        CHECK(snprintf(tag,sizeof(tag),"%s-io%u-%u",which,mode,serial++) > 0); fx_t copy; fx_copy_set(base->fx,tag,&copy);
        io_reset(&copy); if (mode == 0) io.short_io = true; else io.interrupt_io = true;
        candidate_wal_status_t status = recover_with(&copy,8,&r);
        printf("FAULT %s %s status=%s\n",tag,mode == 0 ? "short" : "eintr",name_of(status));
        CHECK(status == CANDIDATE_WAL_OK); built_t b = *base; fx_t view = copy; b.fx = &view; check_answers(r,&b,1);
        OK(candidate_recovery_close(&r)); check_slots(&copy,&base->anchor,8); settle(&b,8,1); fault_iterations++;
    }
    printf("CASE faults-%s iterations=%u reuse_retries=%u\n",which,fault_iterations,reuse_retries);
    CHECK(reuse_retries > 0);
}

typedef struct { const char *name; int kind, mode; unsigned at; } point_t;
enum { P_FAIL, P_TORN, P_STOP, P_FAIL_SYNC, P_STOP_SYNC, P_STOP_AFTER_SYNC };

static unsigned next_step(unsigned from, int kind, uint8_t sync)
{
    for (unsigned i = from; i < trace.count; i++) if (trace.steps[i].kind == kind && trace.steps[i].sync == sync) return i;
    CHECK(false); return 0;
}

static candidate_recovery_publication_t run_point(const built_t *base, const point_t *pt, unsigned serial)
{
    char tag[96]; CHECK(snprintf(tag,sizeof(tag),"large-%s-%u",pt->name,serial) > 0); fx_t copy; fx_copy_set(base->fx,tag,&copy);
    if (pt->mode <= P_TORN || pt->mode == P_FAIL_SYNC) {
        candidate_recovery_t *r = NULL; io_reset(&copy);
        if (pt->mode == P_FAIL_SYNC) { io.fail_sync_kind = pt->kind; io.fail_sync_at = pt->at; }
        else { io.fail_kind = pt->kind; io.fail_write_at = pt->at; io.torn = pt->mode == P_TORN; }
        candidate_wal_status_t status = recover_with(&copy,8,&r);
        printf("FAULT %s kind=%d mode=%d at=%u status=%s\n",tag,pt->kind,pt->mode,pt->at,name_of(status)); fflush(stdout);
        CHECK(status != CANDIDATE_WAL_OK && r == NULL && io.live == 0);
    } else {
        faults_t plan = plan_none(); int code;
        if (pt->mode == P_STOP) { plan.stop_kind = pt->kind; plan.stop_write_at = pt->at; }
        else { plan.stop_sync_kind = pt->kind; plan.stop_sync_at = pt->at; plan.stop_after_sync = pt->mode == P_STOP_AFTER_SYNC; }
        run_child(&copy,&plan,&code);
        printf("FAULT %s kind=%d mode=%d at=%u exit=%d\n",tag,pt->kind,pt->mode,pt->at,code); fflush(stdout);
        CHECK(code == (pt->mode == P_STOP ? 78 : pt->mode == P_STOP_SYNC ? 79 : 80));
    }
    built_t b = *base; fx_t view = copy; b.fx = &view; candidate_recovery_t *r = NULL;
    CHECK(recover(&view,8,&r) == CANDIDATE_WAL_OK); receipt(tag,r);
    candidate_recovery_publication_t p = candidate_recovery_publication(r);
    check_answers(r,&b,1); OK(candidate_recovery_close(&r)); CHECK(io.live == 0);
    check_slots(&view,&base->anchor,8); settle(&b,8,1); fault_iterations++;
    return p;
}

/* 4096 요청 publication의 실제 기록 순서에서 reserve·첫/마지막 stage·checkpoint·control 지점만 골라 실패·정지 뒤 재시도한다. */
static bool is_member(uint16_t type) { return (type >= 4 && type <= 7) || type == 15 || type == 16; }

static unsigned find_wal(unsigned from, unsigned to, uint16_t type)
{
    for (unsigned i = from; i < to; i++) if (trace.steps[i].kind == K_WAL && !trace.steps[i].sync && trace.steps[i].type == type) return i;
    CHECK(false); return 0;
}

static void case_large_rep(const built_t *base, unsigned from, unsigned to)
{
    fx_t ref; fx_copy_set(base->fx,"ref",&ref); candidate_recovery_t *r = NULL;
    io_reset(&ref); trace.count = 0; trace.on = true; candidate_wal_status_t status = recover_with(&ref,8,&r); trace.on = false;
    printf("CASE large-ref status=%s\n",name_of(status)); fflush(stdout); CHECK(status == CANDIDATE_WAL_OK);
    receipt("large-ref",r); candidate_recovery_publication_t ref_p = candidate_recovery_publication(r); OK(candidate_recovery_close(&r));
    unsigned begins[64], ends[64], nb = 0, ne = 0, reserve_group = 0, cp_begin = 0, cp_end = 0;
    for (unsigned i = 0; i < trace.count; i++) {
        const step_t *t = &trace.steps[i];
        if (t->kind != K_WAL || t->sync) continue;
        if (t->type == 3) { CHECK(nb < 64); begins[nb++] = i; }
        if (t->type == 8) { CHECK(ne < 64); ends[ne++] = i; }
        if (t->type == 16 && reserve_group == 0) reserve_group = nb;
        if (t->type == 12) cp_begin = i;
        if (t->type == 14) cp_end = i;
    }
    printf("CASE large-shape steps=%u groups=%u reserve_group=%u cp_begin=%u cp_end=%u outcomes=%u nodes=%u new=%u system_groups=%u\n",
        trace.count,nb,reserve_group,cp_begin,cp_end,ref_p.outcomes,ref_p.nodes,ref_p.new_nodes,ref_p.system_groups);
    fflush(stdout);
    CHECK(trace.count < 16384 && nb == ne && nb >= 3 && nb == ref_p.system_groups && reserve_group == 1);
    CHECK(ref_p.outcomes == 4096 && ref_p.nodes == 90 && cp_begin > ends[nb - 1] && cp_end > cp_begin);
    const step_t *s = trace.steps;
    unsigned first = begins[1], first_image = find_wal(begins[1],ends[1],4), last_end = ends[nb - 1], member = last_end - 1;
    while (member > begins[nb - 1] && (trace.steps[member].kind != K_WAL || trace.steps[member].sync)) member--;
    unsigned reserve_sync = next_step(ends[0],K_WAL,1), final_data = next_step(last_end,K_DATA,0), final_sync = next_step(last_end,K_DATA,1);
    unsigned control = next_step(cp_end,K_CONTROL,0), control_sync = next_step(cp_end,K_CONTROL,1);
    point_t points[] = {
        {"first-stage-begin",K_WAL,P_TORN,s[first].count}, {"first-stage-image",K_WAL,P_STOP,s[first_image].count},
        {"final-stage-member",K_WAL,P_TORN,s[member].count}, {"final-stage-end",K_WAL,P_STOP,s[last_end].count},
        {"final-stage-data",K_DATA,P_FAIL,s[final_data].count}, {"final-stage-sync",K_DATA,P_STOP_AFTER_SYNC,s[final_sync].count},
        {"checkpoint-begin",K_WAL,P_TORN,s[cp_begin].count}, {"checkpoint-end",K_WAL,P_STOP,s[cp_end].count},
        {"control-write",K_CONTROL,P_TORN,s[control].count}, {"control-sync",K_CONTROL,P_FAIL_SYNC,s[control_sync].count},
        {"control-synced",K_CONTROL,P_STOP_AFTER_SYNC,s[control_sync].count},
    };
    unsigned steps[] = {first,first_image,member,last_end,final_data,final_sync,cp_begin,cp_end,control,control_sync,control_sync};
    printf("CASE large-phase first=%u first_image=%u member=%u member_type=%u final_end=%u group_begins=%u,%u,%u\n",first,first_image,
        member,s[member].type,last_end,begins[0],begins[1],begins[nb - 1]);
    CHECK(s[first].type == 3 && s[first_image].type == 4 && is_member(s[member].type) && member > begins[nb - 1]);
    CHECK(s[last_end].type == 8 && s[cp_begin].type == 12 && s[cp_end].type == 14 && reserve_sync > ends[0] && reserve_sync < begins[1]);
    unsigned count = sizeof(points) / sizeof(points[0]);
    for (unsigned i = from; i <= to && i < count; i++) {
        const step_t *t = &s[steps[i]];
        printf("POINT %s kind=%d mode=%d at=%u index=%u step=%u type=%u sync=%u\n",points[i].name,points[i].kind,points[i].mode,points[i].at,i,
            steps[i],t->type,t->sync);
        fflush(stdout);
        CHECK(t->kind == points[i].kind && t->count == points[i].at);
        run_point(base,&points[i],i);
    }
    if (to < count) { printf("CASE faults-large-rep iterations=%u\n",fault_iterations); return; }
    point_t reuse = {"reserve-durable",K_WAL,P_STOP_AFTER_SYNC,s[reserve_sync].count};
    printf("POINT %s kind=0 mode=%d at=%u step=%u sync=1\n",reuse.name,reuse.mode,reuse.at,reserve_sync); fflush(stdout);
    candidate_recovery_publication_t p = run_point(base,&reuse,99);
    printf("CASE large-reuse reserved=%" PRIu64 " reused=%" PRIu64 " alloc=%u hwm=%" PRIu64 "->%" PRIu64 " ref_reserved=%" PRIu64 " ref_hwm=%" PRIu64 "\n",
        p.reserved_pages,p.reused_reserved_pages,p.allocation_pages,p.page_highwater_before,p.page_highwater_after,ref_p.reserved_pages,
        ref_p.page_highwater_after);
    CHECK(p.reused_reserved_pages > 0 && p.reserved_pages + p.reused_reserved_pages == ref_p.reserved_pages + ref_p.reused_reserved_pages);
    CHECK(p.page_highwater_after == ref_p.page_highwater_after && p.page_highwater_after == p.page_highwater_before + p.reserved_pages);
    printf("CASE faults-large-rep iterations=%u\n",fault_iterations);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    if (strcmp(argv[1],"segments") == 0) case_segments();
    else if (strcmp(argv[1],"faults") == 0) {
        built_t base = build_mixed("faults",true,1); case_faults(&base,"mixed",1,1,false); scen_free(&base);
    } else if (strcmp(argv[1],"faults-large") == 0) {
        req_t *req = calloc(2800,sizeof(*req)); CHECK(req != NULL);
        for (unsigned i = 0; i < 2800; i++) req[i] = (req_t){REQ_COMMIT,STORE_NONE,1 + i / 1024,12 + i % 21,false,false};
        req[2799].state = REQ_UNFINISHED; req[2798].state = REQ_UNFINISHED;
        char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-faults-large-%ld",(long)getpid()) > 0);
        scen_t s = {.name = name,.requests = 2800,.req = req,.root_epoch = 3,.root_floor = 1,.txn_highwater = 8192};
        built_t base = scen_build(&s); scen_finish(&base);
        bool rep = argc > 2 && strcmp(argv[2],"rep") == 0;
        case_faults(&base,"large",rep ? 1 : argc > 2 ? (unsigned)strtoul(argv[2],NULL,10) : 25,1,rep); scen_free(&base); free(req);
    } else if (strcmp(argv[1],"faults-large-rep") == 0) {
        req_t *req = calloc(4096,sizeof(*req)); CHECK(req != NULL);
        for (unsigned i = 0; i < 4096; i++) req[i] = (req_t){REQ_COMMIT,STORE_NONE,1 + i / 1024,12 + i % 21,false,false};
        req[4094].state = REQ_UNFINISHED; req[4093].state = REQ_UNFINISHED;
        char name[96]; CHECK(snprintf(name,sizeof(name),"t23pg-large-rep-%ld",(long)getpid()) > 0);
        scen_t s = {.name = name,.requests = 4096,.req = req,.root_epoch = 4,.root_floor = 1,.txn_highwater = 8192};
        unsigned from = argc > 2 ? (unsigned)strtoul(argv[2],NULL,10) : 0, to = argc > 3 ? (unsigned)strtoul(argv[3],NULL,10) : 11;
        built_t base = scen_build(&s); scen_finish(&base); case_large_rep(&base,from,to); scen_free(&base); free(req);
    } else return gates_main(argc,argv);
    printf("PASS %s checks=%u\n",argv[1],checks);
    return 0;
}
