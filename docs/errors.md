# 오류

StreamLite 0.3.0에는 C 호출 상태와 SQL 오류 결과라는 두 오류 층이 있다. 예를 들어 `SELECT 1 / 0`은 `streamlite_exec`가 `STREAMLITE_OK`로 오류 버퍼를 반환하고 Python에서는 `SqlError`가 된다. 반면 C 결과 인코딩 상한 초과는 `STREAMLITE_LIMIT`이며 Python의 `LimitError`다. 버퍼 판독 조건은 [프로토콜](protocol.md)에 있다.

## 오류 코드

### C 호출 상태

상태 번호는 [streamlite.h](../include/streamlite.h), Python 매핑은 [errors.py](../python/streamlite/errors.py)에 있다. C status에는 메시지 payload가 없으며 상태마다 하나의 고정된 메시지도 없다. Python의 `raise_for_status`는 `streamlite_status_name`의 이름으로 `이름 (status 번호)`를 만들지만 일부 호출은 별도 메시지를 쓴다.

| 코드 | C 이름 | Python 매핑 |
|---|---|---|
| `0` | `STREAMLITE_OK` | 없음 |
| `1` | `STREAMLITE_INVALID` | `InvalidArgumentError` |
| `2` | `STREAMLITE_NOMEM` | `StorageError` |
| `3` | `STREAMLITE_LIMIT` | `LimitError` |
| `4` | `STREAMLITE_BUSY` | `BusyError` |
| `5` | `STREAMLITE_NOT_INSTALLED` | `NotInstalledError` |
| `6` | `STREAMLITE_NOT_FOUND` | `NotFoundError` |
| `7` | `STREAMLITE_STALE` | `StaleError` |
| `8` | `STREAMLITE_CONFLICT` | `ConflictError` |
| `9` | `STREAMLITE_UNSUPPORTED` | `UnsupportedError` |
| `10` | `STREAMLITE_IO` | `StorageError` |
| `11` | `STREAMLITE_NOT_APPLIED` | `NotAppliedError` |
| `12` | `STREAMLITE_UNCERTAIN` | `UncertainError` |
| `13` | `STREAMLITE_POISONED` | `PoisonedError` |
| `14` | `STREAMLITE_TXN_FAILED` | `TransactionFailedError` |
| `15` | `STREAMLITE_INTERNAL` | `InternalError` |
| `16` | `STREAMLITE_CANCELLED` | `CancelledError` |
| `50` | `STREAMLITE_EMPTY` | `Queue.get`의 `None` |
| `101` | `STREAMLITE_DB_INVALID` | `DatabaseOpenError` |
| `102` | `STREAMLITE_DB_IO` | `DatabaseOpenError` |
| `103` | `STREAMLITE_DB_CORRUPT` | `DatabaseOpenError` |
| `104` | `STREAMLITE_DB_LOCKED` | `DatabaseOpenError` |
| `105` | `STREAMLITE_DB_UNSUPPORTED` | `DatabaseOpenError` |
| `106` | `STREAMLITE_DB_NOMEM` | `DatabaseOpenError` |
| `107` | `STREAMLITE_DB_NOT_FOUND` | `DatabaseOpenError` |
| `108` | `STREAMLITE_DB_FORKED` | `ForkedHandleError` |

알 수 없는 비0 상태는 Python에서 `InternalError`다. `STREAMLITE_EMPTY`는 `Queue.get`만 `None`으로 처리한다. 일반 `raise_for_status(50, ...)`는 `InternalError`이며 프로세스 종료 코드가 아니다. 미지원 SQL 문법은 일반적으로 encoded SQL 오류이지 C status `9`가 아니다.

#### `0` `STREAMLITE_OK`

호출이 성공했다. SQL은 결과 kind도 검사한다.

#### `1` `STREAMLITE_INVALID`

인자·시간·이름이 잘못됐거나 다른 DB의 트랜잭션이다. 새로 설치하는 큐 이름이 `_keys`·`_state`·`_idem`·`_due`·`_lease`로 끝나거나 이미 있는 큐의 내부 테이블·색인 이름과 겹칠 때도 이 상태이며 아무것도 만들지 않는다. 인자와 핸들 소속을 고친다.

#### `2` `STREAMLITE_NOMEM`

할당이 실패했다. 실행 단계와 반영 상태를 확인한다.

#### `3` `STREAMLITE_LIMIT`

payload나 반환 버퍼의 한도를 넘었다. 크기를 줄이되 쓰기를 맹목 재실행하지 않는다.

#### `4` `STREAMLITE_BUSY`

writer가 있거나 close에 자식 자원이 남았다. 경합이면 재시도하고 close면 자원을 먼저 끝낸다. Python은 `busy_timeout_s`까지 다시 시도하지만, 호출한 스레드가 같은 `Database`에 연 트랜잭션이 writer를 쥐고 있으면 기다리지 않고 바로 원인을 적은 `BusyError`(자동 commit SQL은 BUSY 분류의 `SqlError`)를 던진다. 그 트랜잭션을 `txn=`으로 넘기거나 먼저 끝낸다.

#### `5` `STREAMLITE_NOT_INSTALLED`

큐 테이블이 없다. 올바른 DB·이름과 설치 여부를 확인한다.

#### `6` `STREAMLITE_NOT_FOUND`

지정한 작업 ID가 없다. `purge`가 지운 작업은 fence를 확인할 행이 없으므로 같은 claim의 중복 ACK도 이 상태다. ID와 업무 상태를 확인한다.

#### `7` `STREAMLITE_STALE`

fence 또는 상태가 현재 작업과 맞지 않는다. `recover`·`release_leases`가 돌려놓은 작업은 아직 재획득되지 않았어도 옛 claim의 ack·nack·fail·extend·update가 모두 이 상태다. 옛 claim으로 변경하지 않는다.

#### `8` `STREAMLITE_CONFLICT`

같은 키에 다른 payload다. 기존 작업과 요청 내용을 대조한다.

#### `9` `STREAMLITE_UNSUPPORTED`

하위 트랜잭션 계층이 요청을 지원하지 않는다. 지원 API와 트랜잭션 조건을 확인한다.

#### `10` `STREAMLITE_IO`

저장소 연산이 실패했다. 트랜잭션과 핸들 상태를 확인한다.

#### `11` `STREAMLITE_NOT_APPLIED`

commit이 확정적으로 미반영이다. poison 가능성도 확인하고 새 트랜잭션 여부를 정한다.

#### `12` `STREAMLITE_UNCERTAIN`

commit 반영 여부를 알 수 없다. 닫고 재개방하여 업무 상태를 확인한다.

#### `13` `STREAMLITE_POISONED`

이 핸들로 작업을 계속할 수 없다. 자원을 정리하고 닫아 재개방한다.

#### `14` `STREAMLITE_TXN_FAILED`

호출자 트랜잭션이 실패 상태다. rollback한 뒤 새 트랜잭션을 시작한다.

#### `15` `STREAMLITE_INTERNAL`

내부 응답이 예상과 다르다. 입력과 재현 조건을 보존한다.

#### `16` `STREAMLITE_CANCELLED`

큐 연산이 취소됐다. 호출자 트랜잭션이라면 전체 rollback한다.

#### `50` `STREAMLITE_EMPTY`

다음 claim 대상이 없다. 정상 부재로 처리한다.

#### `101` `STREAMLITE_DB_INVALID`

열기 인자가 잘못됐다. 경로와 인자를 확인한다.

core의 `DATABASE_LOST_COMMIT`도 현재 이 값으로 나온다(대응하는 공개 상태가 아직 없다). 성공을 돌려준 최신 commit이 손상돼 쓰기 열기를 거절했다는 뜻이며 파일은 바뀌지 않았다. 파일과 `<db>-lock`을 보존하고 원인을 조사한다. 판정 조건과 한계는 [안전성](safety.md#이전-세대로-물러난-열기)에 있다.

#### `102` `STREAMLITE_DB_IO`

파일 열기·읽기·생성이 실패했다. 파일 권한과 저장소 상태를 확인한다.

#### `103` `STREAMLITE_DB_CORRUPT`

열기 중 손상이나 잘림을 발견했다. 파일을 보존하고 원인을 조사한다.

최신 세대만 읽을 수 없고 이전 세대가 온전하면 이 상태가 아니다. 그때는 손실이 확정되지 않으면 이전 세대로 열리고, 확정되면 위 `101`의 `DATABASE_LOST_COMMIT` 경로로 거절된다.

#### `104` `STREAMLITE_DB_LOCKED`

열기가 게시 상태를 읽을 공유 잠금을 5초(`PAGE_FILE_LOCK_TIMEOUT_MS`) 안에 얻지 못했다. 다른 핸들의 게시(commit의 제어 쓰기와 동기화)가 그보다 오래 걸릴 때만 생기며 다시 열면 된다. 같은 경로를 동시에 만드는 생성(`create`가 참이고 파일이 없을 때)도 이 상태일 수 있다.

0.3.0부터(#283) 쓰기 가능한 열기도 writer 잠금이 아니라 읽기와 같은 공유 잠금으로 게시 상태를 읽는다. 그래서 다른 핸들·프로세스가 쓰기 거래를 쥐고 있어도 열기는 기다리지 않고 성공하며, 그 거래 때문에 이 상태가 되지 않는다. 열기가 하는 일(두 제어 페이지 중 현재 세대 고르기, 끝나지 않은 commit이면 이전 세대로 물러나기, 승인한 commit의 손실 판정과 쓰기 열기 거절, 빈 페이지 목록 적재, 카탈로그 적재)은 모두 게시 상태를 읽기만 한다. 공유 잠금을 쥔 동안에는 어떤 writer도 게시하지 못하고 손실 판정이 읽는 잠금 파일의 내구 확인 기록도 게시 잠금 안에서만 바뀌므로 writer 잠금 아래에서 연 것과 판정이 같다. writer 잠금은 첫 쓰기(`begin`, 변경 문장, 큐 변경 연산)가 잡고, 그때 디스크와 다시 맞추며 손실 판정도 다시 한다. 다른 writer가 쓰는 중이면 `begin`과 큐 변경 연산은 `STREAMLITE_BUSY`(Python `BusyError`)이고, 거래 없이 실행한 변경 문장(C `streamlite_exec`, Python `Database.execute`)은 BUSY 분류의 오류 결과(아래 `7, 29`, Python은 `error_class` 7·`error_code` 29의 `SqlError`)다. 어느 쪽이든 `busy_timeout_ms`·`busy_timeout_s`만큼 다시 시도한 뒤 돌려준다. 열기 대기를 열기 옵션의 busy timeout으로 받고 시간 초과를 BUSY로 돌려주는 방법은 고르지 않았다. 기다릴 까닭 자체가 없는데 소비자 재시작을 다른 writer의 거래 길이만큼 늦추기 때문이다.

#### `105` `STREAMLITE_DB_UNSUPPORTED`

다른 파일 형식 또는 미지원 형식이다. 맞는 파일과 버전을 사용한다.

#### `106` `STREAMLITE_DB_NOMEM`

열기나 세션 생성 할당이 실패했다. 메모리와 재현 조건을 확인한다.

#### `107` `STREAMLITE_DB_NOT_FOUND`

`create=0`인데 파일이 없다. 경로 또는 생성 의도를 확인한다.

#### `108` `STREAMLITE_DB_FORKED`

자식 프로세스가 부모 핸들을 사용했다. 자식에서 독립적으로 연다.

### SQL 오류 분류

`kind=2` 결과의 `error_class: u32`, `error_code: i32`, message는 Python의 `SqlError.error_class`, `.error_code`, 예외 메시지로 옮긴다. `SqlError.status`의 기본값 `-1`은 C 호출 상태가 아니다.

| 코드 | 이름 |
|---|---|
| `0` | `NONE`, 오류 결과의 정상 분류 아님 |
| `1` | `SYNTAX` |
| `2` | `SEMANTIC` |
| `3` | `CONSTRAINT` |
| `4` | `LIMIT` |
| `5` | `NOMEM` |
| `6` | `IO` |
| `7` | `BUSY` |
| `8` | `CANCELLED` |
| `9` | `INTERNAL` |

상세 코드는 전역 고유 번호가 아니다. 같은 분류 안에서도 lexer, parser, 값 변환과 저장 계층의 번호가 겹치므로 아래 표는 발생 문맥을 포함한다. 표는 대표 공개 실행 경로를 찾기 위한 것이며 모든 내부 reason을 하나의 enum으로 합친 목록이 아니다.

메시지의 `%u`, `%.*s`, `%zu`는 소스의 형식 문자열이며 실제 메시지에는 개수·이름·위치가 들어간다. 예를 들어 코드 `30`은 분류 `7`에서 파일 잠금 경합이지만 분류 `4`의 트랜잭션 시작 경로에서는 트랜잭션 번호 한도다. Lexer의 reason도 parser의 reason과 다른 번호 집합이다. 각 경로의 정의는 [lexer.h](../include/sql/lexer.h)와 [sql_statement.h](../include/sql/sql_statement.h)에 있다.

메시지는 내부에서 최대 1024바이트로 잘릴 수 있고 C 결과 버퍼에는 잘림 표시가 없다. 메시지 전체 일치만으로 오류를 분류하지 않는다. 문맥별 생성 지점은 [database.c](../src/sql/database.c), [database_txn.c](../src/sql/database_txn.c), [database_insert.c](../src/sql/database_insert.c)에 있다.

| 분류·코드 | 발생 문맥 |
|---|---|
| `1, 5` | 바인딩 개수 검사 |
| `1, 20` | 문장 개수 검사 |
| `2, 1` | 테이블 조회 |
| `2, 2` | 열 조회 |
| `2, 3` | parser의 잘못된 바인딩 값 |
| `2, 26` | 실패한 트랜잭션의 후속 문장 |
| `2, 40..53` | 식·집계·집합·서브쿼리·충돌 절 검사 |
| `3, 8` | INSERT의 NULL 검사 |
| `3, 11` | 기본 키·유일 색인 검사 |
| `3, 53` | ON CONFLICT 갱신 대상 검사 |
| `4, 25` | SQL 입력 길이 검사 |
| `4, 54` | 저장할 행의 TEXT·BLOB 합 검사 |
| `5, 24` | 메모리 예산 검사 |
| `6, 19` | poisoned 핸들의 SQL |
| `6, 32` | 자동 commit 또는 SQL `COMMIT` |
| `6, 33` | 자동 commit 또는 SQL `COMMIT` |
| `7, 29` | 자동 실행의 읽기 전용 경로에서 변경 거절 또는 SQL `BEGIN` 경합 |
| `7, 30` | 파일 잠금을 얻는 문장 진입 경로 |
| `8, 23` | SQL 취소 |

#### `1, 5` 바인딩 개수 검사

메시지: `매개변수 개수가 맞지 않습니다 (SQL %u개)`

원인: placeholder 수와 인자 수가 다르다.

대응: 바인딩을 맞춘다.

#### `1, 20` 문장 개수 검사

메시지: `한 번에 문장 하나만 실행할 수 있습니다`

원인: 문장이 하나가 아니다.

대응: 호출을 나눈다.

#### `2, 1` 테이블 조회

메시지: `테이블이 없습니다: %.*s`

원인: 지정 테이블이 없다.

대응: DB와 테이블 이름을 확인한다.

#### `2, 2` 열 조회

메시지: `컬럼이 없습니다: %.*s`

원인: 지정 열이 없다.

대응: 열 이름을 확인한다.

#### `2, 3` parser의 잘못된 바인딩 값

메시지: `지원하지 않거나 올바르지 않은 문장입니다 (바이트 %zu)`

원인: 사용한 값이 유효한 UTF-8 또는 유한 REAL이 아니다.

대응: 값과 타입을 고친다.

#### `2, 26` 실패한 트랜잭션의 후속 문장

메시지: `이 트랜잭션은 이전 문장 오류로 실패한 상태입니다. rollback해야 합니다`

원인: 앞 문장이 실패했다.

대응: rollback한다.

#### `2, 40..53` 식·집계·집합·서브쿼리·충돌 절 검사

메시지: 없음, 입력 위치와 원인에 따라 다름

원인: 타입·0 나누기·overflow 또는 식 구성이 잘못됐다.

대응: [database.h](../include/sql/database.h)의 해당 `DATABASE_CODE_*`와 SQL을 대조한다.

#### `3, 8` INSERT의 NULL 검사

메시지: `NOT NULL 제약을 어겼습니다: %.*s`

원인: 필수 열이 NULL이다. UPDATE가 기본 키를 NULL로 바꾸려는 경우도 같다(AUTOINCREMENT 기본 키 포함. INSERT의 NULL은 자동 번호를 받는다).

대응: 열 값을 채운다.

#### `3, 11` 기본 키·유일 색인 검사

메시지: 기본 키 중복은 `기본 키가 중복됩니다: %.*s` 또는 `기본 키가 중복됩니다 (%s)`, 유일 색인 위반은 `유일 색인의 값이 중복됩니다 (%s)`, 색인을 만들 때 기존 행의 중복은 `기존 행에 중복된 인덱스 값이 있습니다`

원인: 기본 키나 유일 색인 열의 값이 다른 행과 같다. 같은 코드를 쓰므로 둘을 가를 때는 메시지를 본다.

대응: 기존 행과 키를 확인한다.

#### `3, 53` ON CONFLICT 갱신 대상 검사

메시지: `ON CONFLICT DO UPDATE는 한 문장에서 같은 행을 두 번 바꿀 수 없습니다`

원인: 같은 행을 한 문장에서 두 번 갱신한다.

대응: 입력의 충돌 대상을 정리한다.

#### `4, 25` SQL 입력 길이 검사

메시지: `입력이 한도를 넘습니다`

원인: 입력 실효 상한을 넘었다.

대응: SQL 크기를 줄인다.

#### `4, 54` 저장할 행의 TEXT·BLOB 합 검사

메시지: `행의 TEXT·BLOB 값 합이 한도 %zu바이트를 넘습니다`

원인: INSERT·UPDATE·`ON CONFLICT DO UPDATE`가 저장할 한 행의 TEXT·BLOB 바이트 합이 63MiB(`66060288`바이트)를 넘는다. 이 한도를 넘는 행은 저장해도 SELECT 결과 한도 때문에 돌려줄 수 없으므로 문장 전체를 반영하지 않는다([설정](configuration.md#sql-입력과-결과-한도)).

대응: 값을 나눠 여러 행에 저장한다. 큐 payload도 같은 행에 저장되므로 이 한도 안이어야 한다(Python `LimitError`).

#### `5, 24` 메모리 예산 검사

메시지: 없음, 발생 경로에 따라 다름

원인: 메모리 예산이 거절됐다.

대응: 입력과 메모리 사용을 줄인다.

#### `6, 19` poisoned 핸들의 SQL

메시지: 없음, 발생 경로에 따라 다름

원인: 불확정 commit 또는 I/O 실패 뒤의 핸들이다.

대응: 닫고 재개방한다.

#### `6, 32` 자동 commit 또는 SQL `COMMIT`

메시지: `커밋 결과를 알 수 없습니다. 데이터베이스를 닫고 다시 열어 확인하세요`

원인: commit 반영 여부가 불확정이다.

대응: 닫고 재개방하여 업무 상태를 확인한다.

#### `6, 33` 자동 commit 또는 SQL `COMMIT`

메시지: `커밋하지 못했습니다. 아무것도 반영하지 않았습니다`

원인: commit이 확정적으로 미반영이다.

대응: poison 여부와 자원 수명을 확인한다.

#### `7, 29` 자동 실행의 읽기 전용 경로에서 변경 거절 또는 SQL `BEGIN` 경합

메시지: 없음, 발생 경로에 따라 다름

원인: writer를 얻지 못했다.

대응: 새 호출에서 재시도한다.

#### `7, 30` 파일 잠금을 얻는 문장 진입 경로

메시지: `다른 연결이 파일을 쓰는 중입니다`

원인: 다른 핸들이 writer다.

대응: 새 호출에서 재시도한다.

#### `8, 23` SQL 취소

메시지: `문장이 취소되어 아무것도 반영하지 않았습니다`

원인: 취소 요청을 관측했다.

대응: 열린 트랜잭션을 rollback한다.

## 정상 부재

| 경우 | 결과 |
|---|---|
| SELECT 0행 | `STREAMLITE_OK`, rows 결과, Python `rows == ()` |
| SELECT NULL 한 행 | Python `rows == ((None,),)` |
| 다음 claim 대상 없음 | `STREAMLITE_EMPTY`, Python `None` |
| 빈 payload claim 성공 | `STREAMLITE_OK`, Python `Claim(payload=b'')` |
| 지정 ID 없음 | `STREAMLITE_NOT_FOUND`, Python `NotFoundError` |
| 지정 작업이 현재 claim 불가 | `STREAMLITE_STALE`, Python `StaleError` |
| 목록·정리 대상 0건 | 성공한 빈 목록 또는 처리 수 `0` |

할당 실패는 이 정상 부재 중 어느 것으로도 바꾸지 않는다. `persistqueue_compat`의 `Empty`, blocking과 lease 재개 정책은 native `Queue`와 다른 별도 인터페이스다. 호환 계층의 `get(id=...)`은 그 작업이 이미 끝났거나 지워졌으면 block이어도 기다리지 않고 `Empty`이고, 열 때의 `auto_resume`은 만료된 lease만 되돌린다.

## 실패 뒤 상태

문장 성공, commit 성공, 결과 전달 성공은 서로 다르다. 자동 SQL commit의 불확정은 `SqlError(6, 32)`이고, 명시적 `Transaction.commit()`과 큐 자체 트랜잭션 commit의 불확정은 status `12`의 `UncertainError`다. 이후 poisoned 핸들은 SQL에서 `SqlError(6, 19)`, 큐에서는 `PoisonedError`로 나타날 수 있다.

> [!WARNING]
> 결과 인코딩의 `STREAMLITE_LIMIT`과 `STREAMLITE_NOMEM`은 변경이 commit된 뒤에도 발생할 수 있다. 부분 버퍼가 없다는 사실은 변경 미반영을 뜻하지 않는다.

반환 실패 뒤 쓰기를 재실행하기 전에 업무 키로 저장 상태를 조회한다. 불확정 또는 poison이면 자식 자원을 정리하고 DB를 닫은 뒤 새 핸들로 확인한다. Flat API에는 core의 commit ID 조회 기능이 없다. `NOT_APPLIED`는 해당 commit의 미반영을 뜻하지만 핸들이 정상이라는 뜻은 아니다. 게시 전 fsync 실패도 poison을 남길 수 있다. 게시 경계는 [아키텍처](architecture.md), 장애 시험의 범위는 [안전성](safety.md)에 있다.

명시적 트랜잭션 안 문장 오류 뒤에는 rollback한다. 큐의 `STALE`, `NOT_FOUND`, `CONFLICT`는 무변경 거절이며 호출자 트랜잭션을 실패시키지 않는다. 그 밖의 큐 비OK는 일부 SQL이 이미 수행됐을 수 있으므로 호출자 트랜잭션 전체를 rollback한다. 같은 DB의 업무 SQL과 ACK를 묶는 범위는 [트랜잭션 큐 설계](design/transaction-queue.md)에 있다.

C `streamlite_commit`과 `streamlite_rollback`은 일반 성공·실패 모두 트랜잭션 핸들을 해제하고 NULL로 만든다. Python도 호출 시 트랜잭션을 소비한다. Fork 거절은 C 핸들을 소비하지 않는 예외다. 자식은 부모 핸들로 close도 하지 않고 새로 연다. 반환형이 void인 cancel·queue close는 fork 시 no-op이며 모든 함수가 상태 번호를 반환하는 것은 아니다.

C DB close의 BUSY는 살아 있는 큐·트랜잭션을 먼저 끝내야 한다는 뜻이다. Python DB close는 큐부터 닫으므로 열린 트랜잭션 때문에 `BusyError`가 나면 DB는 살아 있어도 큐는 이미 닫혀 있다. 최종 C close가 오류여도 DB 포인터는 이미 해제·NULL일 수 있으므로 옛 포인터를 재사용하지 않는다. 최종 close와 새 호출의 수명 동기화는 호출자가 맡는다. 핸들을 끝내는 호출(DB close, queue close, commit, rollback)은 그 핸들을 쓰는 다른 호출과 겹치면 안 된다. 이미 끝낸 핸들을 다시 넘기면 표시 검사로 `STREAMLITE_INVALID`(DB close는 `STREAMLITE_DB_INVALID`)가 될 수 있지만, 해제된 메모리를 읽는 최선의 방어일 뿐 보장이 아니다.

BUSY 재시도 범위는 [설정](configuration.md#데이터베이스-열기)에 있다. 취소는 협력적 문장 취소이며, 게시 시작 이후에는 실제 결과로 끝난다. 실행 종료와 겹친 취소는 다음 문장 하나에 적용될 수 있다. 반환 버퍼 판독이나 Python decode까지 취소하는 기능은 아니다.
