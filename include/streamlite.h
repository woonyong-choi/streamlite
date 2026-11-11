#ifndef STREAMLITE_H
#define STREAMLITE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Python 같은 다른 언어에서 부르는 평탄한(flat) C 인터페이스(#140). 구조체 포인터를 값으로 넘기지 않고 핸들과 바이트
 * 버퍼만 쓴다. ack_queue.h의 큐와 database를 감싸며 database·storage 코어는 이 파일을 모른다. 이 파일은 공유
 * 라이브러리 libstreamlite에만 들어가고 DB 단독 사용 빌드(DATABASE_OBJS)에는 들어가지 않는다.
 *
 * 스레드: 모든 호출은 streamlite_db_t의 mutex로 직렬화된다. 이 라이브러리는 mutex를 쥔 채 다른 writer를 기다리지 않는다.
 * 트랜잭션 시작과 큐의 자기 트랜잭션은 writer를 얻지 못하면 STREAMLITE_BUSY다. busy_timeout_ms(streamlite_open_ex)가 0이 아니면
 * 라이브러리가 mutex 밖에서 그 시간까지 다시 시도하고, 0이면 호출자가 다시 시도한다.
 * 살아 있는 db·큐·트랜잭션 핸들을 쓰는 호출(streamlite_txn_failed 포함)은 여러 스레드에서 동시에 불러도 된다.
 *
 * 핸들 수명: 핸들을 끝내는 호출(streamlite_close, streamlite_queue_close, streamlite_commit, streamlite_rollback)은 그
 * 핸들을 쓰는 다른 호출과 동시에 부르면 안 된다. 끝내는 호출이 시작된 뒤에는 그 핸들(포인터 사본 포함)을 쓰지 않으며,
 * 다른 스레드가 그 핸들로 호출 중이거나 mutex를 기다리는 중이 아님을 호출자가 보장한다. 큐·트랜잭션 핸들은 자기 db가
 * 닫히기 전에 끝낸다(살아 있으면 streamlite_close가 STREAMLITE_BUSY다). 이를 어기면 해제된 메모리를 읽는 것이다.
 * 방어로, 핸들마다 표시를 두고 끝내면서 지운다. 표시가 맞지 않는 포인터(이미 끝낸 핸들, 핸들이 아닌 메모리)는 잠그거나
 * 실행하기 전에 STREAMLITE_INVALID(streamlite_close는 STREAMLITE_DB_INVALID, 반환값이 없는 호출은 아무것도 하지 않음)로
 * 거절한다. 해제된 메모리가 다시 쓰이기 전까지만 잡히므로 보장이 아니며 위 규칙을 대신하지 않는다.
 *
 * 세션: 문장과 큐 연산은 session.h의 세션으로 실행한다. 트랜잭션 밖의 호출은 연결 하나에 하나인 자동 commit 세션을,
 * 트랜잭션 안의 호출은 트랜잭션마다 따로 연 세션을 쓴다. 연결의 세션 풀은 한도가 없다.
 *
 * 여러 프로세스: 같은 파일을 여러 프로세스가 각자 streamlite_open으로 연다. 쓰기 트랜잭션은 파일 전체에서 한 번에 하나이고,
 * 다른 프로세스가 쓰는 동안 변경 문장·큐 연산·streamlite_begin은 STREAMLITE_BUSY(문장은 BUSY 오류 결과)다. 기본은 기다리지
 * 않으며 streamlite_open_ex의 busy_timeout_ms로 다시 시도할 시간을 준다. SELECT는 기다리지 않고 마지막 게시 상태를 읽는다.
 * 게시하는 순간(commit의 제어 쓰기와 동기화)만 비켜 준다. 핸들은 연
 * 프로세스에서만 쓴다. fork한 자식에서는 streamlite_close를 포함한 모든 호출이 STREAMLITE_DB_FORKED이며 아무것도 하지 않는다.
 *
 * 반환값: 아래 STREAMLITE_* 값이다. 1~16은 큐 모듈의 상태와 같은 번호다.
 */
#define STREAMLITE_OK 0
#define STREAMLITE_INVALID 1        /* 인자 오류: 이름·길이·개수·시간, 다른 database의 트랜잭션, 끝낸 핸들, 내부 이름과 겹치는 큐 이름 */
#define STREAMLITE_NOMEM 2
#define STREAMLITE_LIMIT 3          /* payload·결과 크기 한도 */
#define STREAMLITE_BUSY 4           /* 다른 writer(다른 프로세스 포함)가 쓰는 중이다. 반영한 것이 없으며 다시 시도할 수 있다 */
#define STREAMLITE_NOT_INSTALLED 5  /* 큐가 설치되지 않았다 */
#define STREAMLITE_NOT_FOUND 6      /* id가 없다 */
#define STREAMLITE_STALE 7          /* fence가 현재 claim이 아니거나 상태가 맞지 않는다. 아무것도 바꾸지 않았다 */
#define STREAMLITE_CONFLICT 8       /* 같은 idempotency key에 payload가 다르다 */
#define STREAMLITE_UNSUPPORTED 9
#define STREAMLITE_IO 10            /* 저장소 오류. commit 전이라면 상태는 그대로다 */
#define STREAMLITE_NOT_APPLIED 11   /* commit이 확정적으로 적용되지 않았다 */
#define STREAMLITE_UNCERTAIN 12     /* commit 결과를 알 수 없다. 성공으로 취급하면 안 된다. 다시 열어 확인한다 */
#define STREAMLITE_POISONED 13      /* 불확실한 commit 뒤 이 핸들로는 더 쓰지 못한다. 닫고 다시 연다 */
#define STREAMLITE_TXN_FAILED 14    /* 트랜잭션이 이미 실패 상태다. rollback 뒤 다시 한다 */
#define STREAMLITE_INTERNAL 15
#define STREAMLITE_CANCELLED 16     /* streamlite_cancel로 취소됐다 */
#define STREAMLITE_EMPTY 50         /* claim할 작업이 없다 */
#define STREAMLITE_DB_INVALID 101   /* database 열기 인자 오류 */
#define STREAMLITE_DB_IO 102
#define STREAMLITE_DB_CORRUPT 103
#define STREAMLITE_DB_LOCKED 104
#define STREAMLITE_DB_UNSUPPORTED 105
#define STREAMLITE_DB_NOMEM 106
#define STREAMLITE_DB_NOT_FOUND 107 /* create가 0인데 파일이 없다 */
#define STREAMLITE_DB_FORKED 108    /* fork한 자식이 부모가 연 핸들을 썼다. 아무것도 하지 않았다. 자식은 streamlite_open으로 새로 연다 */

typedef struct streamlite_db streamlite_db_t;
typedef struct streamlite_queue streamlite_queue_t;
typedef struct streamlite_txn streamlite_txn_t;
typedef uint64_t (*streamlite_clock_fn)(void *ctx);

typedef enum { STREAMLITE_PARAM_NULL = 0, STREAMLITE_PARAM_INTEGER = 1, STREAMLITE_PARAM_REAL = 2, STREAMLITE_PARAM_TEXT = 3, STREAMLITE_PARAM_BLOB = 4 } streamlite_param_type_t;

/* SQL 매개변수 하나. TEXT·BLOB은 bytes와 size를 쓴다. */
typedef struct {
    int32_t type;
    int64_t integer;
    double real;
    const uint8_t *bytes;
    size_t size;
} streamlite_param_t;

/* 이 헤더의 버전(유의적 버전). 1.0 전에는 파일 형식과 API가 바뀔 수 있다 */
#define STREAMLITE_VERSION "0.3.0"

/* 링크한 라이브러리의 버전. 헤더의 STREAMLITE_VERSION과 다르면 다른 빌드를 섞어 쓴 것이다 */
const char *streamlite_version(void);
const char *streamlite_status_name(int status);
void streamlite_free(void *pointer);

/*
 * streamlite_exec가 만드는 인코딩 버퍼의 상한(바이트). 기본은 STREAMLITE_DEFAULT_MAX_RESULT_BYTES다. 넘는 결과는 부분 행을 주지 않고
 * STREAMLITE_LIMIT으로 거절하며, 메모리 할당 실패는 STREAMLITE_NOMEM이다. TEXT·BLOB 셀과 열 이름은 u32 길이로 인코딩하므로
 * UINT32_MAX를 넘는 길이도 STREAMLITE_LIMIT이다. 이 값은 프로세스 전역이며 결과를 만들기 전에 정한다.
 */
#define STREAMLITE_DEFAULT_MAX_RESULT_BYTES ((size_t)64 << 20)
void streamlite_set_max_result_bytes(size_t limit);
size_t streamlite_max_result_bytes(void);

/*
 * commit 동기화 단계. 어느 단계든 COW 게시 순서(새 데이터 페이지 → 제어 페이지)는 같아서 프로세스가 죽은 뒤에는 commit 전부
 * 또는 전무로 다시 열린다. 단계는 OS 장애·전원 차단에 대한 보장만 바꾼다.
 *   NORMAL(기본): commit마다 fsync(2). 성공을 돌려준 commit은 OS 장애 뒤에도 남는다. Apple 플랫폼의 fsync(2)는 장치의 쓰기
 *                 캐시를 비우지 않으므로 그 플랫폼에서 전원이 끊기면 최근 commit을 잃을 수 있다.
 *   FULL:         commit마다 Apple 플랫폼은 fcntl(F_FULLFSYNC)(지원하지 않는 파일 시스템은 fsync), 그 밖은 fsync(2).
 *                 장치가 요청을 지키면 전원 차단 뒤에도 남는다. commit 한 번에 수 ms가 더 든다.
 *   OFF:          commit에서 동기화하지 않고 정상 streamlite_close에서 한 번 동기화한다. 프로세스 종료에는 NORMAL과 같이
 *                 안전하다. OS 장애·전원 차단 뒤에는 최근 commit을 잃을 수 있고 파일이 손상으로 열릴 수도 있다.
 */
#define STREAMLITE_DURABILITY_NORMAL 0
#define STREAMLITE_DURABILITY_FULL 1
#define STREAMLITE_DURABILITY_OFF 2

/* 열기 옵션. 모두 0이면 streamlite_open과 같다. */
typedef struct {
    uint32_t durability;         /* STREAMLITE_DURABILITY_*. 그 밖의 값은 STREAMLITE_DB_INVALID */
    /*
     * 다른 writer(다른 프로세스·같은 db의 다른 트랜잭션)가 쓰는 동안 streamlite_begin, txn 없이 부른 큐 변경 연산, 자동 commit
     * 변경 문장이 STREAMLITE_BUSY 대신 다시 시도하는 시간의 상한(밀리초). 0이면 기다리지 않는다. 기다리는 동안 db의 mutex를
     * 쥐지 않는다(잠깐씩 쉬며 다시 시도한다). 이 db에 열린 트랜잭션이 있으면 그 트랜잭션이 끝나야 풀리므로 그 트랜잭션을 연
     * 스레드가 이 시간 동안 같은 db의 다른 쓰기를 부르면 그만큼 기다린 뒤 BUSY다.
     */
    uint32_t busy_timeout_ms;
} streamlite_open_options_t;

/* path의 DB를 연다. create가 0이 아니면 없을 때 만든다. 동기화 단계는 NORMAL, busy_timeout_ms는 0이다. */
int streamlite_open(const char *path, int create, streamlite_db_t **out);
/* 옵션을 주는 열기. options가 NULL이면 streamlite_open과 같다. 새로 만드는 파일의 생성 자체는 항상 동기화한다. */
int streamlite_open_ex(const char *path, int create, const streamlite_open_options_t *options, streamlite_db_t **out);
/*
 * 살아 있는 큐 핸들(streamlite_queue_t)이나 트랜잭션(streamlite_txn_t)이 있으면 STREAMLITE_BUSY이고 핸들은 그대로다. 먼저 닫는다.
 * 이 db를 쓰는 다른 호출이 모두 돌아온 뒤에 부른다(핸들 수명).
 */
int streamlite_close(streamlite_db_t **db);
/*
 * 지금 실행 중인 문장이나 큐 연산 하나를 취소한다. mutex를 쥐지 않으므로 다른 스레드에서 부른다. 취소된 문장은
 * RESULT_E_CANCELLED 오류 결과(큐 연산은 STREAMLITE_CANCELLED)이며 게시를 시작한 문장은 실제 결과로 끝난다. 끝나는 순간과
 * 겹친 요청은 같은 세션의 다음 문장 하나에 적용될 수 있다(session_cancel 계약).
 */
void streamlite_cancel(streamlite_db_t *db);

/*
 * SQL 문장 하나를 실행한다. txn이 NULL이 아니면 그 트랜잭션 안에서 실행한다. 성공하면 *out에 결과를 인코딩한 버퍼를
 * 주고(streamlite_free로 해제) 문장 수준 오류도 인코딩된 오류 결과로 돌아온다. 인코딩:
 *   u8 kind(0 행, 1 명령, 2 오류)
 *   행:   u32 열 수, (u32 이름 길이, 이름, u8 형식, u8 NULL 가능)*열 수, 행마다 [u8 1, 셀*열 수], 끝에 u8 0
 *         형식은 셀 type과 같은 번호이며 0은 식의 형식을 정하지 못했다는 뜻이다(예: SELECT NULL)
 *   명령: u32 tag 길이, tag, u64 영향 행 수
 *   오류: u32 분류, i32 코드, u32 메시지 길이, 메시지
 *   셀:   u8 type(0 NULL,1 INTEGER,2 REAL,3 TEXT,4 BLOB), INTEGER는 i64, REAL은 f64, TEXT·BLOB은 u32 길이와 바이트
 * 모든 정수는 little-endian이다. txn은 이 db의 것이어야 하며 아니면 STREAMLITE_INVALID다(문장을 실행하기 전에 거절한다).
 * 결과를 읽고 해제하는 동안에도 db의 mutex를 놓지 않는다.
 */
int streamlite_exec(streamlite_db_t *db, streamlite_txn_t *txn, const char *sql, const streamlite_param_t *params, size_t param_count,
               uint8_t **out, size_t *out_size);

/* 쓰기 트랜잭션 시작. 다른 writer가 있으면 busy_timeout_ms(기본 0)까지 다시 시도한 뒤 STREAMLITE_BUSY다. */
int streamlite_begin(streamlite_db_t *db, streamlite_txn_t **out);
/* commit·rollback은 성공하든 실패하든 트랜잭션을 해제하고 *txn을 NULL로 만든다. */
int streamlite_commit(streamlite_txn_t **txn);
int streamlite_rollback(streamlite_txn_t **txn);
/* 문장 오류로 실패 상태인지(commit하면 안 되고 rollback만 된다). 다른 호출처럼 mutex 안에서 읽는다. 끝낸 핸들이면 0 */
int streamlite_txn_failed(const streamlite_txn_t *txn);

/* 큐 옵션. 모두 0이면 기본값이다(ack_queue.h의 ackq_options_t와 같은 뜻). */
typedef struct {
    uint32_t max_attempts;        /* 0이면 제한 없음 */
    size_t max_payload_bytes;     /* 0이면 1 MiB */
    uint64_t idem_retention_ms;   /* 0이면 7일, UINT64_MAX면 purge 뒤 key를 남기지 않음 */
    streamlite_clock_fn clock;          /* NULL이면 실시간(밀리초) */
    void *clock_ctx;
} streamlite_queue_options_t;

/*
 * 큐 이름 name을 연다. install이 0이 아니면 없을 때 만든다. options가 NULL이면 모두 기본값이다. 새로 만드는 이름이
 * _keys·_state·_idem·_due·_lease로 끝나거나 이미 있는 큐의 내부 테이블·색인 이름과 겹치면 STREAMLITE_INVALID이고 아무것도 만들지
 * 않는다(ack_queue.h의 이름 규칙). 이미 있는 큐는 이름과 무관하게 열린다.
 */
int streamlite_queue_open(streamlite_db_t *db, const char *name, int install, const streamlite_queue_options_t *options,
                    streamlite_queue_t **out);
void streamlite_queue_close(streamlite_queue_t **queue);

typedef struct {
    uint64_t id;
    uint64_t fence;
    uint32_t attempts;
    uint64_t lease_until_ms;
    uint8_t *payload;   /* streamlite_queue_item_free로 해제 */
    size_t payload_size;
} streamlite_queue_item_t;

int streamlite_queue_put(streamlite_queue_t *queue, streamlite_txn_t *txn, const uint8_t *payload, size_t size, const char *idem_key, uint64_t *id,
             int *duplicate);
/*
 * id가 0이면 다음 작업, 아니면 그 작업을 claim한다. 가져올 것이 없으면 STREAMLITE_EMPTY(id 지정이 불가하면 STREAMLITE_STALE).
 * 다음 작업의 순서와, 빈 결과 다음 호출이 writer 없이 먼저 확인하는 동작은 ack_queue.h의 ackq_claim과 같다.
 */
int streamlite_queue_get(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t lease_ms, streamlite_queue_item_t *item);
void streamlite_queue_item_free(streamlite_queue_item_t *item);
int streamlite_queue_ack(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t fence);
/* 같은 claim의 lease를 지금 + lease_ms로 늘린다. fence는 그대로다. 성공하면 새 만료 시각을 *lease_until에 준다(NULL 가능). */
int streamlite_queue_extend(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t fence, uint64_t lease_ms,
                      uint64_t *lease_until);
int streamlite_queue_fail(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t fence, const char *reason);
int streamlite_queue_nack(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t fence, uint64_t retry_delay_ms, const char *reason,
              int *terminal);
int streamlite_queue_update(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t id, uint64_t fence, const uint8_t *payload, size_t size);
int streamlite_queue_recover(streamlite_queue_t *queue, streamlite_txn_t *txn, size_t limit, size_t *recovered);
int streamlite_queue_release_leases(streamlite_queue_t *queue, streamlite_txn_t *txn, size_t limit, size_t *released);
int streamlite_queue_purge(streamlite_queue_t *queue, streamlite_txn_t *txn, int include_failed, size_t limit, size_t *purged);
/* counts: ready, leased, acked, failed 순서의 u64 네 개 */
int streamlite_queue_stats(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t counts[4]);

typedef struct {
    uint64_t id;
    int32_t state;
    uint64_t fence;
    uint32_t attempts;
    uint64_t ready_at_ms;
    uint64_t lease_until_ms;
    uint64_t created_at_ms;
    char note[201];
    uint8_t *payload;   /* with_payload일 때만. streamlite_queue_list_free로 해제 */
    size_t payload_size;
} streamlite_queue_list_item_t;

/* items는 limit개 이상의 배열이다. 채운 수를 *count에 준다. 해제는 streamlite_queue_list_free(items, *count). */
int streamlite_queue_list(streamlite_queue_t *queue, streamlite_txn_t *txn, uint64_t after_id, size_t limit, int with_payload, streamlite_queue_list_item_t *items,
              size_t *count);
void streamlite_queue_list_free(streamlite_queue_list_item_t *items, size_t count);

#endif
