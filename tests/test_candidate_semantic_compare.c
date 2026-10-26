#include "test_candidate_semantic_fixture.h"

#include <string.h>

#include "../src/storage/candidate_wal_witness_state_internal.h"

static bool same_ref(candidate_version_ref_t a, candidate_version_ref_t b)
{
    bool same_page = a.page.id == b.page.id && a.page.generation == b.page.generation;
    return same_page && a.slot == b.slot && a.slot_generation == b.slot_generation;
}

static void same_event(const candidate_witness_system_event_t *a, const candidate_witness_system_event_t *b)
{
    CHECK(a != NULL && b != NULL);
    CHECK(a->type == b->type && a->group_id == b->group_id && a->end_lsn == b->end_lsn);
    CHECK(a->owner_epoch == b->owner_epoch);
    CHECK(a->value.allocation.page_id == b->value.allocation.page_id);
    CHECK(a->value.allocation.previous_generation == b->value.allocation.previous_generation);
    CHECK(a->value.allocation.next_generation == b->value.allocation.next_generation);
    CHECK(a->value.allocation.release_horizon == b->value.allocation.release_horizon);
}

static void same_value(const candidate_witness_transaction_t *a, const candidate_witness_transaction_t *b)
{
    CHECK(a->transaction_id == b->transaction_id && a->first_lsn == b->first_lsn && a->last_lsn == b->last_lsn);
    CHECK(a->records == b->records && a->csn == b->csn && a->terminal_end_lsn == b->terminal_end_lsn);
    CHECK(a->request_epoch == b->request_epoch && memcmp(a->request_nonce, b->request_nonce, 16) == 0);
    CHECK(memcmp(a->operation_digest, b->operation_digest, 32) == 0);
    CHECK(memcmp(a->result_digest, b->result_digest, 32) == 0 && a->result_bytes == b->result_bytes);
    CHECK(a->result_chunks == b->result_chunks && a->groups == b->groups && a->chain_crc == b->chain_crc);
    CHECK(a->state == b->state && a->has_admission == b->has_admission && a->has_begin == b->has_begin);
}

static void same_transport(const candidate_wal_info_t *a, const candidate_wal_info_t *b)
{
    CHECK(a->records == b->records && a->valid_end_lsn == b->valid_end_lsn);
    CHECK(a->durable_end_lsn == b->durable_end_lsn && a->highest_owner_epoch == b->highest_owner_epoch);
    CHECK(a->tail_bytes == b->tail_bytes && a->resident_bytes == b->resident_bytes);
    CHECK(a->segments == b->segments && a->has_tail == b->has_tail);
}

static void same_system(const candidate_wal_witness_t *a, const candidate_wal_witness_t *b)
{
    candidate_witness_system_info_t x = candidate_wal_witness_system_info(a);
    candidate_witness_system_info_t y = candidate_wal_witness_system_info(b);
    CHECK(x.transaction_highwater == y.transaction_highwater && x.page_highwater == y.page_highwater);
    CHECK(x.current_epoch == y.current_epoch && x.retention_floor == y.retention_floor);
    CHECK(x.events == y.events && x.system_groups == y.system_groups);
    CHECK(x.has_reservation == y.has_reservation && x.has_epoch == y.has_epoch);
    for (uint32_t i = 0; i < x.events; i++)
        same_event(candidate_wal_witness_system_at(a, i), candidate_wal_witness_system_at(b, i));
    CHECK(candidate_wal_witness_system_at(a, x.events) == NULL && candidate_wal_witness_system_at(b, y.events) == NULL);
}

void fixture_same_witness(const candidate_wal_witness_t *a, const candidate_wal_witness_t *b)
{
    candidate_witness_info_t x = candidate_wal_witness_info(a), y = candidate_wal_witness_info(b);
    CHECK(x.records == y.records && x.highest_csn == y.highest_csn && x.incomplete_group_lsn == y.incomplete_group_lsn);
    CHECK(x.transactions == y.transactions && x.commits == y.commits && x.aborts == y.aborts);
    CHECK(x.unfinished == y.unfinished && x.complete_groups == y.complete_groups);
    CHECK(x.resident_bytes == y.resident_bytes);
    same_transport(&x.transport, &y.transport);
    same_system(a, b);
    for (uint32_t i = 0; i < x.transactions; i++) {
        candidate_semantic_transaction_t p, q;
        CHECK(candidate_witness_transaction_at(a, i, &p) && candidate_witness_transaction_at(b, i, &q));
        same_value(&p.value, &q.value);
        CHECK(p.owner_epoch == q.owner_epoch && p.result_limit == q.result_limit && p.chunk_bytes == q.chunk_bytes);
    }
    for (uint32_t i = 0; i < x.complete_groups; i++) {
        candidate_semantic_group_t p, q;
        CHECK(candidate_witness_group_at(a, i, &p) && candidate_witness_group_at(b, i, &q));
        CHECK(p.id == q.id && p.transaction_id == q.transaction_id && p.end_lsn == q.end_lsn);
    }
}

void fixture_same_seed(const candidate_semantic_seed_t *a, const candidate_semantic_seed_t *b)
{
    candidate_semantic_seed_info_t x = candidate_semantic_seed_info(a), y = candidate_semantic_seed_info(b);
    CHECK(memcmp(x.cut.uuid, y.cut.uuid, 16) == 0 && x.cut.end_lsn == y.cut.end_lsn);
    CHECK(x.cut.records == y.cut.records && x.cut.highest_owner_epoch == y.cut.highest_owner_epoch);
    CHECK(x.cut.segments == y.cut.segments && x.admissions == y.admissions);
    CHECK(x.resident_bytes == y.resident_bytes && x.has_system == y.has_system);
    fixture_same_witness(candidate_semantic_seed_witness(a), candidate_semantic_seed_witness(b));
    for (uint32_t i = 0; i < x.admissions; i++) {
        candidate_semantic_admission_t p, q;
        CHECK(candidate_semantic_seed_admission_at(a, i, &p) && candidate_semantic_seed_admission_at(b, i, &q));
        CHECK(p.transaction_id == q.transaction_id && p.owner_epoch == q.owner_epoch);
        CHECK(p.result_limit == q.result_limit && p.has_result == q.has_result && same_ref(p.result, q.result));
    }
}

static void expect_transaction(const fixture_transaction_t *e, const candidate_semantic_seed_t *seed, uint32_t index)
{
    candidate_semantic_transaction_t t;
    CHECK(candidate_semantic_seed_transaction_at(seed, index, &t));
    const candidate_witness_transaction_t *v = &t.value;
    CHECK(v->transaction_id == e->chain.id && v->first_lsn == e->chain.first && v->last_lsn == e->chain.last);
    CHECK(v->records == e->chain.count && v->chain_crc == e->chain.crc && v->groups == e->groups);
    CHECK(v->state == e->state && v->csn == e->csn && v->terminal_end_lsn == e->terminal_end);
    CHECK(v->has_begin == e->has_begin && v->has_admission == e->has_admission);
    CHECK(v->result_chunks == e->chunks && v->result_bytes == e->result_bytes);
    CHECK(t.owner_epoch == e->owner_epoch && t.result_limit == e->limit && t.chunk_bytes == e->chunk_bytes);
    if (e->has_admission) {
        CHECK(v->request_epoch == e->epoch && memcmp(v->request_nonce, e->nonce, 16) == 0);
        CHECK(memcmp(v->operation_digest, e->operation, 32) == 0);
        CHECK(memcmp(v->result_digest, e->result_digest, 32) == 0);
    }
    const candidate_witness_transaction_t *found = candidate_wal_witness_find(candidate_semantic_seed_witness(seed),
        e->chain.id);
    CHECK(found != NULL && found->records == v->records && found->state == v->state);
}

static void expect_system(const fixture_t *f, const candidate_wal_witness_t *w)
{
    candidate_witness_system_info_t s = candidate_wal_witness_system_info(w);
    bool has_state = f->system_groups > 0;
    CHECK(s.events == f->event_count && s.system_groups == f->system_groups);
    CHECK(s.has_reservation == has_state && s.has_epoch == has_state);
    if (has_state) {
        CHECK(s.transaction_highwater == f->system.transaction_highwater);
        CHECK(s.page_highwater == f->system.page_highwater);
        CHECK(s.current_epoch == f->system.epoch && s.retention_floor == f->system.floor);
    }
    for (uint32_t i = 0; i < f->event_count; i++) same_event(candidate_wal_witness_system_at(w, i), &f->events[i]);
    CHECK(candidate_wal_witness_system_at(w, f->event_count) == NULL);
}

static void expect_outcomes(const fixture_t *f, const candidate_witness_info_t *info)
{
    unsigned commits = 0, aborts = 0, unfinished = 0;
    uint64_t highest = 0;
    for (unsigned i = 0; i < f->transaction_count; i++) {
        const fixture_transaction_t *t = &f->transactions[i];
        if (t->state == CANDIDATE_WITNESS_COMMIT) commits++;
        else if (t->state == CANDIDATE_WITNESS_ABORT) aborts++;
        else unfinished++;
        if (t->csn > highest) highest = t->csn;
    }
    CHECK(info->commits == commits && info->aborts == aborts && info->unfinished == unfinished);
    CHECK(info->highest_csn == highest);
}

static void expect_admissions(const fixture_t *f, const candidate_semantic_seed_t *seed)
{
    candidate_semantic_admission_t a;
    for (uint32_t i = 0; i < f->admission_count; i++) {
        const fixture_transaction_t *e = &f->transactions[f->admitted[i]];
        CHECK(candidate_semantic_seed_admission_at(seed, i, &a));
        CHECK(a.transaction_id == e->chain.id && a.owner_epoch == e->owner_epoch && a.result_limit == e->limit);
        CHECK(a.has_result == (e->chunks > 0));
        if (a.has_result) CHECK(same_ref(a.result, e->ref));
    }
    CHECK(!candidate_semantic_seed_admission_at(seed, f->admission_count, &a));
}

void fixture_expect(const fixture_t *f, const candidate_semantic_seed_t *seed)
{
    candidate_semantic_seed_info_t info = candidate_semantic_seed_info(seed);
    CHECK(memcmp(info.cut.uuid, fixture_uuid, 16) == 0 && info.cut.end_lsn == f->end_lsn);
    CHECK(info.cut.records == f->records && info.cut.highest_owner_epoch == f->highest_owner);
    CHECK(info.cut.segments == 1 && info.has_system == f->has_system && info.admissions == f->admission_count);
    const candidate_wal_witness_t *w = candidate_semantic_seed_witness(seed);
    candidate_witness_info_t wi = candidate_wal_witness_info(w);
    CHECK(wi.records == f->records && wi.transactions == f->transaction_count);
    CHECK(wi.complete_groups == f->group_count && wi.incomplete_group_lsn == 0);
    CHECK(wi.transport.valid_end_lsn == f->end_lsn && wi.transport.records == f->records && !wi.transport.has_tail);
    expect_outcomes(f, &wi);
    expect_system(f, w);
    for (uint32_t i = 0; i < f->transaction_count; i++) expect_transaction(&f->transactions[i], seed, i);
    candidate_semantic_transaction_t t;
    CHECK(!candidate_semantic_seed_transaction_at(seed, f->transaction_count, &t));
    for (uint32_t i = 0; i < f->group_count; i++) {
        candidate_semantic_group_t g;
        CHECK(candidate_semantic_seed_group_at(seed, i, &g));
        CHECK(g.id == f->groups[i].id && g.transaction_id == f->groups[i].transaction_id);
        CHECK(g.end_lsn == f->groups[i].end_lsn);
    }
    candidate_semantic_group_t g;
    CHECK(!candidate_semantic_seed_group_at(seed, f->group_count, &g));
    expect_admissions(f, seed);
}
