# StreamLite 리뉴얼 구조

| 항목 | 값 |
|---|---|
| 상태 | 제안 |
| 관련 검토 | [구조 결정 #257](https://github.com/woonyong-choi/totedb/issues/257), [브랜드 전환 #258](https://github.com/woonyong-choi/totedb/issues/258) |
| 브랜드 | StreamLite, 기존 배포·API의 ToteDB 명칭 유지 |

## 요약

StreamLite는 SQL, 영속 이벤트 스트림, ACK 작업 처리를 각각 선택하거나 같은 트랜잭션에서 결합하는 내장 엔진을 목표로 한다. 여러 독립 쓰기 트랜잭션이 실제로 겹쳐 진행하면서 결과·격리·복구·자원 상한을 지키는 것이 다중 writer의 요구다. 요청을 받아 한 writer가 차례로 실행하는 것만으로 이 요구를 충족했다고 표시하지 않는다.

새 엔진의 기본 방향은 page/redo WAL, bytes 상한이 있는 buffer pool, MVCC, 조기 key 잠금으로 권고한다. 작은 상태의 잦은 변경을 durable commit과 본문 page 반영으로 분리하고, 같은 leaf의 다른 행을 쓰는 거래가 매번 옛 root를 병합하지 않게 하기 위해서다. 선택 근거와 반증 조건은 [성능과 안전을 함께 고르는 순서](#성능과-안전을-함께-고르는-순서)에 있다. 이는 현재 요구에서 도출한 설계 권고이며 새 backend의 구현·동등 조건 실측·제품 전환 승인은 아니다.

기본 실행 경로는 SQL 부분집합·단일 writer·선택형 ACK다. 문장 cache와 transaction workspace는 기본 OFF인 내부 비교 경로로 분리하며 성능 채택은 측정 전이다. Python의 큐 등록부는 첫 명시 사용 때 생성하며 수명·실패 회귀를 검증했다. 영속 fanout 스트림·새 backend·실제 동시 writer는 구현 완료가 아니다. StreamLite 표시명을 채택해도 기존 `totedb` 패키지·Python import·`tote_*` C ABI·`libtotedb`·DB 파일 형식은 그대로 둔다.

### 하지 않는 것

- NATS wire protocol 전체 호환: 사용 중인 애플리케이션 EventBus를 구현하는 것과 다른 호환성 과업이다.
- 다중 노드 복제·자동 failover: 로컬 다중 writer와 분산 합의는 별개다.
- 기존 파일 자동 덮어쓰기: 새 저장 형식은 별도 이관·내용 대조·복원 검증이 필요하다.
- 외부 작업의 exactly-once: DB 밖 HTTP·메일·결제에는 별도 멱등 처리가 필요하다.

## 해결하는 문제

로컬 작업의 재시작 보존, 외부 서비스 없는 배포, 업무 상태와 enqueue의 분리 commit, 오래 쌓인 이벤트의 보존·메모리 비용에는 실제 요구가 있다. [Inngest 개발 서버 요청](https://github.com/inngest/inngest/issues/5028)은 외부 DB 없는 개발용 persistence를, [goqite 배포 요구](https://github.com/maragudk/goqite/discussions/53)는 배포 마찰을, [Moneymentum 결함](https://github.com/dataclique/moneymentum/issues/404)은 상태 기록과 enqueue 사이의 실패를 보여 준다. 이 사례만으로 자체 SQL 엔진·모든 기능 결합·유료 고객 수요를 입증하지 않는다. 2026-10-23 확인이다.

SQLite 기반 큐·outbox, DBOS 같은 내구 실행 SDK, 단일 노드 NATS도 대안이다. 제품의 채택 조건은 필요한 로컬 전달 의미를 충족하고, 같은 보장 아래 전체 자원·배포·연동 비용을 줄이며, SQLite 조합 대비 이익을 보여 주는 것이다. 현재 [실측](../experiments/local-db-comparison/report.md)은 속도 우위를 보여 주지 않는다.

## 사용 예

### SQL만 사용하는 애플리케이션

1. 앱은 일반 SQL로 데이터를 저장하고 조회한다.
2. 큐·스트림 객체나 소비자 상태를 생성하지 않는다.
3. 코어의 SQL 실행과 commit 경로에 큐 전용 polling·조회·hook을 추가하지 않는다.

### 여러 로컬 처리자의 독립 소비

1. 생산자는 이벤트 원본을 기록한다.
2. 같은 그룹의 처리자들은 작업을 나눠 처리한다.
3. 다른 소비자 그룹은 같은 이벤트를 자기 진행 위치와 ACK 상태로 독립 처리한다.
4. 한 그룹의 실패나 ACK가 다른 그룹의 전달 완료를 뜻하지 않는다.

### 업무 상태와 이벤트의 같은 commit

1. 같은 DB의 업무 변경·이벤트 기록 또는 로컬 효과·소비 완료를 한 트랜잭션으로 확정한다.
2. 업무 DB를 PostgreSQL에 그대로 두면 두 저장소 사이의 outbox·재전송 경계는 남는다.
3. 브로커 교체만으로 외부 DB와의 원자성이 생겼다고 표시하지 않는다.

## 작동 방식

### SQL과 저장의 공통 비용

SQL template와 실행별 binding을 분리하고 제한된 준비 결과 cache를 둔다. parser의 AST에는 binding 값이 복제되므로 SQL 문자열만 key로 그 AST를 공유하지 않는다. 같은 handle의 데이터 commit마다 물리 root가 바뀐다는 이유로 모든 template를 버리지 않는다. 외부 게시 뒤 catalog reload는 schema 전용 세대가 없으므로 보수적으로 무효화하며, 이 miss 비용을 비교에 포함한다.

불변 schema와 트랜잭션의 root·행 수 등 변경 상태를 분리한다. transaction-private dirty workspace에서 문장 간 변경을 누적하고 중복 인코딩·페이지 쓰기를 줄인다. 기존 fresh-page 재사용·읽기 cache는 유지하되 이것만으로 문장별 heap 완료 비용이 없어졌다고 보지 않는다. cache·결과·read/write set·private page·spill·회수 pin을 예산에 포함하고 OS 페이지 cache는 별도 측정한다.

### 영속 로그와 소비자 상태

이벤트 원본은 하나의 논리 payload 정본으로 보존하고, 소비자별 cursor·ACK hole·lease·fence·시도·실패 상태를 hot metadata로 관리한다. 원본 하나라는 뜻은 WAL·COW·cache·FFI 복사가 전혀 없다는 뜻이 아니다. 같은 저장 backend의 transaction으로 payload와 업무·소비자 상태를 확정한다. 별도 payload segment를 쓰면 그 끝 위치·checksum·가시성 참조도 같은 commit record로 보호한다. 장애 복구용 WAL은 replay용 이벤트 보관소와 역할이 다르므로, 소비자의 replay 기간 때문에 복구 WAL을 무조건 영구 보존하지 않는다.

새 claim은 소비자·이벤트·현재 상태를 조건으로 소유권을 얻고 fence를 갱신한다. ACK·retry·실패 격리·redrive는 해당 소비자의 소유권과 상태를 검사한다. 재생 위치와 보존 한도, 긴 reader·느린 소비자의 pin을 함께 관리한다. 느린 소비자를 무기한 보존하면서 저장량 제한과 모든 publish 성공을 동시에 약속하지 않는다.

### 다중 writer의 검증·병합·게시

각 transaction은 읽기 기준·schema 식별자·잠금·변경 version·실행별 메모리와 할당 책임을 가진다. 권고하는 WAL 경로는 논리 행·unique key·범위의 충돌과 물리 page의 짧은 latch를 분리한다. 같은 leaf의 다른 행은 page를 수정하는 짧은 구간만 조정하고, 그 page의 I/O나 transaction 전체 동안 latch를 유지하지 않는다. page split·root 교체·할당은 별도의 구조 변경과 redo 순서로 보호한다. 옛 page 사본을 통째로 덮어 다른 writer의 변경을 지우는 방식은 금지한다.

buffered COW 대조군에도 같은 조기 충돌 조정을 적용한다. 이 대조군은 private 변경을 최신 tree에 합쳐 게시해야 하므로 shared-leaf·ancestor·catalog의 재구성 비용이 남는다. row/key 잠금만 추가해 이 병합 문제까지 해결했다고 보지 않는다. COW 대조군의 병합을 WAL 권고안의 정상 commit 절차와 혼동하지 않는다.

같은 프로세스의 여러 connection과 같은 호스트의 직접 file writer를 각각 검증한다. WAL 안은 공유 buffer의 일관성, 잠금·transaction 상태의 프로세스 간 공유, 사망 소유자의 fencing과 복구 진입 차단까지 필요하다. DB 원본 파일을 직접 수정하는 공유 mapping으로 WAL-before-data를 우회하지 않으며, 명시적 writeback이 로그 내구 경계를 확인한다. PID나 timeout만으로 소유권을 회수하지 않는다. 이를 검증하지 못하면 새 backend의 다중 프로세스 지원을 완료하지 않으며, 기존 backend가 지원하던 안전한 단일 writer 사용도 보존한다.

group commit은 독립 거래의 결과와 순서를 유지한다. 자신의 commit record까지 WAL이 동기화된 뒤에만 durable 성공을 반환하고 새 읽기 기준에 공개한다. 실행·page latch·durability group을 하나의 긴 전역 writer 잠금으로 합치지 않는다. 예약 sequence와 확정된 가시성 순서를 구분하고, 미완료 번호를 소비자가 완료로 건너뛰지 않게 한다. WAL append·group barrier·공유 cursor에는 직렬 구간이 남으므로 실행 중첩과 tail·공정성은 별도 검증한다.

### 계약 제안과 현재 구현 경계

같은 leaf에 있는 행 A와 B를 두 transaction이 각각 변경한다고 가정한다. WAL 권고안에서는 page latch와 version 가시성으로 두 결과를 보존하고, COW 대조군에서는 최신 root에 B의 변경을 합친다. B가 옛 leaf 전체를 게시해 A를 지우면 정확성 실패다. leaf가 같다는 이유만으로 거래 전체를 항상 거절하는 방식도 독립 쓰기의 진행 목표를 충족한 것으로 세지 않는다.

아래 계약은 검토할 제안이다. 반환 이름은 동작을 구분하는 표기이며 새 C ABI·Python 예외·설정 키를 정의하지 않는다. [비교 후보의 저장·복구 계약](#비교-후보의-저장복구-계약)은 내부 형식·결과 조회·소유권과 검증 한도를 구체화한다. 공개 API·기본값·제품 보존 수치와 구현 증명은 `미해결`의 G1–G6에 남긴다. 현 코드의 지원 범위를 제안으로 소급해 넓히지 않는다.

| 경계 | 현재 구현의 근거 | 갱신안에서 확인할 차이 |
|---|---|---|
| writer와 격리 | [SQL transaction](../../include/sql/database_txn.h)의 단일 writer·SNAPSHOT, 읽기 전용 transaction handle 미지원 | 독립 writer의 동시 실행, 조기 key/range 잠금, 격리별 읽기와 긴 snapshot의 수명 |
| 프로세스와 잠금 | [쓰기 시작·종료](../../src/sql/database.c)의 `database_work_begin`·`database_work_end`가 작업 전체에서 파일 writer 잠금 보유 | 같은 프로세스의 여러 connection과 같은 호스트의 여러 직접 file writer를 각각 검증 |
| 결과 조회 | [transaction core](../../include/storage/transaction.h)의 `txn_query_commit`은 최근 writer 결과 ring과 같은 열기 세대에 한정 | 재시작 후 조회할 영속 식별자·결과 기록과 그 보존 종료 의미 |
| 게시와 회수 | [page file](../../include/storage/page_file.h)의 조건부 manifest 1-sync 또는 data sync·control write·sync, fallback을 위한 지연 회수 | 병렬 할당·group별 내구 경계, 활성 snapshot과 복구 root를 함께 보호하는 회수 |
| 메모리 | [ledger](../../include/sql/mem_ledger.h)의 요청 bytes 회계와 선택적 활성화 | transaction·connection·DB 전체 예산, 프로세스 간 예약과 pin·spill 상한 |

### writer 범위와 격리

목표는 같은 호스트에서 같은 DB를 연 여러 connection과 여러 프로세스의 직접 쓰기다. 한 connection은 한 번에 한 호출자가 소유한다. 파일 writer 잠금만 끄고 프로세스 간 할당·게시·회수의 책임을 비우는 전환은 허용하지 않는다.

새 동시 쓰기의 격리는 serializable을 권고하고, 충돌 정책은 접근할 때 key·범위를 조정하며 잠금을 거래 종료까지 보유하는 strict two-phase locking을 권고한다. 쓰기 대상 행과 unique key의 권한을 비싼 변경 준비 전에 얻는다. 읽은 key·빈 key와 predicate/range도 보호해 write skew와 phantom을 막는다. schema·catalog·index 수명에는 호환 잠금을 적용하고, DDL과 읽기·쓰기의 충돌도 같은 잠금 계층에서 처리한다. 이를 모든 거래의 전역 배타 잠금으로 대체하지 않는다. 지원 색인으로 범위를 표현할 수 없는 scan·집계는 table 잠금으로 보수적으로 보호한다. 지원할 수 없는 문장은 동시 쓰기 모드에서 명시적으로 거절한다. 잠금마다 bytes를 계상하며 한도 초과를 무제한 lock 축적이나 조용한 격리 축소로 처리하지 않는다.

이 경로의 읽기는 필요한 잠금을 얻은 뒤 확정된 version과 자기 변경을 읽는다. 거래 시작 snapshot을 고정한 채, 기다렸다는 이유로 그 뒤의 version을 임의로 섞지 않는다. 별도의 SNAPSHOT 경로는 고정된 읽기 기준을 유지하므로 대기한 행의 version이 바뀌면 serialization 실패가 필요할 수 있다. 현행 단일 writer의 SNAPSHOT API를 자동으로 serializable로 바꾸지 않는다. 격리별 공개 API·기본값과 지원 문장 범위는 G1에서 구체화한다.

A가 `id=1`의 값을 증가시키는 동안 B도 같은 행을 증가시키면, B는 그 행의 쓰기 권한 앞에서 기다린다. locking 경로에서는 권한을 얻은 뒤 확정된 값으로 SQL 조건과 계산을 수행한다. 반면 호출자가 밖에서 읽은 10으로 `SET value=11`을 보냈다면 이를 증가 연산이나 12로 바꾸지 않는다. 앞서 반환한 값·ID·RETURNING이나 사용자 callback을 몰래 재실행하지 않는다. COW를 사용하더라도 이 조기 조정 원칙은 같다.

같은 leaf의 다른 행은 논리 key가 독립이면 함께 실행한다. 같은 key·unique 충돌, 이미 읽은 범위와 겹치는 변경은 의미상 경합으로 다룬다. claim의 최소 eligible ID 선택도 같은 결과를 보존해야 하므로 잠긴 후보를 임의로 건너뛰는 정책을 추가하지 않는다. page latch와 transaction 잠금의 역할을 분리한다. MVCC 자체가 serializable이나 writer 병렬 진행을 보장하지 않는다는 점은 [PostgreSQL 18 격리 설명](https://www.postgresql.org/docs/18/transaction-iso.html)과도 구분해 읽는다.

잠금 대기는 유한하게 제한하고 wait-for 관계의 cycle을 검사해 교착 victim을 정리한다. 큐 순서를 유지해 새 writer의 반복 추월을 막고, 취소·deadline 뒤 자기 잠금·version·예약을 정리한다. 대량 scan의 table 잠금은 무관해 보이는 쓰기도 막을 수 있으므로 잠금 범위·대기·완료량을 비교한다. MVCC를 쓰는 명시적 snapshot 읽기와 serializable 잠금 읽기의 진행·자원 비용도 구분한다. 조기 잠금은 피할 수 있는 재작업을 줄이는 선택이며 교착·timeout·제약 위반·snapshot 충돌을 없애겠다는 약속은 아니다. [PostgreSQL 18 잠금 설명](https://www.postgresql.org/docs/18/explicit-locking.html)은 행 잠금과 page latch, 교착 실패가 서로 다른 문제임을 확인할 참고다.

### commit 결과와 취소

transaction의 SQL 결과는 commit 전에는 잠정 결과다. 업무 변경·이벤트·해당 소비자의 완료 기록은 같은 backend transaction에서 전부 반영하거나 전부 미적용으로 끝낸다. 다른 DB의 거래와 외부 작업은 이 원자성에 포함하지 않는다. group commit도 transaction별 검증과 결과를 유지하며, 그룹에서 제외한 transaction을 성공으로 응답하지 않는다.

| 상태 | 뜻 | 다음 상태 |
|---|---|---|
| 실행 중 | 읽기 기준·binding·잠금·변경 version을 소유, 미확정 version은 다른 거래에 비가시 | 검증 중 또는 확정 미적용 |
| 검증 중 | 잠금 소유·제약·결과와 commit 순서 확인, COW 대조군은 최신 root 후보 구성 | 게시 중 또는 확정 미적용 |
| 게시 중 | 조정자가 영속 상태를 바꿀 수 있어 단순 취소로 미적용을 약속할 수 없는 구간 | 반영·내구 확인, 확정 미적용의 증명, 또는 불확정 |
| 반영·내구 확인 | 해당 transaction을 포함하는 durable barrier 완료 | 성공 응답과 결과 조회 기록 유지 |
| 확정 미적용 | 어느 유효한 게시 상태에도 반영하지 않았음을 확인한 `NOT_APPLIED` | rollback 정리와 종료 |
| 불확정 | 반영 여부 또는 요청 내구성 달성 여부를 확인할 수 없는 `UNKNOWN` | 쓰기 차단과 복구·결과 대조 |

취소·deadline은 조정자가 게시 권한을 넘겨받기 전까지 확인한다. 이때 변경을 게시하지 않고 private 자원을 정리할 수 있으면 확정 미적용이다. 권한 인수와 취소 처리는 같은 동기화 경계로 순서를 정한다. 인수 뒤 취소·timeout은 rollback 성공의 증거가 아니다. durable 결과를 확인했다면 그 결과를 반환하고, 응답이 끊겼거나 sync·control 기록 결과를 확정하지 못하면 불확정으로 다룬다. 단순 대기 만료, 검증 충돌, 자원 부족, 게시 I/O 실패를 같은 재시도 오류로 합치지 않는다.

COW의 sync 순서와 manifest 완전성, WAL 안의 WAL-before-data와 commit record는 각각 선택한 backend가 증명한다. 둘 다 해당 transaction의 내구 경계 뒤에만 durable 성공을 반환한다. sequence는 검증 후 게시 순서에서 부여하는 안을 우선한다. 예약 번호를 먼저 발급하면 미완료·취소 hole과 committed frontier를 구분한다. 소비자는 최대 예약 번호만 보고 중간 이벤트를 완료 처리하지 않는다.

불확정 뒤에는 관련 DB의 게시·회수에 참여하는 writer들을 조정해 추가 쓰기를 차단한다. 어느 root가 유효한지 모르는 페이지·spill을 반환하지 않는다. 다른 프로세스도 차단 상태를 확인하는 방식과 복구 소유권을 검증해야 한다. 검증된 snapshot 읽기조차 보장할 수 없으면 읽기도 거절한다. 다시 열어 구조가 정상이라는 사실만으로 원래 요청이 미적용이었다고 결론 내리지 않는다.

비교 후보는 [영속 요청 ID와 결과 조회](#영속-요청-id와-결과-조회)를 선택한다. 같은 ID·다른 요청 내용은 거절하고 유효한 기록에는 기존 결과를 반환한다. 기록이 없거나 보존 밖이면 `NOT_APPLIED`를 추론하지 않는다. 미리 선언할 수 없는 대화형 SQL과 외부 callback은 이 조회 계약의 자동 재전송 대상에서 제외하며 업무 key 대조 책임을 유지한다. 현재 메모리 ring은 이 영속 조회의 구현 근거가 아니다.

### 소유권과 자원 상한

| 자원 | 소유자와 수명 | 실패·한도에서의 처리 제안 |
|---|---|---|
| 준비 결과와 schema | 공유 cache가 값 없는 불변 template와 schema 식별자를 보유, 실행 중 참조 유지 | schema 변경 때 새 사용 무효화, 참조 중 해제 금지, cache bytes 상한과 eviction |
| SQL 입력·binding | 호출 동안 빌린 입력과 transaction이 계속 쓸 복사본 구분 | 호출 뒤 재사용할 값은 소유권 확보와 예산 예약, 이전 binding 재사용 금지 |
| key/range 잠금·논리 변경·실행별 workspace | transaction이 rollback·commit 정리까지 보유 | 잠금·version·COW 대조군 병합 사본까지 예약, OOM이면 논리 부분 반영 없이 실패 |
| 공유 buffer·WAL buffer·transaction 상태·옛 version | DB 관리자가 pin·확정 상태·복구/가시성 horizon에 따라 관리 | 프로세스 전체 bytes와 dirty writeback·version GC·사망 소유권을 계상, 한도 초과는 admission 제한 |
| 결과와 payload view | C 결과·Python 객체의 소유자와 해제 지점 구분 | borrowed view는 유효 기간과 pin 계상, 소유권 이전은 예산도 이전 |
| 게시 후보·group buffer | commit 조정자가 barrier와 결과 확정까지 보유 | 그룹 전체·transaction별 예약, 한도 때문에 내구 확인 생략 금지 |
| retire 목록·snapshot pin | DB 회수 관리자가 활성 reader/writer와 backend별 복구 참조 종료까지 보유 | 논리 삭제와 물리 재사용 분리, 나이만으로 사용 중 페이지 재사용 금지 |
| spill과 결과 조회 기록 | transaction 또는 복구 관리자가 파일·ID·보존 종료를 소유 | private·미게시·checksum·disk quota·중단 복구를 검증하기 전 spill 자동 활성화 금지 |

한도는 건수와 bytes를 함께 다룬다. 최대 payload·반환 bytes·in-flight bytes·ACK holes·private 작업량·cache·retire/pin·spill·보존 로그·실패 기록·결과 조회 기록의 범위를 정해야 한다. 연결별 한도만 두면 연결을 늘려 전체 한도를 우회하므로 DB 전체 한도와 여러 프로세스의 예약 회계를 함께 검증한다. 현재 ledger의 요청 bytes를 allocator overhead·단편화·RSS·OS page cache와 같다고 표시하지 않는다.

초기 검증안은 게시 전에 필요한 메모리와 디스크 여유를 예약하고, 확보할 수 없으면 유한 대기 또는 명시적 자원 거절로 끝낸다. 무제한 대기·재시도나 검증하지 않은 spill을 기본값으로 숨기지 않는 안을 권고한다. 큰 transaction을 자동 분할하면 원자성이 달라지므로 호출자가 선택하지 않은 분할은 하지 않는다. 수치·대기 정책을 정하기 전 자원 상한 달성을 주장하지 않는다.

중단 writer와 긴 reader의 등록에는 프로세스 재시작·PID 재사용·소유 세대를 구분하는 식별과 참조 해제가 필요하다. 시간이 오래 지났다는 이유만으로 살아 있는 snapshot의 pin을 훔치지 않는다. 죽은 소유자의 부재와 게시 권한 회수를 증명한 뒤에만 예약·pin을 회수한다. 증명할 수 없으면 신규 예약을 제한하고 운영자가 원인을 확인할 상태를 제공한다. 긴 reader가 보존 공간을 붙잡는 동안에도 다른 writer가 진행할 수 있는 범위는 남은 예산에 한정한다.

### 공통 실행 계약

같은 `SELECT ?1`을 7과 9로 차례로 호출하면 각각 7과 9를 돌려줘야 한다. 첫 binding이 들어 있는 AST를 재사용하면 이 계약을 깨뜨린다. 현행 [parse_parameter](../../src/sql/sql_statement.c#L447-L465)는 binding의 값을 복제하며, [sqlstmt_t 소유권](../../include/sql/sql_statement.h)은 이름과 값을 해제까지 소유한다. 공통 개선은 이 의미와 오류·거래 수명을 보존하는 내부 구현으로 시작한다. 공개 prepared handle·새 격리 기본값·저장 형식 변경은 포함하지 않는다.

| 선택지 | 얻는 것 | 비용과 판단 |
|---|---|---|
| binding 없는 구문 template와 실행별 소유 AST | lexer·parser 재사용과 기존 실행기 입력의 수명 보존 | hit에도 AST·값 복제와 schema 해석 비용이 남지만 첫 구현으로 선택 |
| binding이 든 현행 AST 공유 | 재구성 작업 감소 | 다른 실행의 값 혼입·수명 침범 때문에 제외 |
| lexer 토큰만 cache | parser 변경 범위 축소 | 반복 parser 준비 비용이 남으므로 준비 결과 cache의 완료로 대체하지 않는 판단 |
| 공유 template를 실행기가 직접 참조 | hit의 복제 감소 | 모든 식·결과·오류 경로의 불변성과 pin 수명 변경이 필요하므로 후속 비교 후보로 보류 |

cache는 DB handle마다 분리한다. key는 SQL의 정확한 bytes·길이와 내부 구문 형식 식별자다. 공백·주석·문자열 literal을 임의로 지우거나 binding 값·타입을 key에 넣지 않는다. template는 SQL 자체의 이름·literal과 parameter ID·오류 offset을 소유하되 실행 binding, catalog 포인터, 해석된 column 번호·row locator·실행 결과를 보관하지 않는다. 처음에는 지원하는 SELECT·INSERT·UPDATE·DELETE의 구문을 대상으로 하며 DDL·거래 제어는 기존 경로로 실행한다. cache 대상 밖 문장을 지원하지 않는 것으로 바꾸지 않는다.

hit도 입력 bytes 상한·binding 개수/값·취소·poison·실패한 transaction 검사를 생략하지 않는다. template를 빌리는 동안 pin하고, 실행마다 현재 binding을 복제한 독립 소유 AST를 만든 뒤 pin을 해제한다. 실행 AST의 이름·literal·binding 사본은 query 몫이다. 반환 결과는 기존 소유권 이전을 따르며 template나 호출자의 SQL/binding buffer를 참조하지 않는다. 잘못된 binding을 이전 값으로 메우거나 cache 준비 실패 뒤 SQL을 두 번 실행하지 않는다. 기존 오류 class·code·offset과 한 호출의 실행 횟수를 대조한다.

schema를 해석한 실행 계획은 cache하지 않는다. 실행마다 잠금과 최신 catalog를 확보한 뒤 이름·열·색인·타입을 다시 해석한다. 같은 handle의 DDL에는 보수적으로 template를 무효화하고 rollback 뒤에도 옛 실행 metadata를 복원하지 않는다. 다른 handle의 게시를 감지해 [catalog를 다시 읽을 때](../../src/sql/database.c#L893-L933)도 우선 cache를 비운다. 이 정책은 외부의 데이터 변경에도 miss를 늘릴 수 있지만 별도 파일 세대 형식을 추가하지 않는다. 그 miss 비용을 측정하며, schema 전용 세대나 해석된 계획 재사용은 별도 개선 근거 없이 섞지 않는다. 이미 실행별로 복제한 AST와 호출자가 소유한 결과의 수명은 eviction과 독립이다.

cache는 없을 때 지연 생성하고 항목 수와 전체 bytes를 함께 제한한다. SQL key·template·parameter 목록·색인/교체 metadata·pin된 퇴출 대기 항목까지 상주 비용에 포함한다. 준비 중 old+new의 최고 요청량을 먼저 예약한다. 실행 binding을 포함한 AST 복제는 query ledger로, cache 소유물은 DB의 resident ledger로 계상한다. resident ledger가 없는 bounded 실행에는 문장 ledger를 붙잡아 cache를 만들지 않고 기존 경로를 쓴다. 무계상 직접 API에서도 cache 자체의 유한 한도는 지킨다. 한 handle의 한도가 여러 handle·pool 전체나 RSS의 한도를 뜻하지 않는다.

퇴출은 참조하지 않는 LRU 항목부터 하며 pin된 항목을 해제하지 않는다. 선택적인 cache 저장 거절은 이미 성공한 실행의 결과를 바꾸지 않는다. 실제 실행 AST/결과의 필수 할당이 실패하면 기존 NOMEM 계약을 유지한다. session의 문장 반환 때 query 몫은 0이어야 하며 cache는 query 몫의 사후 초기화에 기대지 않는다. [session 회계](../../include/sql/session.h#L13-L32)처럼 결과는 호출자의 해제까지, cache는 DB close까지 각 소유자가 책임진다. 첫 구현은 현행 handle 실행 직렬화 계약을 따르며, 새 병렬 경로에서는 LRU·pin metadata의 짧은 동기화와 실행 중 참조를 따로 검증한다. cache 보호를 거래 전체의 전역 잠금으로 늘리지 않는다.

내부 비교 경로는 [구문 복제](../../src/sql/sql_statement_clone.c)와 [cache 소유권](../../src/sql/database_cache.c)을 분리한다. miss는 현행 parser로 준비한 SQL을 한 번 실행하고, [실행 성공 뒤](../../src/sql/database.c#L1006-L1012)에만 값 없는 template 저장을 시도한다. 저장 admission이나 할당을 확보하지 못하면 저장을 생략한다. 이미 성공한 SQL을 다시 준비·실행하거나 결과를 오류로 바꾸지 않는다. 필수 query 예약이 부족하면 참조 중이 아닌 cache를 먼저 회수한다. hit 준비의 필수 예약을 확보하지 못한 경우에는 pin을 놓고 [실행 전에만 uncached 준비](../../src/sql/database_prepare.c#L105-L119)로 돌아간다. 실행 AST 할당이 실패했거나 SQL이 실행된 뒤에는 이 경로로 재시도하지 않는다.

이 경로의 [회귀 시험](../../tests/test_database_cache.c)은 SQLite 값·타입 대조, binding 오류 위치, DDL·rollback·다른 handle, 항목/bytes 퇴출, pin, 큰 SQL, 취소, 실패 주입과 마지막 소유자 해제를 검사한다. 세션의 전체 할당 회계와 결과 보유 시험도 함께 검증한다. [시험 설정](../build-and-test.md#sql-template-cache)은 기본 OFF이며 새 writer·workspace·stream 구현이나 성능 채택을 뜻하지 않는다. 내부 통계는 cache가 살아 있는 동안의 hit/miss·퇴출·거절이며 전체 회수·비활성화·close 때 초기화한다. 수집기는 초기화를 감지해야 한다.

### 변경 누적과 실패 경계

workspace는 transaction이 소유한 catalog·행·색인 변경과 dirty page의 최신 사본을 문장 사이에 유지한다. 같은 transaction은 자기 변경을 읽고 다른 transaction은 미확정 변경을 보지 않는다. 새 문장은 이전 문장까지의 작업 상태에서 시작하며, 공개된 page를 직접 덮거나 다른 writer의 변경을 자기 rollback으로 해제하지 않는다. 정상 문장 반환은 commit의 내구 성공이 아니다.

문장별 논리 검증·제약 오류는 그 문장에서 반환한다. 오류·취소·필수 할당 실패 뒤에는 [현행 실패 상태](../../include/sql/database_txn.h#L14-L24)처럼 transaction을 실패로 표시하고, 후속 문장과 commit을 거절하며 명시적 rollback으로 정리한다. 실패한 문장의 일부 catalog·색인·dirty map만 다음 문장에 넘기지 않는다. 앞선 성공 문장의 결과를 몰래 다시 계산하지 않는다. 새로 할당할 사본·재할당의 old+new·예약/retire 목록까지 할당 전에 계상한다.

문장마다 수행하던 물리 완료를 미루면 I/O 오류가 commit에서 관측될 수 있다. 이 이동을 숨기거나 모든 commit 오류를 UNKNOWN으로 합치지 않는다. 게시 권한 인수 전과 미적용을 증명한 실패, 인수 후 확정할 수 없는 실패를 구분한다. NOT_APPLIED면 자기 private 자원을 정리하고, UNKNOWN이면 게시·회수를 차단하며 저장 자원을 재사용하지 않는다. 임의 spill·큰 transaction의 자동 분할·내구 barrier 생략으로 예산을 맞추지 않는다. 새 다중 writer의 공유 buffer/version과 복구 형식은 G6의 별도 계약을 따른다.

workspace의 문장 밖 보유량은 현재 작업 catalog와 같은 DB resident/shared 회계에 남기고, 별도 transaction 작업량 한도로 제한한다. session의 query/result 한도를 transaction 전체 한도라고 다시 해석하지 않는다. 실행 중 임시 사본과 반환 결과도 동시에 존재할 수 있으므로 전체 shared 예산과 각 scope를 함께 검사한다. cache/workspace의 OFF·ON 네 조건에는 같은 논리 입력·결과·상위 예산과 내구 조건을 적용한다.

내부 [workspace 경로](../../src/sql/database_workspace.c)는 첫 문장의 private catalog를 다음 문장에서 재사용한다. [페이지 사본](../../src/storage/page_workspace.c)은 작업이 새로 할당한 COW page만 보유한다. 다음 문장이 대체하거나 삭제한 사본은 놓고, 살아 있는 최종 사본을 기존 게시 절차 전에 encode·write한다. 읽기는 자기 private 사본을 우선하며 작업 밖의 catalog는 미확정 page를 가리키지 않는다. 문장별 heap·색인 계산과 root restamp는 남으며, 최종 사본의 목록 탐색·복사·메모리 보유 비용도 든다. 이 경로를 새 WAL·buffer pool이나 실제 동시 writer 완료로 쓰지 않는다.

사본·catalog·회수 목록은 작업 한도와 resident/shared 예산을 함께 검사한다. 파일의 할당 목록이 커질 때도 할당 전에 그 상주 몫을 확보하며, 실패 뒤 남는 목록은 DB 소유로 계속 계상한다. 작업 중 resident ledger를 새로 붙이는 일은 거절하므로 bounded 열기나 작업 시작 전에 연결한다. 논리 오류·취소·할당 거절은 실패 거래와 명시적 rollback을 유지한다. commit에서 최종 page 쓰기에 실패하면 기존 NOT_APPLIED·UNKNOWN 경계로 정리하며, UNKNOWN의 디스크 page는 재사용하지 않는다. [회귀 시험](../../tests/test_database_workspace.c)은 cache/workspace 네 조건의 SQLite 결과, 자기 변경·미확정 비노출, 큰 값·초과 거절, 할당/I/O 실패, 삭제·재사용과 보유 결과의 수명을 검사한다. [내부 설정](../build-and-test.md#transaction-workspace)은 기본 OFF이며 비계측 처리량·tail·전체 RSS와 채택은 별도 수집 대상이다.

### 공통 개선의 시험용 한도

아래 값은 유한한 구현·회귀 대상을 만드는 가역적인 시험 설정이다. 제품 기본값·사용자 SLO·채택 기준이나 RSS 보장이 아니다. 새 경로는 명시적인 내부 시험 설정으로 OFF·ON을 비교하고, 효과와 회귀 판정 전 기존 배포 기본값을 바꾸지 않는다. 공개 설정 API를 이 표에서 확정하지 않는다.

| 대상 | 초기 시험 설정 | 이유와 한도 도달 동작 |
|---|---|---|
| cache | handle당 64항목·총 1MiB, 원 SQL 16KiB 이하만 admission | 반복 짧은 문장과 eviction을 함께 검증하는 출발값이며, 큰 SQL이나 예산 부족은 지원을 줄이지 않고 uncached 실행 |
| transaction workspace | 작업당 요청 bytes 8MiB와 기존 shared 예산을 모두 적용 | 작은 commit group과 초과 거절을 검증하는 출발값이며, 부족하면 NOMEM·실패 상태와 명시적 rollback |
| scope와 최고치 | resident cache/workspace + 실행 query + 보유 result를 중복 없이 합산 | 재할당 old+new·퇴출 대기·동시 결과를 포함하며, 예약 실패 뒤 카운터와 소유권 보존 |

실제 부하에서 miss·admission 거절·NOMEM·추가 복제·처리량·tail·전체 메모리가 불리하면 한도나 cache 공유 방식을 독립 가설로 다시 검토한다. 수집 뒤 목표를 맞추려고 실패 표본을 버리거나 한도를 바꾸지 않는다. G5의 실제 workload·수치 판정 기준은 수집기 사전 계획에서 따로 고정한다. [현재 C API](../../src/capi/totedb.c#L119-L142)의 무제한 pool 설정을 이 시험값으로 바꿨다거나, 여러 pool의 프로세스 전체 상한을 구현했다고 주장하지 않는다.

### 선택형 수명과 조건부 갱신

SQL만 쓰는 Python DB는 큐 전용 빈 set을 만들지 않고 종료 때 빈 목록을 복제하지 않는 경로를 선택한다. 하위 자원 등록은 기능을 처음 명시적으로 사용할 때 지연 생성한다. 공통 소유 자원 등록부가 필요하면 generic 수명 관리와 queue/stream 전용 호출·분기를 실제 경로에서 구분한다. `_queues`를 다른 이름으로 바꾸는 것만으로 전용 비용을 제거했다고 판정하지 않는다. SQL 실행에는 기능 전용 polling·dispatcher·등록 호출을 넣지 않는다.

등록한 자원은 기존처럼 DB보다 오래 살지 않는 강한 소유 관계와 명시적 unregister를 유지한다. 닫기 중 등록부가 바뀌어도 각 자원의 해제를 중복하지 않고, 해제 오류가 나면 살아 있는 handle을 잃지 않는다. [현재 Python close](../../python/totedb/database.py#L265-L288)처럼 열린 native transaction 때문에 DB close가 Busy이면 DB는 열려 있고 먼저 닫힌 큐는 닫힌 상태인 경계를 보존한다. 이를 조용히 transaction 자동 rollback으로 바꾸지 않는다. 공통 close 검사까지 비용 0이라고 약속하지 않고 생성·실행·종료를 각각 관측한다.

현재 Python 구현은 SQL 단독 생성에서 큐 등록부를 할당하지 않는다. 첫 큐 사용 때 기존 instance dict에 등록부를 두고, 이후 같은 등록부를 사용한다. 마지막 큐가 닫혀도 빈 등록부는 DB 수명 동안 유지한다. close의 등록부 존재 검사와 기능을 사용한 뒤 snapshot 복제는 남는다. 큐를 여는 데 성공한 뒤 Python 등록 할당이 실패하면 native handle을 해제하고 `MemoryError`를 그대로 전달한다. 설치 과정에서 이미 commit한 큐 테이블까지 되돌리지는 않는다. [수명 시험](../../tests/test_queue_lifecycle.py)은 강한 소유·반복 close·Busy 뒤 commit/rollback·재개방·Python 등록 할당/close 오류와 동시 첫 등록을 검사한다. 호출·등록부 할당 관측은 SQL 단독 경로의 빈 set/목록 제거를 확인하며, import·공통 DB 상태·전체 RSS나 비계측 지연의 비용 0을 입증하지 않는다. stream 수명은 구현 전이며 실제 기능을 추가할 때 같은 계약을 적용한다.

정상 claim은 최소 eligible ID를 고른 조건부 UPDATE와 RETURNING으로 합치는 안을 먼저 구현한다. ACK도 ID·상태·fence를 조건으로 갱신하고, 영향 행이 없을 때만 필요한 상태를 다시 읽는 안을 우선한다. 이는 [일반 SQL](../../include/sql/sql_statement.h)의 기존 갱신·서브쿼리·RETURNING 의미를 활용하는 구현 방향이며 실제 지원·접근 경로는 구현에서 검증한다. 최소 후보를 임의로 건너뛰거나 fence·만료 검사를 제거하지 않는다. 같은 fence의 ACKED만 중복 성공으로 판정하고 FAILED를 완료로 바꾸지 않는다. 실패 경로의 추가 SQL·scan·복사·반환 소유권도 비교하며 SQL 수만으로 성능을 채택하지 않는다.

조건부 갱신에 앞선 로컬 SQL 구현에서 [일반 동등 조건의 탐침 추출](../../src/sql/database_expr.c)은 이미 계산한 비상관 scalar subquery의 한 행·한 열 값을 재사용한다. 값이 NULL이 아니고 열과 같은 타입일 때 기존 색인 탐침을 만들며 전체 WHERE 조건도 다시 검사한다. 탐침 값은 query 소유로 복제하고 문장 끝에 해제한다. 서브쿼리를 다시 실행하거나 큐 전용 hook을 넣지 않는다. NULL·빈 결과·다른 타입·상관 서브쿼리는 기존 평가 경로를 유지하며 형태 오류도 그대로 반환한다. 이 보수적 범위에서는 빈 후보의 전체 scan 비용이 남을 수 있으므로 큐 통합 때 별도로 다룬다.

[SQL 회귀](../../tests/test_database.c)는 두 상태 색인에서 각각 최소 ID를 구한 뒤 합친 최소 후보의 UPDATE·DELETE, 현재 binding과 닫기 뒤 반환 값의 수명을 검사한다. 6행 합성 입력의 변경 전 UPDATE·DELETE는 각각 전체 scan 6행·색인 조회 2회였고, 탐침을 추가한 경로는 각각 전체 scan 0행·색인 조회 3회다. 이는 특정 접근 경로의 관측이며 처리량 개선율이 아니다. [SQLite 대조](../../tests/test_database_sqlite.c)와 [할당·I/O 실패 시험](../../tests/test_database_fault.c)을 함께 적용한다. 이 선행 SQL 구현을 claim/ACK의 통합·lease/fence 회귀·실측 완료로 해석하지 않는다.

로컬 [조건부 큐 경로](../../src/queue/ack_queue_conditional.c)는 `ACKQ_CONDITIONAL=1`에서 claim·claim_id와 ACK·fail을 연결한다. [내부 설정](../build-and-test.md#conditional-queue-sql)의 기본값은 0이다. claim은 기존 두 상태 색인에서 후보를 하나씩 구해 최소 ID를 선택한다. 빈 집합의 `COALESCE(MIN(id), -1)`도 전체 eligibility 조건과 함께 검사하므로 sentinel 행을 claim하지 않는다. fence·attempts 상한은 산술 갱신 전에 거절한다. 배치는 같은 write transaction과 시각 아래 최소 후보를 반복해서 처리하며, 앞서 새 lease를 받은 행을 다시 고르지 않는다.

영향 행이 없으면 [기존 상태 조회](../../src/queue/ack_queue.c)만 수행해 빈 후보·상한·누락·ineligible·중복·stale 결과를 구분한다. mutation을 재실행하거나 최소 후보의 상한 오류를 건너뛰지 않는다. 같은 fence의 ACKED만 중복 ACK 성공이며 FAILED는 그대로 남긴다. 중복 fail은 첫 note를 보존한다. 0행 UPDATE는 연산의 dirty 표시를 이전 값으로 돌려 빈 claim·중복 ACK를 commit하지 않는다. SQL 실패는 기존 failed transaction과 명시적 rollback을 유지하고, SQL 성공 뒤 payload 복사 실패는 기존 호출자 transaction 소유권을 보존한다. UNKNOWN이면 claim 결과를 호출자에게 주지 않는 기존 경계도 유지한다.

[조건부 회귀](../../tests/test_ack_queue_conditional.c)는 cache/workspace 네 조건에서 기존·조건부 경로를 함께 검사한다. 정상 claim은 SQL 3회에서 1회, 정상 ACK는 2회에서 1회로 줄었다. 같은 시험의 부분 배치는 4회에서 5회, 중복·stale ACK는 1회에서 2회, 빈 claim은 2회에서 3회로 늘었다. 전체 table scan은 없지만 부분 배치의 색인 조회는 4회에서 11회로 늘었다. 정상 claim의 문장별 query 예약 정점은 기존 40,425–62,730B, 후보 132,245–210,325B였고 고정 96KiB query 한도에서 후보는 NOMEM으로 거절했다. 이 값은 해당 합성 입력과 cache 상태의 요청 bytes이며 RSS나 보편적 메모리 비율이 아니다. SQL·binding 수, 가변 binding bytes, payload 복사 할당, query/result 예약, 색인 페이지, pread·pwrite·fsync와 wall/CPU 관측에는 시험 hook의 비용이 포함된다. 비계측 채택 근거로 사용하지 않는다. [게시 실패·강제 종료 시험](../../tests/test_ack_queue_fault.c)과 실제 C/Python 경로의 재개방도 별도로 검사하며 물리 전원 장애 보장은 추가하지 않는다.

1·4·16·32개를 모두 가져오는 배치도 별도로 관측했다. cache/workspace OFF의 32개 배치는 SQL 34회에서 32회로 줄었지만 색인 조회는 34회에서 96회, 색인 페이지 관측은 358회에서 544회로 늘었다. 후보를 매 항목 다시 선택하는 비용을 단순 SQL 감소로 숨기지 않는다. 이 경로는 비교 후보이며 큰 배치의 성능 채택도 완료되지 않았다.

빈 후보 회귀에서 SQL 코어의 빈 UNION 병합이 NULL 배열에 0 오프셋을 적용하는 UBSan 오류를 발견했다. [결과 병합](../../src/sql/database_select.c)은 실제 행이 있을 때만 배열을 복사하며, [일반 SQL 회귀](../../tests/test_database.c)에 빈·비어 있지 않은 양쪽 조합을 추가했다. 오류를 낸 바이너리·DB·로그를 보존하고 수정 후 네이티브 검증은 UBSan 오류 즉시 종료와 stderr 검사까지 적용했다. 오류가 남았던 최초 실행의 종료 코드 0을 통과 증거로 사용하지 않는다.

### 공통 계약의 검증 입력

이 표는 각 구현이 확인할 기대 결과다. cache·workspace·Python 큐 수명과 로컬 조건부 SQL의 회귀·실패 검증은 앞의 시험 파일에 연결하며, stream 수명은 구현 전이다. 조건부 SQL과 수명의 비계측 업무 결과·성능 채택은 별도 수집으로 판정한다.

| 입력 | 기대 결과와 검사 경계 |
|---|---|
| 같은 `SELECT ?1`에 7·9, TEXT·BLOB·NULL과 반복 parameter | 매 호출 현재 값·타입·bytes를 반환, 이전 binding과 입력 buffer의 수명 침범 없음 |
| 같은 buffer 주소의 SQL bytes 변경, 잘못된 UTF-8·복수 문장·binding 개수/값 | 정확한 SQL key로 구분하고 기존 오류 class·code·offset과 실행 횟수 보존 |
| cache한 조회 뒤 DROP/recreate·색인 변경·transaction DDL/rollback·다른 handle 게시 | 현재 catalog에서 다시 해석, 옛 열 번호·포인터 사용 없음, conservative flush의 miss 기록 |
| 기존 입력 한도 안에서 cache admission의 16KiB를 넘는 SQL·64항목 초과·pin/eviction·cache 저장 OOM | 지원 SQL은 uncached 경로로 한 번 실행, 사용 중 template·기존 항목·카운터 손상 없음 |
| AST/결과 NOMEM·취소·오류 뒤 query 종료, 결과 보유 중 session/DB 종료 | query 몫 0, cache는 resident 소유, 결과는 해제까지 유효하고 마지막 소유자가 반환 |
| transaction의 첫 변경 성공 뒤 제약 오류·OOM·취소 | 실패 상태에서 문장/commit 거절, rollback 뒤 기존 DB 보존, 다른 거래 자원 해제 없음 |
| workspace의 자기 변경 조회·같은 page 재수정·DELETE/재사용·최고량 초과 | `test_database_workspace.c`의 SQLite 대조·초과 거절·반복 삭제·결과 수명 검사, 자동 분할 없음 |
| 게시 전후 I/O·sync 실패와 응답 유실 | NOT_APPLIED/UNKNOWN/내구 성공을 해당 경계로 판정, 불확정 저장 자원 재사용 없음 |
| SQL 단독 생성/실행/종료와 기능 첫 사용·반복 close·Busy close | 전용 빈 set/list·polling 없음, 명시 사용의 등록·강한 소유·오류 뒤 handle 상태 보존 |
| 최소 eligible 후보 경합·expiry·stale/중복 ACK·FAILED·반환 결과 해제 | 기존 후보와 fence/상태 의미 일치, 실패 readback·scan·복사·추가 SQL도 비용에 포함 |

### 소비자 보존과 이관

느린 소비자와 저장 상한이 충돌할 때는 기존 이벤트를 몰래 버리기보다 신규 publish를 거절하거나 제한된 시간만 기다리는 안을 권고한다. 자동 consumer expiry·강제 truncate는 별도 데이터 손실 정책이다. replay 기간·최대 보존 bytes·소비자별 지연·실패 격리 및 redrive pin의 한도와 우선순위를 정하기 전 무기한 보존이나 모든 publish 성공을 약속하지 않는다.

논리 삭제는 필요한 소비자의 ACK horizon, 허용 replay 범위, 실패 기록·redrive·멱등 기록의 참조가 모두 끝난 자료만 대상으로 한다. 물리 재사용에는 활성 snapshot과 각 backend의 복구 참조 종료도 필요하다. COW의 fallback page와 WAL의 redo/옛 행 version은 따로 확인한다. 보존 밖 replay는 빈 성공 대신 요청 위치가 남아 있지 않음을 알린다. 같은 fence의 중복 ACK는 ACKED 기록이 남은 동안에만 기존 완료로 판정하고, 새 fence 뒤 옛 ACK는 거절한다. FAILED를 ACKED로 바꾸는 호출은 redrive와 구분해 거절한다. terminal 기록을 지운 뒤 중복인지 stale인지 증명할 수 없으면 구분 불가를 반환한다. 한 소비자의 ACK·실패·redrive는 다른 소비자의 상태를 바꾸지 않는다.

기존 single-writer backend·배포 기본값·파일은 전환 검증이 끝날 때까지 fallback으로 보존한다. 새 형식이 필요하면 원본을 보존한 채 별도 DB에 논리 이관하고 행·색인·event·consumer 상태·미완료 lease/fence·보존 이력을 대조한다. 새 형식 식별자·별도 경로·배타 전환으로 mixed-version writer 진입 거절을 검증한다. 검증하지 않은 구버전 프로세스가 새 DB를 열 수 있는 경로로 전환하지 않는다.

전환 후 새 쓰기가 생기면 옛 DB로 코드만 되돌려 그 쓰기를 복원할 수 없다. 무쓰기 전환 구간의 취소와, 새 쓰기를 보존하는 역이관·복원은 다른 검사다. export/import·backup/restore·복구 결과 대조와 역변환 한계를 확인하기 전 원본 덮어쓰기나 자동 cutover는 하지 않는다. SQL 사용자에게 queue/stream 전용 호출·분기·할당을 추가하지 않는 요구도 전환 전후 각각 검사한다.

### 시나리오와 계약 검토

이 표는 제안을 검토할 입력과 기대 결과다. 실행한 시험 결과가 아니며 다중 writer 안전성·성능 입증을 대신하지 않는다.

| 입력·중단 지점 | 제안 계약의 기대 결과 | 구현 시 필요한 관측 |
|---|---|---|
| 열린 A transaction과 독립 B writer | B가 A의 종료 전체를 기다리지 않고 실행·commit, 두 결과 보존 | connection·프로세스별 겹친 실행 시간과 durable 결과, 최대 대기 |
| 같은 leaf의 다른 두 행 | WAL은 짧은 latch·version으로, COW는 최신 root 병합으로 둘 다 보존 | barrier로 실행 순서 고정, 두 행·ancestor·catalog와 재개방 결과 |
| 같은 행의 update/delete | 쓰기 준비 전 key 조정, locking 읽기와 snapshot 충돌 구분, 실패 거래 전체 미적용 | 행·색인·연관 이벤트에 부분 반영 없음 |
| 같은 unique key 삽입 | 두 성공 금지, unique 충돌 또는 검증 충돌 | 재개방 뒤 unique 불변식과 실패 거래의 무반영 |
| 범위·집계 뒤 다른 행 쓰기 | 범위/table 잠금으로 전제 보호, phantom·write skew 허용 금지 | 읽은 빈 범위·집계 전제, table/range 잠금과 schema 변경 |
| 열린 writer 정지·프로세스 종료 | 살아 있는 소유권 탈취 금지, 사망·권한 회수 확인 뒤 정리 | PID 재사용·소유 세대·고아 예약과 다른 writer 진행 |
| 긴 reader와 반복 삭제 | pin·fallback 페이지 재사용 금지, 예산 초과는 명시적 backpressure | snapshot 내용 불변, pinned/reclaimable bytes와 admission 결과 |
| 느린 소비자·보존 한도 도달 | 승인 없는 이벤트 손실 금지, publish 거절·유한 대기 | 보존 원본·소비자 cursor·실패 pin과 보존 밖 replay 응답 |
| lease 재획득·역순 ACK·redrive | 새 fence만 변경 허용, ACK hole을 건너뛴 완료 금지, 소비자별 실패 | terminal 보존 전후 duplicate/stale 구분과 다른 그룹 상태 |
| 대형 payload·OOM·spill 한도 | 예약 실패 전후 전체 거래 미적용, spill 미검증이면 사용 거절 | 반환 결과·예약 해제·disk full·고아 파일, RSS와 ledger 별도 값 |
| 검증 전 취소·게시와 취소 경합 | 게시 권한 인수 전 확정 미적용, 인수 뒤 결과 확인 또는 불확정 | 두 실행 순서의 결과와 유효 root 대조 |
| control 기록 또는 sync 실패 | 성공 응답 금지, 확정 불가면 UNKNOWN과 관련 writer 차단 | short/torn/reordered write·복구 root·추가 쓰기와 회수 차단 |
| group 중 일부 충돌·barrier 실패 | 충돌 거래 성공 금지, 각 거래의 durable 포함 여부와 응답 일치 | 그룹 구성·의존 순서·각 결과 조회, 예약 sequence hole |
| durable 뒤 응답 유실·재시작 | 영속 결과가 있으면 기존 결과, 기록 밖은 미적용 추론 금지 | ID 재사용·내용 불일치·조회 보존 만료와 중복 재전송 |
| 구버전 writer·이관 중단·전환 뒤 쓰기 | 혼합 쓰기 거절, 원본 보존, 새 쓰기의 복원 한계 명시 | 내용 대조·backup/restore·역이관 또는 무쓰기 취소 |

### 성능과 안전을 함께 고르는 순서

새 엔진은 page/redo WAL·bounded buffer pool·MVCC와 조기 key/range 잠금을 기본 개발 방향으로 권고한다. 현행 COW를 그대로 확장하는 안을 기본 방향에서 내리고, buffered COW는 같은 계약의 대조군으로 남긴다. 이는 잦은 INSERT·UPDATE·DELETE와 작은 ACK 상태 변경, 같은 page에 모인 독립 writer를 중시한 설계 추론이다. 새 WAL이 실측에서 이겼다거나 현행 COW 제품의 안전성이 실패했다는 판정은 아니다. 구현·복구·동등 조건 비교가 끝날 때까지 기존 backend·배포 기본값·DB를 보존한다.

저장 방식과 격리·충돌 정책을 따로 고른다. COW도 여러 page version으로 snapshot을 제공할 수 있고, WAL도 단일 writer로 구현될 수 있다. buffer와 변경 누적은 양쪽에 적용한다. MVCC는 version의 가시성과 회수를, 조기 잠금은 경합의 조정 시점을 맡는다. 네 이름을 한 묶음의 성능 점수로 비교하지 않는다.

| 대안 | 같은 입력에서 얻는 것 | 부담과 현재 판단 |
|---|---|---|
| 현행 단일 writer COW | 기존 commit·fallback·오류 경계와 파일 유지 | 독립 writer가 열린 거래 전체를 기다려 새 요구 미충족, 현행 제품과 회귀 기준으로 보존 |
| buffered COW + 조기 잠금·snapshot 관리 | 불변 page 읽기와 기존 codec·게시 경험 재사용, group 내부 page 변경 누적 | 같은 leaf·ancestor·catalog의 최신 병합과 재작성, 옛 page 회수와 게시 조정의 새 증명 필요, 비교군으로 유지 |
| page/redo WAL + bounded pool·MVCC·조기 잠금 | durable group 사이에도 같은 mutable page의 변경을 모으고, 행 변경과 전체 root 게시를 분리 | WAL 순서·공유 frame·version/transaction 상태·checkpoint·GC·재시작을 새로 구현, 새 엔진의 기본 방향으로 권고 |
| 일반 SQLite WAL + 제품 상태기계 | SQL·복구 구현 재사용과 작은 개발 범위 | writer 하나 제약으로 실제 독립 writer 요구 미충족, 요구 변경 전 채택 완료 판정 제외 |

일반 SQLite WAL의 한 writer 제약은 [공식 WAL 문서](https://www.sqlite.org/wal.html#concurrency)에 있다. SQLite를 제품 대안으로 남기는 것과 이 제약을 충족했다고 주장하는 것은 다르다. WAL 권고의 근거도 PostgreSQL을 그대로 복제해야 한다는 뜻이 아니다.

### 소스에서 확인한 비용과 저장 단위

기본 OFF 경로의 [publish_inner](../../src/sql/database.c#L751-L788)는 transaction 안에서도 문장별 `heap_batch_finish` 뒤에 변경을 stage한다. [catalog_clone](../../src/storage/catalog.c#L249-L257)은 encode/decode를 거치고, [page_file_write](../../src/storage/page_file.c#L1180-L1213)는 page를 encode한 뒤 실제 파일 쓰기와 cache 갱신을 한다. 내부 workspace ON 경로는 사본을 보유하고 commit 전에 최종 사본만 쓰며, 문장별 논리 완료와 색인 계산은 유지한다. 준비 결과 cache와 transaction workspace는 양쪽 저장 방식에 적용할 공통 개선이다. 이 비용 전부를 COW의 필수 비용으로 세거나, WAL을 붙이면 파싱·복사도 사라진다고 계산하지 않는다.

여기서 COW 대조군은 변경 page를 복사하고 root를 직접 영속 게시하는 방식이다. 공통 개선을 적용해도 새로 공개할 변경 page와 필요한 참조 경로를 영속화한다. 같은 leaf의 서로 다른 행을 각 writer가 옛 사본에서 준비하면 최신 상태로 합쳐야 한다. 조기 행 잠금은 같은 행 충돌을 줄이지만 이 물리 공유를 없애지 않는다. WAL 안은 같은 page의 짧은 latch와 redo 순서로 이를 조정하고, commit은 해당 로그의 내구성에 연결한다. [PostgreSQL 18 WAL 설명](https://www.postgresql.org/docs/18/wal-intro.html)의 WAL-before-data 원칙은 data page를 매 commit마다 동기화하지 않고도 redo로 복구하는 경계를 설명한다. 이 원칙이 새 구현의 안전성 증거를 대신하지 않는다.

page 크기를 `P`, 정리까지 포함한 durable group 수를 `G`, group `g`에서 누적한 COW 변경 page 집합을 `D_g`라고 둔다. buffered COW의 정상 page 쓰기 항은 `P * sum_g |D_g|`이며 catalog·control·free-list·회수 기록을 더한다. 같은 group 안의 여러 거래를 한 page에 합치는 대조군도 허용해야 공정하다. 현행 `P`는 [page_file.h](../../include/storage/page_file.h#L33)의 8192바이트이며, 이를 새 backend의 최종 형식으로 확정하지 않는다.

같은 page 크기의 압축 없는 WAL 모델은 전체 page image 본문을 제외한 로그 bytes `R`, 전체 page image 수 `F`, data/index writeback page 수 `C`로 나누어 `R + P*F + P*C`를 본다. `R`에는 commit·checkpoint·할당·로그 header도 포함한다. GC가 만든 로그·image·writeback도 이 항에 포함하고 진단에서 별도 태그로 구분하며 다시 더하지 않는다. COW의 `D_g`에도 GC의 page 변경을 포함한다. 두 식은 저장 단위를 드러내는 분석 모형이며 실측값·성능비·물리 장치 쓰기량의 보장이 아니다. CPU에는 encode·병합·latch·version 탐색·잠금·GC를, 지연에는 admission·대기·group barrier·정리를 별도로 포함한다.

작은 변경이 같은 page에 여러 durable group에 걸쳐 쌓이면 WAL은 group 사이의 data writeback도 합칠 수 있다. COW의 group 누적은 각 공개 group의 page 영속화까지 없애지 못한다. 이 차이가 WAL을 권고하는 구조적 이유다. 큰 bulk 쓰기, 큰 durability group, 거의 변경하지 않는 읽기에서는 이 이점이 줄 수 있고, 전체 page image·GC·cache miss 비용 때문에 WAL이 더 불리할 수도 있다. 각 commit이 새 물리 page를 무조건 할당한다고 가정하지 않는다. 현행 COW의 fresh 재사용과 조건부 1-sync도 대조군에 보존한다.

메모리 우위는 저장 방식만으로 정해지지 않는다. COW의 옛 page는 디스크에 둘 수 있으므로 pin된 파일 bytes를 전부 RSS로 세지 않는다. WAL 안은 shared frame·redo buffer·잠금·transaction 상태·옛 행 version 때문에 새 상주 비용이 생긴다. 같은 전체 예산으로 cache와 실행별 작업량을 제한하고, allocator·OS cache·binding·호출자의 전체 payload 축적까지 포함한다. 작은 consumer 상태와 불변 payload의 분리는 양쪽에 똑같이 적용한다. WAL로 바꾸어도 호출자가 pending 본문 전체를 list로 유지하는 비용은 없어지지 않으며 [목록 비교](#pending-목록의-메모리와-호출-경계)로 따로 확인한다.

### WAL 권고안의 복구·정리 경계

redo는 실행된 변경을 복구하며 SQL이나 외부 callback을 다시 실행하지 않는다. 변경 record와 commit record를 checksum·순서로 확인하고, 해당 data page의 LSN까지 WAL이 동기화된 뒤에만 그 page를 파일에 쓴다. commit 성공도 해당 commit record의 durable barrier 뒤에만 반환한다. full-page image와 checksum으로 torn page를 복구하는 안을 우선하며 image 이후 redo와 구조 변경 순서까지 검사한다. [PostgreSQL 18 checkpoint 설명](https://www.postgresql.org/docs/18/wal-configuration.html)은 checkpoint 후 첫 변경의 전체 page image 비용과 회복 시간·쓰기 비용의 교환을 확인할 참고다.

MVCC는 이전 행을 보존한 채 새 version을 만들고 commit 상태로 가시성을 판정한다. 첫 비교 후보는 미확정 거래에 의존하는 frame을 파일에 쓰지 않는 NO-STEAL을 선택한다. redo로 미확정 제자리 덮어쓰기를 취소한다고 가정하지 않는다. index split·page 할당은 논리 abort와 별도로 복구한다. [비교 후보의 저장·복구 계약](#비교-후보의-저장복구-계약)은 이 형식과 증명 의무를 정하며 실제 구현·실패 시험은 별도다.

checkpoint는 재개 지점, data writeback 완료와 필요한 transaction 상태를 영속화한 뒤에만 오래된 WAL을 회수한다. 긴 snapshot이 참조하는 옛 행 version은 따로 보존한다. checkpoint를 했다는 이유로 이 version이나 소비자의 replay payload를 지우지 않는다. COW 대조군도 활성 snapshot과 fallback이 참조하는 page를 회수하지 않는다. 양쪽 모두 장기 reader·실패 기록·느린 소비자에 의한 저장 압력과 남은 정리 debt를 측정한다.

WAL의 복구 재생과 checkpoint·version GC는 COW의 root 선택·manifest 확인보다 큰 시작·정리 부담이 될 수 있다. COW의 새 병렬 게시도 기존 두 세대 회수 규칙만으로 안전해지지는 않는다. 양쪽의 성공 응답 보존, UNKNOWN, 프로세스 사망, disk full·OOM·sync 실패와 장기 정리를 같은 조건으로 확인한다. 전원 차단·lost write·network FS는 실제 검증 범위를 넘겨 보장하지 않는다. 새 형식과 다중 프로세스 공유 제어 영역의 비용도 숨기지 않는다.

### 비교 후보의 저장·복구 계약

같은 leaf의 A와 B를 각각 바꾸는 두 writer를 예로 든다. A의 새 version이 미확정이어도 B는 자기 key를 잠그고 version을 추가한 뒤 자기 WAL commit을 동기화할 수 있다. A 때문에 그 frame의 writeback은 기다리지만 B의 durable 성공까지 A의 종료를 기다릴 필요는 없다. 재시작하면 유효한 commit을 가진 B만 새 값으로 보이고 A의 이전 값은 남는다. 이 입력에서 B가 항상 A의 종료까지 막히면 독립 writer 요구를 통과하지 못한다. frame 압력으로 거절되는 경우는 별도 자원·공정성 결과로 남긴다.

이 절은 [#266](https://github.com/woonyong-choi/totedb/issues/266)의 격리된 비교용 형식과 내부 계약이다. 기존 8192B `LRPG`/`TOTEDB` 형식 3, `database_txn_begin`의 SNAPSHOT 기본값, C ABI와 Python API는 바꾸지 않는다. 새 파일 집합은 독립 DB UUID와 backend 식별자를 가지며 기존 파일을 새 decoder로 열거나 자동 변환하지 않는다. 새 후보도 8192B page를 사용해 page 크기 변화의 효과를 분리한다. UUID·version·byte order·길이·backend가 다르면 쓰기 없이 거절한다. 생성은 배타 생성과 부모 directory 동기화를 포함하며 이미 있는 경로를 덮지 않는다.

| 내부 경계 | 입력과 출력 | 소유자·실패 책임 |
|---|---|---|
| 실행기 → version 저장소 | transaction ID, row/key, 읽기 기준 → 자기 version 또는 확정 version | 실행기가 논리 잠금을 보유, 저장소는 이전 값과 index 참조 보존 |
| version 저장소 → WAL | page 변경 묶음 → 완전한 physical group의 끝 LSN | WAL 조정자가 append 순서, buffer는 page latch·pin과 변경 세대 관리 |
| commit 조정자 → 내구성 계층 | commit LSN까지 force → durable LSN 또는 불확정 | 거래별 commit 포함 여부 확인 후 가시성 공개·잠금 해제 |
| writeback → WAL | frame 사본과 required LSN → 쓰기 허용 여부 | 미확정 의존성이 없고 WAL 내구 순서가 충족될 때만 data 쓰기 |
| 복구 → 전체 DB | control·log·page → 단일 복구 세대와 transaction 상태 | 배타 복구 소유자가 검증을 끝내기 전 일반 읽기·쓰기 차단 |
| 결과 조회 → 호출자 | DB UUID·요청 ID·내용 digest → 상태와 소유 결과 사본 | 조회는 SQL·callback을 실행하지 않으며 반환 메모리는 호출자가 해제 |

물리 group은 page split·할당·root 참조 변경을 한 단위로 복구하기 위한 짧은 묶음이다. SQL transaction과 다르며 논리 abort 뒤에도 구조는 남을 수 있다. 같은 group이 건드리는 page latch는 page ID 순으로 얻고, 완성할 bytes와 WAL 공간을 먼저 예약한다. group 전체의 연속 LSN을 정한 뒤 끝 record까지 append하고 frame에 공개한다. append mutex는 순서 부여와 bytes 복사에만 사용하며 data I/O·내구 barrier 동안 page latch를 잡지 않는다. process 사망으로 중간 공유 상태가 남으면 다른 writer가 추측해 이어 쓰지 않고 복구 차단으로 전환한다.

### version 가시성과 논리 abort

행 version은 `(page ID, page generation, slot, slot generation)`으로 참조한다. 만드는 transaction ID, 이전 version 참조, table/row ID, payload 길이와 tombstone을 보존한다. UPDATE와 DELETE는 이전 확정 payload를 덮지 않고 새 version 또는 tombstone을 추가한다. 여러 번 갱신한 자기 행은 자기 최신 version을 읽는다. 다른 거래는 미확정·aborted version을 건너뛰고 읽기 기준에 맞는 확정 version을 찾는다. unique/key index도 역사적 binding을 가시성 기준으로 판정하며 DELETE 때 이전 snapshot이 쓰는 binding을 즉시 제거하지 않는다.

SNAPSHOT은 시작 시의 확정 순번 `S`를 고정하고 자기 변경 또는 commit 순번이 `S` 이하인 version만 읽는다. 쓰기 잠금을 기다린 뒤 대상이 `S` 이후 바뀌었다면 실패 거래로 전환하며 최신 값을 섞거나 앞선 결과를 재실행하지 않는다. serializable 경로는 key·빈 범위·predicate/table·schema의 호환 잠금을 얻고 확정 version을 읽으며 끝까지 잠금을 보유한다. 두 격리를 같은 snapshot 규칙으로 구현하지 않는다. 기존 API가 지원하지 않는 새 격리는 명시적인 후보 진입점으로만 사용한다.

commit 순번은 로그의 commit 순서로 예약한다. `durable LSN`이 해당 commit record의 끝을 지난 뒤 공유 transaction 표에 한 번 공개하고 잠금을 푼다. reader에 제공할 확정 frontier는 앞선 예약의 commit·abort 판정까지 끝난 연속 구간이다. 조정자는 이 순서로 공개를 마친 뒤 해당 거래의 성공을 응답한다. 최대 예약 번호를 그대로 snapshot 순번으로 쓰지 않는다. commit을 포함한 group의 force 실패는 성공이나 미적용으로 바꾸지 않고 UNKNOWN·DB fencing으로 끝낸다. 이미 성공한 앞 group의 거래까지 실패로 덮지 않는다.

abort는 transaction 표를 비가시 상태로 만드는 논리 동작이다. 새 version과 구조상 남은 slot은 GC 대상이며 이전 확정 값은 계속 읽을 수 있어야 한다. split·할당·root 변경은 완전한 physical group이면 redo하며, 미완성 group은 적용하지 않는다. 사용자 abort를 이유로 옛 page 전체를 복원하면 다른 writer의 변경을 지울 수 있으므로 그렇게 처리하지 않는다. 제약·문장 오류 뒤에는 기존처럼 거래를 실패 상태로 두고 명시적 rollback만 허용한다.

첫 후보의 NO-STEAL은 frame에 미확정 version·구조 변경의 의존 거래가 하나라도 있으면 writeback·dirty eviction을 금지한다. 의존 거래의 COMMIT/ABORT가 내구화된 뒤에만 이 의존성을 해제하며 그 terminal LSN도 writeback의 required LSN에 포함한다. frame 공간이 없으면 거래 전체를 자동 분할하거나 무제한 pin·spill로 우회하지 않고 예약을 거절한다. 독립 거래의 commit은 가능해도 같은 frame의 미확정 writer가 공간을 붙잡는 비용은 남는다. 이 정책이 공정성·메모리 기준을 못 맞추면 후보 결함으로 기록한다. STEAL로 바꾸려면 영속 transaction 판정·이전 값·index/구조 redo·재시작 비가시성·GC 시험을 먼저 통과시키고 계약을 별도로 수정한다. 아직 없는 증명을 조건문 하나로 대신하지 않는다.

| 선택지 | 선택과 이유 | 불리해지는 조건·확인 |
|---|---|---|
| 기존 값 제자리 변경 + redo만 저장 | 제외, commit 전 덮은 이전 값을 abort 때 복구할 수 없음 | abort·종료 뒤 이전 값이 사라지는 반례로 거절 |
| undo/redo와 보상 record | 대안 유지, 제자리 변경의 undo·재중단 복구가 추가됨 | 큰 거래의 NO-STEAL 압력이 지배적이면 재검토, 보상 중 재종료까지 필요 |
| 불변 version + 논리 abort + NO-STEAL | 첫 후보로 선택, 이전 값 보존과 runtime writeback 규칙을 분리 | 같은 leaf의 미확정 writer·긴 거래에서 공간·대기 악화 측정 |
| 불변 version + 미확정 writeback | 첫 후보에서 제외, transaction 표·index·GC의 영속 증명 추가 필요 | 안전 게이트와 변경 계약 없이 성능 때문에 활성화 금지 |

### page와 WAL의 형식 불변식

형식 숫자는 압축하지 않은 내부 비교 형식 1의 선택이다. 모든 정수는 little-endian이며 구조체를 그대로 dump하지 않는다. 길이와 offset의 덧셈·곱셈은 overflow를 먼저 검사한다. 알 수 없는 version·record 종류·flag는 무시하지 않고 unsupported로 거절한다. checksum은 손상 탐지이며 인증이나 lost-write 검출 보장이 아니다.

| 단위 | 고정 필드와 길이 | 검사·복구 불변식 |
|---|---|---|
| data page, 8192B | header 64B: UUID 16B, magic `SLPG` 4B, version/type 각 u16, page ID/generation/pageLSN 각 u64, body 길이 u32, CRC32C u32, 예약 8B | checksum 필드를 0으로 한 전체 page의 CRC, 예약·본문 뒤 padding 0, UUID·PageRef·종류 일치 |
| slot과 version | slot 8B: offset/길이 각 u16, slot generation u32. version header 72B: transaction ID/frozen CSN 각 u64, 이전 VersionRef 24B, table/row ID 각 u64, payload 길이 u64, flags/reserved 각 u32 | slot 범위·중첩·generation 검사, frozen CSN 0은 transaction 표 필요. tombstone payload 0, 이전 참조 cycle·잘못된 길이 거절 |
| 큰 값 | version 본문에 inline 또는 overflow tag와 전체 길이·첫 PageRef, overflow page에 다음 PageRef·chunk 길이·bytes | 원 SQL 값 codec의 NULL·한글·BLOB·9000B 값 보존, chunk 합계·중복/cycle·EOF 검사, 잘라서 성공 금지 |
| WAL segment | 64B header: magic `SLWAL001` 8B, version/header 길이 각 u16, flags u32, UUID 16B, segment 번호/시작 LSN 각 u64, CRC32C u32, 예약 12B | 4MiB segment, header CRC와 단조 번호·연속 LSN 확인. record는 segment를 넘지 않고 남은 영역은 명시적 padding |
| WAL record | header 80B: magic `SLWR` 4B, version/type 각 u16, 전체/본문 길이 각 u32, 시작 LSN/transaction ID/이전 transaction LSN 각 u64, UUID 16B, owner epoch u64, flags/CRC32C 각 u32, 예약 8B | 전체 길이는 8B 정렬, 96B 이상 1MiB 이하. header의 CRC를 0으로 하여 header·본문·0 padding·footer 전체를 검사 |
| record footer, 16B | 전체 길이 u32, magic `SLWE` 4B, 시작 LSN u64 | 앞 길이·LSN과 일치, 다음 record offset은 현재 끝. LSN은 segment header/padding을 포함한 절대 byte 위치이며 wrap 금지 |
| control slot | 별도 control 파일의 두 8192B slot, magic `SLCT`, version·UUID·backend·control 세대·owner epoch·checkpoint 위치·redo 시작·ID 예약 highwater·요청 epoch/floor·CRC | 세대 큰 유효 slot부터 검증, 해당 checkpoint와 redo 범위를 모두 읽을 수 있어야 사용. data root만 열어 정상으로 추정 금지 |

`PageRef`는 page ID와 generation의 u64 두 개다. `VersionRef`는 여기에 slot과 slot generation의 u32 두 개를 더한다. 0 참조는 없음이며 실제 ID·generation은 1부터 시작한다. transaction ID와 page/slot generation은 재사용하지 않는다. 예약 highwater를 내구화한 뒤 번호를 발급하고 재시작 때 남은 예약 구간은 건너뛴다. counter 소진은 LIMIT이며 wrap하지 않는다. owner epoch는 파일 복구 권한의 세대이고 transaction ID·요청 epoch와 다른 필드다.

[후보 control](../../include/storage/candidate_control.h)은 별도 파일의 두 8192B slot을 사용한다. 각 slot의 192B header는 magic `SLCT` 4B·version/header 길이 각 u16·UUID 16B·backend/slot 각 u32로 시작한다. 이어 generation·owner epoch·checkpoint ID·checkpoint begin/end·redo begin·transaction/page highwater·요청 epoch/floor·visible CSN의 u64 열한 개, outcome/할당/catalog의 PageRef 세 개, GC horizon u64·CRC32C u32·예약 12B를 둔다. 나머지 padding은 0이며 CRC 위치 176–179B를 0으로 한 전체 slot을 검사한다. generation 1은 slot 0이고 `(generation-1)%2`로 위치를 고정한다. WAL backend 1은 checkpoint/redo LSN을 사용하고 buffered COW backend 2는 세 LSN을 0으로 둔다. COW의 checkpoint ID와 root 내구성은 별도로 검증하며 가짜 WAL 비용을 부과하지 않는다. 선택 SQL catalog root는 0일 수 있지만 outcome·할당 root는 양수이고 서로 다른 page다.

[control 파일 모듈](../../src/storage/candidate_control.c)은 디렉터리 fd·control inode와 배타 flock을 보유한다. 입력 options는 allocator callback 전에 복사한다. create는 새 16384B 파일만 만들고 파일·디렉터리를 fsync한다. 아직 anchor가 없으므로 그대로 닫은 파일은 정상 복구 기준으로 열리지 않는다. open은 높은 유효 세대부터 필수 verifier를 호출한다. verifier가 CORRUPT를 반환한 경우만 이전 anchor를 검사하며 IO·OOM·LIMIT·UNSUPPORTED를 fallback 성공으로 감추지 않는다. 이 verifier는 안정된 view에서 checkpoint 묶음·WAL 범위·data/root 정합을 검사해야 한다. 현재 native 시험의 합성 verifier는 그 전체 구현 증거가 아니며 control 선택은 일반 DB 진입이나 APPLIED를 제공하지 않는다.

publish는 검증된 기존 anchor의 반대 slot만 쓰고 fsync 후 메모리 선택을 갱신한다. 최신 slot의 의존성이 불량해 옛 anchor를 선택했다면 옛 slot을 보존하고 불량쪽에 더 큰 같은 parity 세대를 쓴다. 따라서 세대 간격은 1보다 클 수 있다. 관측한 두 slot의 highwater·epoch/floor·frontier·GC·WAL 위치는 퇴행시키지 않으며 redo 하한은 선택한 slot 하나가 아닌 두 유효 slot의 최솟값이다. info의 valid_mask는 CRC·형식 검증, verified_mask는 호출한 verifier의 성공을 뜻한다. redo 하한은 의존성 불량으로 선택하지 않은 구조 유효 slot도 포함해 보수적으로 계산한다. slot 참조와 로그를 실제로 회수하는 기능은 아직 없다. 쓰기 시도 뒤 오류·fsync 실패·관측한 경로 교체는 handle poison이며 성공/미적용을 응답하지 않는다. 짧은 쓰기 오류 뒤에도 header와 기존 0 padding이 합쳐 새 slot이 유효할 수 있으므로 재열기 결과를 미리 단정하지 않는다. 상위 DB fencing과 system 예약/epoch 전이·일반 재생/활성화는 후속 조정자의 책임이다.

[page codec](../../include/storage/candidate_page.h)은 type 1을 version page, type 2를 overflow page로 정의한다. type 4 metadata와 type 5 할당 본문은 아래의 별도 codec이 검사하며 type 3 본문은 아직 지원하지 않는다. version page 본문은 slot 수 u32·예약 u32 뒤 8B slot 표와 slot 순서의 조밀한 record다. slot offset은 page 시작 기준이며 빈 영역·겹침·뒤의 숨은 bytes를 허용하지 않는다. 슬롯은 최대 101개이며 실제 수용량은 각 record 길이로 제한한다. 기존 witness의 type 1–5 공통 header 검사만으로 이 본문 형식을 통과한 것은 아니다.

live version의 72B header 뒤에는 저장 tag u32·예약 u32·논리 bytes u64·첫 PageRef 16B의 32B envelope를 둔다. tag 1은 PageRef 0과 inline bytes, tag 2는 양수 PageRef와 overflow를 사용한다. 단일 slot의 inline 최대는 8008B다. tombstone은 flags 1·payload 길이 0인 72B record이며 envelope가 없다. 다른 flag·예약 값은 unsupported다. codec은 즉시 자기 자신을 가리키는 이전 VersionRef를 거절하지만 전체 version 가시성·이전 chain cycle·transaction 표는 후속 MVCC 계층의 책임이다.

overflow 본문은 다음 PageRef 16B·chunk 길이 u32·예약 u32 뒤 bytes다. 마지막 전 chunk는 정확히 8104B이며 마지막은 1–8104B다. [본문 reader](../../src/storage/candidate_result.c)는 요청한 page/slot generation·transaction·전체 길이, 중복 page·cycle·EOF·초과 연결과 전체 SHA-256을 검사한 뒤에만 소유 사본을 반환한다. 선택한 응답은 최대 1MiB이며 이 제한은 일반 SQL 값 전체의 새 제한이 아니다. 할당·loader·digest 실패에는 부분 사본을 반환하지 않는다. frozen CSN 0의 raw bytes를 읽는 것과 그 version의 가시성·commit 판정은 구분한다.

[내부 SHA-256](../../src/storage/candidate_digest.c)은 [FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf)의 계산 형식과 [NIST 예제](https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf)를 참고한다. 외부 crypto 라이브러리를 추가하지 않고 본문 digest만 계산한다. 인증·FIPS 검증 모듈이나 전체 DB 복구 검증을 제공하지 않는다. [codec 시험](../../tests/test_candidate_page.c)과 [WAL/page 시험](../../tests/test_candidate_result_wal.c)은 이 범위의 실제 bytes를 검증한다. metadata 조회와 그 기록의 내구 게시·복구 소유권·정상 진입 허용은 구분한다.

[요청 metadata codec](../../include/storage/candidate_catalog.h)은 같은 공통 header의 type 4를 쓴다. 본문은 magic `SLMD` 4B·version/kind 각 u16로 시작하며 kind 1은 root, 2는 outcome node다. root의 고정 본문 72B는 이 8B prefix와 owner epoch·현재 요청 epoch·보존 floor·transaction/page highwater·visible CSN·retained bytes의 u64 일곱 개, outcome 수 u32·예약 u32다. 뒤에는 `ceil(count/46)`개의 직접 PageRef가 온다. 연결 리스트 대신 최대 90개 직접 참조를 선택해 COW에서 바뀐 node 앞부분까지 다시 쓰는 비용을 만들지 않는다. node는 prefix 뒤 ordinal/count 각 u32·예약 16B, 최대 46개 record이며 전체 본문은 `32 + count*176`B다. 마지막을 제외한 node는 46개씩 채우고 transaction ID 오름차순으로 배치한다. CSN은 거래 시작 순서와 다를 수 있다.

각 176B outcome은 요청 epoch u64·nonce 16B·operation digest 32B·transaction ID u64·상태 u32·예약 u32·CSN/terminal 끝 LSN 각 u64·결과 VersionRef 24B·결과 길이 u64·digest 32B·chunk 수 u32·예약 u32·선언 결과 상한/입장 owner epoch 각 u64다. 상태 1 ADMITTED에는 아직 terminal·결과가 없고, 2 COMMITTED는 양수 CSN/terminal과 빈 결과에도 실제 VersionRef를 요구한다. 3 ABORTED는 CSN 0과 양수 terminal을 쓴다. terminal은 8B 정렬, 결과는 최대 1MiB·64KiB chunk 수와 일치해야 하며 빈 결과 digest는 SHA-256(empty)다. nonce 0은 유효하다. root의 숫자는 자체 일관성 검사 대상이며 durable ID 예약·연속 가시성·보존 종료의 증명이 아니다.

[catalog loader](../../src/storage/candidate_catalog.c)는 최대 4epoch·epoch당 1024개·전체 4096개의 outcome을 검증한 뒤 소유 표를 반환한다. 중복 요청 키(epoch, nonce)·CSN·page ID, 잘못된 세대·ordinal/count·root보다 뒤인 node LSN·상한 불일치와 부분 읽기를 거절한다. 이번 빌드의 고정 소유 요청량은 788,000B이며 loader·stack·본문 사본·allocator overhead·RSS는 별도다. 결과 논리 길이와 metadata page 수는 retained bytes의 하한으로 검사할 뿐 실제 파일 64MiB quota를 구현한 것은 아니다. 두 후보가 같은 codec·조회·보존 비용을 부담해야 한다. loader는 caller가 확보한 안정된 page view를 읽으며 동시 게시·복구 배제는 후속 조정자의 책임이다.

조회는 UUID·epoch·nonce·operation digest를 받아 `MATCH`, `NOT_FOUND`, `OUTSIDE_RETENTION`, `CONTENT_MISMATCH`, `INVALID_ID`, `UNAVAILABLE`을 구분한다. MATCH는 catalog 수명 동안 빌리는 원시 기록이며 본문·WAL corroboration이나 APPLIED·NOT_APPLIED·현재 owner의 IN_PROGRESS 판정이 아니다. 없는 ID·보존 범위 밖의 ID로 SQL/callback을 재실행하지 않는다. system 예약/epoch 전이·내구 terminal 게시·floor 회수와 control/할당 이력을 포함한 일반 복구 조정자는 아직 구현 대상이다. [metadata 시험](../../tests/test_candidate_catalog.c)은 이 조회 경계와 부분 실패·bounded 소유권을 검증한다.

segment 끝의 padding은 남은 bytes 전부를 0으로 채우는 별도 영역이다. decoder는 잔여 영역 전체가 0이고 다음 segment의 번호·시작 LSN·header가 연속일 때만 이를 건너뛴다. 잘못된 record 길이를 padding으로 재해석하지 않는다. EOF의 미완료 tail은 다음 segment로 이어지는 정상 padding과 구분해 기록한다.

| record type 번호 | 본문이 담는 값 | 완료 판정 |
|---|---|---|
| 1 ADMIT, 2 BEGIN | 요청 ID·operation digest·응답 예약 상한 또는 transaction의 격리·snapshot 순번 | ADMIT은 변경보다 먼저 append하지만 단독 force를 필수로 삼지 않음. commit의 force가 앞선 ADMIT까지 포함 |
| 3 PHYS_BEGIN, 8 PHYS_END | group ID, page/record 수, 끝에는 전체 group digest | group의 모든 page record가 있고 순서·digest가 맞아야 적용, 불완전 group 무시 |
| 4 PAGE_IMAGE, 5 PAGE_DELTA | group ID, PageRef, 기대 이전 pageLSN, 완성 page image 또는 offset/길이/bytes 목록 | pageLSN은 PHYS_END의 끝 LSN, 검증된 동일/후속 세대의 이미 반영된 변경만 건너뜀. 증명 없는 generation 불일치·기대 이전 LSN 공백은 손상 |
| 6 ALLOC, 7 FREE | group ID, PageRef, 이전/새 할당 세대와 참조 해제 증거의 horizon | 할당 표·참조 변경과 같은 physical group, page 번호만으로 재사용 금지 |
| 9 RESULT_CHUNK | 요청 ID, chunk 순번·전체 bytes·digest, 본문이 있는 VersionRef와 chunk offset/길이 | 본문은 최대 64KiB씩 보통 version/overflow page에 보존하고 physical group이 보호, commit의 참조·count·길이·digest 전부 확인 |
| 10 COMMIT, 11 ABORT | transaction의 첫/마지막 record LSN·개수·chain digest, COMMIT에는 CSN·의존 physical group ID/끝 LSN 목록·요청 ID·operation/result digest·chunk 수 | commit과 모든 의존 group·결과가 유효해야 APPLIED, ABORT 뒤 COMMIT·중복 CSN·끊긴 chain 거절 |
| 12 CHECKPOINT_BEGIN, 13 CHECKPOINT_CHUNK, 14 CHECKPOINT_END | checkpoint ID·redo 시작, dirty/active/outcome/할당 상태 chunk, 끝 count·digest | END가 내구화되고 control이 가리킨 완전 묶음만 시작점 |
| 15 REQUEST_EPOCH, 16 ID_RESERVE | 요청 epoch/floor 변경 또는 transaction/page 번호의 새 highwater | 단조 증가·내구 순서 확인, 활성 요청을 넘는 floor 갱신 거절 |

[할당 codec](../../include/storage/candidate_allocation.h)은 type 5 본문을 magic `SLAL` 4B·version/kind 각 u16로 시작한다. kind 1 root는 이 prefix 뒤 owner epoch·page highwater·GC horizon 각 u64·count/reserved 각 u32의 고정 40B와 직접 node PageRef를 둔다. kind 2 node는 prefix·ordinal/count 각 u32·예약 16B의 32B 뒤 최대 202개 기록을 담는다. 각 40B 기록은 page ID/generation 각 u64·상태/reserved 각 u32·release horizon/마지막 transition LSN 각 u64다. LIVE 1은 horizon 0, FREE 2는 양수 horizon을 사용한다. 정렬한 희소 표에 마지막 FREE도 남겨 재사용 뒤 generation을 1로 되돌리지 않는다. 새 번호는 generation 1, FREE 뒤 재할당은 이전 generation+1이며 소진은 LIMIT다.

내부 비교 설정은 총 65,536개 lifetime page identity·최대 325개 직접 node 참조다. 8192B page 512MiB에 해당하는 identity 수이며 실제 data 파일 크기·quota나 고객 SLO가 아니다. 더 큰 표는 LIMIT로 거절한다. [loader](../../src/storage/candidate_allocation.c)는 전체 정렬·중복·highwater·horizon·page/transition LSN과 root/node 자신의 정확한 LIVE 세대를 대조한다. 이 빌드의 고정 소유 요청량은 2,626,736B이며 stack·loader·allocator overhead·RSS·전체 앱 cap은 별도다. 안정된 loader view는 caller 책임이고 부분 적재/OOM에는 표를 반환하지 않는다. FREE를 버려 공간을 줄이는 안은 generation 이력 단절 때문에 사용하지 않는다. 직접 root 참조와 같은 기록 형식을 buffered COW에도 적용한다.

메모리 batch는 최대 64개 정렬한 서로 다른 ID를 받고 이전 상태·generation과 다음 highwater/GC horizon·LSN을 전부 확인한 뒤 반영한다. 하나라도 틀리면 전체 상태를 보존한다. 원 snapshot root/node의 FREE는 새 anchor를 load하기 전까지 거절한다. WAL의 양수 LSN은 엄격히 증가하고 COW snapshot은 LSN 0을 유지할 수 있다. COW의 내구 게시 순서는 별도 checkpoint ID/control 검증에 맡겨 가짜 WAL을 만들지 않는다. `find`/`at`는 원시 기록 사본만 반환하며 없는 ID는 안전한 발급·미적용의 증거가 아니다. batch 자체는 완전한 WAL system group·번호 예약 force·reader/fallback 참조 해제나 안전 horizon을 증명하지 않는다. 실제 삭제·발급·page redo·복구 활성화는 후속 조정자 의무다. [할당 시험](../../tests/test_candidate_allocation.c)은 이 산술/소유권 경계와 합성 파일 재열기를 검증한다.

REQUEST_EPOCH와 ID_RESERVE는 대응 metadata page 변경과 같은 system physical group에 넣는다. 조정자는 그 PHYS_END까지 force한 뒤 floor 회수나 새 번호 발급을 허용한다. 단독 record가 append됐다는 이유로 예약·회수를 진행하지 않는다. 다른 process의 번호 예약도 같은 제어 gate에서 순서를 정한다.

PAGE_IMAGE는 checkpoint 이후 처음 바꾸는 page의 변경 후 전체 사본이며 해당 physical group의 다른 변경과 함께 적용한다. 새 page에도 image가 필요하다. 그 뒤 delta의 순서와 기대 이전 pageLSN을 확인한다. torn page는 유효한 image부터 재생하고 checksum 실패를 무조건 빈 page로 취급하지 않는다. 최소 한 개의 유효 checkpoint와 그 redo 범위를 남겨 image 없는 손상 page를 감추지 않는다. [PostgreSQL 18 WAL 설명](https://www.postgresql.org/docs/18/wal-internals.html)은 LSN·checkpoint·전체 page image의 참고이며 이 자체 형식의 구현 증거는 아니다.

page 번호가 재사용되면 디스크 page가 재생 중인 record보다 새 generation일 수 있다. 복구 분석에서 checkpoint의 할당 표와 완전한 ALLOC/FREE group으로 세대의 연속 이력을 먼저 확인한다. 그 이력이 증명한 후속 generation과 유효 pageLSN이면 옛 generation의 redo를 건너뛴다. 같은 generation의 delta는 기대 이전 LSN을 요구하고 새 generation은 해당 ALLOC와 image부터 시작한다. generation 숫자가 크다는 이유만으로 정상이라 추측하거나, 정상 재사용을 모두 손상으로 거절하지 않는다. 모든 redo가 끝난 뒤 tree·index·version 참조의 도달성과 live/free 분리를 검사한다.

최종 segment의 불완전 record/group은 검증된 끝까지만 보존하며 미완료 거래는 확정하지 않는다. checksum이 틀린 마지막 record도 마지막 물리 tail이고 이후 유효 record·commit 증거가 없음을 확인할 때만 불완전 tail로 분류한다. 중간 checksum 손상, segment 공백, 뒤의 유효 commit, 의존 group 누락, 검증된 로그 끝을 넘는 pageLSN은 CORRUPT로 차단한다. 성공 뒤 장치가 잃은 마지막 쓰기를 항상 탐지할 수 있다는 주장은 하지 않는다.

### checkpoint와 회수 순서

[checkpoint codec과 reader](../../include/storage/candidate_checkpoint.h)는 control이 지정한 CHECKPOINT_BEGIN/CHUNK/END의 선언 상태를 검증한다. 본문은 little-endian `SLCP`·version1을 사용한다. BEGIN160B는 magic/version/kind0의8B·UUID16B, checkpoint ID·redo 시작·owner·transaction/page highwater·요청 epoch/floor·visible CSN·GC horizon과 outcome/할당/catalog PageRef의 u64 열다섯 개, dirty/transaction/chunk count와 예약의 u32 네 개다. control의 대응 값과 정확히 같아야 한다. control generation/slot과 begin/end 위치는 본문에 복제하지 않고 선택한 anchor와 record 위치로 묶는다.

CHUNK는 magic/version/kind·checkpoint ID·전역 ordinal/count·예약8B의32B prefix 뒤에 최대128개 entry를 둔다. kind1 dirty entry24B는 PageRef·recLSN이다. kind2 transaction entry56B는 ID·first/last LSN·terminal 끝 LSN·CSN·owner의 u64 여섯 개와 상태/예약의 u32 두 개다. 상태1은 active이며 terminal/CSN은0, 상태2는 commit이며 양수 CSN, 상태3은 abort이며 CSN0이다. terminal 상태의 last LSN은 terminal record 위치이고 끝 LSN보다 작다. ABORT의 끝 차이는128B, COMMIT은248B 이상·16B 의존 참조 간격이며 record 한도를 넘지 않는다. terminal은 첫 record일 수 없고 첫/마지막 record가 다르면 최소112B 떨어진다. 이 수치 검사는 실제 terminal record의 존재·이력 대조와 별도다. ID는 각 표에서 정렬·유일하며 commit CSN도 유일하지만 transaction ID 순서대로 증가할 필요는 없다. active firstLSN과 모든 dirty recLSN보다 redo 시작이 뒤일 수 없다. 활성 거래는 최대64개다.

dirty chunk를 먼저, transaction chunk를 다음에 놓고 마지막 chunk만128개보다 작게 둔다. 전체8192개 dirty/4096개 transaction·96chunk를 초과하면 LIMIT이며 잘라서 성공하지 않는다. dirty 상한은 page 사본의64MiB/8192B 관계에서 정한 내부 snapshot 형식 상한이며 pool 전체/RSS의 보장이 아니다. lifetime 할당/outcome 기록은 일반 metadata root로 보존하고 chunk에 중복 저장하지 않는다. END64B는 magic/version/kind3의8B·checkpoint ID/begin LSN의16B·chunk 수/예약의8B·digest32B다. 초기0인32B digest 뒤에 각 BEGIN/CHUNK payload를 붙여 SHA-256하고 그 결과를 다음 입력으로 쓴다. 전체 payload를 한 번 더 모으는 buffer나 새 streaming hash 인터페이스 대신 기존 one-shot helper로 scratch를 제한한다.

[owning reader](../../src/storage/candidate_checkpoint.c)는 같은 checkpoint의 previousLSN·owner·UUID·정확한 anchor begin/end, 개수·순서·범위·digest를 확인한다. 일반 WAL record는 checkpoint 사이에 끼어들 수 있으며 별도 거래 의미를 이 reader가 판정하지 않는다. 같은 구간의 다른 checkpoint는 허용하지 않는다. END 이후의 I/O·frame 손상도 포함해 전체 WAL scan이 성공한 뒤에만 현재 빌드의 한 할당426,320B로 snapshot을 반환한다. transport·stack·allocator overhead·RSS는 별도다. 미지원 형식/예약 값은 UNSUPPORTED이며 control fallback으로 감추지 않는다. [시험](../../tests/test_candidate_checkpoint.c)의 실제 WAL/control 재열기는 이 부분 verifier를 검증한다. 선언된 표의 산술 완전성과 전체 data/terminal 이력의 완전성은 다르다. root loading·page redo·실제 fuzzy writeback/force·로그 회수·결과 판정·배타 복구 활성화는 후속 조정자와 buffer 구현의 책임이다.

명시적 `candidate_wal_witness_analyze_checkpoint`는 system 모드에서 checkpoint를 소유한 뒤 보존된 WAL 전체를 다시 스캔한다. BEGIN 직전의 실제 거래와 선언된 ID·first/last LSN·terminal 끝·CSN·첫 record의 owner·상태를 대조한다. 복구 ABORT의 owner가 새 epoch여도 거래의 creator owner와 구분한다. 선언한 phantom 거래와 빠진 활성 거래를 거절한다. 참조가 끝난 terminal 거래의 생략은 허용하므로 모든 미확정 CSN 참조의 terminal 보존 여부는 후속 조정자가 검증해야 한다. 관측한 거래 ID·최대 CSN·owner와 알려진 system highwater/epoch/floor도 anchor에 대조한다.

각 dirty recLSN은 BEGIN 전에 완성된 physical group의 시작이어야 하며 그 group이 같은 PageRef를 실제로 포함해야 한다. group당64개 참조와8192개 일치 여부만 추가로 보관한다. checkpoint chunk 사이에 거래가 commit하거나 system 상태가 바뀌어도 BEGIN 시점과 이후 최종 witness를 구분한다. 두 번째 scan에서도 선택한 BEGIN/CHUNK/END bytes와 chain을 다시 대조하고 END 뒤의 거래 오류·I/O 실패에도 결과를 반환하지 않는다. 선택 구간 밖 transaction0 checkpoint는 상태 변경이 없는 미사용 metadata로 건너뛰며 그 본문의 의미 검증은 주장하지 않는다. 미완성 physical group 안의 checkpoint나 사용자 transaction의 checkpoint는 거절한다.

[대조 시험](../../tests/test_candidate_wal_checkpoint.c)은 실제 거래·group이 있는8192개 dirty/4096개 거래·96chunk와64개 활성 거래의 경계를 확인한다. 이 빌드의 소유 요청량 최고치는 세 할당 합계2,108,056B이고 checkpoint를 해제한 뒤 반환하는 system witness는1,681,736B다. transport·stack·allocator overhead·전체 앱 RSS는 별도다. 기존 `analyze`의 구조체 배치와 할당량은 바꾸지 않는다. 이 진입점은 처음부터 보존된 전체 이력을 요구하며 이미 회수한 이력을 checkpoint로 seed하는 복구는 지원하지 않는다. data/root·전체 할당·도달성/pin·terminal 참조 완전성, redo·내구 발급·APPLIED·배타 복구 활성화는 계속 후속 조정자의 책임이다.

[비교용 WAL 파일](../build-and-test.md#비교용-wal-파일)의 로컬 transport는 위 record/segment 형식과 순차 append·force·재시작 scan을 구현한다. 이 계층은 16개 record 종류의 본문을 opaque bytes로 취급한다. [witness 분석기](../../include/storage/candidate_wal_witness.h)는 별도로 거래 chain·terminal·CSN 순서, image/delta physical group의 개수·checksum과 commit 의존 참조를 검사한다. 결과 본문·실제 redo·영속 ID 조회와 복구 완료를 판정하지 않는다. 마지막의 불완전 길이는 명시적 tail 수리 대상으로, 완전 길이의 checksum 손상은 보수적으로 CORRUPT로 처리한다. 유효 prefix visitor는 전체 scan 성공 전에 외부 효과를 확정하지 않는다. 이 부분 구현과 아래 checkpoint·가시성·결과 복구의 미완료 범위를 구분한다.

이 내부 분석기는 아래 본문 배치를 사용한다. 정수는 앞의 little-endian 규칙을 따르고 예약 bytes는 0이다. ADMIT의 operation digest와 RESULT_CHUNK의 결과 digest는 32B 참조 값이며 분석기는 SQL 선언이나 실제 응답 bytes의 SHA-256을 계산하지 않는다. 이 값을 서로 대조한 것만으로 본문 무결성을 확인했다고 표시하지 않는다.

파일 transport는 create/open에서 디렉터리 fd를 고정하고 segment마다 device/inode와 열린 fd를 보유한다. 상대 prefix로 연 뒤 cwd가 바뀌어도 inventory·scan·회전·force·tail 수리는 그 디렉터리를 사용한다. 절대 경로 문자열만 보존하는 안은 이후 같은 경로가 다른 디렉터리를 가리킬 수 있어 채택하지 않는다. 보유 fd는 inode가 재사용되는 동안 같은 식별자로 오인하는 것을 막으며 최대 64개 segment에 한정한다. 디렉터리·lock/active·임시 scan fd의 비용도 별도로 남는다.

각 작업의 진입과 append·force·scan·회전·tail 수리가 성공하기 전에 보유 segment의 경로·inode를 대조한다. 누락·교체를 관측하면 IO와 poison으로 차단하며, 다른 파일을 동기화한 결과로 durable LSN을 올리지 않는다. 이 검사는 외부 파일 변경을 허용하는 프로토콜이나 악의적 경로 경합의 원자적 배제가 아니다. 외부 제자리 편집과 파일 교체는 지원하지 않는다. [파일 소유권 시험](../../tests/test_candidate_wal_files.c)은 cwd 변경 뒤 같은 이름·다른 UUID의 파일, 같은 UUID·같은 bytes의 inode 교체와 force 도중 교체를 대조한다. fd 보유와 작업별 metadata 조회 비용은 후속 총비용 비교에 포함한다.

| record | 분석하는 본문 배치 | 검사 범위 |
|---|---|---|
| ADMIT, 64B | 요청 epoch u64·nonce 16B·operation digest 32B·결과 상한 u64 | 첫 record, 중복 요청 ID 거절, 1MiB 이하 상한 |
| BEGIN, 16B | 격리 u32·예약 u32·snapshot u64 | 격리 0은 SNAPSHOT, 1은 serializable 후보. 중복 BEGIN 거절, serializable의 snapshot은 0 |
| PHYS_BEGIN, 16B / PHYS_END, 24B | group ID u64·page 수 u32·record 수 u32, 끝에는 CRC32C u32·예약 u32 추가 | 연속 group, 단조 group ID·개수·checksum과 END 위치 대조 |
| ALLOC / FREE, 40B | group ID·page ID·이전/새 generation·release horizon 각 u64 | 새 ID는0→1, 재사용은 이전 FREE의 generation+1, FREE는 같은 세대와 양수 horizon |
| ID_RESERVE, 40B | group ID·이전/새 transaction highwater·이전/새 page highwater 각 u64 | 두 값이 퇴행하지 않고 하나 이상 증가, catalog/할당 root의 새 값과 대조 |
| REQUEST_EPOCH, 40B | group ID·이전/새 요청 epoch·이전/새 보존 floor 각 u64 | 단조 값·최대4epoch·catalog root·관측한 미완료 요청의 floor pin 대조 |
| PAGE_IMAGE / PAGE_DELTA | 앞 32B는 group ID·page ID·generation·기대 이전 pageLSN의 u64 네 개. image는 8192B 사본, delta는 개수 u32·예약 u32 뒤 offset u32·길이 u32·bytes 반복 | image UUID·종류 1–5·header/padding/checksum·END LSN, delta의 page 본문 범위·겹침·정렬·정확 길이. 실제 page 이력·redo는 별도 |
| RESULT_CHUNK, 112B | 요청 epoch u64·nonce 16B·chunk 순번/수 각 u32·전체 길이 u64·digest 32B·VersionRef 24B·offset u64·길이 u32·예약 u32 | 64KiB 분할의 연속 순번·합계·동일 참조 digest, 최대 16chunk. 참조한 본문 존재와 hash는 별도 |
| ABORT, 32B / COMMIT, 152B 이상 | 첫/직전 record LSN·앞선 record 수 각 u64·chain CRC32C u32·예약 u32. COMMIT은 CSN u64·요청 epoch u64·nonce 16B·operation/result digest 각 32B·결과 bytes u64·chunk 수 u32·예약 8B·의존 group 수 u32·group ID/끝 LSN의 u64 쌍 반복 | terminal 이후 record·끊긴 chain·중복/역행 CSN·빠진 group·결과 참조 불일치 거절 |

chain과 group CRC는 record마다 종류 u16·예약 6B·LSN/transaction ID/이전 LSN/owner epoch/본문 길이의 u64 다섯 개·본문 bytes를 이어 계산한다. chain은 terminal 직전까지, group은 PHYS_BEGIN과 그 안의 page 변경·할당/해제·예약/epoch record를 포함한다. 비어 있는 응답의 참조 digest는 SHA-256의 빈 bytes 값이다. 이 CRC는 손상 검사이며 SHA-256이나 인증을 대신하지 않는다.

분석기는 같은 UUID로 연 transport와 options를 받으며 한 group에서 page별 image 또는 delta 하나를 처리한다. 기본 모드는 ALLOC/FREE·system transaction·checkpoint·epoch/floor·ID_RESERVE를 UNSUPPORTED로 거절한다. 내부 옵션 `enable_system_groups`를 켜면 [system 분석](../../src/storage/candidate_wal_system.c)이 위40B record와 일반 image를 검사한다. system group은 transaction ID0·첫 previous LSN0으로 시작하고 하나의 owner 아래 직전 record를 연결한다. 예약/epoch는 system 전용이고 ALLOC/FREE는 사용자 physical group에도 허용해 논리 ABORT와 구분한다. 기존 `analyze` 진입점은 checkpoint를 계속 거절하며 system delta·type3 image도 아직 지원하지 않는다.

새 모드는 image의 type1/2/4/5 본문을 각 codec으로 검증한다. 예약은 catalog/할당 root highwater와, epoch는 catalog root의 epoch/floor와 대조한다. 할당/해제는 새 할당 root가 참조한 node의 상태·generation·horizon·PHYS_END 끝 LSN을 요구한다. 새 generation에는 기대 이전 LSN0인 전체 image가 필요하다. 같은 page의 중복 event와 관측한 ALLOC/FREE 교대·세대 단절을 거절한다. 이미 본 transaction ID보다 낮은 이전 예약 값, 미완료 admission을 넘는 floor도 거절한다. 끝 record의 개수·CRC와 모든 image LSN이 맞기 전에는 관측 상태나 event 목록을 갱신하지 않는다.

분석 전체에 거래와 완성 group을 각각 최대4096개, 활성 거래64개를 보관한다. system 모드는 group당 최대64개 image/event와 전체4096개 event를 별도로 제한한다. 이 빌드의 활성 system 분석 소유 요청량은 두 할당 합계1,681,736B이며 allocator overhead·stack·transport·전체 앱 RSS는 별도다. options는 allocator callback 전에 복사한다. 더 큰 입력은 LIMIT이고 잘라서 성공하지 않는다. 이 상한은 회수 없는 분석 표를 유한하게 두는 내부 설정이며 전체256MiB WAL을 모두 처리하는 제품 복구 보장이 아니다.

`system_info`와 `system_at`는 관측한 prefix와 완성 group의 원시 기록을 반환한다. 최초 이전 값·미관측 할당/요청·checkpoint seed는 아직 증명하지 않는다. 변경 entry와 해당 node 참조를 검사하는 것과 전체 할당 표·tree/version 도달성·reader/fallback 보호를 대조하는 것은 다르다. 번호 예약 force와 실제 발급, 안전 horizon·회수, data redo·내구 결과·배타 복구 활성화는 후속 조정자가 맡는다. [system 시험](../../tests/test_candidate_wal_system.c)의 실제 WAL force/reopen과 소유 표 검증을 APPLIED나 제품 writer 완료로 확대하지 않는다.

완전한 파일 scan을 통과한 뒤에만 owning 분석 결과를 반환한다. 미완성 거래·group은 미완성으로 남기며 ABORT를 추측하거나 파일을 고치지 않는다. 관측한 최대 CSN은 확정 frontier가 아니고, COMMIT witness는 APPLIED·force 성공·새 reader 가시성의 증명이 아니다. [전용 시험](../../tests/test_candidate_wal_witness.c)은 교차 거래·record 경계·CRC를 다시 맞춘 손상·한도/OOM와 뒤쪽 읽기 오류의 부분 결과 차단을 검증한다. 원래 영속 결과·복구 조정자는 본문과 metadata page, WAL-before-data·내구 terminal·복구 배타 소유권을 함께 검증해야 한다.

[물리 redo 실행기](../../include/storage/candidate_redo.h)는 완성 group의 image와 body delta를 callback target에 복원한다. caller는 target의 UUID·파일 생성 내구성·배타 복구 소유권과 read/write/sync callback을 제공한다. read는 page ID별 전체8192B와 존재 여부를 구분하고 읽기 오류를 부재로 바꾸지 않는다. write는 전체 page를 쓰거나 실패를 반환하며 sync는 target의 파일 barrier를 책임진다. 이 경계는 일반 metadata loader와 같은 target 접근 방식을 재사용하며 별도 파일 소유권 체계를 만들지 않는다. DB membership·control 후보 선택·root와 결과 판정·활성화는 조정자가 담당한다.

의미 검증 witness가 실제로 읽은 모든 record의 정규화된 header/payload를 SHA256 chain에 함께 넣는다. 별도 fingerprint scan만으로 의미 검증 view를 고정하면 그 사이 바뀐 COMMIT을 검증할 수 있으므로 사용하지 않는다. 이어지는 전체 preflight와 apply scan도 같은 chain이어야 한다. preflight는 각 page의 처음 image·이후 expected previousLSN과 generation 연속성, 기존 유효 pageLSN이 같은 PageRef를 포함한 완성 group 끝인지 대조한다. 유효 CRC의 다른 UUID/형식이나 이력에 없는 future LSN은 차단한다. CRC가 깨진 page와 부재 page는 처음부터 보존된 full image로 재구성한다. 이 진입점은 과거 이력을 회수한 checkpoint seed를 지원하지 않는다.

preflight 뒤 WAL 전체 유효 끝을 force하고 완성 group의 최대64개 page를 staging한다. delta는 body에만 적용하며 실제 쓰기에는 같은 generation과 정확한 이전 pageLSN이 필요하다. group의 쓰기 대상 전체를 먼저 형식 검증하고 pageLSN·CRC를 다시 계산한다. 이미 이력으로 확인한 나중 page는 건너뛴다. 완성된 구조 group은 사용자 ABORT/미완료 거래에도 redo하고, 끝나지 않은 group은 data에 쓰지 않는다. 실패는 앞선 group이나 일부 page 쓰기를 남길 수 있으므로 out을0으로 반환하고 target을 정상 reader에 노출하지 않는다. 같은 보존 WAL로 재실행해 부분 쓰기를 복원한다. apply scan 전체와 chain을 다시 통과한 뒤에만 data sync와 물리 실행 결과를 반환한다. 이후 root·할당/참조/pin·terminal·가시성 검증이나 활성화를 대신하지 않는다.

물리 이력은 기존 lifetime 한도65536개 ID를 상한으로 둔131072-slot hash 표를 사용하고 초과는 LIMIT이다. slot마다 전체 page를 보관하는 방식 대신64개 page staging과1MiB hash scratch를 함께 둔다. 이 빌드의 workspace 소유 요청량은7,866,584B다. 의미 검증의 hash/witness와 이후 workspace 수명을 나누며 transport·stack·allocator/RSS는 별도다. [복원 시험](../../tests/test_candidate_redo.c)의 실행 입력은520개 page·두 WAL segment이며65536개 전체 채움의 근거는 아니다. 고정 hash 표 예약·반복 scan·WAL/data barrier·재시작 비용도 이후 COW/WAL 총비용에 포함한다. 실제 메모리·공정성·복구 비용이 불리하면 권고 후보를 다시 판정한다.

writeback은 latch 아래 frame의 bytes·pageLSN·변경 세대를 사본으로 잡고 latch를 놓는다. 미확정 의존성이 없고 WAL의 durable LSN이 해당 physical group 끝 이상인 것을 확인한 뒤 data에 쓴다. 쓰기 중 frame이 바뀌면 이전 사본의 I/O 완료로 최신 dirty 표시를 지우지 않는다. page가 디스크에 도달한 것과 transaction commit은 다른 상태다. NO-STEAL은 확정 data의 매 commit 강제 쓰기인 FORCE를 뜻하지 않는다.

checkpoint는 begin 시점의 dirty page별 최초 미반영 physical group 시작 LSN, 활성 transaction의 첫 LSN·상태, frozen CSN이 없는 잔존 version이 참조하는 terminal transaction 상태를 수집한다. catalog·heap/index·결과 조회 표·할당 표의 root PageRef, 요청 epoch/floor, 할당 highwater와 version GC horizon도 포함한다. root의 변경은 그 root를 담는 metadata page의 physical group으로 보호한다. 제어 gate 아래 일관된 begin 기준을 잡고 사본의 bytes도 예약한다. 대상 dirty page를 가능한 범위에서 쓰되 활성 거래 때문에 못 쓴 page는 dirty 표에 남기고 redo 시작을 그 이전으로 유지한다. checkpoint 전후 처음 바뀐 page의 image 필요 상태를 latch/epoch로 동기화한다. checkpoint가 시작됐다는 이유로 기존 dirty page를 clean으로 표시하지 않는다.

순서는 필요한 WAL force → 대상 data writeback·data fsync → 상태 chunk와 CHECKPOINT_END append·WAL force → 반대 control slot write·control fsync다. 기록 중 추가 변경은 dirty 표의 recLSN 또는 checkpoint 시작 이후 WAL로 포착한다. CHECKPOINT_END 전에 멈추면 이전 checkpoint를 쓰고, control 쓰기 중 멈추면 유효 slot을 검증해 선택한다. redo 시작은 checkpoint begin과 남아 있는 recLSN·활성 거래 첫 LSN의 최솟값 이전을 유지한다. 이 프로토콜은 fuzzy checkpoint의 의도이며 정확한 실행 순서·I/O 실패 검증이 필요하다.

WAL 회수 하한은 두 유효 control slot 각각의 redo 시작·checkpoint chunk 시작, 활성 transaction의 첫 record, 아직 checkpoint 표로 옮기지 못한 outcome/result와 할당 의존성의 최솟값이다. 새 slot을 내구화해도 이전 fallback이 필요한 segment를 바로 지우지 않는다. 이전 slot도 안전하게 교체한 뒤 해당 segment를 삭제하고 directory fsync를 포함한다. 회수 도중 중단·삭제 실패는 잔여 파일 또는 차단으로 남기며 필요한 segment 부재를 정상으로 열지 않는다.

복구자는 control 후보·segment 연속성·frame/group/transaction chain을 검사하고 완전한 physical group을 redo한다. 유효 COMMIT만 committed로 판정하고 완전한 로그 검사 뒤 나머지 ADMIT/BEGIN은 aborted로 정리한다. 검사 도중 읽기 오류·중간 손상이 있으면 aborted를 추측하지 않는다. 복구에서 정한 ABORT도 append·force한 뒤 terminal 의존성을 해제한다. 복구 판정 표와 새 checkpoint를 내구화한 뒤에만 일반 진입을 허용한다. 재복구도 같은 결과여야 한다. aborted version이 물리 복구로 생겨도 일반 reader에는 비가시이며 복구 완료 전 writeback을 일반 NO-STEAL의 예외로 노출하지 않는다.

version GC는 가장 오래된 snapshot, 실행 중인 탐색 pin, 아직 정리하지 않은 index binding·구조 참조가 필요로 하는 이전 version을 보존한다. commit 표 항목은 모든 잔존 version에 확정 CSN을 기록하거나 참조를 제거한 뒤에만 지운다. payload의 불변성과 별도로 이 freeze metadata 변경도 physical group과 WAL 순서로 보호한다. aborted 판정도 모든 참조 제거 전 보존한다. detach와 FREE를 로그로 보호하고 재사용 generation을 올린다. checkpoint와 version GC, 요청 결과의 만료, stream replay 보존은 다른 작업이다. 긴 reader와 느린 소비자의 bytes를 WAL이 줄었다는 이유로 회수하지 않는다.

### 영속 요청 ID와 결과 조회

같은 DB에서 durable commit 뒤 응답만 사라진 경우를 판별하기 위해 영속 요청 ID·결과 조회를 선택한다. 업무 key만 대조하는 안은 임의 SQL·여러 행의 결과 본문과 원자 commit을 일반적으로 복원하지 못하므로 이 후보의 기본 해결책으로 삼지 않는다. 영속 기록의 쓰기·보존 비용은 양쪽 비교에 포함한다. 이득이 사라지더라도 결과 보장을 한쪽에서만 빼지 않는다.

내부 요청 ID는 `(DB UUID, 요청 epoch u64, 호출자 nonce 128bit)`다. process 재시작·PID·새 owner epoch가 이 ID의 의미를 바꾸지 않는다. operation digest는 미리 선언한 순서 있는 SQL bytes·타입/길이를 포함한 binding bytes·격리·내구 요청과 응답 형식을 SHA-256으로 계산한다. 선언과 다른 문장·binding·순서·응답 범위는 commit 전에 거절한다. hash를 인증 수단으로 사용하지 않는다. 동적 callback, 실행 결과에 따라 새 SQL을 정하는 대화형 거래, 외부 부작용의 자동 재실행은 지원하지 않는다. 이 제한 밖의 기존 SQL은 기존 의미로 실행하며, UNKNOWN은 업무 key와 별도 재조정으로 해결한다.

admission은 현재 epoch의 새 ID를 공유 표에서 원자 예약하고 declared result 상한·작업·WAL·영속 outcome 공간을 확보한다. 같은 ID가 진행 중이면 진행 중 상태만 돌려주며 실행을 하나 더 시작하지 않는다. ADMIT과 실행 중 상태는 commit 전 단독 내구성을 약속하지 않는다. WAL 후보는 ADMIT을 앞서 append하고, COW는 최종 게시에 admission 정보·결과를 함께 싣는다. commit에는 전체 요청 digest와 선택한 최종 응답 본문을 데이터와 원자적으로 보존한다. 미적용 결과를 영속 조회로 약속하려면 ABORT/terminal outcome도 내구화한다. 그 동기화가 불확정이면 UNKNOWN으로 차단한다.

결과 본문은 caller가 선택한 최종 응답 envelope다. 임의 SELECT 전체나 외부 HTTP 응답을 자동 보관하지 않는다. commit 전 반환한 SQL/RETURNING은 잠정 결과이며 응답 envelope와 동일한 수명으로 취급하지 않는다. 결과가 선언한 상한을 넘으면 commit record를 만들기 전에 거래를 실패시킨다. 일부 본문만 보관한 성공, 성공 뒤 결과 예약 실패, digest만 남기고 본문 조회 가능이라고 표시하는 동작은 금지한다. 저장된 본문은 버전·길이·digest가 있는 완전한 bytes다. 본문은 보통 version/overflow page, ID index와 epoch/floor는 metadata page에 보존하며 그 변경도 commit의 physical group에 포함한다. RESULT_CHUNK는 이 본문의 참조와 검증 정보를 담아 본문을 별도로 두 번 로그하지 않는다. checkpoint는 본문 bytes 전체 대신 이 표의 root와 참조·상태를 보존한다. 조회 시 owning copy의 예산을 별도로 검사한다. 조회 OOM은 이미 확정된 거래의 결과를 바꾸지 않는다.

| 조회 입력·상태 | 반환 의미 | 재실행·소유권 경계 |
|---|---|---|
| 유효 COMMIT과 전체 결과 | APPLIED, CSN과 원래 응답 bytes | 변경·callback 실행 없이 사본 반환 |
| 내구 ABORT 또는 완전한 복구로 확인한 admitted 미완료 거래 | NOT_APPLIED, 보존한 terminal 결과 | 자동 재실행 없음, 새 업무 시도는 호출자 선택 |
| 같은 ID·다른 내용 digest | CONTENT_MISMATCH | 원래 기록 유지, 새 변경 없음 |
| 활성 ID | IN_PROGRESS | 같은 DB owner 세대 안에서 조회, timeout 뒤 IN_PROGRESS 유지 |
| 유효 보존 epoch의 없는 ID | NOT_FOUND | 미적용 추론·admission·SQL 실행 금지 |
| floor보다 작은 epoch | OUTSIDE_RETENTION | APPLIED/NOT_APPLIED 추측 없음, 오래된 ID로 재등록 거절 |
| 다른 UUID·미래 epoch·형식 불일치 | INVALID_ID 또는 UNSUPPORTED | 다른 DB nonce 조회·실행 금지 |
| 복구 중·손상·owner 불확정 | UNAVAILABLE/UNKNOWN | 정상 결과처럼 캐시하지 않고 새 쓰기·자동 재전송 차단 |

epoch는 벽시계 TTL로 지우지 않는다. 현재 epoch에서는 항목을 개별 삭제하지 않으며, 닫힌 epoch는 진행 중 거래가 없고 호출자가 조회 보존 종료를 명시적으로 허용한 뒤에만 floor를 올려 회수한다. floor 변경 자체를 원자·내구화한 뒤 본문과 index를 GC한다. crash 사이에는 이전 보존 상태 또는 OUTSIDE_RETENTION 중 하나이며, 같은 ID가 다시 새 요청으로 실행되어서는 안 된다. 시험 harness의 명시적 보존 종료가 제품의 암묵적 만료 승인은 아니다. 공간을 확보할 수 없으면 새 요청 admission을 거절한다. retry 가능한 기간의 제품 값과 사용자 동의 방식은 별도 공개 계약으로 남긴다.

### 후보의 자원·대기 한도

아래 값은 [#266](https://github.com/woonyong-choi/totedb/issues/266)의 가역적인 내부 검증 설정이다. 무제한 예약·본문·복구 log를 막는 출발점이며 고객 SLO·성능 합격선·기존 공개 API의 새 제한이 아니다. 정식 비교는 같은 값과 구현된 회계를 첫 표본 전에 고정한다. overflow·OOM·disk full을 값 상향이나 sample 제외로 처리하지 않는다.

| 대상 | 검증 설정 | 한도에서의 동작·선택 이유 |
|---|---|---|
| DB의 계상 대상 요청 bytes | 공유·프로세스별 private 합계 128MiB, 거래별 작업 상한 8MiB | 기존 연구 예산 재사용, shared frame·사본·WAL staging·잠금·cache·결과·제어 상태의 겹친 peak를 모두 포함, 배정 실패는 명시적 거절 |
| frame·WAL staging·논리 잠금 | 각각 최대 64MiB·8MiB·4MiB, 전체 128MiB 안의 부분 상한 | page header·descriptor도 계상, NO-STEAL frame을 강제 퇴출하거나 잠금 축소 금지 |
| 활성 거래·참여 프로세스 | DB당 64거래·16프로세스 | 고정 제어 공간과 owner 슬롯 한정, 추가 begin/open은 LIMIT, 모든 거래 작업 상한 합계의 선예약과 구분 |
| physical group | 64page·2MiB, record는 1MiB | 짧은 latch/staging 상한, 큰 논리 거래는 여러 완전 group으로 구성하되 논리 commit은 한 번 |
| 요청 결과 | 본문당 1MiB·chunk당 64KiB, epoch당 1024 ID, 현재 포함 최대 4epoch, 전체 영속 표·본문 64MiB | 큰 응답은 선언 때 거절, 실제 저장 bytes와 count 중 먼저 찬 한도에서 새 admission 거절, 중간 결과 LRU 삭제 없음 |
| 복구 WAL | 4MiB segment·총 256MiB, 그 안에 정리용 8MiB 예약 | 거래의 최악 commit/result·abort 예산을 먼저 확보, checkpoint·GC 예약 공간의 신규 요청 사용 금지 |
| admission·key 대기 | admission 공간 부족은 즉시 거절, key 대기는 내부 기본 1000ms 또는 더 짧은 caller deadline | monotonic clock, FIFO 호환 대기와 취소. 교착 cycle의 가장 새 transaction ID를 victim으로 선택, 살아 있는 owner 탈취 금지 |

교착 victim의 실행 권한을 중단한 뒤 논리 abort·terminal 내구화와 잠금 해제를 진행한다. 객체는 실패 상태로 남아 명시적 rollback을 요구한다. 내구화가 불확정이면 DB를 fence하며 다른 writer가 그 상태를 이어 쓰지 않는다. 일반 문장 오류의 실패 상태와 명시적 rollback 규칙도 보존한다. 잠금 예산을 넘으면 implicit escalation으로 전체 DB를 잠그지 않고 실패 거래로 전환한다. 색인 없는 predicate는 처음부터 명시적 table 보호를 선택한다. 유지할 cache·query/result와 private 메모리도 위 전체 ledger에 들어가야 새 후보의 cap 검증이다. 현재 Python C API의 unlimited pool에 이 설정이 이미 구현됐다고 주장하지 않는다. allocator overhead·단편화·OS page cache·Python 객체·RSS는 requested bytes와 별도이며 전체 앱 자원 수집에서 확인한다. data·replay·retention의 전체 disk quota와 product 대기 정책은 G3/G5의 별도 사전 계획에 남는다. WAL·결과 상한만으로 전체 저장량 제한을 충족했다고 표시하지 않는다.

### 같은 호스트의 소유권과 복구 차단

owner는 `(DB UUID, 내구 owner epoch, 참여 slot, 재사용하지 않는 process nonce)`로 식별한다. PID는 진단에만 사용한다. 같은 프로세스는 DB별 registry가 lease FD와 handle 참조 수를 관리하고, 다른 프로세스는 동일한 공유 frame·transaction·잠금·할당 표를 사용한다. 공유 mapping은 data 파일이 아닌 임시 제어/버퍼 파일이며 source of truth는 WAL·data·control이다. 닫았다 다시 연 process-local cache로 이 일관성을 대신하지 않는다.

별도 membership 파일의 공유 `flock`을 각 참여 process가 전용 open description으로 보유하고, 짧은 제어 gate와 slot별 liveness lease도 별도로 둔다. inode를 열린 동안 unlink/recreate하지 않는다. FD를 임의 handle close에 넘기지 않고 마지막 registry owner가 닫는다. fork 자식은 상속 FD를 닫고 옛 handle을 거절하며 새로 등록해야 한다. 기존 [page_file의 fork 처리](../../src/storage/page_file.c)처럼 FD 사본이 살아 있으면 사망으로 잠금이 풀렸다고 가정할 수 없다. thread 취소·정지는 process 사망 증거가 아니다.

| 상태 | 허용 동작 | 전이·실패 처리 |
|---|---|---|
| 참여 중 | 현재 epoch·공유 상태 검증 뒤 실행, 짧은 gate·page latch 사용 | owner 죽음·불확정 I/O·공유 변경 중단 감지 시 fence 요청 |
| fence 요청 | 새 begin·게시·회수 차단, 검증 가능한 기존 결과만 반환 | 살아 있는 peer는 실행/I/O를 멈추고 pin·권한을 정리한 뒤 membership 해제 |
| 배타 복구 대기 | membership의 배타 잠금 획득 시도 | 잠든 live peer가 남으면 BUSY/유한 timeout, epoch 강제 변경·공유 상태 재초기화 금지 |
| 배타 복구 | 모든 옛 peer 권한 종료 후 WAL 복구, owner epoch 증가·내구화 | 완료 전 일반 진입 차단, 중간 실패는 fenced 유지 |
| 새 세대 참여 | 새 공유 상태 생성과 재등록 | 옛 epoch descriptor·slot·pin·writeback ticket 거절 |

복구 배타 잠금을 얻지 못한 상태에서 PID 조회·heartbeat 만료만으로 공유 mutex나 frame을 초기화하지 않는다. process-local pthread mutex가 공유 복구를 해결하거나 macOS에 robust mutex가 있다는 가정을 두지 않는다. 공유 상태의 변경 도중 owner가 죽으면 정상 writer는 그 상태를 사용하지 않고 위 배타 복구를 거친다. 공유 상태 변경자는 짧은 OS gate와 owner slot을 연결하며 독자는 gate의 owner lease를 확인한 뒤에만 중간 상태에 접근한다. 죽은 process가 잡은 pthread mutex를 다른 process가 강제 unlock하는 설계는 사용하지 않는다. 살아 있는 비협조 peer 때문에 복구가 지연될 수 있다는 제약을 명시하며 안전을 위해 split brain을 허용하지 않는다. 이 방식의 실제 별도 프로세스·owner death·stale I/O 검증은 후속 구현 조건이다. network FS·다중 호스트 지원을 추론하지 않는다.

### 양쪽 후보의 논리 배치와 비용 경계

E4는 `(event ID, 불변 payload bytes)` 정본과 `(group ID, event ID, 상태, lease, fence, 시도, ACK)`의 작은 version을 분리한다. cursor·ACK hole은 별도 소비자 상태이며 한 그룹의 ACK로 payload를 삭제하지 않는다. WAL과 buffered COW에 같은 key 순서·값 codec·payload 크기·보존 horizon·SQL/ACK 명령과 최종 응답을 사용한다. 한쪽만 payload를 상태 행에 복제하거나 작은 상태 갱신에서 본문을 다시 encode하도록 만들지 않는다. 이 배치 계약은 stream 전달·retry/DLQ API의 완성을 뜻하지 않는다.

COW는 최신 tree에 논리 변경을 병합하고 변경 page·outcome/result·root를 같은 게시로 보호한다. 같은 ID 조회와 floor 원자성, snapshot·fallback 참조 보호도 동일하다. 현재 메모리 ring을 영속 결과 표의 대용으로 쓰지 않는다. WAL은 redo·image·data writeback·checkpoint·version/outcome GC를, COW는 최신 leaf/root 병합·재작성·outcome 게시·pin/fallback 회수를 부담한다. 공통 cache/workspace와 durability group의 최대 크기·대기 정책도 같게 고정한다. 종료 뒤 남은 WAL·version·결과·retire debt를 완료된 비용에서 빼지 않는다.

### 저장 후보의 실패 판정표

각 행은 구현 후 필요한 입력과 독립 oracle이다. 현재 통과한 엔진 시험이 아니며 정적 문서 검사나 상태 모형으로 대체하지 않는다. 양쪽 후보에 해당하는 동일 상태·요청 결과 oracle을 쓰고, WAL framing 전용 행은 COW의 page/control·manifest 손상 지점에 대응시킨다. 기존 15개 시나리오·E0/E5/E6와 함께 실행하며 하나라도 필수 불변식을 위반하면 정식 비용 수집에 진입하지 않는다.

| ID·구현 경계 | 정상·실패 입력과 의도적으로 깨뜨릴 대조 | 독립 기대 결과와 보존할 관측 |
|---|---|---|
| R01 로그, 23 | 모든 header/payload/footer 절단, short write, 과대 길이·overflow·잘못된 UUID/type, 중간 checksum 손상 | 마지막 불완전 tail만 제외, 중간 손상은 차단. 파서가 예약한 최대 bytes와 실제 offset, 실패 파일 보존 |
| R02 내구성, 23 | commit 앞/뒤 force·응답을 각각 중단, append 성공만으로 성공 처리하는 대조 | 성공 응답 거래는 전부 보존, 응답 유실은 기존 결과 조회 또는 UNKNOWN. transaction별 로그·barrier·응답 trace |
| R03 group, 23/27 | 그룹의 일부 거래 제약 실패·barrier 실패, 앞 group 성공 뒤 다음 실패 | 제외 거래 성공 금지, 앞 성공 취소 금지, 결과 chunk·CSN·실제 최종 상태 대조 |
| R04 version, 25 | A update/delete·B commit·A abort, read-own/옛 snapshot, 제자리 덮어쓰기 대조 | 이전 A 값과 B 결과 보존, A의 행·색인·연관 상태 비노출. 입력에서 직접 산출한 row/key 집합 |
| R05 구조, 25 | split·root·overflow·할당 중 abort/종료, incomplete physical group | 완전 구조만 재생, 누락·중복·cycle 없음. tree 도달성·free/live 교집합·재시작 반복 |
| R06 writeback, 24/25 | WAL force 전 data 쓰기, 미확정 의존 frame 퇴출, 오래된 사본 완료로 dirty 제거하는 대조 | 순서 위반은 실패, NO-STEAL 거절·잔여 dirty 보존. pageLSN/durableLSN·pin·frame 세대 trace |
| R07 torn page, 24 | image 전후 page sector 손상·reordered I/O, image 제거 대조 | image+연속 redo로 복구하거나 CORRUPT, checksum만 맞춘 빈 page 성공 금지 |
| R08 checkpoint, 24 | 각 WAL/data/END/control force·GC 지점 종료, fallback 필요 segment 조기 삭제 대조 | 유효 이전/새 checkpoint로 같은 확정 결과, 필요한 로그 누락이면 차단. 두 slot과 실제 파일 보존 |
| R09 GC, 25/29 | 긴 snapshot·pin 중 반복 갱신/삭제, transaction 표 조기 제거·page/slot ABA 대조 | 옛 읽기 유지·generation 거절·자원 압력 명시, 회수 뒤 재열기와 live/free 참조 검사 |
| R10 격리, 26 | hot row·unique·빈 범위·집계/write skew·DDL, 기다린 SNAPSHOT에 최신 값 혼합 대조 | 격리별 허용 결과만 반환, 반환 값/ID·callback 재실행 없음, failed 거래 rollback 필요 |
| R11 독립 진행, 27/29 | 열린 A와 독립 B·같은 leaf 다른 행, 옛 leaf 덮어쓰기 대조 | B 실행·commit 중첩과 두 결과 보존, 전체 거래 mutex로 직렬화한 경우 요구 실패 |
| R12 잠금 압력, 26 | 역순 key 교착·FIFO 경쟁·취소·1000ms/짧은 deadline·잠금 예산 초과 | 명시 victim/timeout·예약/잠금 반환·추월 제한, 완료량·최대 대기·실패도 보존 |
| R13 owner, 28 | 실제 두 process의 allocation/publish/checkpoint 중 사망, PID/slot 재사용·fork | 옛 권한 종료 후에만 복구, stale epoch 쓰기 거절, 타 거래/pin 침범 없음 |
| R14 live peer, 28 | process 정지와 thread 중단, timeout takeover 대조 | live membership이면 복구 BUSY/timeout, 강제 epoch 변경·split brain 없음 |
| R15 요청 중복, 23/29 | 같은 ID 동시 admission·응답 유실·새 owner·같은 nonce 다른 내용 | 단일 실행·원래 결과 bytes·내용 불일치 거절, 적용된 업무 효과는 한 번 |
| R16 요청 부재·만료, 23/29 | ADMIT 전 종료·없는 ID·floor 전후 종료·다른 UUID·미래 epoch | NOT_FOUND/OUTSIDE_RETENTION은 미적용 아님, 오래된 ID 재등록/자동 재실행 없음 |
| R17 자원·결과, 23–29 | 본문 한도·shared/private/dirty/pin·WAL quota·OOM·disk full·조회 사본 OOM | commit 전 실패와 UNKNOWN 구분, 부분 결과 성공·무제한 spill 없음. 원 실패 파일·요청/상주/RSS 구분 |
| R18 동등 비용, 29–32 | payload/작은 상태 배치·결과 보존·group 정책·cleanup 종료 조건 대조 | 같은 최종 bytes/업무 상태/결과, 양쪽 회수·checkpoint·복구 debt 포함, 다르면 비교 불가 |

### 권고를 바꾸는 조건과 보존할 것

같은 격리·조기 잠금·cache/workspace·payload 배치·group 정책에서 buffered COW가 필요한 tail·공정성·전체 메모리·정리·복구 기준을 채우고, WAL의 추가 복구/공유 상태·checkpoint/GC 비용이 이익을 없애면 COW를 다시 선택할 근거가 된다. 반대로 WAL의 correctness 실패나 자원 한도 위반은 기존 구현을 자동 폐기할 근거가 아니라 WAL 후보의 탈락 사유다. 두 후보가 모두 부적합하면 새 backend를 채택하지 않고 원인과 요구를 다시 검토한다. 실제 writer 요구를 포기하거나 표준 SQLite의 직렬화로 대체하려면 별도 요구 변경이 필요하다.

포기하는 것은 새 엔진에서 현행 COW 게시를 무조건 기본으로 확장한다는 방향이다. B+Tree·행 codec·SQL 의미·선택형 ACK·소유권·오류 결과·예산을 함께 버리는 선택은 아니다. 재사용할 코드도 새 page/version/복구 계약과 맞는지 검사한다. 현행 제품의 파일과 단일 writer 경로는 새 backend 검증과 이관이 끝날 때까지 보존하며, 두 backend를 영구 지원하겠다는 약속은 하지 않는다. [제품·저장 선택 D52](../development-report.md#d52-streamlite의-새-요구와-cow-유지교체의-판단-근거)는 현재 권고와 과거 선택의 이력을 구분한다. [기존 결합 경로 제외 근거](https://github.com/woonyong-choi/totedb/issues/137#issuecomment-6202455142)처럼 예전 SQL 재실행 WAL을 다시 얹는 안을 고른 것이 아니다.

### 분리 비교의 범위

반복 SQL 준비 비용이 줄어든 COW와 준비 비용이 그대로인 WAL을 비교하면 저장 방식의 차이를 분리할 수 없다. 같은 변경 누적·준비 결과 재사용·payload 배치를 적용한 두 후보에서, COW의 병합·재작성·회수와 WAL의 redo·writeback·checkpoint·version GC를 비교한다. 짧은 put→drain만으로 장기 삭제·재사용이나 다중 writer의 이점을 판정하지 않는다.

아래 E0–E7은 비교를 나누는 설계다. E0–E3와 선택형 수명의 실행 조건은 [공통 비용 수집의 사전 계획](#공통-비용-수집의-사전-계획)에서 고정한다. 이후 저장 후보·writer·stream 비교의 수집기와 G1–G6는 별도 과업이다. 기존 [로컬 DB 비교 계획](../experiments/local-db-comparison/design.md)과 [release 비용 계획](../experiments/release-cost/design.md)은 사전 지식으로만 사용하고 원자료·실패 표본·당시 판정은 고치지 않는다. 새 표본을 과거 결과에 합치거나 서로 다른 실험의 개선 배수를 곱하지 않는다.

| 단계 | 기준과 후보 | 고정 조건과 반증할 가설 |
|---|---|---|
| E0 비교 적격성 | 현재 single-writer 제품과 각 후보의 기능·결과 대조 | SQL 단독·ACK 단독·stream 단독·같은 DB 결합을 구분, 미구현 기능은 비교 불가, 성공 경계·보존·자원·isolation 일치 확인 |
| E1 준비 결과 cache | 같은 저장 구현에서 cache만 OFF/ON | SQL·binding·문장/commit 수 유지, 반복 해석 CPU·할당 감소가 최종 업무에도 남는지 확인 |
| E2 변경 누적 workspace | E1의 cache OFF/ON 각각에 workspace OFF/ON을 교차한 네 조건 | 문장 수·내구 경계 유지, clone·heap 완료·root restamp·중복 encode/pwrite 감소와 peak memory 증가 대조 |
| E3 조건부 SQL | 고정한 cache/workspace에서 기존 claim·ACK와 조건부 갱신 | lease·최소 후보 선택·fence·중복 ACK 의미 유지, 정상·실패 readback의 SQL 수와 전체 비용 대조 |
| E4 원본과 상태 배치 | 같은 E3 설정에서 기존 배치와 공유 payload·소비자별 상태 | fanout·보존·transaction 범위 유지, PUT 추가 비용과 CLAIM/ACK·삭제 절감 대조 |
| E5 실제 동시 writer | 같은 backend·공통 설정에서 writer 1개와 여러 개 | 같은 프로세스·여러 프로세스 별도, 실행 중첩·독립 진행과 shared-leaf/root·행·unique/range·cursor 충돌의 비용 대조 |
| E6 저장 방식 | 같은 공통 개선과 로그·소비자 모델을 가진 buffered COW와 WAL·buffer pool·MVCC | SQL 결과·isolation·writer 범위·durable 성공·자원·보존을 맞춘 뒤 병합/회수와 redo/checkpoint/GC 총비용 대조 |
| E7 제품 대안 | SQLite 기반 같은 상태기계, 같은 호스트의 JetStream R=1 file과 비교 | SQL/ACK 결합과 순수 messaging을 구분, 필요한 기능·복구·전체 앱 자원·설치·연동 부담 대조 |

각 단계의 불일치·미지원은 0초·0byte·실패 속도로 순위에 넣지 않는다. E1–E4의 효과를 한꺼번에 넣은 뒤 어느 변경이 원인인지 추정하지 않는다. E5에서는 처리량이 늘지 않아도 실행 중첩 여부를 먼저 확인한다. 실행 중첩만으로 tail·공정성·총비용 개선을 주장하지 않는다. E6의 부분 prototype이 복구·회수 검사를 통과하지 못하면 완성 backend의 대안으로 채택하지 않는다.

### 입력과 실행 경계

초기 입력은 기존 비교의 크기를 재사용하는 실험 가정이다. 제품 최대값·실제 고객 부하·승인된 SLO가 아니다. SQL fixture는 10,000행, 짧은 실행은 1,024논리 작업, payload는 256B와 4,096B, commit 묶음은 1개와 32개를 제안한다. writer 수는 1·2·4, 소비자 그룹은 1·4를 각각 분리 축으로 사용한다. 모든 조합을 자동 실행하지 않고 아래 부하별 필요한 축만 사전 등록한다.

| 부하 | 작업과 입력 | 분리해 확인할 내용 |
|---|---|---|
| SQL 반복·배치 | 같은 key 집합의 UPDATE, 개별 commit과 32개 묶음, SQL만 사용 | cache·workspace 효과, 큐 미사용 호출·할당·I/O, binding과 schema 변경 |
| pending 목록 조회 | backlog와 payload를 늘리고 반환 page·필드를 고정, metadata·본문 반환과 소비 방식 분리 | 전체 payload 목록과 bounded 조회의 실제 메모리·복사·역직렬화, 호출자의 결과 축적과 해제 |
| ACK 수명주기 | PUT→CLAIM→ACK→DELETE, backlog 목표 1,024개 | 성공한 완료 ID·payload·fence, terminal 보존·purge 비용과 페이지 재사용 |
| 독립·충돌 쓰기 | 다른 table, 같은 leaf의 다른 행, 같은 행, unique/range, 인접 append·공유 cursor | leaf는 실제 페이지 배치로 확인, 무작위 key를 독립 물리 페이지로 가정하지 않는 조건 |
| stream·실패 처리 | payload 원본과 그룹별 소비, 역순 ACK·재획득·실패 격리·redrive·replay | 이벤트 완료와 그룹별 delivery 완료를 따로 계산, ACK holes와 느린 소비자 pin |
| 장기 반복·압력 | 30분 수집 가정의 수명주기, 긴 reader·멈춘 writer·느린 소비자·대형 payload | 유지되는 backlog와 실제 성장, 승인한 한도의 직전·도달·초과에서 admission·회수·복구 |

장기 실행의 30분은 최초 수집 예산이다. 그 시간 안에 cleanup·checkpoint·GC가 발생하지 않거나 파일 성장의 안정 여부를 확인하지 못하면 장기 효율 판정을 보류한다. 고정 길이 작업을 끝낸 시간과 고정 도착률의 지속 부하는 다른 그룹이다. 지속 부하는 목표 도착률·burst·최대 in-flight·처리 지연을 G5에 고정하고, 밀린 요청·거절·timeout을 분모에서 빼지 않는다. 클라이언트가 응답을 기다리며 천천히 보내는 방식만으로 과부하 tail을 설명하지 않는다.

기준과 후보는 같은 호스트·filesystem·장치·CPU 배치·메모리 한도·입력 hash·작업 seed·초기 DB 내용·cache 조건을 사용한다. 다른 실행과 장치 부하를 공유하면 그 사실을 기록하고 적용 범위를 제한한다. 새 프로세스·새 DB가 OS cache까지 비웠다는 뜻은 아니다. warm과 cold 주장을 분리하고, cold 조건을 통제하지 못하면 warm 관측으로만 보고한다.

최종 성능은 [Makefile](../../Makefile)의 `BENCHMARK=0`에 해당하는 비계측 빌드로 비교한다. 요청 단계·lock·syscall 진단은 별도 계측 빌드와 별도 표본으로 수집하고, 계측 ON/OFF 비용을 확인한다. 진단 CPU·wall 시간이나 `pwrite` 성공 bytes를 비계측 업무 성능·물리 장치 쓰기량으로 대신하지 않는다. 기존 [DB 수집기](../../bench/local_db_compare.py)는 현재 성공 경로와 재개방 비교의 근거이며 E5–E7의 수집기가 구현됐다는 근거가 아니다.

steady 구간의 cleanup·checkpoint·GC는 실제 실행한 CPU·I/O·지연에 포함한다. 신규 입력을 멈춘 뒤 남은 작업·정리 debt를 같은 종료 조건까지 처리한 시간도 별도로 기록한다. 활성 구간 처리량과 `완료량 / (활성 시간 + 추가 drain·필수 정리 시간)`을 함께 내고 겹친 정리 시간을 두 번 더하지 않는다. 물리 파일 축소와 재사용 가능 공간은 다르므로 각 backend에 파일 길이 0을 요구하지 않는다. 정리가 제한 시간 안에 끝나지 않으면 잔여 bytes·pin·debt와 timeout을 남기고 정리 비용을 0으로 계산하지 않는다.

### pending 목록의 메모리와 호출 경계

작업 목록의 ID·상태만 확인하는데 모든 본문까지 복원하면, 필요한 화면 결과는 작아도 backlog와 payload에 따라 메모리 비용이 커질 수 있다. 이는 [개발 선택 D51](../development-report.md#d51-제한된-sql-cache-목표와-아직-충족하지-못한-요구)의 아직 검증하지 못한 원래 문제다. 쓰기 처리량이나 일정 backlog의 삭제 반복과 별도로, 조회할 결과를 고정한 메모리 비교가 필요하다.

현재 [호환 계층](../../python/totedb/persistqueue_compat.py)의 `queue()`는 `iter_items(..., with_payload=True)` 전체를 list로 모으고 `_row`에서 각 payload를 역직렬화한다. 내부 페이지 크기가 100이어도 반환 list 전체를 유지한다. [Python Queue](../../python/totedb/queue.py)의 `list_items(after_id, limit, with_payload=False)`는 한 page를 반환하고 `iter_items`는 page를 차례로 제공한다. `list(iter_items(...))`나 호출자의 별도 저장 목록은 다시 전체 결과를 유지할 수 있다. 이 소스 대조는 실제 소비 프로젝트의 교체나 메모리 절감 실측이 아니다.

[native 목록 코드](../../src/queue/ack_queue.c)의 `ackq_list`는 metadata 후보를 제한하고 `fill_item`에서 `with_payload`일 때 반환할 본문을 얻는다. [C ABI](../../src/capi/totedb.c)의 `tote_queue_list`는 payload 포인터 소유권을 넘기고 Python `_to_info`는 필요한 경우 `ctypes.string_at`으로 Python bytes를 만든다. Python `list_items`는 `finally`에서 C payload를 해제하지만 반환한 Python 객체는 호출자가 계속 소유한다. page 전환의 겹친 수명, metadata·결과·cache·schema 메모리까지 관측해야 하며 payload의 논리 저장 1개를 메모리 복사 0개로 해석하지 않는다.

먼저 고정된 사용자 과제를 정한다. 한 page의 metadata만 필요한 과제, 본문이 포함된 한 page, 모든 항목을 하나씩 처리하는 과제, 전체 복원 목록을 유지하는 과제는 서로 다른 결과다. 공통 필드·시간 단위·직렬화·상태·ID 순서를 맞추고 누락·중복·본문 checksum을 확인한다. metadata 과제에서 호환 전체 목록을 읽은 뒤 같은 필드와 앞 page만 남기는 기존 경로의 내부 비용은 측정에 포함한다. 이때 최종 소비 결과가 같다는 사실과 API 반환 형태가 다르다는 사실을 함께 기록한다. 서로 다른 과제의 지연·메모리 비를 같은 우위로 계산하지 않는다.

| 조회 조건 | 기준과 후보의 호출·소비 경계 | 확인할 결과와 비용 |
|---|---|---|
| Q1 한 page metadata | 호환 `queue()` 뒤 같은 metadata·앞 page만 선택, native `list_items(..., with_payload=False)` | 같은 ID·상태·필드, page 결과 유지 중 peak memory·읽은/반환한 bytes·불필요한 본문 역직렬화 |
| Q2 한 page 본문 | 호환 `queue()` 뒤 앞 page 선택, native `list_items(..., with_payload=True)` 뒤 동일 serializer로 복원 | 동일 본문·필드와 실제 복사/역직렬화 비용, C payload와 Python 결과가 겹치는 peak |
| Q3 전체 순차 소비 | 기준 전체 목록과 후보의 `iter_items(..., with_payload=True)`·동일 역직렬화 결과를 하나씩 소비, 소비자가 별도 결과를 축적하지 않는 조건 | 전체 ID·상태·본문의 동일 digest, 마지막까지 걸린 시간과 중간 peak·page 교체·마지막 참조 해제 |
| Q4 전체 목록 유지 | 호환 list와 native `iter_items(..., with_payload=True)`·동일 역직렬화 결과의 명시적 list 축적 | 전체 결과가 같은 경우의 메모리, iterator 사용만으로 전체 결과 비용이 사라지지 않는 조건 |
| Q5 실패·수명·상태 | page 경계·빈 결과·소비 중단·OOM·payload/page 한도·혼합 상태·조회 중 변경 | partial 결과·예외·C 해제와 Python 보유 객체, DB 상태 불변 또는 명시한 실패/일관성 계약 |

조회 fixture의 backlog는 128·1,024·10,000개, 본문은 256B·4,096B, 요청 page는 16·100개를 시작 가정으로 둔다. fixture 생성은 별도 단계에서 끝내고 조회 worker가 입력 본문 전체를 미리 들고 시작하지 않게 한다. 같은 DB 내용·serializer·cache 설정에서 backlog·본문·page 중 한 축씩 바꾼다. 결과 객체를 계속 유지할지, 한 page 또는 한 항목 처리 뒤 참조를 놓을지도 조건으로 고정한다. 이 수치는 수집 규모이며 승인된 메모리 상한이나 절감률이 아니다.

첫 fixture는 모두 READY로 만들어 같은 대상 목록을 비교한다. 현재 `list_items`의 조건은 `id > after_id`이며 pending 상태 필터 인자는 없다. READY·LEASED·ACKED·FAILED 혼합에서는 원하는 pending 상태의 정의, 호출자 필터링과 실제로 훑은 행 수를 별도로 기록한다. 앞 page의 일부만 필터링한 결과를 필요한 pending page 전체로 바꾸어 부르지 않는다. 여러 page 사이의 변경·삭제는 고정 fixture와 별도 조건이며 iterator가 전체 snapshot을 보장한다고 가정하지 않는다.

[현재 목록 계약](../../include/queue/ack_queue.h)의 page 개수 상한과 건당 payload 상한을 전체 bytes 상한과 구분한다. Python의 상한 초과 page 제한과 C의 거절도 같은 입력 의미라고 가정하지 않고 실제 경로대로 확인한다. `with_payload=False`는 반환 본문과 그 복사를 생략하는 선택이지 저장 엔진의 scan·행 해석·I/O가 0이라는 증거가 아니다. 전체 scan이 필요하면 그 읽기량·시간도 포함한다. 큰 payload·OOM·중간 소비 중단에서 잘못된 일부 성공이나 해제 후 참조가 없는지 검사하고, 실패한 DB·원본 오류를 보존한다.

조회 전 열린 DB의 상주 메모리, 조회 중 peak, 반환 객체를 유지한 값, 참조를 놓은 뒤 값을 각각 기록한다. 엔진 요청 bytes·Python 객체/역직렬화 bytes·프로세스 RSS·읽은 bytes·payload 복사량·반환 bytes·실제 행 수와 page 수를 분리한다. 객체·ledger 해제와 RSS 회수를 같은 결과로 판정하지 않는다. RSS의 상속 고수위와 관측 주기 때문에 peak를 놓칠 수 있으면 측정 한계를 밝히고 0 또는 절감으로 판정하지 않는다. 복사·할당 계측은 별도 진단 표본으로 다루며 반환 개수만으로 엔진 전체 메모리나 장치 I/O를 추정하지 않는다.

G5에는 조회별 필요한 필드·상태·결과 수명, backlog/page/payload 축과 전체 메모리 예산·허용 증가폭을 수집 전에 고정한다. 같은 Q1/Q2 결과와 page를 유지한 채 backlog만 늘렸을 때 엔진·binding·소비자 전체 비용이 어떻게 변하는지가 bounded 조회 가설의 반증 자료다. Q3의 전체 읽기 시간과 Q4의 전체 결과 보관 비용까지 backlog와 무관하다고 요구하지 않는다. 고정한 예산을 넘거나 호출자가 전체 본문을 축적하면 그 경로는 원래 메모리 문제를 해결했다고 판정하지 않는다. 실제 persist-queue와의 비교는 고정 버전의 공개 API·serializer·같은 조회 과제를 별도 기준군으로 검사한다. 현재 호환 계층의 정적 분석을 그 라이브러리의 실측이나 원 소비 프로젝트의 교체 효과로 바꾸지 않는다. 실제 소비 API의 변경·실측은 별도 후속 검증이다.

### pending 조회 수집의 사전 계획

[#265](https://github.com/woonyong-choi/totedb/issues/265)의 [수집기](../../bench/pending_query.py)와 [고정 입력·판정 설정](../../bench/pending_query_plan.json)은 현재 COW 제품 코드 `9d70c9b4`를 대상으로 한다. 수집기·설정을 함께 담은 commit을 먼저 고정하고 정식 수집에서 그 조상을 검사한다. 작은 정상·실패 대조는 수집기 검증이며 메모리 절감 효과나 원 소비 프로젝트 교체의 근거가 아니다.

Q1–Q4는 ID·상태·초 단위 timestamp, 본문 과제에서는 복원한 `data`까지 같은 ID 오름차순 digest를 요구한다. READY 합성 입력의 ID는 1부터 backlog까지, timestamp는 1초다. 본문 값은 `{"id": 정수, "body": "x" 반복}`이고 세 경로 모두 기본 `json.dumps`의 UTF-8 bytes와 같은 `json.loads`를 쓴다. 256B·4,096B는 body 문자열 길이이며 JSON 전체 크기가 아니다. 실제 persist-queue 1.1.0의 공개 `queue()`, 현재 호환 `queue()`, native 목록 API를 별도 조건으로 둔다. vendor fixture는 공개 `put()`으로 만들고 합성 SQLite 파일의 timestamp만 정규화한다. vendor의 `SELECT *`에는 ORDER BY 보장이 없으므로 기대 순서와 다르면 비교 실패로 남긴다.

128·1,024·10,000 backlog, 두 본문 크기, 16·100 page의 12개 입력에 네 조회 과제를 적용한다. 각 cell에서 세 조건의 fresh worker를 10개 대응 block, 사전 1개, 진단 1개씩 실행한다. 총 1,728건 중 정식은 1,440건, 사전·진단은 각각 144건이다. seed 26518의 인접 두 block 순서를 반대로 배치해 각 기준군과 native의 앞뒤 순서를 5회씩 맞춘다. fixture 12개는 조회 worker 전에 생성하며 세대별 원본을 그대로 보존한다. 같은 fixture를 순차 재사용하므로 OS cache를 비운 실험으로 부르지 않는다. 생성·조회·검증·원자료 보존 모두 campaign 한도에 포함한다.

| 가설·보호 조건 | 고정한 연구 판정 |
|---|---|
| Q1·Q2·Q3의 유한 입력 메모리 증가 | 같은 body/page에서 backlog 128 대비 각 큰 입력의 native 요청 query peak·Python peak 증가 각각 1MiB 이하, 관측 RSS 증가 16MiB 이하 |
| Q1·Q2의 큰 backlog 대체 효과 | 10,000개에서 각 기준군 대비 native RSS 비의 조정 구간 상한 0.75 이하, query wall 비의 상한 1.05 이하 |
| Q4의 결과 축적 | 일정 메모리를 요구하지 않음, 전체 복원 list 유지·해제와 payload 축적 비용을 반례로 보고 |
| 절대 자원 한도 | worker RSS 256MiB, 진단 Python peak 128MiB; 별도 native driver의 ledger shared/query 128/32MiB |
| 수집 중단 | worker 120초·전체 1,800초·원자료 8GiB·디스크 여유 2GiB, 첫 오류·불일치·한도 초과에서 중단하고 실패·미실행 수 보존 |

RSS·Python peak 한도는 관측 뒤 표본을 수용할지 판단하는 기준이며 OS의 강제 할당 상한은 아니다. 초과·OOM·프로세스 종료는 성공 표본으로 분석하지 않는다. 25% RSS 차이는 불필요한 전체 본문 복원을 대체할 실질적 차이를 확인하려는 연구 가정이다. 5% 시간 보호 폭과 메모리 증가 허용값도 고객이 승인한 SLO가 아니다. 대응 비 중앙값에 seed가 고정된 percentile bootstrap 20,000회와 95%·192개 family의 Bonferroni 보정을 쓴다. 96개 대응 cell에서 시간·RSS 두 지표의 구간을 계산한다. 메모리 증가의 유한 격자 진단에는 별도 신뢰구간을 붙이지 않는다. 표본 10쌍의 극단 분위수는 불안정할 수 있으며 이는 정확한 동시 coverage나 검정력 보장이 아니다. 퇴화·비유한 구간은 보류한다. 필수 결과가 빠지면 비교 불가, 위 보호 조건 중 하나라도 미충족이면 채택을 보류한다. 유한 격자의 진단 상한은 점근적 boundedness의 증명이 아니다. Q3 총 읽기 시간과 Q4 축적 비용을 backlog와 무관하다고 요구하지 않는다.

[Python worker](../../bench/pending_query_worker.py)는 열린 조회 직전, 반환 결과 유지 중, 참조 해제·GC 뒤, close 뒤의 RSS snapshot·프로세스 RSS 고수위를 기록한다. 진단에서만 open 전부터 tracemalloc을 켜고 query 직전에 peak를 재설정해 baseline·peak·증가분·보유·해제 bytes와 GC 추적 객체 수를 구분한다. tracer 시작 이전의 Python 할당과 GC가 추적하지 않는 객체는 포함하지 않는다. query peak는 유지 결과 감사 전에 읽으며, 전체 프로세스 wall/CPU와 query wall/CPU를 함께 보존한다. RSS snapshot은 순간 peak를 놓칠 수 있고 고수위는 startup·감사·close도 포함한다. 상속 고수위가 남는 플랫폼에서는 query 증가가 그 아래에 가려질 수 있다. RSS가 내려가지 않았다는 사실만으로 ledger 누수를 판정하지 않는다.

같은 진단은 역직렬화 횟수·입력 bytes, 실제 C 목록 호출/해제 행 수, 반환 payload bytes·동시 보유량, `ctypes.string_at`의 논리 복사 bytes와 page 전환 때 살아 있는 ItemInfo 수를 기록한다. 사전 OFF와 진단 ON의 시간·RSS 차이는 한 쌍의 설명 자료로만 낸다. 전체 Python 할당 횟수나 물리 장치 I/O로 환산하지 않는다. [native driver](../../bench/pending_query_driver.c)는 같은 파일에서 독립적으로 ledger query/shared peak·해제, scan/index·성공 pread bytes를 확인한다. 이 ledger에는 열기의 임시 할당, 큐의 별도 payload malloc, 내부 metadata 배열, C API·Python 결과 배열 전체가 들어 있지 않는다. caller metadata와 반환 payload는 별도로 세며, driver는 page를 해제하므로 Q4의 caller 축적은 Python worker가 측정한다. 128/32MiB는 driver의 ledger 한도이며 현재 무제한 C API에 전체 native 또는 프로세스 byte cap을 구현했다는 뜻이 아니다.

Q5 대조는 빈 결과, page 0·음수·1,024 초과, payload 상한, metadata·본문 소유권, 중도 중단, 혼합 상태, 다음 page 전 변경·append, 두 번째 page 변환 실패와 serializer MemoryError를 검사한다. native 실패 주입은 payload malloc과 session reserve 경계를 훑어 부분 page 성공·남은 결과 소유권을 거절한다. 이미 소비한 앞 page를 뒤의 오류가 되돌린다고 가정하지 않는다. native 목록은 여전히 상태 필터가 없으며 페이지 전환 중 변경을 볼 수 있다. SQLite의 `-shm`과 잠금 파일 같은 임시 상태는 동결한 데이터 파일 hash에서 제외하고 실행 산출물로 보존한다. 오류를 성공 표본으로 바꾸거나 실패한 입력을 정리하지 않는다. 원 소비 프로젝트의 실제 pending 정의·누적 수명은 후속 읽기 전용 감사에서 확인한다.

등록은 설정·정확한 일정·수집 코드·runtime Git 내용·interpreter/vendor source·SQLite 모듈·플랫폼·기존 OFF/ON 라이브러리와 새 driver의 hash를 남긴다. 데이터 파일 hash는 첫 조회 전에 fixture 등록에 기록하고 매 worker 뒤 다시 확인한다. 원자료를 fsync한 case는 완료로 세고 이후 deadline 초과의 campaign 실패와 실패 case 수를 구분한다. raw·stdout/stderr·fixture·manifest를 원본 그대로 보존하고 `verify`와 `analyze`가 동일 자료를 다시 읽는다. 재현 명령은 [pending 조회 수집](../build-and-test.md#pending-조회-수집)에 있다. 기존 main·YCSB·실패 자료·다른 작업본과 WAL/buffer/MVCC/early-locking 권고는 이 수집기 작업으로 바꾸지 않는다.

검증기는 manifest에 적힌 hash만 검사하지 않고, 등록 일정·fixture 조합에서 필요한 파일을 도출해 누락을 먼저 거부한다. raw·fixture 등록·필수 데이터·worker/native 진단 로그가 대상이며, 존재하는 SQLite WAL/SHM·native lock은 부속 자료로 검사한다. 필수 목록·경로·hash와 fixture 등록을 대조한 뒤 원자료를 분석한다. 수집 뒤 생성한 분석 출력은 이 필수 집합에 넣지 않는다. 잘못된 경로·alias·symlink·중복 JSON key·digest도 거부하지만 전체 장부 교체에 대한 인증을 뜻하지 않는다.

### pending 조회 수집의 첫 판정

현재 COW runtime `9d70c9b4`에서 backlog 10,000건의 native 한 page 조회는 등록한 표본 범위의 메모리·지연 기준을 충족했다. 이는 persist-queue 1.1.0과 현재 호환 계층의 전체 `queue()` 복원 뒤 필요한 결과를 고르는 경로를 native page API와 비교한 결과다. 작은 backlog와 전체 순차 소비에는 시간 비용이 남고, caller가 모든 payload를 쌓으면 메모리도 backlog와 함께 증가한다. 원 소비자 교체와 새 WAL backend의 효과는 아직 확인하지 않았다.

계획/source 조상 `c0cb4216ed75d608b9595d3dd2e01da662483e15`의 clean 작업본에서 기존 입력·기준을 유지해 1,728건을 수집했다. 정식 1,440건·사전 144건·진단 144건을 569.33초에 마쳤고 실패·미실행은 0건이었다. fixture 12개와 artifact hash 3,609개를 검증했다. macOS 26.5.2 arm64·CPython 3.9.6·SQLite 3.51.0과 등록한 vendor source·OFF/ON binary·driver를 유지했고, 새 빌드나 기본 backend 변경은 없었다. NumPy 1.26.4·SciPy 1.13.1의 등록 환경에서 CLI 분석 두 번은 SHA-256 `8a2cd02e4b3281373248e5617ee3b1423820ca18be1cc90f21c7422b66682fc3`으로 byte 동일했다. 원자료에서 모든 기대 digest와 주 비교 구간·메모리 증가 기준·96개 비교의 앞뒤 5회 교대를 별도로 계산해 같은 판정을 확인했다. 원자료와 실행 증거는 로컬 작업본에 보존했으며 공개 배포한 자료로 표시하지 않는다.

아래 값은 native/기준군의 query wall·프로세스 RSS 대응 비에 대한 조정 구간 상한이다. 기준은 각각 1.05·0.75 이하다. 표의 16개 비교가 모두 통과해 전체 판정은 `support_sampled_scope`다. 중앙값·원 구간·세부 96cell은 재현한 분석 JSON에 남겼다. 신뢰구간 방법과 유한 표본의 한계는 위 사전 계획을 따른다.

| body B | page | 과제 | 기준군 | wall 비 상한 | RSS 비 상한 |
|---|---|---|---|---|---|
| 256 | 16 | Q1 metadata | vendor | 0.087 | 0.519 |
| 256 | 16 | Q1 metadata | compat | 0.003 | 0.599 |
| 256 | 16 | Q2 payload | vendor | 0.070 | 0.517 |
| 256 | 16 | Q2 payload | compat | 0.002 | 0.638 |
| 256 | 100 | Q1 metadata | vendor | 0.328 | 0.518 |
| 256 | 100 | Q1 metadata | compat | 0.013 | 0.591 |
| 256 | 100 | Q2 payload | vendor | 0.458 | 0.529 |
| 256 | 100 | Q2 payload | compat | 0.015 | 0.601 |
| 4096 | 16 | Q1 metadata | vendor | 0.031 | 0.213 |
| 4096 | 16 | Q1 metadata | compat | 0.004 | 0.238 |
| 4096 | 16 | Q2 payload | vendor | 0.049 | 0.217 |
| 4096 | 16 | Q2 payload | compat | 0.011 | 0.254 |
| 4096 | 100 | Q1 metadata | vendor | 0.056 | 0.213 |
| 4096 | 100 | Q1 metadata | compat | 0.010 | 0.227 |
| 4096 | 100 | Q2 payload | vendor | 0.075 | 0.241 |
| 4096 | 100 | Q2 payload | compat | 0.012 | 0.256 |

작은 backlog의 Q1/Q2 시간은 설명 지표다. 아래 범위는 각 8cell의 wall 대응 중앙값 중 최소·최대이며 통합 효과나 새 채택 기준이 아니다. backlog 128·256B·page 100의 Q2는 vendor 대비 중앙값 10.065, 조정 구간 9.063–11.573이었다. 작은 입력에서도 항상 빨라졌다는 해석은 성립하지 않는다. 개별 cell의 조정 구간은 원 분석 JSON에 보존한다.

| backlog | 기준군 | cell 중앙값 범위 | 중앙값이 1보다 큰 cell |
|---|---|---|---|
| 128 | vendor | 0.625–10.065 | 6/8 |
| 128 | compat | 0.095–0.769 | 0/8 |
| 1024 | vendor | 0.080–2.169 | 2/8 |
| 1024 | compat | 0.012–0.104 | 0/8 |

Q1·Q2·Q3의 12개 native 증가 비교도 모두 통과했다. backlog 128 대비 큰 입력에서 관측한 최대 증가는 요청 query ledger 2,976B·Python peak 331,695B·RSS 2,555,904B였다. 각각 고정한 1MiB·1MiB·16MiB 이하였다. 전체 worker RSS 최대는 93.48MiB, 진단 Python peak 최대는 49.25MiB였고 별도 native probe의 query/shared 요청 peak 최대는 68,530/2,550,110B였다. 이 값은 유한 입력의 관측이며 Python C API 전체의 할당 cap이나 OS 강제 상한을 구현했다는 뜻이 아니다.

10,000건·4,096B body·page 100의 단일 진단에서 Q1 native는 역직렬화 0회, Q2는 100회·411,792B였다. 두 전체-list 기준군은 두 과제 모두 10,000회·41,198,894B를 복원했다. Q3 native는 전체 10,000건을 복원하면서 Python peak 2.08MiB·RSS 22.80MiB를 관측했다. Q4처럼 결과를 모두 유지하면 Python peak 47.41MiB·RSS 87.03MiB였고, vendor 49.25/93.48MiB·compat 47.47/87.27MiB와 가까워졌다. 이 진단값을 정식 비계측 효과의 신뢰구간이나 전체 allocator 보장으로 사용하지 않는다.

메모리 지지는 전체 순차 소비의 속도 우위를 뜻하지 않는다. 10,000건 Q3의 vendor 대비 native wall 대응 중앙값과 조정 구간은 아래와 같았다. Q3 시간은 사전에 정한 설명 지표이며 이 결과를 본 뒤 채택 기준을 바꾸지 않았다.

| body B | page | wall 대응 중앙값 | 조정 구간 |
|---|---|---|---|
| 256 | 16 | 12.718 | 10.667–19.865 |
| 256 | 100 | 13.600 | 10.103–15.853 |
| 4096 | 16 | 3.005 | 2.549–3.744 |
| 4096 | 100 | 3.024 | 0.883–3.344 |

같은 큰 본문·page 100의 native C 진단은 전체 순차 조회에서 101page·색인 탐침 30,101회·성공 pread 575,700,992B를 관측했다. 반환 payload는 41,198,894B였다. [행별 채우기](../../src/queue/ack_queue.c#L1095-L1116)는 note·created·payload를 각각 가져오므로 이 반복 경로는 후속 비용 후보다. metadata만 반환한 Q1도 성공 pread 3,301,376B를 기록했다. 반환·복사 bytes와 syscall 읽기는 서로 다른 값이며, OS cache를 거친 실제 장치 I/O로 환산하지 않는다. 계측 ON 진단을 비계측 시간의 비용 기여율로 사용하지 않는다. 반복 SQL과 열별 조회는 두 backend의 공통 실행 비용으로 남으며 WAL로 바꾸면 사라진다고 계산하지 않는다.

원 소비자의 보관된 소스를 읽기만 하고 복구 loop의 AST를 합성 row producer에 적용한 8개 대조를 별도로 수행했다. 전체-list와 가상의 iterator producer 모두 비ACK request를 같은 사전에 유지했다. 1,024건 중 ACK 상태 256건을 뺀 768건·4,096B body 합계 3,145,728B가 producer 반환 뒤에도 남았고 caller가 사전을 비운 뒤 보유 항목 수가 0이 됐다. 이는 loop의 참조 유지 대조이며 실제 소비자 프로세스의 RSS·속도·native 통합 결과가 아니다. 기본 256건·논리 payload 64MiB admission은 process RSS 상한과 다르며, 복구 loop는 전체 복원 뒤 cleanup을 호출한다. 보관된 checkout의 다른 미커밋 변경과 실제 서비스는 건드리지 않았다.

원 소비자의 `clear_acked_data(keep_latest=0)` 호출은 고정한 vendor 소스의 기본 `max_delete=1000`을 사용한다. 남은 ACK 이력이 있으면 전체 `queue()` 복원 뒤 상태를 걸러내므로 pending admission 256건을 저장된 전체 행 수의 상한으로 해석하지 않는다. 이 판단은 소스 감사이며 실제 보관 DB의 행 수나 실행 효과를 측정한 것은 아니다.

큰 backlog에서 한 page가 필요한 호출에는 현재 native 목록 API의 표본 내 근거가 생겼다. 전체 request를 계속 보유하는 원 caller의 메모리 문제는 이 API로만 해결됐다고 판정하지 않는다. 기존 Q5 소규모 혼합 상태·중단·변환/serializer 오류·native 할당 실패·해제 회귀는 동일 소스와 고정 binary의 근거로 재사용했고, 큰 입력이나 실제 소비자 전체에 대한 새 장애 시험으로 확대하지 않았다. 현재 파일·ABI·기본 backend와 WAL/buffer/MVCC/early-locking 권고는 유지한다. 실제 writer와 정리·복구를 포함한 동등 buffered-COW 비교는 별도 미완료 과업이다.

### 기본키 규모 실측의 재개 조건

[#158](https://github.com/woonyong-choi/totedb/issues/158)의 기본키 구현·기능 검사는 보존하며 1천·1만·10만 행의 같은 조건 3회 실측은 이번 재개 전까지 미완료였다. [원래 120초 계획](https://github.com/woonyong-choi/totedb/issues/137#issuecomment-6175778370), [기본키 구현 당시의 부분 완료](https://github.com/woonyong-choi/totedb/pull/178), [180초로 바꾼 결합 실측](https://github.com/woonyong-choi/totedb/pull/179), [다시 연 이유](https://github.com/woonyong-choi/totedb/issues/158#issuecomment-6233945394)를 구분한다. 180초의 성공을 원래 120초 조건의 완료로 바꾸지 않는다. 현재 작업본과 보존된 정리 장부에서 당시 성능 raw를 찾지 못했으므로 그 파일을 재해시하거나 복구했다고 표시하지 않는다. 공개 원기록의 실패·부분 완료 수치는 새 결과로 덮지 않는다.

[재개 수집기](../../bench/primary_key_scale.py)는 [기존 입력 생성기](../../bench/sqlite_workload.py)와 [기존 설정](../../bench/sqlite_workload.json)을 그대로 읽는다. seed137·크기 1천/1만/10만·반복3회, 같은 items/groups 스키마와 자동 commit INSERT를 유지한다. NULL·한글·BLOB, 매 1천 번째 9,000B 본문도 같은 식으로 생성한다. point·missing·range·bucket·null·full_scan의 예열1회와 반복20회의 순서를 모두 보존해, 기본키 두 조회만 남겨 캐시 상태까지 바꾸는 일을 피한다. 세 반복의 엔진 순서는 원래대로 정순·역순·정순이다. 혼합 변경·복구 전체 캠페인은 이 기본키 측정에 다시 포함하지 않는다.

[추가 계획](../../bench/primary_key_scale_plan.json)은 runtime `9d70c9b4`와 기존 계측 OFF·cache/workspace/conditional OFF 라이브러리 hash를 고정한다. 기존 driver에 `@scan`을 추가하고 같은 공개 database API·SQLite C API를 호출하는 별도 실행 파일을 만든다. driver의 준비·실행·typed 행 인코딩·결과 해제 시간을 유지하고 통계 조회는 SQL 시간 밖에서 한다. 추가 통계 왕복은 전체 수집 시간에 남으며, 과거 대비 지연 변화의 원인별 기여로 사용하지 않는다. native heap의 스캔 행·색인 조회·색인 페이지를 기록하고 비기본키 조건으로 스캔 계수기의 양성 대조를 둔다. SQLite의 직접 스캔 행 수는 측정하지 않으며 native 계수기를 SQLite의 0회 스캔으로 옮기지 않는다.

| 항목 | 고정 조건과 판정 |
|---|---|
| 적재 | 두 엔진의 같은 SQL·응답 대조를 포함한120초, 각 문장30초, 같은 완료 행에서 중단 |
| 요청 규모 완료 | 해당 크기의3회가 모두 요청 행 수와120초 조건 충족, partial은 별도 실제 행 수로 보고 |
| 정확도 | 모든 SQL의 typed 행·열·변경 수 대조, point/missing의 독립 기대값, 재열기 뒤 행 수, native INSERT·기본키 조회 스캔0과 계수기 양성 대조 |
| 지연 | 반복별 nearest-rank p50/p95/p99의 설명 통계. 조회별20표본의 p99는 최대값이며3회는 일반적인 속도 우위 검정의 근거로 부족 |
| 수집 자원 | 전체1,800초·자료8GiB·여유2GiB, 기존 worker 관측 RSS2GiB·인코딩 결과512MiB의 사후/반환 한계 유지 |
| 보존·실패 | 새 prefix만 허용, 등록·각 case·DB·stderr·존재하는 lock hash 검증, 실패 시 완료 쌍과 진행 중 한쪽 응답·DB·오류 보존 |

전체 시간·저장 한도는 수집을 제한하는 연구 가정이며 고객 SLO가 아니다. 문장 한 쌍이 경계를 넘겨 끝날 수 있으나 최종 적재 시간이120초 이상이면 요청 행 수에 닿아도 완료로 세지 않는다. 정식 등록은 이 수집기와 계획을 담은 clean commit·Git 조상, runtime·driver·라이브러리·build fingerprint·interpreter·SQLite·플랫폼을 고정한다. small control의 입력은24행·반복1회·조회2회이며 정식 효과가 아니다. 원자료의 누락·변조, 계획·source·binary 변경과 잘못된 partial 완료 표시는 분석 전에 거절한다. 두 엔진이 같은 잘못된 point 결과를 반환해도 독립 기대값과 다르면 실패다.

SQLite DELETE/FULL/fullfsync OFF와 현행 COW/fsync의 전원 손실 보장 동등성, OS cold·할당자 최고치·실제 다중 writer는 이 실험으로 입증하지 않는다. 새 runtime에는 기본키 외의 여러 변경이 있으므로 옛 결과와의 차이를 #158 하나의 효과로 귀속하지 않는다. 아래 정식 크기 수집은 이 계획을 커밋한 다음 별도 경계에서 수행했다. 실제 writer·WAL/buffer/MVCC·buffered-COW·stream 검증을 대신하지 않는다.

### 기본키 규모 실측의 첫 판정

2026-10-24의 새 로컬 실측은 계획/source `ff3af84d`에서 사전 등록한9case를 모두 완료했다. 1천·1만·10만 행이 각각3회 모두 요청 행 수와120초 적재 기준을 충족했다. 부분 적재·실패·미실행은0건이었다. 기존120초 실패와180초 성공 기록은 그대로 보존했다. 현재 runtime의 새 결과이며 과거 실패를 같은 실행의 성공으로 고친 것이 아니다.

환경은 macOS26.5.2 arm64·CPython3.9.6·SQLite3.51.0이다. 기존 최적화 OFF 라이브러리는 runtime `9d70c9b4`와 같은 내용이며 cache/workspace/conditional OFF를 유지했다. 수집기·계획·기존 입력·driver·회귀의6source, runtime 코드78개와 실제4identity를 등록했다. SQLite DELETE/FULL/fullfsync OFF·page4096·cache-2048·mmap0·temp FILE을 확인했다. 예열과 여섯 조회 순서·반복20·seed137·엔진 순서·반환 소비·중단 조건은 위 계획과 같다. 새 build나 제품 source 변경 없이 수행했다.

적재 시간은 두 엔진의 SQL 왕복과 결과 대조를 합한 벽시계 시간이다. 지연은 각 엔진의 준비·실행·typed 결과 인코딩·해제 시간이며 IPC와 대조는 제외한다. 아래 각 지연 칸은 p50 / p95 / p99의 ms 값이다. 반복별 값을 유지하고 합쳐서 한 분포로 만들지 않았다.

| 요청 행 | 반복 | 공유 적재 초 | native INSERT ms | SQLite INSERT ms |
|---|---|---|---|---|
| 1000 | 1 | 0.359163 | 0.088 / 0.118 / 0.208 | 0.206 / 0.388 / 0.55 |
| 1000 | 2 | 0.353276 | 0.087 / 0.167 / 0.245 | 0.204 / 0.352 / 0.493 |
| 1000 | 3 | 0.350845 | 0.088 / 0.12 / 0.204 | 0.21 / 0.288 / 0.496 |
| 10000 | 1 | 4.267065 | 0.128 / 0.258 / 0.402 | 0.214 / 0.402 / 0.6 |
| 10000 | 2 | 4.517837 | 0.132 / 0.26 / 0.461 | 0.218 / 0.409 / 0.714 |
| 10000 | 3 | 4.384685 | 0.13 / 0.276 / 0.445 | 0.209 / 0.383 / 0.66 |
| 100000 | 1 | 44.148171 | 0.139 / 0.277 / 0.461 | 0.212 / 0.378 / 0.643 |
| 100000 | 2 | 53.063050 | 0.142 / 0.321 / 0.637 | 0.216 / 0.457 / 0.913 |
| 100000 | 3 | 68.333559 | 0.156 / 0.381 / 1.986 | 0.239 / 0.52 / 1.799 |

| 요청 행 | 반복 | 조회 | native ms | SQLite ms |
|---|---|---|---|---|
| 1000 | 1 | point | 0.014 / 0.023 / 0.028 | 0.009 / 0.015 / 0.016 |
| 1000 | 1 | missing | 0.011 / 0.015 / 0.017 | 0.008 / 0.009 / 0.011 |
| 1000 | 2 | point | 0.015 / 0.029 / 0.05 | 0.012 / 0.041 / 0.06 |
| 1000 | 2 | missing | 0.011 / 0.021 / 0.035 | 0.008 / 0.014 / 0.023 |
| 1000 | 3 | point | 0.019 / 0.069 / 0.101 | 0.013 / 0.095 / 0.195 |
| 1000 | 3 | missing | 0.013 / 0.051 / 0.14 | 0.008 / 0.045 / 0.104 |
| 10000 | 1 | point | 0.043 / 0.067 / 0.09 | 0.042 / 0.073 / 0.089 |
| 10000 | 1 | missing | 0.015 / 0.022 / 0.023 | 0.012 / 0.047 / 0.076 |
| 10000 | 2 | point | 0.035 / 0.075 / 0.149 | 0.061 / 0.152 / 0.485 |
| 10000 | 2 | missing | 0.017 / 0.056 / 0.156 | 0.017 / 0.061 / 0.12 |
| 10000 | 3 | point | 0.041 / 0.05 / 0.072 | 0.038 / 0.072 / 0.111 |
| 10000 | 3 | missing | 0.014 / 0.023 / 0.057 | 0.01 / 0.025 / 0.051 |
| 100000 | 1 | point | 0.083 / 0.166 / 0.22 | 0.071 / 0.108 / 0.266 |
| 100000 | 1 | missing | 0.037 / 0.086 / 0.409 | 0.033 / 0.099 / 0.453 |
| 100000 | 2 | point | 0.082 / 0.109 / 0.17 | 0.089 / 0.119 / 0.174 |
| 100000 | 2 | missing | 0.036 / 0.051 / 0.081 | 0.035 / 0.079 / 0.1 |
| 100000 | 3 | point | 0.087 / 0.105 / 0.244 | 0.072 / 0.099 / 0.101 |
| 100000 | 3 | missing | 0.039 / 0.051 / 0.053 | 0.038 / 0.054 / 0.07 |

모든 반복에서 native INSERT의 heap 스캔은0행이었고 색인 조회 수는 적재 행 수와 같았다. point와 missing은 각각20회 합계 스캔0행·색인 조회20회·색인 페이지 계수60이었다. 비기본키 조건의 양성 대조는 각 요청 행 수 전부를 스캔했고, 재열기 뒤 행 수와 결과도 일치했다. heap 스캔0행은 heap 접근·물리 I/O가0이라는 뜻이 아니다. SQLite의 직접 스캔 행 수는 측정하지 않았다.

실제 CLI verify는9case/control=false를 확인했고 analyze 두 번은 byte 동일했다. 독립 계산은 전체 조회1080쌍의 일정, typed 기대 결과 digest1440건과 point/missing720건, INSERT·두 기본키 조회의54개 분위수/QPS 계열, 종료·재열기·스캔을 확인했다. 기준을 바꾸거나 표본을 제외·재수집하지 않았다. 필수 자료46개와 존재하는 lock9개의55hash를 확인했다. 분석 SHA256은 `3d9a318d4b18b46d0a9c473e5dbf83fcc890da9e993e863d001cbe9a720d6eea`다.

수집기 시간은227.685초였고 CLI 전체는228.655초였다. 등록·case·DB·stderr·lock55개의 자료는68,702,742B였다. 관측한 worker 고수위는 native63.25MiB·SQLite29MiB로 각각2GiB 이하였다. 전체 앱·동시 두 worker의 메모리 상한이나 실제 allocator 최고치를 입증한 값은 아니다. 원자료는 기존 로컬 실행 영역에 보존했으며 공개 발행·이슈 완료는 별도다.

이번 판정은 고정한 규모/스캔/지연 측정 범위를 충족했다는 뜻이다. 10만 행의 적재44.148·53.063·68.334초와 세 번째 native INSERT p99의1.986ms처럼 반복 간 차이도 남긴다. 세 반복·조회별20표본은 설명 통계이며 조회 p99는 각20개 중 최대값이다. OS cache를 비우지 않았고 공유 호스트를 사용했다. 과거 실행과 runtime·환경을 통제한 전후 비교가 아니므로 일반적인 속도 우위나 #158 한 변경의 개선율을 검정하지 않는다. 새 WAL/backend·실제 writer·전원 손실 동등성·정리 총비용은 이 결과에 포함되지 않는다. 현재 WAL/buffer/MVCC/early-locking 권고와 동등 buffered-COW 비교를 유지한다.

### 동등한 보장과 관측값

SQLite는 WAL/FULL·실제 연결 수·cache·checkpoint 설정을 기록하고 같은 상태기계와 원자성·fence·보존 계약을 검증한다. 일반 WAL의 writer 직렬화는 제품 대안의 제약으로 남기며 E5의 실제 병렬 writer 충족으로 세지 않는다. 새 다중 연결 실험은 [공식 WAL 문서](https://www.sqlite.org/wal.html#the_wal_reset_bug)가 명시한 WAL-reset 수정 포함 버전의 binary·source hash를 고정한다. 과거 버전의 결과와 새 비교를 합쳐 전후 개선으로 해석하지 않는다.

JetStream은 같은 호스트의 단일 노드 R=1·file storage를 사용하고 프로세스·consumer·client·필요 relay를 모두 계상한다. [공식 sync 설정](https://docs.nats.io/reference/config/jetstream/sync_interval)의 `always`는 매 write 명시적 sync이며 periodic과 별도 그룹이다. 실행할 server·client 버전과 실제 설정을 고정하고 publish 확인·consumer ACK 확인·재시작 보존을 각각 검사한다. 설정 이름만으로 DB의 durable commit과 같은 내구성이라고 판정하지 않는다. 이 공식 계약의 확인일은 2026-10-24이며 실제 비교 binary는 아직 고정하지 않는다.

순수 messaging 비교는 필요한 filter·fanout·lease·재전달·중복 억제·실패 보존·replay 의미가 같은 부분만 측정한다. SQL 결합 비교에서는 외부 SQL DB와 JetStream 사이의 outbox·relay·멱등 처리 및 복구를 포함한다. 그 조합에 같은 DB의 단일 transaction 원자성이 없으면 기능 차이를 명시하고, 더 약한 계약의 처리량을 동등한 통합 효과로 해석하지 않는다. 설치 단계·프로세스·포트·설정·복구 절차·연동 코드·실제 투입 시간을 각각 관측하며 코드 줄 수를 개발 시간으로 환산하지 않는다.

| 관측값 | 측정 경계와 단위 | 함께 보존할 자료 |
|---|---|---|
| 완료 처리량·오류 | durable 완료한 논리 작업/s, stream은 event/s와 delivery/s 분리 | offered·admitted·완료·거절·timeout·UNKNOWN 건수, ID·payload audit |
| 지연·공정성 | 예정 도착 또는 호출부터 최종 durable 응답까지 ms, p50·p95·p99·최대 | begin 대기·retry·재조회 포함, writer별 완료량·최대 대기와 미완료 요청 |
| CPU·메모리 | 앱과 backend·relay 전체의 CPU 초·RSS bytes, 엔진 요청 bytes 별도 | 새 프로세스·측정 구간, allocator·OS cache의 계상 범위 |
| 변경·쓰기 비용 | logical changed bytes, unique dirty pages, encode·pwrite·redo bytes/calls, fsync calls·wall | COW rebase·경로 재작성, WAL writeback·checkpoint·GC, 계측 여부 |
| 공간·정리 | live·retained·pinned·reclaimable·spill·log·file high-water bytes | 시간별 backlog·ACK holes·삭제·회수량, 종료 때 남은 debt |
| 복구·사용 비용 | reopen·중단 복구·이관/복원 시간, 설치·연동의 실제 작업 | durable 승인 목록, 복구 상태, 원본 오류·실패 DB·사용한 절차 |

lost update·같은 leaf의 변경 손실·unique/range 불변식 위반·중복 claim·stale ACK 수용·sequence hole 건너뛰기·부분 결합 반영·성공 응답 뒤 유실·UNKNOWN 오보고·조기 페이지 재사용은 정확성 탈락 조건이다. 앞의 15개 시나리오에 게시 전후 종료, short/torn/reordered write·sync 실패·disk full·OOM·spill·fallback·mixed-version·이관 복원을 적용한다. 정상 reopen·프로세스 종료·OS crash·전원 차단은 각각 검증 범위를 기록하고, 실행하지 않은 장애 종류는 미검증으로 남긴다.

### 반복과 판정 규칙

두 조건 비교 cell은 새 DB의 사전 실행 1쌍과 정식 대응 10쌍을 제안한다. 이는 수집 한도이며 목표 효과를 검출하는 검정력 산정값이 아니다. 사전 실행은 통계에서 제외하되 정확성 실패는 보존한다. 같은 입력 seed와 반복 ID를 대응시키고 기준 먼저 5쌍·후보 먼저 5쌍을 한 쌍씩 교대로 배치한다. E2는 사전 1block·정식 10block으로 네 조건을 매번 모두 실행하고 시작 조건을 순환한다. 이때 같은 반복 block의 대응을 유지하고 cache별 workspace 효과와 workspace별 cache 효과를 따로 계산한다. 실행 중 결과를 보고 순서·반복 수·payload·주요 부하를 바꾸거나 유리한 구간만 늘리지 않는다. 재실행은 새 실행 ID로 남기고 실패 표본을 대체하지 않는다.

정식 수집 전 후보·기준 commit과 binary hash, 도구·binding·실제 SQLite 링크 버전, OS·장치·filesystem, 설정 조회값, 입력 hash·작업 seed, 도착률·종료 조건·timeout, 결과 분석 버전, 해당 G1–G6 계약을 실행 계획에 고정한다. 명령과 env 기록을 재현할 수 없으면 수집하지 않는다. 기존 성공 경로 비교 도구를 새 concurrency·장기 stream 시험에 그대로 쓸 수 있다고 가정하지 않는다.

정확성 실패·설정 불일치·원자료 손실을 발견하면 해당 비교의 성능 수집을 중단하고 DB·부분 결과·원본 오류를 보존한다. 계획한 반복 수와 실제 완료·실패·미실행 수를 모두 남긴다. 실행·복구·정리 timeout과 전체 수집 시간/저장 예산도 수집 전에 정한다. 느린 실행을 이상치로 제거하거나 실패를 성공 실행으로 교체하지 않는다.

고정 작업의 지연은 API 호출, 지속 부하의 지연은 예정 도착에서 시작해 각각 최종 durable 응답까지 잰다. 두 경계를 같은 비율로 비교하지 않는다. 실행별 percentile은 `ceil(p*n)`번째 관측값을 택하는 nearest-rank로 고정한다. `p`는 0과 1 사이의 누적 비율이고 `n`은 지연 관측 수다. 전체 요청 수와 미완료 요청의 censor·timeout도 별도로 보존하며 성공한 요청의 percentile만으로 그 비교를 통과시키지 않는다.

처리량은 대응한 `후보/기준` 비율 10개와 그 중앙값·범위를 보고한다. tail도 실행별 p99의 대응 비를 내고 서로 다른 실행의 percentile을 전체 요청 p99라고 합치지 않는다. 재표집은 실행 쌍을 단위로 유지하고 [SciPy bootstrap 계약](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.bootstrap.html)의 paired BCa·10,000회·seed 257을 분석안으로 둔다. 분석 runtime과 RNG 버전도 고정한다. 퇴화 표본·NaN·대응 누락은 판정 보류이며 유리한 방법으로 사후 교체하지 않는다. 같은 호스트의 반복 구간은 해당 실행 변동의 근사 추정이며 장비·고객 모집단의 보장이 아니다.

E1–E4의 여러 원인 탐색 구간은 명목 95%로 표시하고 그중 좋은 구간을 골라 채택하지 않는다. 채택 비교의 후보·주요 부하·효과와 판정할 구간 수 `m`을 정식 수집 전에 고정하고 각 구간의 신뢰수준을 `1 - 0.05/m`으로 조정하는 [Bonferroni 안](https://www.itl.nist.gov/div898/handbook/prc/section4/prc473.htm)을 둔다. 이 보정이 bootstrap의 작은 표본 불확실성이나 공유 호스트의 상관을 해결하지는 않는다. 후보를 탐색 표본에서 골랐으면 새로운 정식 표본으로 비교한다. 정밀도가 부족하면 보류하고 변경한 계획과 새 표본을 별도 등록한다. 실패·timeout·부하 미완료를 빼고 성공 표본만으로 효과 비를 만들지 않는다.

G5에는 부하별 최소 완료량 `Q_min`, p99 상한 `L99_max`, writer 최대 대기 `W_max`, 메모리·저장량·회수 debt 상한, 복구 시간, 실질 개선폭 `delta_min`과 tail 악화 허용폭 `epsilon99`를 값·단위·구간·대상과 함께 정한다. 실제 사용자 부하·자원과 연결되지 않은 임의 값을 승인된 합격선으로 쓰지 않는다. 아래 식의 `L`과 `U`는 조정한 구간의 하한과 상한이다.

| 판정 | 사전 조건과 해석 |
|---|---|
| 비교 불가 | 기능·원자성·isolation·내구성·보존·측정 경계가 다르거나 설정/완료 자료가 불완전 |
| 후보 탈락 | 정확성 실패, 승인된 자원·지연·공정성·복구 상한 위반, 기존 DB·기능 보존 실패 |
| 효과 보류 | 해당 G1–G6 미확정, 표본 부족·구간 퇴화, 비율 구간에 1 포함, 또는 실질 개선 기준 미달. 무효과·동등성의 증명은 아님 |
| 성능 채택 후보 | 모든 안전·자원 게이트 통과 후 주요 부하에서 `L(처리량 비) > 1 + delta_min`, `U(p99 비) <= 1 + epsilon99`, `Q_min`·`L99_max`·`W_max` 충족 |
| 제품 채택 후보 | 필요한 로컬 전달 의미, 전체 앱 배포·자원·연동 이익, SQLite 조합 대비 이익을 각각 근거로 충족. 실제 병렬 writer 미충족을 다른 장점으로 상쇄하지 않는 조건 |

WAL을 새 엔진의 기본 방향으로 권고하더라도 제품 채택은 이 비교의 검증 결과로 판단한다. 같은 예산에서 COW가 shared-root 병합·경로 재작성·pin/회수·공정성 기준에 실패하고 WAL 안이 통과하면 제품 교체 근거가 된다. COW의 국소 구현 결함인지 구조상 비용인지와 배제하지 못한 원인도 남긴다. 둘 다 실패하면 어느 쪽도 채택하지 않으며 SQLite 제품 대안과 요구·비용을 다시 비교한다. SQLite를 택해 실제 병렬 writer 요구를 완화하려면 별도 명시적 결정이 필요하다. 우열이 불확정이면 기존 backend를 보존한다.

탈락·철회에는 실패한 부하와 사전 기준, 양쪽 commit·설정·원자료·실패 표본, 원인 경로와 반례, 포기할 기존 보장, 새 복구·checkpoint·이관 부담과 복원 범위를 D52에 연결한다. 수집 전 계획 commit이 결과의 조상인지, 원자료 hash와 집계 재생성이 일치하는지도 검사한다. 성능 채택 후보가 나와도 기본 backend 변경·기존 파일 전환·배포 완료를 뜻하지 않는다.

### 공통 비용 수집의 사전 계획

같은 1,024개 UPDATE를 cache/workspace 네 조건으로 실행하고 마지막 전체 행·본문을 SQLite 계산과 대조한다. 문장이 적어졌다는 사실이나 workspace의 사본 flush 수만으로 성능을 채택하지 않는다. [수집기](../../bench/common_cost.py), [입력·한도](../../bench/common_cost_plan.json), [native 실행기](../../bench/common_cost_driver.c), [Python 실행기](../../bench/common_cost_worker.py), [분석기](../../bench/common_cost_analysis.py)가 이 계획을 실행한다. 추적은 [#239](https://github.com/woonyong-choi/totedb/issues/239), cache·workspace·조건부 SQL·선택형 수명의 각 후속 이슈를 사용한다.

| 항목 | 고정한 값과 이유 |
|---|---|
| 사전 지식 | 기존 보고서와 cache/workspace 회귀, 작은 수집기 대조, 10,000행·4,096B 입력의 완료 확인. 효과 확인 표본에서 제외 |
| 대상 | 제품 기준 `ce7a9f4609d19fbcac3e12eeda6f37a1b8d62c28`, 현행 단일 writer·COW·durable commit. 새 WAL·MVCC 후보의 성능 판정과 분리 |
| 실행 환경 | 검증 환경 macOS 26.5.2 arm64, Apple M4 10코어·24GiB, APFS. 등록 시 OS·장치·compiler·실제 라이브러리 링크·filesystem을 다시 조회하고 hash와 함께 고정 |
| 분석 도구 | CPython 3.9.6, NumPy 1.26.4, SciPy 1.13.1. `random_state=257`의 RandomState와 BCa 10,000회, worker 단위 대응 유지 |
| 빌드 | 같은 `-O2` 공유 경로와 설정, 기본 cache/workspace 0. handle별 내부 제어만 변경. ON은 `BENCHMARK=1`만 다르며 시간·CPU·쓰기 진단 표본을 OFF 성능과 분리 |
| SQL 입력 | `counters(id INTEGER PRIMARY KEY,counter INTEGER NOT NULL,payload TEXT NOT NULL)` 10,000행, 초기 counter 0·본문 `x` 반복 256/4,096B. fixture는 별도 생성하고 같은 파일 hash를 조건마다 복제 |
| UPDATE | `UPDATE counters SET counter=counter+? WHERE id=?`, binding은 `(1,(i*17+257)%10000+1)`, i는 0–1,023. 반환 영향 행 1, 매 결과 소비·해제 |
| 준비 경로 | `hit`는 같은 SQL, `miss`는 96종 주석을 순환해 64항목 cache를 초과, `unique`는 문장마다 다른 주석. binding·문장·commit 수는 네 조건에서 동일 |
| 거래 | commit당 1/32문장, 마지막 부분 batch 없음. 같은 UPDATE 한 번을 rollback하는 handle 예열은 별도 기록하며 실제 작업에서 제거하지 않음. 새 handle이 OS cold cache를 뜻하지 않는 조건 |
| E1/E2 표본 | 3종 준비 경로×2종 본문×2종 batch의 12cell. cell마다 사전 1block·정식 10block·진단 1block, block마다 `00/10/11/01` 네 조건 모두 실행. 첫 숫자는 cache, 둘째는 workspace |
| 순서 | 정식 block 2개씩 시작 조건을 순환하고 두 번째는 첫 번째의 역순. 각 대비에서 후보 먼저 5회·기준 먼저 5회. 사전·진단은 각각 block -1/-2이며 정식 효과에서 제외 |
| 선택형 수명 | SQL 단독 본문 축 없음, 큐 사용 256/4,096B의 3cell. worker마다 DB 32개를 차례로 열어 CREATE·INSERT·UPDATE·SELECT·commit·결과 소비·close·reopen. 큐 사용은 각 DB에 put/get/ack/purge 한 번 포함 |
| 수명 기준군 | `54b43509`의 Python Database class를 메모리에서 읽은 eager 경로와 현재 lazy 경로. native 라이브러리와 Queue 구현은 같게 고정. import·비교용 source 적재는 API 시간 밖이며 별도 process wall과 RSS에 포함 |
| E3 | 현재 경로와 다른 후보의 OFF/ON 라이브러리, 소스 commit·hash·최소 ID·lease/fence·중복/stale/FAILED·cancel/OOM·failure readback·reopen 근거를 먼저 등록. 없으면 비교 불가로 거절하며 현행 경로 두 개를 후보로 위장하지 않는 조건 |
| E3 입력 | 256/4,096B의 2cell, worker당 put/get/ack/purge 1,024회. 첫 작업에서 중복 ACK·stale ACK·ACKED readback을 추가 검사. 정상 작업 지연과 guard 시간을 구분하고 whole-worker 비용에는 둘 다 포함 |
| 두 조건의 표본 | cell마다 사전 1쌍·정식 10쌍을 교대로 실행하고 진단 1쌍은 별도 수집. SQL/큐 수명과 E3는 서로 다른 효과 모집단 |

native의 업무 완료 시간은 open·예열 rollback·begin/UPDATE/commit·결과 해제·work 정리·close·reopen/close를 포함한다. 전체 행 digest와 page 무결성을 확인하는 감사 시간만 `audit_ns`로 따로 재며 `wall_ns = completion_ns + audit_ns`를 검사한다. CPU도 같은 구분을 보존한다. 감사는 성능 분모에서 제외해 반복 UPDATE 비용이 검증용 전체 스캔으로 가려지지 않게 한다. 실제 commit의 encode·쓰기·barrier·page 회수는 제외하지 않는다. 전체 worker 시간과 CPU, subprocess wall도 함께 보존한다.

지연은 native에서 begin부터 durable commit·work 정리까지의 거래별 관측이고 batch 32를 개별 UPDATE p99로 바꾸지 않는다. Python 수명은 DB 한 개의 전체 수명, E3는 정상 put/get/ack/purge의 작업별 관측이다. 각 worker의 p50·p95·p99·최대는 nearest-rank로 계산하며 대응한 worker의 p99 비를 비교한다. 입력 생성·fixture 복제·연구 자료 압축은 업무 밖이다. 부모 process의 대기 방식 때문에 subprocess wall은 부가 관측이며 업무 완료 시간을 대신하지 않는다.

RSS는 새 worker의 process high-water이며 baseline과 최종 최고치를 bytes로 기록한다. native의 최고치에는 감사용 결과·무결성 메모리도 포함하므로 이를 순수 UPDATE 작업 메모리나 allocator 절감으로 해석하지 않는다. resident·query·result 예약 최고치, 마지막 resident, close 뒤 0 반환, 거래별 workspace 최고치·최종 사본 flush·catalog clone을 따로 기록한다. cache hit/miss·eviction은 매 문장, workspace 통계와 APPLIED 결과는 매 거래에서 수집한다. 예상하지 않은 cache 초기화·한도 거절은 표본을 버릴 이유가 아니라 중단 조건이다.

실제 pread/pwrite/fsync·file lock의 호출·kernel 수락 bytes·중첩 및 배타 시간은 ON 자료에만 있다. OFF의 미수집 값을 0으로 채우지 않는다. workspace flush는 buffered image만 세며 전체 pwrite·제어 쓰기 수가 아니다. native 직접 실행에는 C API handle mutex가 없고 Python API에는 있으므로 서로의 단계 시간을 합치지 않는다. SQL 실행 경계 수는 현재 계측으로 직접 셀 수 없으면 null로 남긴다. ON과 OFF 사전 표본의 같은 조건 wall/CPU 비는 계측 교란의 기술 통계이며 효과 추정이나 기여율과 구분한다.

| 판정 항목 | 수집 전 규칙 |
|---|---|
| 정확성 | 영향 행·타입·순서·본문을 포함한 전체 행 digest를 독립 SQLite 실행과 대조, close/reopen 뒤 같은 digest와 page orphan/conflict 0. Python은 SQL 행·ID·payload·attempt·fence·terminal·purge·empty·reopen 검사. 기존 OOM·rollback·UNKNOWN 회귀 근거도 보존 |
| 메모리·중단 | shared 요청 128MiB, workspace 8MiB와 cache 기존 한도, whole-worker RSS 256MiB. worker의 생성·실행·종료·감사를 합쳐 120초, campaign 1,800초·보존 파일 8GiB·남은 디스크 1GiB. collection 한도이며 고객 SLO가 아님 |
| 보존 | 오류·timeout·설정/소스/binary 불일치·자료 손실에서 즉시 중단, 전체 예정·완료·실패·미실행 수 보존. 실패 DB/부분 stdout/stderr는 그대로 유지. 성공한 새 합성 DB만 gzip으로 무손실 보존하고 역해시 확인 후 작업용 전개본을 제거. 기존 원자료는 미수정 |
| 분석 | 10개 대응 비의 중앙값·범위·명목 95% 및 Bonferroni 조정 BCa. E1/E2는 12cell×4대비×4지표=192구간, 수명 12구간, E3 8구간을 각 독립 family로 고정. 퇴화·NaN·누락·불완전 campaign은 보류하며 방법 교체나 성공 표본만 분석 금지 |
| 개선 가설 | cache의 주 부하는 hit·256B·batch 1, workspace는 hit·256B·batch 32. 각각 다른 기능 OFF/ON 두 문맥 모두 조정 처리량 비 하한 1.05 이상, 나머지 모든 cell에서는 하한 0.95 이상 |
| 공통 보호 기준 | 모든 비교에서 조정 p99/CPU 비 상한 1.05 이하, worker RSS 증가 최대 16MiB, 논리 DB 크기 비 최대 1.10. workspace 메모리를 숨기거나 gzip 크기를 DB 크기로 사용 금지 |
| 수명·E3 가설 | SQL 단독 수명·E3 256B를 각 주 부하로 처리량 하한 1.05 이상, 다른 cell은 0.95 이상. 동일 tail/CPU/RSS/공간 보호 기준 적용 |
| 채택의 의미 | 위 수치는 5% 미만의 작은 국소 개선을 기본값 변경 근거로 쓰지 않기 위한 가역적 연구 기준. 통과는 검토 후보이며 자동 기본값 변경·제품 통합이 아님. 부적합·불확실은 이유와 반례를 보존하고 기존 기본값 유지 |

campaign의 1,800초는 수집 시작부터 fixture 생성·복제, worker, 결과 검증, 압축·역해시, raw fsync와 보존 자료 hash·manifest 기록까지 적용한다. 각 worker에는 120초와 campaign의 남은 시간 중 작은 값을 전달한다. 복사·압축·검증은 chunk마다 한도를 확인하고, 마지막 case와 manifest 기록 뒤에도 다시 검사한다. 동기 파일 I/O나 OS 스케줄링을 정확히 그 시각에 강제 중단한다는 보장은 없으며, 반환 뒤 한도 초과를 발견하면 전체 비교를 실패로 남긴다. 이미 생성한 자료의 실패 기록·hash 보존은 한도 뒤에도 마무리할 수 있지만 새 표본을 시작하거나 성공 분석으로 넘기지 않는다. manifest의 `elapsed_seconds`는 자료 hash 완료 뒤 기록 직전의 경과이며, manifest 기록에서 한도를 넘기면 초과 경과와 실패를 다시 기록한다.

`completed`는 검증·보존 뒤 raw에 기록한 case 수이고 `failed`는 시작했으나 완료하지 못한 case 수다. `unexecuted`는 시작하지 않은 case 수이며 세 수의 합은 `planned`와 같다. 완료된 마지막 case 뒤 한도를 넘으면 그 raw는 남기되 campaign 실패를 별도로 기록한다. 실패 기록 수를 실패 case 수로 대신하지 않는다. 보존 중 한도를 넘긴 DB는 전개본과 부분 압축본을 유지하며, 역해시 확인과 한도 확인 전에는 전개본을 제거하지 않는다.

현재 수집 코드와 이 계획을 함께 담은 commit을 정식 수집의 조상으로 지정한다. 수집기를 고친 뒤에는 이전 계획 commit을 그대로 재사용하지 않는다. `register`는 고정한 수집 코드·설정이 그 commit과 같은지 확인하고 현재 모든 제품 소스·Python·compiler·환경·fixture 입력과 양쪽 binary hash를 수집 전에 기록한다. E3는 구현 뒤 같은 기준으로 후보·실패 검증 근거를 추가 고정하며 그 결과를 보고 기준을 바꾸지 않는다. `verify`는 원자료 hash·대응 순서·정확성·전체 완료와 등록한 경과 한도를 검사하고 `analyze`는 원자료에서 같은 JSON을 재생성한다. 실행 명령은 [공통 비용 수집](../build-and-test.md#공통-비용-수집)에 있다. 비교 수집기가 준비됐다는 사실은 정식 실측·개선 효과·새 backend 완료를 뜻하지 않는다.

[자료 검증기](../../bench/common_cost_artifacts.py)는 등록 일정에서 필수 로그·fixture·DB gzip의 경로를 도출해 hash 목록 누락을 거절한다. raw에 적힌 archive는 정해진 DB 개수·경로와 gzip 역해시·해제 bytes·storage 합계가 일치해야 하며 fixture hash도 보존한 원본과 같아야 한다. 늦은 분석 파일은 필수 집합에 넣지 않는다. 명시적 과거 source 처리에도 현재 자료 검사를 적용하고 당시 commit의 실제 기준 파일 목록·hash를 대조한 뒤 원 분석을 재현한다. 이 보강은 기존 raw·실패 표본·효과·판정 기준을 소급 수정하거나 장부 전체 교체를 인증하지 않는다.

### 공통 비용 수집의 첫 판정

2026-10-24의 로컬 실행 `task15-cache-formal`은 cache와 workspace의 기본 적용을 모두 보류한다. 수리한 수집기·계획 commit `94552080`에서 등록한 576건을 468.181초에 완료했고 실패·미실행은 0건이다. 12cell의 비계측 정식 표본 480건, 사전 표본 48건, 계측 진단 48건을 구분한다. 정식 표본은 UPDATE 491,520건을 실행하고 전체 행 digest·재개방·page 무결성·소유권 검사를 통과했다. 원자료 hash 검증과 분석 JSON의 바이트 일치 재생성도 통과했다. 원자료와 상세 결과는 로컬 보존 상태이며 공개 통합·발행은 완료하지 않았다.

아래 값은 반복별 후보/기준 처리량 비 10개의 중앙값과 사전 고정한 Bonferroni 조정 BCa 구간이다. 각 조건의 처리량 중앙값을 나눈 값과 구분한다. cache의 주 부하는 hit·256B·batch 1, workspace는 hit·256B·batch 32다.

| 대상 | 기준→후보 | 대응 비 중앙값 | 조정 구간 | 판정 |
|---|---|---|---|---|
| cache, workspace OFF | `00→10` | 1.064 | 0.742–2.205 | 개선 하한·p99/CPU 보호 기준 미충족 |
| cache, workspace ON | `01→11` | 1.143 | 0.791–1.552 | 개선 하한·p99/CPU 보호 기준 미충족 |
| workspace, cache OFF | `00→01` | 1.311 | 0.876–2.230 | 개선 하한·p99 보호 기준 미충족 |
| workspace, cache ON | `10→11` | 1.073 | 0.619–2.037 | 개선 하한·p99/CPU 보호 기준 미충족 |

전체 표본의 worker RSS 최고는 98.594MiB, shared 요청 최고는 83.818MiB, workspace 내부 계상 최고는 0.680MiB였다. 대응 RSS 증가 최대 2.625MiB와 논리 DB 크기 비 최대 1.000은 자원 보호 기준 안이다. 세 RSS 비율 구간은 퇴화해 판정 근거에서 제외하지 않고 보류 사유로 남겼다. 공유 호스트의 수집 전 보존한 load average는 15.05/11.07/9.76이었고 코어는 10개다. 교대 순서는 유지했지만 이 변동을 장비·고객 모집단의 특성이나 두 기능의 무효과로 확정하지 않는다. 기본 OFF와 내부 비교 코드를 유지한다.

다음 단일 비용 가설은 [#239](https://github.com/woonyong-choi/totedb/issues/239)의 H4로 정한다. 같은 durable 응답과 barrier 횟수를 유지하면서 commit barrier의 wall/CPU 대기와 metadata·data write 작업을 나눠 확인한다. 계측 진단에서 batch 1의 fsync 배타 wall 시간은 ON 업무 완료 시간의 50.5–68.7%, batch 32는 18.3–42.2%였다. 이는 각 부하·조건 한 번씩의 기술 관측이며 OFF 기여율·장치 지연·인과 효과로 확대하지 않는다. hit·256B·batch 32에서 workspace는 catalog clone 1,024→32회, pwrite 3,136→742회였지만 barrier는 두 경로 모두 32회다. 중간 작업 감소와 전체 성능 채택은 별개다.

H4는 [cleanup 포함 backend 비교](#분리-비교의-범위)에서 같은 barrier·도착·반환 조건과 호스트 상태를 고정해 검증한다. sync 생략·자동 거래 합치기로 기존 응답 계약을 바꾸지 않는다. 이번 단일 writer COW 결과는 새 WAL·MVCC·실제 독립 writer의 실측이 아니며, 현재 WAL 권고와 buffered-COW 대조 후보를 유지한다.

등록 당시 schema 1 분석의 `preflight_records` 값 96은 사전·진단을 합친 잘못된 표기다. 원 분석과 등록·raw·manifest는 보존하고, 이 작업본의 schema 2에서는 비계측 block -1의 사전 48건, 비계측 정식 block 0–9의 480건, 계측 block -2의 진단 48건을 각각 계상한다. 다른 block·계측 조합은 거절한다. Python 수명·조건부 분석에도 같은 분류를 적용한다. 이 표기 수리는 정식 효과·대응 관계·구간·보류 판정과 수집 기준을 바꾸지 않는다.

소스가 바뀐 뒤의 과거 재현은 [공통 비용 수집](../build-and-test.md#공통-비용-수집)의 명시적 `--source-commit` 경로를 따른다. 등록한 모든 기준 소스의 hash를 지정한 로컬 조상 commit과 대조한 뒤 당시 분석 코드를 메모리에서 실행한다. 기본 재현은 잘못된 96 표기까지 포함한 원 JSON을 그대로 반환한다. `--correct-metadata`를 함께 지정하면 원 효과 계산을 유지하고 별도 schema 2 결과에 정확한 표본 수와 소스·처리기 hash를 남긴다. 새 수집에는 수리한 코드와 계획을 포함한 새 commit으로 등록하며 과거 등록을 소급 수정하지 않는다.

### 선택형 수명과 조건부 SQL의 첫 실측

2026-10-24의 로컬 실행 `task17-lifecycle-formal`과 `task17-conditional-formal`은 성능 가설을 모두 보류한다. 체크포인트 commit `df37ea61`에서 수집 전에 등록하고, 실제 runtime 소스·네 빌드·실패 검증 증거를 구현 commit `9d70c9b4`의 내용과 대조했다. 수명 비교는 예정 72건을 17.027초, 조건부 SQL은 예정 48건을 41.154초에 완료했다. 실패·미실행은 없고 정식/사전/진단은 수명 60/6/6건, 조건부 SQL 40/4/4건이다. 원자료 검증, 2,352개 합성 DB 압축본의 역해시와 분석 JSON의 바이트 일치 재생성도 통과했다. 원자료·등록·상세 JSON은 로컬 보존 상태이며 공개 통합·발행은 완료하지 않았다.

아래 값은 cell마다 비계측 정식 10쌍의 후보/기준 처리량 비 중앙값과 조정 BCa 구간이다. 수명의 후보는 lazy, 조건부 SQL의 후보는 `ACKQ_CONDITIONAL=1`이며 native cache/workspace는 모두 OFF다. 수명은 12구간, 조건부 SQL은 8구간의 사전 family를 유지했다. p99·CPU는 같은 조정 구간의 상한이며 보호 기준 1.05와 대조한다. 각 조건의 중앙값끼리 나눈 값과 구분한다.

| 조건 | 처리량 비 중앙값 | 조정 구간 | p99 비 상한 | CPU 비 상한 |
|---|---|---|---|---|
| 수명 SQL 단독 | 1.001 | 0.652–1.109 | 2.759 | 1.117 |
| 수명 큐 256B | 1.067 | 0.825–1.417 | 1.370 | 1.098 |
| 수명 큐 4,096B | 0.936 | 0.714–1.102 | 1.551 | 1.209 |
| 조건부 SQL 256B | 0.963 | 0.723–1.044 | 2.820 | 1.319 |
| 조건부 SQL 4,096B | 0.972 | 0.922–1.077 | 1.271 | 1.083 |

두 주 부하인 SQL 단독 수명과 조건부 256B의 처리량 하한은 1.05에 못 미쳤다. 나머지 조건도 처리량 하한 0.95와 p99·CPU 보호 기준을 충족하지 못했다. 중앙값이 1보다 큰 수명 256B도 채택하지 않는다. 조건부 SQL의 정상 호출 감소는 이 업무에서 개선 근거가 되지 못했지만, 이 표본을 보편적인 무효과·동등성이나 모든 부하의 성능 열세로 해석하지 않는다. 한 공유 호스트의 짧은 합성 실행이며 큰 지연 비의 원인을 scheduler·barrier·query 경로 중 하나로 분리하지 않았다.

전체 표본의 worker RSS 최고는 수명 18.344MiB, 조건부 SQL 17.094MiB였다. 정식 대응 RSS 차이의 최대는 수명 -0.812MiB, 조건부 SQL 0.406MiB였고 논리 DB 크기 비는 모두 1.000이었다. RSS·공간 보호 기준은 통과했지만 시간·CPU 판정을 대신하지 않는다. eager class를 과거 소스에서 적재하는 비교 방식은 API 시간 밖이고 process RSS에는 포함되므로 이 차이를 빈 set 하나의 allocator 절감이나 일반 메모리 우위로 귀속하지 않는다.

수명 worker는 DB 32개를 차례로 생성·사용·종료·재개방한다. 아래 생성·실행·종료 값은 정식 worker 10개의 각 단계 합계 중앙값(ms)을 기술한 추가 관측이며 채택 기준을 새로 만들지 않는다. 실행에는 SQL 또는 큐 작업·guard가 포함된다. 전체 처리량 분모에는 재개방과 검증도 남는다.

| 수명 입력 | 생성 eager→lazy(ms) | 실행 eager→lazy(ms) | 종료 eager→lazy(ms) |
|---|---|---|---|
| 수명 SQL 단독 | 21.087→20.711 | 10.955→12.385 | 0.453→0.465 |
| 수명 큐 256B | 20.160→19.568 | 48.110→46.513 | 0.536→0.565 |
| 수명 큐 4,096B | 26.952→26.380 | 50.014→54.118 | 0.546→0.699 |

이 Python 비교는 양쪽 모두 기존 C API의 무제한 session pool을 사용한다. 사전 계획의 shared 요청 128MiB는 native UPDATE 실행기의 한도이며 Python 경로에 적용한 한도로 보고하지 않는다. Python 수집에는 worker RSS 256MiB·시간·파일 보존 한도를 적용했다. 유한 query 예산에서의 거절·rollback은 별도 조건부 회귀 근거이며, 이번 RSS 관측이 bounded C API나 프로세스 전체 메모리 상한의 구현을 뜻하지 않는다.

실측의 E3는 단건 put/get/ack/purge 1,024회와 첫 작업의 중복·stale ACK 검사다. 배치 claim·장기 backlog·실제 병렬 writer·새 WAL 엔진의 판정으로 확대하지 않는다. 앞서 보존한 부분/32개 배치의 탐색 증가와 96KiB query 거절도 그대로 남긴다. ON 10건의 단계/I/O 진단과 각 조건의 ON/사전 OFF 비는 기술 자료이며 OFF 기여율로 사용하지 않는다.

조건부 경로는 기본 OFF인 비교 후보로 유지한다. Python lazy 수명은 전용 빈 set/list를 요구하지 않는 소유권 계약으로 유지하며, 이번 결과로 5% 성능 우위나 미사용 비용 0을 주장하지 않는다. 고정 입력·기준·분석 방법을 바꾸지 않았고, 다른 수치 계획의 재수집을 이번 결과에 덧붙이지 않는다. 재현은 `bench/common_cost.py verify`와 `analyze`에 각 등록·접두사를 전달하며, 소스가 바뀐 뒤에는 `--source-commit df37ea61`로 등록 당시 기준을 명시한다. WAL 권고와 동등한 buffered-COW 비교, pending·기본키 규모·실제 writer·stream·정리 포함 제품 검증은 미완료로 유지한다.

### 후속 구현과 검증

[구조 결정 #257](https://github.com/woonyong-choi/totedb/issues/257)은 계약과 선택 기준을, [후속 구현 #260](https://github.com/woonyong-choi/totedb/issues/260)은 독립 구현·측정 결과를 추적한다. 이슈 연결은 계약 확정·제품 코드·실측의 완료를 뜻하지 않는다. 각 과업에 필요한 G1–G6를 구체화하고, 각 과업은 자기 선행 조건과 검증 근거를 채운 뒤 완료한다.

| 결과 | 후속 과업 | 선행 조건과 보존할 검증 경계 |
|---|---|---|
| 준비 결과 cache | [#261](https://github.com/woonyong-choi/totedb/issues/261) | 공통 실행 계약으로 구현 착수, binding/schema·소유권·예산 회귀 뒤 #239의 hit/miss·비계측 업무 판정 |
| transaction 변경 누적 | [#262](https://github.com/woonyong-choi/totedb/issues/262) | #239의 공통 비용 선택과 G1–G3, cache/workspace 교차·오류/rollback·삭제/회수·peak memory |
| 실제 독립 writer | [#263](https://github.com/woonyong-choi/totedb/issues/263) | #238·#257의 계약과 #266 뒤 제품 통합, 비교용 원형의 프로세스별 실행 중첩·shared leaf·격리·UNKNOWN·회수 검증은 비교 전에 수행 |
| 소비자별 stream | [#264](https://github.com/woonyong-choi/totedb/issues/264) | #257의 전달·보존·실패 계약, fanout·lease/fence·replay·redrive·같은 DB 거래·형식 변경 시 이관/복원 |
| pending 조회 메모리 | [#265](https://github.com/woonyong-choi/totedb/issues/265) | G5와 Q1–Q5, 같은 소비 결과·실제 serializer·소유권·caller 축적·backlog별 전체 메모리와 읽기량 |
| buffered COW와 WAL/MVCC 총비용 | [#266](https://github.com/woonyong-choi/totedb/issues/266) | #238·#257 계약과 #261·#262 공통 개선, 비교용 prototype의 안전성·정리/회수·checkpoint/GC·실패 포함 판정 |
| SQLite·로컬 JetStream 제품 대안 | [#267](https://github.com/woonyong-choi/totedb/issues/267) | #263·#264의 필요한 구현/복구와 G5, 전달 의미·전체 앱 비용·외부 SQL 경계·같은 보장의 채택 조건 |
| 조건부 claim·ACK | [#268](https://github.com/woonyong-choi/totedb/issues/268) | #239·#257의 SQL/결과 선택, 최소 후보·expiry·stale/중복 ACK·실패 readback·실제 scan/쓰기/복사 비용 |
| 미사용 큐 초기화·종료 비용 | [#269](https://github.com/woonyong-choi/totedb/issues/269) | 선택형 수명 계약으로 구현 착수, SQL 단독 전용 비용과 명시 사용의 등록/해제·Busy close 회귀 뒤 효과 판정 |
| 기존 기본키 조회의 규모 검증 | [#158](https://github.com/woonyong-choi/totedb/issues/158) | 기존 구현·실패 표본을 보존하고 1천·1만·10만 행의 스캔 수와 p50/p95/p99를 같은 조건으로 3회 측정, 시간 제한의 부분 적재를 요청 규모 완료로 세지 않는 조건 |

공통 실행 계약을 먼저 구체화한 뒤 cache·workspace·선택형 수명·조건부 SQL을 각각 구현하고 검증한다. 수집기와 사전 계획을 고정한 뒤 대응 실측으로 각 비용을 판정한다. pending 조회와 기존 기본키 규모 검증은 별도 입력·결과로 남긴다. 앞선 보고서나 이슈 연결만으로 이 실측을 완료하지 않는다.

저장 방식 비교에는 먼저 G6의 record·version·복구 계약과 안전한 비교용 구현이 필요하다. WAL·buffer pool·MVCC·조기 잠금·다중 프로세스 제어를 단계별로 구현하고, buffered COW에도 같은 충돌 정책과 공통 개선을 적용한다. 두 후보의 정확성·실패 검증 뒤 총비용을 수집하며, 그 결과로 제품에 통합할 후보를 판정한다. #263의 제품 통합을 원형 구현의 선행으로 요구하지 않는다. stream은 전달·보존 계약, 기능 구현과 중단/압력 시험을 거친 뒤 제품 대안과 비교한다.

각 단계는 필요한 계약, 코드와 실제 검증 근거로 닫는다. 가역적인 시험용 선택은 이유와 반증 조건을 적어 진행하며 전체 설계의 공개 통합을 기다리는 무기한 보류로 바꾸지 않는다. correctness나 필수 요구가 미충족이면 다음 안전 단계 전에 해당 결함의 수리 과업을 넣는다. 부적합한 후보의 제외는 근거로 남기되, 제품의 필수 요구까지 완료한 것으로 처리하지 않는다.

[다중 쓰기 안전성 #238](https://github.com/woonyong-choi/totedb/issues/238)과 [공통 비용 개선 #239](https://github.com/woonyong-choi/totedb/issues/239)는 기존 결정 범위를 재사용한다. COW/WAL 비교용 prototype과 선택한 backend의 제품 구현은 구분한다. [표시명 변경 #258](https://github.com/woonyong-choi/totedb/issues/258)의 범위는 기존 패키지·import·ABI·파일 유지다. 후속 구현도 이를 보존하고, 형식 변경이 필요하면 별도 계약과 이관·mixed-version 거절·복원 검사를 거친다. 이 연결로 rename·기존 파일 cutover·운영 배포 권한을 넓히지 않는다.

### 요구사항

| 요구사항 | 검증 계획 |
|---|---|
| SQL·ACK·stream을 독립 또는 같은 DB 거래로 결합한다. | SQL 단독·ACK 단독·stream 단독·결합의 결과와 미사용 기능 호출·할당·I/O 대조 |
| 독립 transaction이 실제로 겹쳐 진행한다. | 다른 table·같은 leaf의 다른 행·같은 행·unique/range·append·공유 cursor의 조기 잠금·실행 중첩·교착·최대 대기와 재개방 결과 검사 |
| 입력·결과·메모리 소유권과 자원 한도를 유지한다. | 반복 binding·schema 변경·cache hit/miss·eviction·OOM·spill·긴 reader·중단 writer 검사 |
| pending 목록에서 필요한 결과와 조회 메모리를 통제한다. | Q1–Q5의 backlog/page/payload·metadata/본문·순차 소비/전체 축적·해제 전후와 실제 호출 경로 대조 |
| claim·ACK·재생·실패 격리가 소비자별로 정확하다. | fanout·out-of-order ACK·lease 재획득·stale fence·retry·DLQ·redrive·sequence hole 검사 |
| 성공 응답과 저장·복구 결과가 일치한다. | G6의 WAL 순서·LSN·page image·미확정 version·구조 변경·checkpoint/GC와 게시 전후 종료·sync 실패·UNKNOWN 재조회·이관/복원 검사 |
| 빈번한 갱신·삭제에서 총비용을 줄인다. | PUT/CLAIM/ACK/DELETE steady cycles, cleanup·checkpoint·회수 포함 CPU·지연·쓰기량·파일 성장 측정 |
| 단일 노드 JetStream·SQLite 대안과 같은 보장을 비교한다. | NATS R=1 file의 periodic sync와 per-write sync를 구분하고 전체 앱 자원·배포·통합 비용 비교 |

## 단점

- WAL 권고안은 공유 buffer·잠금·transaction 상태와 version 복구·checkpoint·GC를 새로 구현한다. COW 경험만으로 그 안전성을 승계할 수 없다.
- strict two-phase locking의 range/table 잠금은 읽기·대량 갱신에서 writer 대기와 교착을 늘릴 수 있다.
- buffered COW 대조군도 병합·재작성·snapshot 회수와 병렬 게시의 새 검증 비용을 부담한다.
- 소비자·ACK hole·보존·실패 metadata는 별도 상한과 청소가 필요하다.
- 별도 server가 없다는 사실만으로 NATS보다 CPU·RAM·지연이 작다는 결론은 나오지 않는다.
- 원래의 bounded 조회·장기 삭제 효율과 현재 성능 열세를 이름 변경으로 해결할 수는 없다.

## 대안

- SQLite 기반 큐·outbox: 구현·복구 부담을 줄일 수 있으므로 총비용 비교에 남긴다.
- 단일 노드 NATS: 이미 가볍게 배포할 수 있고 Go에는 서버 내장도 가능하므로 다중 노드 설정만 비교하지 않는다.
- buffered COW 기반 확장: 불변 page·기존 게시 경험을 재사용하지만 잦은 작은 갱신과 shared-leaf 병합 때문에 기본 방향에서 제외한다. 같은 계약의 총비용 비교에서 [권고를 바꾸는 조건](#권고를-바꾸는-조건과-보존할-것)을 충족하면 재선택한다.
- commit 때만 충돌을 찾는 낙관적 쓰기: 경합이 적으면 잠금 비용을 줄일 수 있지만 hot key에서 뒤늦은 전체 재작업을 기본 경로로 만들므로 현재 권고에서 제외한다.
- SQL 재실행 WAL과 현행 COW의 결합: 로그 승인 뒤에도 COW 본문 게시 비용을 내므로 이번 권고의 redo와 writeback 분리와 다르다. [기존 제거 이력](../development-report.md#d52-streamlite의-새-요구와-cow-유지교체의-판단-근거)과 구분한다.

## 미해결

- G1 격리·충돌: 내부 후보는 SNAPSHOT·locking 읽기와 FIFO·교착 victim·대기/잠금 한도를 위 계약으로 구분한다. 공개 API·기본값·전체 지원 문장과 실제 key/range/schema 구현·반환 결과 보존은 검증 전이다 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G2 불확정 결과: 비교 후보는 영속 요청 ID·결과 조회와 epoch/floor 보존을 선택한다. 실제 restart·동일/다른 내용·없는/만료 ID·본문 수명 구현과 제품의 조회 기간·보존 종료 API는 검증 전이다. 동적 SQL/callback의 자동 재실행은 지원 범위에 넣지 않는다 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G3 자원·보존: payload·메모리·pin·spill·in-flight·ACK holes·로그·실패/결과 기록의 bytes와 개수 한도, 유한 대기 시간, replay·멱등 보존 기간의 확정. 한도 도달 시 publish 거절 권고와 명시적 consumer expiry·데이터 손실 정책 비교 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G4 프로세스·이관: membership·lease·내구 owner epoch와 배타 복구 계약의 실제 프로세스 간 구현·사망/정지/fork 검증, 이관·역이관은 미완료다. 별도 시험 형식 선택을 기존 DB 전환으로 적용하지 않는다 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G5 비교 실행: E0–E7·조회 Q1–Q5의 구현·수집기, 실제 workload·도착률·timeout·환경·버전·입력 hash·재현 명령 고정, 최소 완료량·tail·공정성·조회 결과 수명·메모리·저장·복구 예산과 실질 개선폭의 수치 확정. 제안한 입력 규모·30분·정식 10쌍은 수집 가정이며 제품 SLO·검정력·실행 완료 근거가 아닌 조건 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G6 WAL·version 형식: 내부 형식·NO-STEAL·논리 abort·checkpoint/GC 계약은 위 절을 따른다. 로컬 record/segment transport와 별도로 transaction/group/결과 복구·가시성·구조 변경·공유 frame의 실제 구현과 R01–R18 전체 실패 증명, 양쪽 후보의 비용 수집은 미완료다 ([#263](https://github.com/woonyong-choi/totedb/issues/263), [#266](https://github.com/woonyong-choi/totedb/issues/266))
- 느린 소비자·보존 한도 충돌, 지원 subject·replay·DLQ/redrive의 정확한 API 계약 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- 표시명 이후 패키지·import·ABI·경로·파일 형식의 별도 호환 전환과 상표 등 권리 확인 ([#258](https://github.com/woonyong-choi/totedb/issues/258))
