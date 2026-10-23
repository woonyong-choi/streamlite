# StreamLite 리뉴얼 구조

| 항목 | 값 |
|---|---|
| 상태 | 제안 |
| 관련 검토 | [구조 결정 #257](https://github.com/woonyong-choi/totedb/issues/257), [브랜드 전환 #258](https://github.com/woonyong-choi/totedb/issues/258) |
| 브랜드 | StreamLite, 기존 배포·API의 ToteDB 명칭 유지 |

## 요약

StreamLite는 SQL, 영속 이벤트 스트림, ACK 작업 처리를 각각 선택하거나 같은 트랜잭션에서 결합하는 내장 엔진을 목표로 한다. 여러 독립 쓰기 트랜잭션이 실제로 겹쳐 진행하면서 결과·격리·복구·자원 상한을 지키는 것이 다중 writer의 요구다. 요청을 받아 한 writer가 차례로 실행하는 것만으로 이 요구를 충족했다고 표시하지 않는다.

현재 구현은 SQL 부분집합·단일 writer·선택형 ACK다. 문장 캐시·트랜잭션 변경 누적·영속 fanout 스트림·실제 동시 writer의 갱신 설계는 구현 완료가 아니다. StreamLite 표시명을 채택해도 기존 `totedb` 패키지·Python import·`tote_*` C ABI·`libtotedb`·DB 파일 형식은 그대로 둔다.

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

SQL template와 실행별 binding을 분리하고 제한된 준비 결과 캐시를 검토한다. 현재 AST에는 binding 값이 복제되므로 SQL 문자열만 key로 AST를 재사용하지 않는다. 무효화는 schema 의미와 logical ID에 연결하고, 데이터 commit마다 물리 root가 바뀐다는 이유로 모든 준비 결과를 버리지 않는다.

불변 schema와 트랜잭션의 root·행 수 등 변경 상태를 분리한다. transaction-private dirty workspace에서 문장 간 변경을 누적하고 중복 인코딩·페이지 쓰기를 줄인다. 기존 fresh-page 재사용·읽기 cache는 유지하되 이것만으로 문장별 heap 완료 비용이 없어졌다고 보지 않는다. cache·결과·read/write set·private page·spill·회수 pin을 예산에 포함하고 OS 페이지 cache는 별도 측정한다.

### 영속 로그와 소비자 상태

이벤트 원본은 하나의 논리 payload 정본으로 보존하고, 소비자별 cursor·ACK hole·lease·fence·시도·실패 상태를 hot metadata로 관리한다. 원본 하나라는 뜻은 WAL·COW·cache·FFI 복사가 전혀 없다는 뜻이 아니다. 우선 같은 저장 backend와 transaction을 사용하고 별도 segment가 필요하면 그 끝 위치·checksum과 SQL root를 함께 commit하는 계약을 정한다.

새 claim은 소비자·이벤트·현재 상태를 조건으로 소유권을 얻고 fence를 갱신한다. ACK·retry·실패 격리·redrive는 해당 소비자의 소유권과 상태를 검사한다. 재생 위치와 보존 한도, 긴 reader·느린 소비자의 pin을 함께 관리한다. 느린 소비자를 무기한 보존하면서 저장량 제한과 모든 publish 성공을 동시에 약속하지 않는다.

### 다중 writer의 검증·병합·게시

각 transaction은 snapshot·schema version·필요 read/range set·logical mutation·private dirty map·할당/회수 집합을 소유한다. 다른 행을 바꿔도 같은 leaf·ancestor·catalog root를 공유할 수 있으므로, 옛 root를 그대로 교체하는 방식은 금지한다. 읽기·unique/range 충돌을 검증하고 최신 root와 변경을 병합한 뒤 짧은 게시 구간에서 다시 확인한다.

같은 프로세스의 여러 connection과 현재 지원하는 같은 호스트 다중 프로세스를 구분해 검증한다. 회수는 활성 snapshot과 복구 fallback root의 참조가 끝난 뒤에만 수행한다. 현재 두 commit 지연 규칙을 임의 길이 snapshot의 다중 writer에 그대로 적용하지 않는다. 예약 sequence와 committed visibility를 구분하고 미완료 번호를 소비자가 완료로 건너뛰지 않게 한다.

group commit은 독립 transaction의 충돌·결과·순서를 유지하고 자신의 결과가 포함된 durable barrier 뒤에만 성공을 응답한다. 게시 시작 뒤 timeout을 무반영 성공으로 돌려주지 않고 NOT_APPLIED와 UNKNOWN을 구분한다. 외부 callback이나 달라진 RETURNING 결과를 몰래 재실행하는 retry도 금지한다.

### 계약 제안과 현재 구현 경계

같은 leaf에 있는 행 A와 B를 두 transaction이 각각 변경한다고 가정한다. 먼저 끝난 A의 root 위에 B의 논리 변경을 합쳐 두 결과를 보존하는 것이 목표다. B가 옛 leaf 전체를 게시해 A를 지우면 정확성 실패다. leaf가 같다는 이유만으로 B를 항상 거절하면 안전할 수 있지만, 독립 쓰기의 진행 목표는 별도로 검증해야 한다.

아래 계약은 검토할 제안이다. 반환 이름은 동작을 구분하는 표기이며 새 C ABI·Python 예외·설정 키를 정의하지 않는다. 격리·보존·자원 정책의 선택과 수치는 `미해결`의 G1–G4에 남긴다. 현 코드의 지원 범위를 제안으로 소급해 넓히지 않는다.

| 경계 | 현재 구현의 근거 | 갱신안에서 확인할 차이 |
|---|---|---|
| writer와 격리 | [SQL transaction](../../include/sql/database_txn.h)의 단일 writer·SNAPSHOT, 읽기 전용 transaction handle 미지원 | 독립 writer의 동시 실행, 읽기·범위 검증, 긴 snapshot의 수명 |
| 프로세스와 잠금 | [쓰기 시작·종료](../../src/sql/database.c)의 `database_work_begin`·`database_work_end`가 작업 전체에서 파일 writer 잠금 보유 | 같은 프로세스의 여러 connection과 같은 호스트의 여러 직접 file writer를 각각 검증 |
| 결과 조회 | [transaction core](../../include/storage/transaction.h)의 `txn_query_commit`은 최근 writer 결과 ring과 같은 열기 세대에 한정 | 재시작 후 조회할 영속 식별자·결과 기록과 그 보존 종료 의미 |
| 게시와 회수 | [page file](../../include/storage/page_file.h)의 조건부 manifest 1-sync 또는 data sync·control write·sync, fallback을 위한 지연 회수 | 병렬 할당·group별 내구 경계, 활성 snapshot과 복구 root를 함께 보호하는 회수 |
| 메모리 | [ledger](../../include/sql/mem_ledger.h)의 요청 bytes 회계와 선택적 활성화 | transaction·connection·DB 전체 예산, 프로세스 간 예약과 pin·spill 상한 |

### writer 범위와 격리

목표는 같은 호스트에서 같은 DB를 연 여러 connection과 여러 프로세스의 직접 쓰기다. 단일 프로세스 실험을 먼저 하더라도 그것만으로 다중 프로세스 지원을 완료했다고 표시하지 않는다. 기존 file lock을 끈 채 프로세스 간 할당·게시·회수의 소유자를 비워 두는 전환은 허용하지 않는다. 한 connection은 한 번에 한 호출자가 소유하고, 병렬 실행은 서로 다른 connection에서 수행하는 안을 권고한다.

격리는 serializable 검증안을 권고한다. 각 transaction은 시작 snapshot과 자기 변경을 읽고, commit 때 읽은 행·없는 key·쓰기 전제·unique key와 필요한 predicate/range를 검증한다. 예를 들어 두 writer가 서로 다른 행을 바꿔도 같은 집계 조건에 의존하면 둘 다 성공시켜 업무 조건을 깨뜨릴 수 있다. snapshot isolation만으로 충분한 작업과 이 검증이 필요한 작업을 구분한다. serializable은 실행 결과를 어떤 직렬 순서와 같게 만드는 목표이며 외부 효과까지 포함하는 보장은 아니다.

정확한 범위 추적이 없는 scan·집계는 table/schema 변경 version으로 보수적으로 검증하는 안을 둔다. 이것도 불가능한 문장은 동시 쓰기 모드에서 명시적으로 거절한다. 검증하지 못한 읽기를 조용히 제외하거나 SNAPSHOT을 serializable로 표시하지 않는다. 보수적 table 검증은 무관한 행 변경도 충돌로 돌릴 수 있으므로 비용을 비교해야 한다. 모든 읽기 의존성과 schema 변경을 검증할 수 있기 전에는 이 격리안을 제공하지 않는다.

row version·unique/range·schema 전제가 그대로인 논리 변경만 최신 root에 합친다. 같은 행의 갱신·삭제, 같은 unique key의 삽입, 읽은 범위의 phantom이 전제를 바꾸면 전체 transaction을 충돌·확정 미적용으로 끝낸다. 물리 root만 바뀌면 제한된 내부 병합을 검토하되, 재계산한 결과가 앞서 반환한 행 수·ID·`RETURNING` 값과 달라지면 충돌을 반환한다. 이미 실행한 사용자 callback과 외부 효과는 엔진이 재실행하지 않는다. 호출자가 새 transaction으로 다시 실행할지는 별도 책임이다.

후보 root는 가능한 한 게시 잠금 밖에서 만들고 잠금 안에서 base·schema·읽기/쓰기 검증을 다시 확인한다. 바뀌었으면 제한된 병합 재시도 또는 충돌로 끝낸다. 한 writer의 열린 transaction이 다른 독립 writer의 실행 전체를 막지 않는 것이 목표다. durable barrier나 공유 cursor 때문에 직렬 구간이 남을 수 있으므로 짧은 잠금과 처리량 향상은 실측할 항목이다. 충돌률·재시도 상한·최대 대기·공정성 수치는 아직 정하지 않는다.

### commit 결과와 취소

transaction의 SQL 결과는 commit 전에는 잠정 결과다. 업무 변경·이벤트·해당 소비자의 완료 기록은 같은 backend transaction에서 전부 반영하거나 전부 미적용으로 끝낸다. 다른 DB의 거래와 외부 작업은 이 원자성에 포함하지 않는다. group commit도 transaction별 검증과 결과를 유지하며, 그룹에서 제외한 transaction을 성공으로 응답하지 않는다.

| 상태 | 뜻 | 다음 상태 |
|---|---|---|
| 실행 중 | snapshot·binding·논리 변경과 private pages를 소유, 외부에 미게시 | 검증 중 또는 확정 미적용 |
| 검증 중 | 읽기/쓰기 전제 검사와 최신 root의 후보 구성 | 게시 중, 실행 중의 제한된 병합, 또는 확정 미적용 |
| 게시 중 | 조정자가 영속 상태를 바꿀 수 있어 단순 취소로 미적용을 약속할 수 없는 구간 | 반영·내구 확인, 확정 미적용의 증명, 또는 불확정 |
| 반영·내구 확인 | 해당 transaction을 포함하는 durable barrier 완료 | 성공 응답과 결과 조회 기록 유지 |
| 확정 미적용 | 어느 유효한 게시 상태에도 반영하지 않았음을 확인한 `NOT_APPLIED` | rollback 정리와 종료 |
| 불확정 | 반영 여부 또는 요청 내구성 달성 여부를 확인할 수 없는 `UNKNOWN` | 쓰기 차단과 복구·결과 대조 |

취소·deadline은 조정자가 게시 권한을 넘겨받기 전까지 확인한다. 이때 변경을 게시하지 않고 private 자원을 정리할 수 있으면 확정 미적용이다. 권한 인수와 취소 처리는 같은 동기화 경계로 순서를 정한다. 인수 뒤 취소·timeout은 rollback 성공의 증거가 아니다. durable 결과를 확인했다면 그 결과를 반환하고, 응답이 끊겼거나 sync·control 기록 결과를 확정하지 못하면 불확정으로 다룬다. 단순 대기 만료, 검증 충돌, 자원 부족, 게시 I/O 실패를 같은 재시도 오류로 합치지 않는다.

COW의 sync 순서와 manifest 완전성, WAL 안의 WAL-before-data와 commit record는 각각 선택한 backend가 증명한다. 둘 다 해당 transaction의 내구 경계 뒤에만 durable 성공을 반환한다. sequence는 검증 후 게시 순서에서 부여하는 안을 우선한다. 예약 번호를 먼저 발급하면 미완료·취소 hole과 committed frontier를 구분한다. 소비자는 최대 예약 번호만 보고 중간 이벤트를 완료 처리하지 않는다.

불확정 뒤에는 관련 DB의 게시·회수에 참여하는 writer들을 조정해 추가 쓰기를 차단한다. 어느 root가 유효한지 모르는 페이지·spill을 반환하지 않는다. 다른 프로세스도 차단 상태를 확인하는 방식과 복구 소유권을 검증해야 한다. 검증된 snapshot 읽기조차 보장할 수 없으면 읽기도 거절한다. 다시 열어 구조가 정상이라는 사실만으로 원래 요청이 미적용이었다고 결론 내리지 않는다.

재시작 후 응답 유실을 해결하려면 DB 식별자와 재사용하지 않는 요청 식별자를 데이터 변경·결과 기록과 함께 원자적으로 보존하는 안을 검토한다. 같은 ID·다른 요청 내용은 거절하고, 유효한 기록이 있으면 재실행 대신 기존 결과를 반환한다. 기록·멱등 key·결과 본문의 보존 기간과 bytes 상한도 자원 계약에 포함한다. 보존이 끝나거나 기록을 찾지 못하면 `NOT_APPLIED`를 추론하지 않고 조회 범위 밖임을 반환한다. 이 기능이 없으면 자동 재전송을 보장하지 않고 업무 key로 대조하는 책임을 밝힌다. 현재 메모리 ring은 이 영속 조회의 구현 근거가 아니다.

### 소유권과 자원 상한

| 자원 | 소유자와 수명 | 실패·한도에서의 처리 제안 |
|---|---|---|
| 준비 결과와 schema | 공유 cache가 값 없는 불변 template와 schema 식별자를 보유, 실행 중 참조 유지 | schema 변경 때 새 사용 무효화, 참조 중 해제 금지, cache bytes 상한과 eviction |
| SQL 입력·binding | 호출 동안 빌린 입력과 transaction이 계속 쓸 복사본 구분 | 호출 뒤 재사용할 값은 소유권 확보와 예산 예약, 이전 binding 재사용 금지 |
| read/range set·논리 변경·private pages | transaction이 rollback·commit 정리까지 보유 | 후보 병합의 동시 사본까지 먼저 예약, OOM이면 부분 게시 없이 실패 |
| 결과와 payload view | C 결과·Python 객체의 소유자와 해제 지점 구분 | borrowed view는 유효 기간과 pin 계상, 소유권 이전은 예산도 이전 |
| 게시 후보·group buffer | commit 조정자가 barrier와 결과 확정까지 보유 | 그룹 전체·transaction별 예약, 한도 때문에 내구 확인 생략 금지 |
| retire 목록·snapshot pin | DB 회수 관리자가 활성 reader/writer와 fallback 참조 종료까지 보유 | 논리 삭제와 물리 재사용 분리, 나이만으로 사용 중 페이지 재사용 금지 |
| spill과 결과 조회 기록 | transaction 또는 복구 관리자가 파일·ID·보존 종료를 소유 | private·미게시·checksum·disk quota·중단 복구를 검증하기 전 spill 자동 활성화 금지 |

한도는 건수와 bytes를 함께 다룬다. 최대 payload·반환 bytes·in-flight bytes·ACK holes·private 작업량·cache·retire/pin·spill·보존 로그·실패 기록·결과 조회 기록의 범위를 정해야 한다. 연결별 한도만 두면 연결을 늘려 전체 한도를 우회하므로 DB 전체 한도와 여러 프로세스의 예약 회계를 함께 검증한다. 현재 ledger의 요청 bytes를 allocator overhead·단편화·RSS·OS page cache와 같다고 표시하지 않는다.

초기 검증안은 게시 전에 필요한 메모리와 디스크 여유를 예약하고, 확보할 수 없으면 유한 대기 또는 명시적 자원 거절로 끝낸다. 무제한 대기·재시도나 검증하지 않은 spill을 기본값으로 숨기지 않는 안을 권고한다. 큰 transaction을 자동 분할하면 원자성이 달라지므로 호출자가 선택하지 않은 분할은 하지 않는다. 수치·대기 정책을 정하기 전 자원 상한 달성을 주장하지 않는다.

중단 writer와 긴 reader의 등록에는 프로세스 재시작·PID 재사용·소유 세대를 구분하는 식별과 참조 해제가 필요하다. 시간이 오래 지났다는 이유만으로 살아 있는 snapshot의 pin을 훔치지 않는다. 죽은 소유자의 부재와 게시 권한 회수를 증명한 뒤에만 예약·pin을 회수한다. 증명할 수 없으면 신규 예약을 제한하고 운영자가 원인을 확인할 상태를 제공한다. 긴 reader가 보존 공간을 붙잡는 동안에도 다른 writer가 진행할 수 있는 범위는 남은 예산에 한정한다.

### 소비자 보존과 이관

느린 소비자와 저장 상한이 충돌할 때는 기존 이벤트를 몰래 버리기보다 신규 publish를 거절하거나 제한된 시간만 기다리는 안을 권고한다. 자동 consumer expiry·강제 truncate는 별도 데이터 손실 정책이다. replay 기간·최대 보존 bytes·소비자별 지연·실패 격리 및 redrive pin의 한도와 우선순위를 정하기 전 무기한 보존이나 모든 publish 성공을 약속하지 않는다.

논리 삭제는 필요한 소비자의 ACK horizon, 허용 replay 범위, 실패 기록·redrive·멱등 기록의 참조가 모두 끝난 자료만 대상으로 한다. 물리 재사용에는 여기에 활성 snapshot과 recovery fallback 참조의 종료도 필요하다. 보존 밖 replay는 빈 성공 대신 요청 위치가 남아 있지 않음을 알린다. 같은 fence의 중복 ACK는 ACKED 기록이 남은 동안에만 기존 완료로 판정하고, 새 fence 뒤 옛 ACK는 거절한다. FAILED를 ACKED로 바꾸는 호출은 redrive와 구분해 거절한다. terminal 기록을 지운 뒤 중복인지 stale인지 증명할 수 없으면 구분 불가를 반환한다. 한 소비자의 ACK·실패·redrive는 다른 소비자의 상태를 바꾸지 않는다.

기존 single-writer backend·배포 기본값·파일은 전환 검증이 끝날 때까지 fallback으로 보존한다. 새 형식이 필요하면 원본을 보존한 채 별도 DB에 논리 이관하고 행·색인·event·consumer 상태·미완료 lease/fence·보존 이력을 대조한다. 새 형식 식별자·별도 경로·배타 전환으로 mixed-version writer 진입 거절을 검증한다. 검증하지 않은 구버전 프로세스가 새 DB를 열 수 있는 경로로 전환하지 않는다.

전환 후 새 쓰기가 생기면 옛 DB로 코드만 되돌려 그 쓰기를 복원할 수 없다. 무쓰기 전환 구간의 취소와, 새 쓰기를 보존하는 역이관·복원은 다른 검사다. export/import·backup/restore·복구 결과 대조와 역변환 한계를 확인하기 전 원본 덮어쓰기나 자동 cutover는 하지 않는다. SQL 사용자에게 queue/stream 전용 호출·분기·할당을 추가하지 않는 요구도 전환 전후 각각 검사한다.

### 시나리오와 계약 검토

이 표는 제안을 검토할 입력과 기대 결과다. 실행한 시험 결과가 아니며 다중 writer 안전성·성능 입증을 대신하지 않는다.

| 입력·중단 지점 | 제안 계약의 기대 결과 | 구현 시 필요한 관측 |
|---|---|---|
| 열린 A transaction과 독립 B writer | B가 A의 종료 전체를 기다리지 않고 실행·commit, A도 전제가 유지되면 병합 | connection·프로세스별 겹친 실행 시간과 durable 결과, 최대 대기 |
| 같은 leaf의 다른 두 행 | 최신 root에 둘 다 보존, 제한된 병합 실패는 명시적 충돌 | barrier로 실행 순서 고정, 두 행·ancestor·catalog와 재개방 결과 |
| 같은 행의 update/delete | 오래된 전제로 덮어쓰기 금지, 충돌한 거래 전체 미적용 | 행·색인·연관 이벤트에 부분 반영 없음 |
| 같은 unique key 삽입 | 두 성공 금지, unique 충돌 또는 검증 충돌 | 재개방 뒤 unique 불변식과 실패 거래의 무반영 |
| 범위·집계 뒤 다른 행 쓰기 | phantom·write skew가 생기는 두 거래의 동시 성공 거절 | 읽은 빈 범위·집계 전제, 보수적 table 검증과 schema 변경 |
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

조건부 출발점은 공통 비용 제거 후 buffered COW·검증/병합과 로그·hot state 모델을 분리 검증하는 것이다. 기존 COW가 항상 가장 빠르거나 안전하다는 결정은 아니다. shared-root rebase·게시 대기·경로 재작성·snapshot 회수가 목표 tail·공정성·자원 상한을 못 채우거나 새 검증 비용이 WAL 설계 수준이면 WAL·buffer pool·MVCC 안과 비교한다.

WAL 안도 WAL-before-data·page LSN·torn-page 보호·복구·checkpoint·version GC가 필요하다. SQLite·LMDB 또는 WAL이라는 이름만으로 진짜 동시 writer와 복구 안전성을 충족했다고 표시하지 않는다. 일반 SQLite backend는 총 구현 위험을 줄이는 대안이지만 실제 병렬 writer 요구는 충족하지 못한다.

COW 교체는 아직 결정하지 않았다. 기존 문장 WAL 제거와 H2/H3 후보 철회는 서로 다른 이유와 범위의 선택이다. [개발 선택 D52](../development-report.md#d52-streamlite의-새-요구와-cow-유지교체의-판단-근거)에 그 근거와 COW 교체 시 남길 실패 조건·대안 결과·감수할 비용을 연결한다. 계약과 공통 개선을 맞춘 대조 없이 기존 COW의 실패나 WAL의 우위로 결론 내리지 않는다.

### 분리 비교의 범위

반복 SQL 준비 비용이 줄어든 COW와 준비 비용이 그대로인 WAL을 비교하면 저장 방식의 차이를 분리할 수 없다. 같은 변경 누적·준비 결과 재사용·payload 배치를 적용한 두 후보에서, COW의 병합·재작성·회수와 WAL의 redo·writeback·checkpoint·version GC를 비교한다. 짧은 put→drain만으로 장기 삭제·재사용이나 다중 writer의 이점을 판정하지 않는다.

아래 E0–E7은 구현 후 사전 등록할 비교의 설계안이다. 수집기·실행 명령·환경·G1–G5를 채운 실행 계획의 대체물이 아니다. 기존 [로컬 DB 비교 계획](../experiments/local-db-comparison/design.md)과 [release 비용 계획](../experiments/release-cost/design.md)은 사전 지식으로만 사용하고 원자료·실패 표본·당시 판정은 고치지 않는다. 새 표본을 과거 결과에 합치거나 서로 다른 실험의 개선 배수를 곱하지 않는다.

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

정식 수집 전 후보·기준 commit과 binary hash, 도구·binding·실제 SQLite 링크 버전, OS·장치·filesystem, 설정 조회값, 입력 hash·작업 seed, 도착률·종료 조건·timeout, 결과 분석 버전, G1–G5를 실행 계획에 고정한다. 명령과 env 기록을 재현할 수 없으면 수집하지 않는다. 기존 성공 경로 비교 도구를 새 concurrency·장기 stream 시험에 그대로 쓸 수 있다고 가정하지 않는다.

정확성 실패·설정 불일치·원자료 손실을 발견하면 해당 비교의 성능 수집을 중단하고 DB·부분 결과·원본 오류를 보존한다. 계획한 반복 수와 실제 완료·실패·미실행 수를 모두 남긴다. 실행·복구·정리 timeout과 전체 수집 시간/저장 예산도 수집 전에 정한다. 느린 실행을 이상치로 제거하거나 실패를 성공 실행으로 교체하지 않는다.

고정 작업의 지연은 API 호출, 지속 부하의 지연은 예정 도착에서 시작해 각각 최종 durable 응답까지 잰다. 두 경계를 같은 비율로 비교하지 않는다. 실행별 percentile은 `ceil(p*n)`번째 관측값을 택하는 nearest-rank로 고정한다. `p`는 0과 1 사이의 누적 비율이고 `n`은 지연 관측 수다. 전체 요청 수와 미완료 요청의 censor·timeout도 별도로 보존하며 성공한 요청의 percentile만으로 그 비교를 통과시키지 않는다.

처리량은 대응한 `후보/기준` 비율 10개와 그 중앙값·범위를 보고한다. tail도 실행별 p99의 대응 비를 내고 서로 다른 실행의 percentile을 전체 요청 p99라고 합치지 않는다. 재표집은 실행 쌍을 단위로 유지하고 [SciPy bootstrap 계약](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.bootstrap.html)의 paired BCa·10,000회·seed 257을 분석안으로 둔다. 분석 runtime과 RNG 버전도 고정한다. 퇴화 표본·NaN·대응 누락은 판정 보류이며 유리한 방법으로 사후 교체하지 않는다. 같은 호스트의 반복 구간은 해당 실행 변동의 근사 추정이며 장비·고객 모집단의 보장이 아니다.

E1–E4의 여러 원인 탐색 구간은 명목 95%로 표시하고 그중 좋은 구간을 골라 채택하지 않는다. 채택 비교의 후보·주요 부하·효과와 판정할 구간 수 `m`을 정식 수집 전에 고정하고 각 구간의 신뢰수준을 `1 - 0.05/m`으로 조정하는 [Bonferroni 안](https://www.itl.nist.gov/div898/handbook/prc/section4/prc473.htm)을 둔다. 이 보정이 bootstrap의 작은 표본 불확실성이나 공유 호스트의 상관을 해결하지는 않는다. 후보를 탐색 표본에서 골랐으면 새로운 정식 표본으로 비교한다. 정밀도가 부족하면 보류하고 변경한 계획과 새 표본을 별도 등록한다. 실패·timeout·부하 미완료를 빼고 성공 표본만으로 효과 비를 만들지 않는다.

G5에는 부하별 최소 완료량 `Q_min`, p99 상한 `L99_max`, writer 최대 대기 `W_max`, 메모리·저장량·회수 debt 상한, 복구 시간, 실질 개선폭 `delta_min`과 tail 악화 허용폭 `epsilon99`를 값·단위·구간·대상과 함께 정한다. 실제 사용자 부하·자원과 연결되지 않은 임의 값을 승인된 합격선으로 쓰지 않는다. 아래 식의 `L`과 `U`는 조정한 구간의 하한과 상한이다.

| 판정 | 사전 조건과 해석 |
|---|---|
| 비교 불가 | 기능·원자성·isolation·내구성·보존·측정 경계가 다르거나 설정/완료 자료가 불완전 |
| 후보 탈락 | 정확성 실패, 승인된 자원·지연·공정성·복구 상한 위반, 기존 DB·기능 보존 실패 |
| 효과 보류 | G1–G5 미확정, 표본 부족·구간 퇴화, 비율 구간에 1 포함, 또는 실질 개선 기준 미달. 무효과·동등성의 증명은 아님 |
| 성능 채택 후보 | 모든 안전·자원 게이트 통과 후 주요 부하에서 `L(처리량 비) > 1 + delta_min`, `U(p99 비) <= 1 + epsilon99`, `Q_min`·`L99_max`·`W_max` 충족 |
| 제품 채택 후보 | 필요한 로컬 전달 의미, 전체 앱 배포·자원·연동 이익, SQLite 조합 대비 이익을 각각 근거로 충족. 실제 병렬 writer 미충족을 다른 장점으로 상쇄하지 않는 조건 |

같은 예산에서 COW가 shared-root 병합·경로 재작성·pin/회수·공정성 기준에 실패하고 WAL 안이 통과하면 교체 근거가 된다. COW의 국소 구현 결함인지 구조상 비용인지와 배제하지 못한 원인도 남긴다. 둘 다 실패하면 어느 쪽도 채택하지 않으며 SQLite 제품 대안과 요구·비용을 다시 비교한다. SQLite를 택해 실제 병렬 writer 요구를 완화하려면 별도 명시적 결정이 필요하다. 우열이 불확정이면 기존 backend를 보존한다.

탈락·철회에는 실패한 부하와 사전 기준, 양쪽 commit·설정·원자료·실패 표본, 원인 경로와 반례, 포기할 기존 보장, 새 복구·checkpoint·이관 부담과 복원 범위를 D52에 연결한다. 수집 전 계획 commit이 결과의 조상인지, 원자료 hash와 집계 재생성이 일치하는지도 검사한다. 성능 채택 후보가 나와도 기본 backend 변경·기존 파일 전환·배포 완료를 뜻하지 않는다.

### 후속 구현과 검증

[구조 결정 #257](https://github.com/woonyong-choi/totedb/issues/257)은 계약과 선택 기준을, [후속 구현 #260](https://github.com/woonyong-choi/totedb/issues/260)은 독립 구현·측정 결과를 추적한다. 이슈 연결은 계약 확정·제품 코드·실측의 완료를 뜻하지 않는다. G1–G5를 먼저 정하고, 각 과업은 자기 선행 조건과 검증 근거를 채운 뒤 완료한다.

| 결과 | 후속 과업 | 선행 조건과 보존할 검증 경계 |
|---|---|---|
| 준비 결과 cache | [#261](https://github.com/woonyong-choi/totedb/issues/261) | #239의 cache 선택과 G1–G3, binding/schema·hit/miss·예산·비계측 업무 결과 |
| transaction 변경 누적 | [#262](https://github.com/woonyong-choi/totedb/issues/262) | #239의 공통 비용 선택과 G1–G3, cache/workspace 교차·오류/rollback·삭제/회수·peak memory |
| 실제 독립 writer | [#263](https://github.com/woonyong-choi/totedb/issues/263) | #238·#257의 계약과 #266의 저장 방식 비교, 프로세스별 실행 중첩·shared leaf·격리·UNKNOWN·회수·기존 DB/이관 복구 |
| 소비자별 stream | [#264](https://github.com/woonyong-choi/totedb/issues/264) | #257의 전달·보존·실패 계약, fanout·lease/fence·replay·redrive·같은 DB 거래·형식 변경 시 이관/복원 |
| pending 조회 메모리 | [#265](https://github.com/woonyong-choi/totedb/issues/265) | G5와 Q1–Q5, 같은 소비 결과·실제 serializer·소유권·caller 축적·backlog별 전체 메모리와 읽기량 |
| buffered COW와 WAL/MVCC 총비용 | [#266](https://github.com/woonyong-choi/totedb/issues/266) | #238·#257 계약과 #261·#262 공통 개선, 비교용 prototype의 안전성·정리/회수·checkpoint/GC·실패 포함 판정 |
| SQLite·로컬 JetStream 제품 대안 | [#267](https://github.com/woonyong-choi/totedb/issues/267) | #263·#264의 필요한 구현/복구와 G5, 전달 의미·전체 앱 비용·외부 SQL 경계·같은 보장의 채택 조건 |
| 조건부 claim·ACK | [#268](https://github.com/woonyong-choi/totedb/issues/268) | #239·#257의 SQL/결과 선택, 최소 후보·expiry·stale/중복 ACK·실패 readback·실제 scan/쓰기/복사 비용 |
| 미사용 큐 초기화·종료 비용 | [#269](https://github.com/woonyong-choi/totedb/issues/269) | #239·#257의 선택형 수명 계약, SQL 단독 전용 관리 비용과 명시적 기능 사용의 등록/해제 보존 |

[다중 쓰기 안전성 #238](https://github.com/woonyong-choi/totedb/issues/238)과 [공통 비용 개선 #239](https://github.com/woonyong-choi/totedb/issues/239)는 기존 결정 범위를 재사용한다. COW/WAL 비교용 prototype과 선택한 backend의 제품 구현은 구분한다. [표시명 변경 #258](https://github.com/woonyong-choi/totedb/issues/258)의 범위는 기존 패키지·import·ABI·파일 유지다. 후속 구현도 이를 보존하고, 형식 변경이 필요하면 별도 계약과 이관·mixed-version 거절·복원 검사를 거친다. 이 연결로 rename·기존 파일 cutover·운영 배포 권한을 넓히지 않는다.

### 요구사항

| 요구사항 | 검증 계획 |
|---|---|
| SQL·ACK·stream을 독립 또는 같은 DB 거래로 결합한다. | SQL 단독·ACK 단독·stream 단독·결합의 결과와 미사용 기능 호출·할당·I/O 대조 |
| 독립 transaction이 실제로 겹쳐 진행한다. | 다른 table·같은 leaf의 다른 행·같은 행·unique/range·append·공유 cursor의 진행과 충돌 검사 |
| 입력·결과·메모리 소유권과 자원 한도를 유지한다. | 반복 binding·schema 변경·cache hit/miss·eviction·OOM·spill·긴 reader·중단 writer 검사 |
| pending 목록에서 필요한 결과와 조회 메모리를 통제한다. | Q1–Q5의 backlog/page/payload·metadata/본문·순차 소비/전체 축적·해제 전후와 실제 호출 경로 대조 |
| claim·ACK·재생·실패 격리가 소비자별로 정확하다. | fanout·out-of-order ACK·lease 재획득·stale fence·retry·DLQ·redrive·sequence hole 검사 |
| 성공 응답과 저장·복구 결과가 일치한다. | 게시 전후 종료·짧은/torn/reordered write·sync 실패·disk full·UNKNOWN 재조회·이관/복원 검사 |
| 빈번한 갱신·삭제에서 총비용을 줄인다. | PUT/CLAIM/ACK/DELETE steady cycles, cleanup·checkpoint·회수 포함 CPU·지연·쓰기량·파일 성장 측정 |
| 단일 노드 JetStream·SQLite 대안과 같은 보장을 비교한다. | NATS R=1 file의 periodic sync와 per-write sync를 구분하고 전체 앱 자원·배포·통합 비용 비교 |

## 단점

- COW의 병합·회수와 WAL의 복구·checkpoint 모두 새 안전성 검증 비용이 크다.
- 소비자·ACK hole·보존·실패 metadata는 별도 상한과 청소가 필요하다.
- 별도 server가 없다는 사실만으로 NATS보다 CPU·RAM·지연이 작다는 결론은 나오지 않는다.
- 원래의 bounded 조회·장기 삭제 효율과 현재 성능 열세를 이름 변경으로 해결할 수는 없다.

## 대안

- SQLite 기반 큐·outbox: 구현·복구 부담을 줄일 수 있으므로 총비용 비교에 남긴다.
- 단일 노드 NATS: 이미 가볍게 배포할 수 있고 Go에는 서버 내장도 가능하므로 다중 노드 설정만 비교하지 않는다.
- WAL 기반 저장: 동시성·갱신과 복구 비용을 같은 workload로 대조한 뒤 채택 여부를 판단한다.

## 미해결

- G1 격리·충돌: serializable 검증 권고와 명시적 snapshot isolation 중 선택, 보수적 table 검증의 비용·지원 문장 범위·호출자 retry·결과 반환 계약의 확정. 검증 비용이 요구 부하를 못 채우고 앱이 write skew를 허용하는 조건에서 격리 대안을 다시 비교 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G2 불확정 결과: 영속 요청 ID·결과 조회 권고와 업무 key 대조 방식의 선택, 재시작·만료·동일 ID의 다른 내용·응답 본문 보존 범위의 확정. 기록 비용과 필요한 재전송 기간이 선택 기준 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G3 자원·보존: payload·메모리·pin·spill·in-flight·ACK holes·로그·실패/결과 기록의 bytes와 개수 한도, 유한 대기 시간, replay·멱등 보존 기간의 확정. 한도 도달 시 publish 거절 권고와 명시적 consumer expiry·데이터 손실 정책 비교 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G4 프로세스·이관: 같은 프로세스 실험과 같은 호스트 직접 file writer의 별도 지원 게이트, 복구 소유권·프로세스 간 예약/차단·형식 전환·역이관 검증과 구현 분할. 기존 다중 프로세스의 안전한 단일 writer 지원을 조용히 축소하지 않는 조건 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- G5 비교 실행: E0–E7·조회 Q1–Q5의 구현·수집기, 실제 workload·도착률·timeout·환경·버전·입력 hash·재현 명령 고정, 최소 완료량·tail·공정성·조회 결과 수명·메모리·저장·복구 예산과 실질 개선폭의 수치 확정. 제안한 입력 규모·30분·정식 10쌍은 수집 가정이며 제품 SLO·검정력·실행 완료 근거가 아닌 조건 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- 느린 소비자·보존 한도 충돌, 지원 subject·replay·DLQ/redrive의 정확한 API 계약 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- 표시명 이후 패키지·import·ABI·경로·파일 형식의 별도 호환 전환과 상표 등 권리 확인 ([#258](https://github.com/woonyong-choi/totedb/issues/258))
