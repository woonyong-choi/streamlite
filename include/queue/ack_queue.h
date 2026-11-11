#ifndef ACK_QUEUE_H
#define ACK_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sql/database.h"
#include "sql/session.h"

/*
 * 선택형 ACK 큐(#140). 독립 DB 위에 따로 설치하는 라이브러리이며 DB 코어(database·storage)는 이 모듈을 모른다.
 * 큐를 설치하지 않으면 객체·worker·전용 테이블이 생기지 않는다. 큐는 세션(session.h)의 공개 SQL과 트랜잭션 API만 쓰고
 * 큐 하나는 테이블 하나(streamlite_queue_<name>)다. 큐마다 따로 설치하며 DB와 같은 파일에 산다.
 * 큐는 key 보존 테이블(<table>_keys)과 색인(<table>_state, <table>_idem, <table>_keys_idem)도 만든다. 그래서 _keys·_state·
 * _idem으로 끝나는 이름과, 이미 있는 큐의 테이블 이름이 자기 내부 이름과 같아지는 이름은 새로 설치하지 않는다
 * (ACKQ_INVALID, 아무것도 만들지 않는다). 이 규칙 전에 만든 큐는 이름과 무관하게 그대로 열린다.
 *
 * 작업(job) 상태: READY -> LEASED -> ACKED | FAILED, 그리고 LEASED -> READY(nack, 만료, 해제).
 *   claim이 READY 작업(또는 만료된 LEASED)을 LEASED로 바꾸고 fence token을 1 올려 돌려준다. fence는 작업별로 단조 증가하며
 *   재사용되지 않는다. ack·nack·fail·extend·update는 (id, fence)를 받아 그 claim이 지금도 유효할 때만 적용한다. 다른
 *   worker가 다시 claim했거나(fence 불일치) 이미 READY로 돌아갔거나 끝난 작업에 대한 오래된 호출은 ACKQ_STALE로 거절한다.
 *   lease 만료만으로는 claim이 끝나지 않는다. 만료된 뒤에도 아무도 다시 claim하지 않았고 ackq_recover·
 *   ackq_release_leases가 그 작업을 돌려놓지 않았다면 ack할 수 있다(같은 fence가 여전히 현재이므로 이중 처리가 아니다).
 *   recover·release는 처리권을 끝내는 연산이다. 작업을 돌려놓으면서 fence를 1 올리므로, 아무도 다시 claim하지 않았어도
 *   옛 claim의 ack·nack·fail·extend·update는 모두 ACKQ_STALE다. 그 뒤의 claim은 다시 1 오른 fence를 받는다.
 *   purge가 지운 작업은 fence를 확인할 행이 없으므로 중복 ack·fail도 ACKQ_NOT_FOUND다. 응답을 잃은 ack를 다시 보낼 수
 *   있는 동안은 그 작업을 purge하지 않는다.
 *
 * 전달 보장: at-least-once다. worker가 죽거나 lease가 만료되면 같은 작업이 다시 전달된다. 외부 부작용(파일·HTTP)의
 * exactly-once나 원자적 commit은 제공하지 않는다. idempotency key가 같고 payload가 같은 enqueue는 기존 작업을
 * 돌려주고(duplicate), payload가 다르면 ACKQ_CONFLICT다. 끝난 작업을 purge한 뒤에도 보존 기간(idem_retention_ms) 동안은
 * key·id·payload 요약을 streamlite_queue_<name>_keys에 남겨 같은 판정을 한다. 요약은 길이와 두 체크섬(CRC32C, FNV-1a 64)이라
 * 다른 payload가 같은 요약을 가질 확률은 무시할 만큼 작지만 0은 아니다.
 *
 * 내구성: 모든 변경은 database의 commit을 따른다(database_txn.h). 성공은 COW 게시의 fsync가 끝난 뒤에만 돌아오므로
 * 승인한 작업은 프로세스·OS 종료 뒤에도 남는다. commit 결과를 알 수 없으면(ACKQ_UNCERTAIN) 성공으로 돌려주지 않는다. 다시 열어 상태를 확인해야 한다.
 *
 * 세션: 모든 연산은 큐를 설치한 데이터베이스의 세션(session_t)을 받는다. 다른 데이터베이스의 세션이면 ACKQ_INVALID다.
 * 문장은 session_exec로 실행하므로 DB 단독 사용과 같은 예산(query·session·shared), 결과 계상, 취소(session_cancel →
 * ACKQ_CANCELLED) 계약을 따른다.
 * 같은 트랜잭션: 세션에 트랜잭션이 열려 있지 않으면 함수가 그 세션에 자기 트랜잭션을 열고 commit한다(자동 commit).
 * 열려 있으면(session_txn_begin) 그 안에서 문장만 실행하고 commit은 호출자가 한다. 그래서 업무 갱신(session_exec)과
 * ack를 하나의 session_txn_commit으로 함께 확정하거나 함께 버릴 수 있다. ACKQ_STALE·NOT_FOUND·CONFLICT는 변경 없이 거절하는
 * 정상 결과이며 호출자의 트랜잭션을 실패 상태로 만들지 않는다(그 앞에서 호출자가 이미 한 변경은 호출자가 rollback할지
 * 정한다). 그 밖의 비OK 상태(IO·NOMEM·LIMIT·INTERNAL 등)는 문장이 일부만 실행됐을 수 있으므로 호출자는 그 트랜잭션을
 * rollback해야 한다(commit하면 큐 상태가 일부만 반영될 수 있다).
 *
 * 저장과 조회: 설치 때 state·idem 보조 색인을 만든다. 정수 기본 키 테이블의 비고유 색인은 같은 값 안에서 id 순서라
 * claim·recover·purge는 WHERE state = ? ... ORDER BY id LIMIT k로 맞는 작업 k개만 읽는다. ack·nack·fail·update는 기본 키
 * 한 행을 읽고 고친다. stats는 상태별 개수라 테이블 전체를 읽는다. 행 단위 잠금은 없고 쓰기 트랜잭션은 하나다.
 * payload는 선택한 작업의 것만 읽는다.
 *
 * 스레드: 한 세션은 한 번에 한 스레드만 쓴다(session 계약). ackq_t의 설정은 바뀌지 않고 아래의 시각 하한과 관측만 짧은 잠금
 * 안에서 갱신하므로 여러 스레드가 각자 세션으로 같은 큐를 써도 된다. 쓰기 트랜잭션은 데이터베이스마다 하나이므로 다른 세션이
 * 쓰는 동안 자기 트랜잭션은 begin_timeout_ms까지 기다린 뒤 ACKQ_BUSY다.
 *
 * 시계: 시간은 now_ms 콜백이 주며 기본은 CLOCK_REALTIME 밀리초다. lease·지연·보존 기한은 절대 시각으로 저장하므로
 * 프로세스 사이와 재시작 뒤에도 비교할 수 있다. 벽시계는 믿을 수 없으므로 핸들(ackq_t)마다 마지막으로 읽은 값과 그때의
 * 단조 시각(절전 중에도 흐르는 CLOCK_BOOTTIME, 없으면 CLOCK_MONOTONIC)을 함께 기억하고 읽은 값을 이렇게 다룬다.
 *   - 범위 밖: 1970년 전(음수)이거나 INT64_MAX - ACKQ_MAX_RETENTION_MS를 넘는 값(음수가 uint64로 감긴 값 포함)은 읽지
 *     못한 것으로 보고 버린다. 유효한 값을 한 번이라도 읽은 핸들은 그 하한으로 진행하므로 연산이 ACKQ_LIMIT이 되지
 *     않는다. 유효한 값을 한 번도 읽지 못한 핸들은 1970년으로 진행하지 않고(lease가 바로 만료로 보인다) 시각을 쓰는
 *     연산(enqueue·claim·extend·nack·recover·release·purge)을 ACKQ_LIMIT으로 거절하며 아무것도 바꾸지 않는다.
 *   - 뒤로 감: 핸들은 돌려준 시각의 하한을 기억하고 그보다 과거인 값은 그 하한으로 읽는다. 그래서 뒤로 간 시계로 lease를
 *     만료시키거나 줄여서 연장하지 않는다. 시계가 따라잡을 때까지 그 핸들의 시각은 멈춰 있으므로 그동안 lease·nack 지연·
 *     key 보존은 뒤로 간 만큼 길어진다.
 *   - 앞으로 뜀: 마지막으로 읽은 값 + 그 뒤의 단조 경과보다 1초(ACKQ_CLOCK_JUMP_MS) 넘게 앞선 값이다. 실제로 시간이 흐른
 *     것(정지 등)과 구분하지 못하므로 받아들인다. 뛴 만큼의 lease가 만료되어 살아 있는 worker의 작업이 다시 전달될 수
 *     있고(옛 fence는 STALE), 지연과 보존 기간도 그만큼 일찍 끝난다. 뛴 시계가 1초 넘게 되돌아오면 하한을 "뛰기 직전의
 *     예상 시각 + 그 뒤의 단조 경과"까지 낮추므로(되돌아온 값보다 낮추지는 않는다) 그 핸들은 뛴 시각에 머물지 않고 실제
 *     경과를 따라간다. 뛴 동안 이 핸들이 정한 lease·지연은 뛴 만큼 길게 남는다.
 * 한계: 하한과 관측은 핸들의 메모리에만 있다. 큐 테이블에는 이를 둘 자리(큐 단위 메타 행)가 없어 저장하지 않으므로,
 * 다른 프로세스나 재시작한 프로세스는 뒤로 간 시계를 알아채지 못하고 그만큼 lease가 길어진다. 결정적 시험은 콜백으로
 * 시간을 직접 주입한다(주입한 시계에도 같은 규칙이 적용되고 단조 시각은 실제 시계다).
 */

#define ACKQ_NAME_MAX 40u
#define ACKQ_IDEM_KEY_MAX 128u
#define ACKQ_NOTE_MAX 200u
/*
 * payload 상한(바이트). 큐마다 max_payload_bytes로 정하고 0이면 기본값이다. 쓰기 트랜잭션은 하나라 큰 payload를 넣는
 * 동안 다른 연산이 기다리고, claim은 최대 ACKQ_MAX_CLAIM_BATCH건의 payload를 메모리에 올리므로 기본값을 작게 둔다.
 * 약 3.9 KB 이하는 행 페이지 안에 들어가 가장 빠르고, 그보다 크면 overflow 체인에 저장된다.
 */
#define ACKQ_DEFAULT_MAX_PAYLOAD_BYTES ((size_t)1 << 20)
#define ACKQ_MAX_PAYLOAD_LIMIT ((size_t)1 << 30)
/* purge 뒤 idempotency key 보존 기간. 0이면 기본값(7일), ACKQ_RETENTION_NONE이면 남기지 않는다 */
#define ACKQ_DEFAULT_IDEM_RETENTION_MS (7ull * 24ull * 3600ull * 1000ull)
#define ACKQ_RETENTION_NONE UINT64_MAX
#define ACKQ_MAX_RETENTION_MS (100ull * 366ull * 24ull * 3600ull * 1000ull)
#define ACKQ_MAX_LEASE_MS (366ull * 24ull * 3600ull * 1000ull)
#define ACKQ_MAX_CLAIM_BATCH 1024u

typedef enum {
    ACKQ_OK,
    ACKQ_INVALID,        /* 인자 오류: 이름·길이·개수·시간 */
    ACKQ_NOMEM,
    ACKQ_LIMIT,          /* payload·메모리 예산 한도 */
    ACKQ_BUSY,           /* 다른 writer가 있고 기다린 시간이 끝났다. 재시도할 수 있다 */
    ACKQ_NOT_INSTALLED,  /* 큐 테이블이 없다. 열기는 만들지 않는다 */
    ACKQ_NOT_FOUND,      /* id가 없다 */
    ACKQ_STALE,          /* fence가 현재 claim이 아니거나 상태가 맞지 않는다. 아무것도 바꾸지 않았다 */
    ACKQ_CONFLICT,       /* 같은 idempotency key에 payload가 다르다 */
    ACKQ_UNSUPPORTED,    /* 트랜잭션 계층 없이 연 database에 쓰기 요청 */
    ACKQ_IO,             /* 저장소 오류. commit 전이라면 상태는 그대로다 */
    ACKQ_NOT_APPLIED,    /* commit이 확정적으로 적용되지 않았다. 상태는 그대로다 */
    ACKQ_UNCERTAIN,      /* commit 결과를 알 수 없다. 성공으로 취급하면 안 된다. 다시 열어 확인한다 */
    ACKQ_POISONED,       /* database가 불확실한 commit 뒤 더는 쓸 수 없다. 닫고 다시 연다 */
    ACKQ_TXN_FAILED,     /* 호출자의 트랜잭션이 이미 실패 상태다. rollback 뒤 다시 한다 */
    ACKQ_INTERNAL,       /* 이 모듈이 기대하지 못한 database 응답 */
    ACKQ_CANCELLED       /* session_cancel로 문장이 취소됐다. 자기 트랜잭션이면 rollback했다 */
} ackq_status_t;

typedef enum {
    ACKQ_STATE_READY = 0,
    ACKQ_STATE_LEASED = 1,
    ACKQ_STATE_ACKED = 2,
    ACKQ_STATE_FAILED = 3  /* terminal: 명시적 fail이나 시도 횟수 초과 */
} ackq_state_t;

typedef struct ackq ackq_t;

typedef struct {
    /* [A-Za-z0-9_]{1..ACKQ_NAME_MAX}. 테이블은 streamlite_queue_<name>. 새 큐는 _keys·_state·_idem으로 끝날 수 없다 */
    const char *name;
    /*
     * 0이면 제한 없음. 시도(claim) 횟수가 이 값에 닿은 작업의 nack·만료(ackq_recover)는 FAILED로 끝난다.
     * ackq_release_leases는 작업의 실패가 아니므로 한도에 닿은 작업을 한 번 더 전달하고, 한도를 넘긴 작업만 FAILED로 끝낸다.
     */
    uint32_t max_attempts;
    uint32_t begin_timeout_ms;       /* 자기 트랜잭션이 다른 writer를 기다리는 시간. 0이면 기다리지 않고 BUSY */
    size_t max_payload_bytes;        /* 0이면 ACKQ_DEFAULT_MAX_PAYLOAD_BYTES. ACKQ_MAX_PAYLOAD_LIMIT를 넘으면 INVALID */
    uint64_t idem_retention_ms;      /* 0이면 기본 7일, ACKQ_RETENTION_NONE이면 남기지 않음. ACKQ_MAX_RETENTION_MS까지 */
    uint64_t (*now_ms)(void *ctx);   /* NULL이면 CLOCK_REALTIME 밀리초 */
    void *clock_ctx;
} ackq_options_t;

/*
 * 큐 테이블(과 key 보존 테이블)이 없으면 만든다(자기 트랜잭션). 있으면 열기와 같다. 새 이름이 다른 큐의 내부 이름과
 * 겹치면 ACKQ_INVALID이고 아무것도 만들지 않는다(파일 머리말의 이름 규칙).
 */
ackq_status_t ackq_install(session_t *session, const ackq_options_t *options, ackq_t **out);
/* 테이블이 있어야 한다. 없으면 ACKQ_NOT_INSTALLED이고 아무것도 만들지 않는다. */
ackq_status_t ackq_open(session_t *session, const ackq_options_t *options, ackq_t **out);
void ackq_close(ackq_t **queue);

typedef struct {
    uint64_t id;
    uint64_t fence;            /* ack·nack·fail·update에 그대로 넘긴다 */
    uint32_t attempts;         /* 이 claim을 포함한 시도 횟수 */
    uint64_t lease_until_ms;
    uint8_t *payload;          /* ackq_claims_release로 해제. 빈 payload면 NULL */
    size_t payload_size;
} ackq_claim_t;

/*
 * enqueue. idem_key는 NULL이거나 1~ACKQ_IDEM_KEY_MAX바이트의 UTF-8이다. 같은 key와 같은 payload는 새 작업을 만들지
 * 않고 기존 id와 *duplicate=true를 돌려준다(작업이 이미 끝났어도 그대로). payload가 다르면 ACKQ_CONFLICT다.
 * payload는 큐의 max_payload_bytes 이하여야 하며 넘으면 자르지 않고 ACKQ_LIMIT이다. duplicate는 NULL 가능.
 */
ackq_status_t ackq_enqueue(ackq_t *queue, session_t *session, const uint8_t *payload, size_t size,
                           const char *idem_key, uint64_t *id, bool *duplicate);

/*
 * 최대 max_items(1..ACKQ_MAX_CLAIM_BATCH)개를 id 순서로 claim한다. 대상은 ready_at이 지난 READY와 lease가 만료된
 * LEASED(시도 횟수가 max_attempts에 닿은 것은 제외, ackq_recover가 FAILED로 끝낸다)다. 각 작업의 fence가 1 오르고
 * 시도 횟수가 늘며 lease는 now + lease_ms다. 가져올 것이 없으면 *count가 0이고 ACKQ_OK다. items 배열은 호출자가
 * max_items 크기로 준비하며 반환된 *count개를 ackq_claims_release로 해제한다. 실패하면 *count는 0이다.
 */
ackq_status_t ackq_claim(ackq_t *queue, session_t *session, size_t max_items, uint64_t lease_ms,
                         ackq_claim_t *items, size_t *count);
void ackq_claims_release(ackq_claim_t *items, size_t count);
/*
 * 특정 id 하나를 claim한다(persist-queue의 get(id=...)에 대응). claim할 수 있는 상태(때가 된 READY 또는 만료된
 * LEASED)가 아니면 ACKQ_STALE, id가 없으면 ACKQ_NOT_FOUND다. 성공하면 item을 ackq_claims_release(item, 1)로 해제한다.
 */
ackq_status_t ackq_claim_id(ackq_t *queue, session_t *session, uint64_t id, uint64_t lease_ms, ackq_claim_t *item);

/*
 * claim이 지금도 유효할 때만 적용한다(state=LEASED, fence 일치). 같은 claim의 중복 ack·fail은 이미 적용됐으므로
 * ACKQ_OK다(응답 소실 뒤 재시도). 다른 fence나 상태면 ACKQ_STALE, id가 없으면 ACKQ_NOT_FOUND다. recover·release가
 * 돌려놓은 작업은 fence가 달라져 STALE이고, purge가 지운 작업은 중복 요청이어도 NOT_FOUND다.
 */
ackq_status_t ackq_ack(ackq_t *queue, session_t *session, uint64_t id, uint64_t fence);
/*
 * 같은 claim의 lease를 now + lease_ms로 바꾼다(#195). fence는 그대로다. 오래 걸리는 작업을 짧은 lease로 처리하며 주기적으로
 * 부른다. lease가 만료됐어도 아직 아무도 다시 claim하지 않았다면 그 claim은 현재이므로 연장된다. 상태가 LEASED가 아니거나
 * fence가 다르면 ACKQ_STALE이고 아무것도 바꾸지 않는다. 성공하면 새 만료 시각을 *lease_until(NULL 가능)에 준다.
 */
ackq_status_t ackq_extend(ackq_t *queue, session_t *session, uint64_t id, uint64_t fence, uint64_t lease_ms,
                          uint64_t *lease_until);
/* reason은 NULL 가능, ACKQ_NOTE_MAX바이트 이하의 UTF-8이며 넘으면 ACKQ_LIMIT이다. */
ackq_status_t ackq_fail(ackq_t *queue, session_t *session, uint64_t id, uint64_t fence, const char *reason);
/*
 * 다시 claim할 수 있게 READY로 돌린다. retry_delay_ms 뒤부터 claim할 수 있다. 시도 횟수가 max_attempts에 닿았으면
 * 재시도하지 않고 FAILED로 끝내며 *terminal(NULL 가능)이 true가 된다. 같은 claim의 중복 nack은 ACKQ_OK이고 처음 적용한
 * 지연과 이유를 바꾸지 않는다. recover·release가 먼저 돌려놓았다면 ack와 같이 ACKQ_STALE다.
 */
ackq_status_t ackq_nack(ackq_t *queue, session_t *session, uint64_t id, uint64_t fence, uint64_t retry_delay_ms,
                        const char *reason, bool *terminal);
/*
 * payload를 바꾼다. fence가 0이면 READY 작업만(claim 전 수정), 0이 아니면 그 claim이 유효한 LEASED 작업만 바꾼다.
 * 끝난 작업(ACKED·FAILED)은 ACKQ_STALE다.
 */
ackq_status_t ackq_update(ackq_t *queue, session_t *session, uint64_t id, uint64_t fence, const uint8_t *payload,
                          size_t size);

/*
 * lease가 만료된 작업을 READY로(시도 횟수가 max_attempts에 닿았으면 FAILED로) 돌린다. 한 번에 최대 limit개를 처리하고
 * 처리한 수를 *recovered에 준다. claim도 만료된 lease를 직접 가져가므로 호출은 선택이다(통계와 FAILED 정리용).
 * 돌려놓은 작업의 fence를 1 올려 옛 claim을 끝낸다. 살아 있는(만료 전) lease는 건드리지 않으므로 여러 프로세스가
 * 같은 큐를 쓰는 중에 불러도 된다.
 */
ackq_status_t ackq_recover(ackq_t *queue, session_t *session, size_t limit, size_t *recovered);
/*
 * 모든 LEASED를 만료와 무관하게 READY로 돌린다(worker가 모두 죽은 것을 호출자가 안다는 약속). 최대 limit개.
 * 살아 있는 worker가 있으면 그 claim을 끝내 버려 ack가 STALE이 되고 작업이 중복 전달된다. 해제는 작업의 실패가 아니므로
 * 시도 횟수가 max_attempts와 같은 작업은 FAILED로 끝내지 않고 READY로 돌린다. 그 작업은 한 번 더 전달되고(시도 횟수가
 * max_attempts + 1이 된다) 그 뒤의 nack·만료·release에서 FAILED가 된다. 그래서 crash 뒤 release를 반복해도 한 작업의
 * 전달은 max_attempts + 1번을 넘지 않는다. recover와 같이 fence를 1 올린다.
 */
ackq_status_t ackq_release_leases(ackq_t *queue, session_t *session, size_t limit, size_t *released);
/*
 * ACKED 작업을 삭제한다(include_failed면 FAILED도). id가 작은 것부터 최대 limit(1..ACKQ_MAX_CLAIM_BATCH)개를 지우고
 * 지운 수를 준다. idempotency key가 있는 작업은 보존 기간 동안 key 보존 테이블에 남기고, 만료 시각이 지난 key는 넣은
 * 순서와 무관하게 최대 limit개를 함께 지운다(만료된 key가 limit개보다 적으면 key 테이블 전체를 읽는다).
 */
ackq_status_t ackq_purge(ackq_t *queue, session_t *session, bool include_failed, size_t limit, size_t *purged);

typedef struct {
    uint64_t ready;
    uint64_t leased;
    uint64_t acked;
    uint64_t failed;
} ackq_stats_t;

/* 상태별 개수. 메타데이터만 읽고 payload를 복사하지 않는다. */
ackq_status_t ackq_stats(ackq_t *queue, session_t *session, ackq_stats_t *stats);

typedef struct {
    uint64_t id;
    ackq_state_t state;
    uint64_t fence;
    uint32_t attempts;
    uint64_t ready_at_ms;
    uint64_t lease_until_ms;
    uint64_t created_at_ms;        /* enqueue 시각 */
    char note[ACKQ_NOTE_MAX + 1];  /* 마지막 nack·fail 이유 */
    uint8_t *payload;              /* with_payload일 때만. ackq_list_release로 해제 */
    size_t payload_size;
} ackq_item_t;

/*
 * after_id보다 큰 id를 오름차순으로 최대 limit개 읽는 bounded cursor. 다음 호출은 마지막 id를 after_id로 준다.
 * 메타데이터는 한 번에 한 행씩 읽어 가장 작은 limit개만 보관하고 payload는 with_payload일 때 반환할 항목의 것만 읽는다.
 */
ackq_status_t ackq_list(ackq_t *queue, session_t *session, uint64_t after_id, size_t limit, bool with_payload,
                        ackq_item_t *items, size_t *count);
void ackq_list_release(ackq_item_t *items, size_t count);

const char *ackq_status_name(ackq_status_t status);

#endif
