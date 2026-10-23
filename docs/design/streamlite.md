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

### 요구사항

| 요구사항 | 검증 계획 |
|---|---|
| SQL·ACK·stream을 독립 또는 같은 DB 거래로 결합한다. | SQL 단독·ACK 단독·stream 단독·결합의 결과와 미사용 기능 호출·할당·I/O 대조 |
| 독립 transaction이 실제로 겹쳐 진행한다. | 다른 table·같은 leaf의 다른 행·같은 행·unique/range·append·공유 cursor의 진행과 충돌 검사 |
| 입력·결과·메모리 소유권과 자원 한도를 유지한다. | 반복 binding·schema 변경·cache hit/miss·eviction·OOM·spill·긴 reader·중단 writer 검사 |
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
- workload·지연·RAM·저장량 예산과 효과 채택 기준, COW/WAL 분리 실험 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- 느린 소비자·보존 한도 충돌, 지원 subject·replay·DLQ/redrive의 정확한 API 계약 ([#257](https://github.com/woonyong-choi/totedb/issues/257))
- 표시명 이후 패키지·import·ABI·경로·파일 형식의 별도 호환 전환과 상표 등 권리 확인 ([#258](https://github.com/woonyong-choi/totedb/issues/258))
