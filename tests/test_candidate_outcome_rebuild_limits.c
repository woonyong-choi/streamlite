#define CANDIDATE_REBUILD_TEST_MAIN outcome_rebuild_regression_main
#include "test_candidate_outcome_rebuild.c"

static aborted_t empty_fixture(void)
{
    aborted_t a = {0};
    CHECK(snprintf(a.files.wal,sizeof(a.files.wal),"build/candidate-rebuild-%ld-%u",(long)getpid(),++serial) > 0);
    CHECK(snprintf(a.files.data,sizeof(a.files.data),"%s.pages",a.files.wal) > 0);
    a.files.anchor = (candidate_control_state_t){.backend = 1,.generation = 1,.owner_epoch = 7,.checkpoint_id = 1,
        .redo_begin = 64,.transaction_highwater = 5000,.page_highwater = 10000,.request_epoch = 1,.retention_floor = 1,
        .outcome_root = {1,1},.allocation_root = {2,1}};
    memcpy(a.files.anchor.uuid,uuid,16); candidate_wal_options_t wo = {.owner_epoch = 7}; memcpy(wo.uuid,uuid,16);
    OK(candidate_wal_create(a.files.wal,&wo,&a.wal)); checkpoint(a.wal,&a.files,NULL);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); OK(candidate_wal_close(&a.wal));
    wo.owner_epoch = 8; OK(candidate_wal_open(a.files.wal,&wo,&a.wal));
    a.fd = open(a.files.data,O_CREAT | O_EXCL | O_RDWR,0600); CHECK(a.fd >= 0);
    candidate_catalog_root_t root = {.owner_epoch = 7,.current_epoch = 1,.retention_floor = 1,
        .transaction_highwater = 5000,.page_highwater = 10000,.retained_bytes = 8192};
    candidate_page_location_t l = {.ref = {1,1}}; memcpy(l.uuid,uuid,16); uint8_t bytes[8192];
    OK(candidate_catalog_encode_root(&l,&root,bytes,8192)); CHECK(write(a.fd,bytes,8192) == 8192); CHECK(fsync(a.fd) == 0);
    printf("REBUILD_FIXTURE %s checkpoint_end=%llu; synthetic metadata, no allocation proof\n",a.files.wal,(unsigned long long)a.files.anchor.checkpoint_end);
    return a;
}

static void admit_request(chain_t *c, uint64_t transaction, uint64_t epoch, uint64_t limit)
{
    uint8_t admit[64] = {0}; write_le(admit,epoch,8); write_le(admit + 8,transaction,8); memset(admit + 24,0x5c,32); write_le(admit + 56,limit,8);
    append_record(c,transaction,1,admit,64);
}

static void abort_request(aborted_t *a, uint64_t transaction, uint64_t epoch)
{
    chain_t c = {.wal = a->wal}; admit_request(&c,transaction,epoch,0);
    uint8_t abort[32] = {0}; write_le(abort,c.first,8); write_le(abort + 8,c.last,8); write_le(abort + 16,c.records,8); write_le(abort + 24,c.crc,4);
    append_record(&c,transaction,11,abort,32);
}

static void epoch_group(aborted_t *a, unsigned gid, uint64_t epoch, uint64_t floor)
{
    uint8_t bytes[8192]; CHECK(pread(a->fd,bytes,8192,0) == 8192); candidate_catalog_root_t root; candidate_page_info_t page;
    OK(candidate_catalog_decode_root(uuid,bytes,8192,&root)); OK(candidate_page_decode_header(uuid,bytes,8192,&page));
    uint64_t end = candidate_wal_info(a->wal).valid_end_lsn + 8688;
    chain_t c = {.wal = a->wal}; uint8_t begin[16] = {0}, event[40] = {0}, image[8224] = {0}, finish[24] = {0};
    write_le(begin,gid,8); write_le(begin + 8,1,4); write_le(begin + 12,2,4);
    write_le(event,gid,8); write_le(event + 8,root.current_epoch,8); write_le(event + 16,epoch,8);
    write_le(event + 24,root.retention_floor,8); write_le(event + 32,floor,8);
    root.current_epoch = epoch; root.retention_floor = floor; root.owner_epoch = 8;
    write_le(image,gid,8); write_le(image + 8,1,8); write_le(image + 16,1,8); write_le(image + 24,page.location.lsn,8);
    page.location.lsn = end; OK(candidate_catalog_encode_root(&page.location,&root,image + 32,8192));
    candidate_wal_record_t r = append_record(&c,0,3,begin,16); uint32_t crc = candidate_wal_witness_crc(0,&r);
    r = append_record(&c,0,15,event,40); crc = candidate_wal_witness_crc(crc,&r);
    r = append_record(&c,0,4,image,sizeof(image)); crc = candidate_wal_witness_crc(crc,&r);
    write_le(finish,gid,8); write_le(finish + 8,1,4); write_le(finish + 12,2,4); write_le(finish + 16,crc,4);
    append_record(&c,0,8,finish,24); CHECK(candidate_wal_info(a->wal).valid_end_lsn == end);
    OK(candidate_wal_force(a->wal,end)); CHECK(pwrite(a->fd,image + 32,8192,0) == 8192); CHECK(fsync(a->fd) == 0);
}

static void capacity_rebuild(void)
{
    aborted_t a = empty_fixture();
    for (unsigned i = 1; i <= 4096; i++) {
        unsigned epoch = (i - 1) / 1024 + 1;
        if (i > 1 && (i - 1) % 1024 == 0) epoch_group(&a,epoch - 1,epoch,1);
        abort_request(&a,i,epoch);
    }
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source4096");
    candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_info(p).count == 4096);
    render_plan(&a,p,"projection4096",2); candidate_outcome_rebuild_close(&p);
    epoch_group(&a,4,4,2); save_source(&a,"source3072");
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_info(p).count == 3072);
    render_plan(&a,p,"projection3072",2); candidate_outcome_rebuild_close(&p);
    abort_request(&a,4097,4); OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn));
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_LIMIT && p == NULL);
    printf("REBUILD_LIMIT %s total=4097 status=3\n",a.files.wal); close_aborted(&a);
    a = empty_fixture();
    for (unsigned i = 1; i <= 1025; i++) abort_request(&a,i,1);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source"); o = rebuild_options(&a);
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_LIMIT && p == NULL);
    printf("REBUILD_LIMIT %s per_epoch=1025 status=3\n",a.files.wal); close_aborted(&a);
}

static void body_pages(aborted_t *a, unsigned transaction, const uint8_t *body)
{
    unsigned first = 2 + (transaction - 1) * 131; uint8_t bytes[8192];
    candidate_page_location_t l = {.ref = {first,1}}; memcpy(l.uuid,uuid,16);
    candidate_page_version_t v = {.transaction_id = transaction,.table_id = 1,.row_id = transaction,
        .payload_bytes = 1048576,.slot_generation = 7,.overflow = {first + 1,1}};
    OK(candidate_page_encode_versions(&l,&v,1,bytes,8192)); CHECK(pwrite(a->fd,bytes,8192,(off_t)(first - 1) * 8192) == 8192);
    for (unsigned i = 0; i < 130; i++) {
        size_t at = (size_t)i * 8104, n = 1048576 - at < 8104 ? 1048576 - at : 8104;
        l.ref.id = first + i + 1; candidate_page_ref_t next = {i == 129 ? 0 : first + i + 2,i == 129 ? 0 : 1};
        OK(candidate_page_encode_overflow(&l,next,body + at,n,bytes,8192)); CHECK(pwrite(a->fd,bytes,8192,(off_t)(l.ref.id - 1) * 8192) == 8192);
    }
}

static void commit_result(aborted_t *a, unsigned transaction, const uint8_t digest[32])
{
    chain_t c = {.wal = a->wal}; admit_request(&c,transaction,1,1048576); uint8_t begin[16] = {0}; append_record(&c,transaction,2,begin,16);
    for (unsigned i = 0; i < 16; i++) {
        uint8_t chunk[112] = {0}; write_le(chunk,1,8); write_le(chunk + 8,transaction,8); write_le(chunk + 24,i,4); write_le(chunk + 28,16,4);
        write_le(chunk + 32,1048576,8); memcpy(chunk + 40,digest,32); write_le(chunk + 72,2 + (transaction - 1) * 131,8);
        write_le(chunk + 80,1,8); write_le(chunk + 92,7,4); write_le(chunk + 96,(uint64_t)i * 65536,8); write_le(chunk + 104,65536,4);
        append_record(&c,transaction,9,chunk,112);
    }
    uint8_t commit[152] = {0}; write_le(commit,c.first,8); write_le(commit + 8,c.last,8); write_le(commit + 16,c.records,8); write_le(commit + 24,c.crc,4);
    write_le(commit + 32,transaction,8); write_le(commit + 40,1,8); write_le(commit + 48,transaction,8); memset(commit + 64,0x5c,32);
    memcpy(commit + 96,digest,32); write_le(commit + 128,1048576,8); write_le(commit + 136,16,4); append_record(&c,transaction,10,commit,152);
}

static void space_rebuild(void)
{
    aborted_t a = empty_fixture(); uint8_t *body = malloc(1048576), digest[32]; CHECK(body != NULL);
    for (unsigned i = 0; i < 1048576; i++) body[i] = (uint8_t)(i % 251);
    OK(candidate_digest_sha256(body,1048576,digest));
    for (unsigned i = 1; i <= 64; i++) body_pages(&a,i,body);
    free(body); CHECK(fsync(a.fd) == 0); save_source(&a,"source");
    for (unsigned i = 1; i <= 63; i++) commit_result(&a,i,digest);
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_info(p).result_bytes == 63u * 1048576u);
    render_plan(&a,p,"projection63",9999); candidate_outcome_rebuild_close(&p);
    commit_result(&a,64,digest); OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn));
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_LIMIT && p == NULL);
    printf("REBUILD_LIMIT %s result_bytes=67108864 plus_metadata status=3; lower bound only\n",a.files.wal); close_aborted(&a);
}

static void empty_rebuild(void)
{
    aborted_t a = empty_fixture(); candidate_outcome_rebuild_options_t o = rebuild_options(&a); candidate_outcome_rebuild_t *p = NULL;
    OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn)); save_source(&a,"source0");
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_info(p).count == 0);
    render_plan(&a,p,"projection0",2); candidate_outcome_rebuild_close(&p);
    chain_t c = {.wal = a.wal}; admit_request(&c,1,1,0); OK(candidate_wal_force(a.wal,candidate_wal_info(a.wal).valid_end_lsn));
    CHECK(candidate_outcome_rebuild_load(a.wal,&o,&p) == CANDIDATE_WAL_STALE && p == NULL);
    uint8_t abort[32] = {0}; write_le(abort,c.first,8); write_le(abort + 8,c.last,8); write_le(abort + 16,c.records,8); write_le(abort + 24,c.crc,4);
    append_record(&c,1,11,abort,32); epoch_group(&a,1,2,2); save_source(&a,"source2");
    OK(candidate_outcome_rebuild_load(a.wal,&o,&p)); CHECK(candidate_outcome_rebuild_info(p).count == 0);
    render_plan(&a,p,"projection2",2); candidate_outcome_rebuild_close(&p); close_aborted(&a);
}

#ifndef CANDIDATE_REBUILD_LIMIT_TEST_MAIN
#define CANDIDATE_REBUILD_LIMIT_TEST_MAIN main
#endif
int CANDIDATE_REBUILD_LIMIT_TEST_MAIN(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0); alarm(90); CHECK(argc == 2);
    if (strcmp(argv[1],"capacity") == 0) capacity_rebuild();
    else if (strcmp(argv[1],"empty") == 0) empty_rebuild();
    else { CHECK(strcmp(argv[1],"space") == 0); space_rebuild(); }
    printf("PASS reconstruction limits checks=%u mode=%s; unpublished metadata only\n",checks,argv[1]); return 0;
}
