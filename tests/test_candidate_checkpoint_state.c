#define CANDIDATE_REBUILD_LIMIT_TEST_MAIN checkpoint_rebuild_limits_main
#include "test_candidate_outcome_rebuild_limits.c"

static unsigned violations;
static unsigned recovery_reads, recovery_writes, recovery_syncs, recovery_truncates;

static candidate_wal_status_t deny_redo_read(void *context, uint64_t id, uint8_t bytes[8192], bool *present)
{
    (void)context; (void)id; (void)bytes; (void)present; recovery_reads++; return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t deny_redo_write(void *context, uint64_t id, const uint8_t bytes[8192])
{
    (void)context; (void)id; (void)bytes; recovery_writes++; return CANDIDATE_WAL_IO;
}

static candidate_wal_status_t deny_redo_sync(void *context)
{
    (void)context; recovery_syncs++; return CANDIDATE_WAL_IO;
}

static ssize_t deny_recovery_write(int fd, const void *bytes, size_t size, off_t offset)
{
    (void)fd; (void)bytes; (void)size; (void)offset; recovery_writes++; errno = EIO; return -1;
}

static int deny_recovery_sync(int fd) { (void)fd; recovery_syncs++; errno = EIO; return -1; }
static int deny_recovery_truncate(int fd, off_t offset)
{
    (void)fd; (void)offset; recovery_truncates++; errno = EIO; return -1;
}

static void reject_before_effects(aborted_t *a)
{
    recovery_reads = recovery_writes = recovery_syncs = recovery_truncates = 0;
    candidate_redo_options_t redo = {.checkpoint = &a->files.anchor,
        .target = {NULL,deny_redo_read,deny_redo_write,deny_redo_sync}};
    memcpy(redo.uuid,uuid,16); candidate_redo_info_t result;
    CHECK(candidate_redo_execute(a->wal,&redo,&result) == CANDIDATE_WAL_CORRUPT);
    CHECK(recovery_reads == 0 && recovery_writes == 0 && recovery_syncs == 0);
    CHECK(result.pages_written == 0 && result.groups == 0 && result.wal_end_lsn == 0);
    CHECK(snprintf(a->files.control,sizeof(a->files.control),"%s.control",a->files.wal) > 0);
    uint8_t slots[16384] = {0}; OK(candidate_control_encode(&a->files.anchor,slots,8192));
    int fd = open(a->files.control,O_CREAT | O_EXCL | O_WRONLY,0600); CHECK(fd >= 0);
    CHECK(write(fd,slots,sizeof(slots)) == sizeof(slots)); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
    OK(candidate_wal_close(&a->wal));
    candidate_wal_io_t io = {pread,deny_recovery_write,deny_recovery_sync,deny_recovery_truncate};
    candidate_recovery_options_t request = options(&a->files); request.owner_epoch = 9; request.io = &io;
    candidate_recovery_t *handle = (void *)1;
    CHECK(candidate_recovery_open(&request,&handle) == CANDIDATE_WAL_CORRUPT && handle == NULL);
    candidate_recovery_terminal_info_t terminal;
    CHECK(candidate_recovery_resolve_terminals(&request,&terminal) == CANDIDATE_WAL_CORRUPT);
    CHECK(terminal.checkpoint_id == 0 && terminal.after_end_lsn == 0 && terminal.appended_aborts == 0);
    candidate_recovery_wal_info_t tail;
    CHECK(candidate_recovery_repair_wal(&request,&tail) == CANDIDATE_WAL_CORRUPT);
    CHECK(tail.discarded_group_lsn == 0 && tail.removed_segments == 0 && tail.terminals.checkpoint_id == 0);
    CHECK(recovery_writes == 0 && recovery_syncs == 0 && recovery_truncates == 0);
    printf("NO_EFFECT %s redo_read=0 write=0 sync=0 truncate=0 snapshot/terminal/tail=CORRUPT\n",a->files.wal);
}

static void first_system_group(aborted_t *a, unsigned kind, uint64_t previous_a,
    uint64_t previous_b, uint64_t next_a, uint64_t next_b)
{
    uint8_t bytes[8192], image[8224] = {0}, allocation_image[8224] = {0};
    CHECK(pread(a->fd,bytes,8192,0) == 8192);
    candidate_catalog_root_t root; candidate_page_info_t page;
    OK(candidate_catalog_decode_root(uuid,bytes,8192,&root)); OK(candidate_page_decode_header(uuid,bytes,8192,&page));
    bool reserve = kind == 16; uint64_t end = candidate_wal_info(a->wal).valid_end_lsn + (reserve ? 17008 : 8688);
    chain_t c = {.wal = a->wal}; uint8_t begin[16] = {0}, event[40] = {0}, finish[24] = {0};
    write_le(begin,1,8); write_le(begin + 8,reserve ? 2 : 1,4); write_le(begin + 12,reserve ? 3 : 2,4);
    write_le(event,1,8); write_le(event + 8,previous_a,8); write_le(event + 16,next_a,8);
    write_le(event + 24,previous_b,8); write_le(event + 32,next_b,8); root.owner_epoch = 8;
    if (reserve) { root.transaction_highwater = next_a; root.page_highwater = next_b; }
    else { root.current_epoch = next_a; root.retention_floor = next_b; }
    write_le(image,1,8); write_le(image + 8,1,8); write_le(image + 16,1,8); write_le(image + 24,page.location.lsn,8);
    page.location.lsn = end; OK(candidate_catalog_encode_root(&page.location,&root,image + 32,8192));
    if (reserve) {
        candidate_allocation_root_t allocation = {.owner_epoch = 8,.page_highwater = next_b,.count = 2,.nodes = {{3,1}}};
        candidate_page_location_t location = {.ref = {2,1},.lsn = end}; memcpy(location.uuid,uuid,16);
        write_le(allocation_image,1,8); write_le(allocation_image + 8,2,8); write_le(allocation_image + 16,1,8);
        OK(candidate_allocation_encode_root(&location,&allocation,allocation_image + 32,8192));
    }
    candidate_wal_record_t r = append_record(&c,0,3,begin,16); uint32_t crc = candidate_wal_witness_crc(0,&r);
    r = append_record(&c,0,kind,event,40); crc = candidate_wal_witness_crc(crc,&r);
    r = append_record(&c,0,4,image,sizeof(image)); crc = candidate_wal_witness_crc(crc,&r);
    if (reserve) { r = append_record(&c,0,4,allocation_image,sizeof(allocation_image)); crc = candidate_wal_witness_crc(crc,&r); }
    write_le(finish,1,8); write_le(finish + 8,reserve ? 2 : 1,4); write_le(finish + 12,reserve ? 3 : 2,4); write_le(finish + 16,crc,4);
    append_record(&c,0,8,finish,24); CHECK(candidate_wal_info(a->wal).valid_end_lsn == end); OK(candidate_wal_force(a->wal,end));
    CHECK(pwrite(a->fd,image + 32,8192,0) == 8192);
    if (reserve) CHECK(pwrite(a->fd,allocation_image + 32,8192,8192) == 8192);
    CHECK(fsync(a->fd) == 0);
}

static void boundary_case(const char *name, unsigned kind, bool gap)
{
    aborted_t a = empty_fixture();
    if (kind == 15) abort_request(&a,1,1);
    if (kind == 15) first_system_group(&a,kind,gap ? 2 : 1,gap ? 2 : 1,gap ? 3 : 2,gap ? 3 : 2);
    else first_system_group(&a,kind,gap ? 5001 : 5000,gap ? 10001 : 10000,6000,12000);
    save_source(&a,"source");
    candidate_witness_options_t request = {.enable_system_groups = true}; memcpy(request.uuid,uuid,16);
    candidate_wal_witness_t *w = NULL;
    candidate_wal_status_t witness = candidate_wal_witness_analyze_checkpoint(a.wal,&request,&a.files.anchor,&w);
    candidate_wal_status_t expected = gap ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK;
    if (witness != expected) violations++;
    if (witness != CANDIDATE_WAL_OK) CHECK(w == NULL);
    candidate_wal_witness_close(&w);
    candidate_outcome_rebuild_options_t options = rebuild_options(&a); candidate_outcome_rebuild_t *plan = NULL;
    candidate_wal_status_t actual = candidate_outcome_rebuild_load(a.wal,&options,&plan);
    if (actual != expected) violations++;
    if (actual == CANDIDATE_WAL_OK) {
        CHECK(plan != NULL && candidate_outcome_rebuild_info(plan).count == 0);
        render_plan(&a,plan,"projection",3);
    } else CHECK(plan == NULL);
    candidate_outcome_rebuild_close(&plan);
    if (gap) reject_before_effects(&a);
    save_source(&a,"after");
    printf("BOUNDARY %s case=%s expected=%u actual=%u witness=%u; synthetic target, no allocation or activation proof\n",
        a.files.wal,name,expected,actual,witness);
    close_aborted(&a);
}

static void interleaved_case(unsigned kind, unsigned placement, bool gap)
{
    aborted_t a = empty_fixture(); abort_request(&a,1,1);
    uint64_t terminal_end = candidate_wal_info(a.wal).valid_end_lsn;
    candidate_checkpoint_transaction_t terminal = {.id = 1,.first_lsn = 480,.last_lsn = 640,
        .terminal_end_lsn = terminal_end,.owner_epoch = 8,.state = CANDIDATE_CHECKPOINT_ABORT};
    CHECK(terminal_end == 768);
    a.files.anchor.owner_epoch = 8; a.files.anchor.checkpoint_id = 2;
    a.files.anchor.checkpoint_begin = terminal_end;
    a.files.anchor.checkpoint_end = terminal_end + 600 + (kind == 15 ? 8688 : 17008);
    candidate_checkpoint_summary_t summary = {.anchor = a.files.anchor,.transaction_count = 1};
    uint8_t bytes[160], digest[32] = {0}; size_t size;
    chain_t c = {.wal = a.wal}; OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));
    append_record(&c,0,12,bytes,size); OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    if (placement == 0) {
        if (kind == 15) first_system_group(&a,kind,gap ? 2 : 1,gap ? 2 : 1,2 + gap,2 + gap);
        else first_system_group(&a,kind,5000 + gap,10000 + gap,6000,12000);
    }
    candidate_checkpoint_chunk_t chunk = {.checkpoint_id = 2,.kind = 2,.count = 1};
    chunk.entries.transactions[0] = terminal;
    OK(candidate_checkpoint_encode_chunk(&chunk,bytes,sizeof(bytes),&size));
    append_record(&c,0,13,bytes,size); OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    if (placement == 1) {
        if (kind == 15) first_system_group(&a,kind,gap ? 2 : 1,gap ? 2 : 1,2 + gap,2 + gap);
        else first_system_group(&a,kind,5000 + gap,10000 + gap,6000,12000);
    }
    candidate_checkpoint_end_t end = {.checkpoint_id = 2,.begin_lsn = summary.anchor.checkpoint_begin,.chunks = 1};
    memcpy(end.digest,digest,32); OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));
    append_record(&c,0,14,bytes,size); CHECK(candidate_wal_info(a.wal).valid_end_lsn == a.files.anchor.checkpoint_end);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source");
    candidate_witness_options_t request = {.enable_system_groups = true}; memcpy(request.uuid,uuid,16);
    candidate_wal_witness_t *w = (void *)1;
    candidate_wal_status_t status = candidate_wal_witness_analyze_checkpoint(a.wal,&request,&a.files.anchor,&w);
    CHECK(status == (gap ? CANDIDATE_WAL_CORRUPT : CANDIDATE_WAL_OK));
    if (gap) CHECK(w == NULL);
    else {
        candidate_witness_system_info_t state = candidate_wal_witness_system_info(w);
        CHECK(state.current_epoch == (kind == 15 ? 2u : 1u));
        CHECK(state.transaction_highwater == (kind == 16 ? 6000u : 5000u));
    }
    candidate_wal_witness_close(&w);
    if (gap) reject_before_effects(&a);
    save_source(&a,"after");
    printf("INTERLEAVED %s kind=%u placement=%u gap=%u status=%u begin=%llu end=%llu\n",a.files.wal,kind,placement,gap,status,
        (unsigned long long)a.files.anchor.checkpoint_begin,(unsigned long long)a.files.anchor.checkpoint_end);
    close_aborted(&a);
}

static void admission_bounds(void)
{
    for (unsigned mode = 0; mode < 2; mode++) {
        aborted_t a = empty_fixture(); abort_request(&a,mode == 0 ? 5001 : 1,mode == 0 ? 1 : 2);
        OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source");
        candidate_witness_options_t request = {.enable_system_groups = true}; memcpy(request.uuid,uuid,16);
        candidate_wal_witness_t *w = (void *)1;
        CHECK(candidate_wal_witness_analyze_checkpoint(a.wal,&request,&a.files.anchor,&w) == CANDIDATE_WAL_CORRUPT && w == NULL);
        reject_before_effects(&a); save_source(&a,"after");
        printf("ADMISSION_BOUND %s mode=%u status=5\n",a.files.wal,mode); close_aborted(&a);
    }
}

static void cut_admission_bounds(void)
{
    for (unsigned mode = 0; mode < 2; mode++) {
        aborted_t a = empty_fixture(); chain_t active = {.wal = a.wal};
        admit_request(&active,9,mode == 0 ? 2 : 1,0);
        a.files.anchor.owner_epoch = 8; a.files.anchor.checkpoint_id = 2;
        a.files.anchor.request_epoch = a.files.anchor.retention_floor = mode == 0 ? 1 : 2;
        a.files.anchor.checkpoint_begin = candidate_wal_info(a.wal).valid_end_lsn;
        a.files.anchor.checkpoint_end = a.files.anchor.checkpoint_begin + 600;
        candidate_checkpoint_summary_t summary = {.anchor = a.files.anchor,.transaction_count = 1};
        candidate_checkpoint_chunk_t chunk = {.checkpoint_id = 2,.kind = 2,.count = 1};
        chunk.entries.transactions[0] = (candidate_checkpoint_transaction_t){9,active.first,active.last,0,0,8,CANDIDATE_CHECKPOINT_ACTIVE};
        chain_t checkpoint = {.wal = a.wal}; uint8_t bytes[160], digest[32] = {0}; size_t size;
        OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));
        append_record(&checkpoint,0,12,bytes,size); OK(candidate_checkpoint_digest(digest,bytes,size,digest));
        OK(candidate_checkpoint_encode_chunk(&chunk,bytes,sizeof(bytes),&size));
        append_record(&checkpoint,0,13,bytes,size); OK(candidate_checkpoint_digest(digest,bytes,size,digest));
        candidate_checkpoint_end_t end = {.checkpoint_id = 2,.begin_lsn = summary.anchor.checkpoint_begin,.chunks = 1};
        memcpy(end.digest,digest,32); OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));
        append_record(&checkpoint,0,14,bytes,size); CHECK(candidate_wal_info(a.wal).valid_end_lsn == a.files.anchor.checkpoint_end);
        OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source");
        candidate_witness_options_t request = {.enable_system_groups = true}; memcpy(request.uuid,uuid,16);
        candidate_wal_witness_t *w = (void *)1;
        CHECK(candidate_wal_witness_analyze_checkpoint(a.wal,&request,&a.files.anchor,&w) == CANDIDATE_WAL_CORRUPT && w == NULL);
        reject_before_effects(&a); save_source(&a,"after");
        printf("ADMISSION_CUT %s mode=%u status=5 begin=%llu end=%llu\n",a.files.wal,mode,
            (unsigned long long)a.files.anchor.checkpoint_begin,(unsigned long long)a.files.anchor.checkpoint_end);
        close_aborted(&a);
    }
}

static void checkpoint_after_system(void)
{
    aborted_t a = empty_fixture(); first_system_group(&a,15,1,1,2,2);
    a.files.anchor.owner_epoch = 8; a.files.anchor.checkpoint_id = 2;
    a.files.anchor.request_epoch = a.files.anchor.retention_floor = 2;
    a.files.anchor.checkpoint_begin = candidate_wal_info(a.wal).valid_end_lsn;
    a.files.anchor.checkpoint_end = a.files.anchor.checkpoint_begin + 416;
    candidate_checkpoint_summary_t summary = {.anchor = a.files.anchor};
    uint8_t bytes[160], digest[32] = {0}; size_t size; chain_t c = {.wal = a.wal};
    OK(candidate_checkpoint_encode_begin(&summary,bytes,sizeof(bytes),&size));
    append_record(&c,0,12,bytes,size); OK(candidate_checkpoint_digest(digest,bytes,size,digest));
    candidate_checkpoint_end_t end = {.checkpoint_id = 2,.begin_lsn = summary.anchor.checkpoint_begin};
    memcpy(end.digest,digest,32); OK(candidate_checkpoint_encode_end(&end,bytes,sizeof(bytes),&size));
    append_record(&c,0,14,bytes,size); CHECK(candidate_wal_info(a.wal).valid_end_lsn == a.files.anchor.checkpoint_end);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source");
    candidate_witness_options_t request = {.enable_system_groups = true}; memcpy(request.uuid,uuid,16);
    candidate_wal_witness_t *w = NULL;
    OK(candidate_wal_witness_analyze_checkpoint(a.wal,&request,&a.files.anchor,&w));
    CHECK(candidate_wal_witness_system_info(w).current_epoch == 2);
    CHECK(candidate_wal_witness_system_info(w).events == 1); candidate_wal_witness_close(&w);
    save_source(&a,"after"); printf("AFTER_SYSTEM %s begin=%llu end=%llu status=0\n",a.files.wal,
        (unsigned long long)a.files.anchor.checkpoint_begin,(unsigned long long)a.files.anchor.checkpoint_end);
    close_aborted(&a);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90);
    boundary_case("epoch-contiguous",15,false); boundary_case("epoch-gap",15,true);
    boundary_case("reserve-contiguous",16,false); boundary_case("reserve-gap",16,true);
    for (unsigned kind = 15; kind <= 16; kind++)
        for (unsigned placement = 0; placement <= 1; placement++) {
            interleaved_case(kind,placement,false); interleaved_case(kind,placement,true);
        }
    admission_bounds(); cut_admission_bounds(); checkpoint_after_system();
    printf("CHECKPOINT state checks=%u violations=%u; no allocation or activation proof\n",checks,violations);
    return violations == 0 ? 0 : 1;
}
