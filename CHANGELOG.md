# 변경 기록

버전별 주요 변경을 적는다. 형식: [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/). 버전: [유의적 버전](https://semver.org/lang/ko/). 1.0 전에는 파일 형식과 API가 호환 없이 바뀔 수 있다.

## 0.3.0 - 미배포

### 변경

- 공개 이름을 StreamLite로 변경: 배포·설치 이름과 Python import `streamlite`, C API `streamlite_*`, 공유 라이브러리 `libstreamlite`. 이전 이름의 별칭은 제공하지 않는다.
- DB 파일 형식을 형식 4 하나로 정리: 16바이트 전체 magic과 SL codec을 쓰며, 이전 ToteDB 형식 파일을 읽는 reader는 제공하지 않는다.
- `recover`·`release_leases`가 돌려놓은 작업의 fence를 1 올림: 재획득 전이어도 옛 claim의 ack·nack·fail·extend·update는 모두 `STALE`이다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- `release_leases`가 시도 한도에 닿은 작업을 `FAILED`가 아닌 `READY`로 되돌림: 해제는 작업의 실패로 세지 않는다. 그렇게 한 번 더 전달된 작업은 다음 해제에서 `FAILED`가 된다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 새 큐 이름이 `_keys`·`_state`·`_idem`으로 끝나거나 다른 큐의 내부 이름과 겹치면 `INVALID`로 거절: 이미 있는 큐는 그대로 열린다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 큐 핸들이 뒤로 간 시계를 자기가 읽은 가장 늦은 시각으로 읽음: lease를 일찍 만료시키거나 줄여서 연장하지 않는다. 앞으로 뛰었다 돌아온 시계는 단조 시계로 잰 경과로 판별해 뛴 시각에 머물지 않는다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- Python `Queue`의 `idem_retention_ms=0`이 C와 같이 기본 7일을 뜻하도록 변경: 키를 남기지 않으려면 `None`을 준다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- `persistqueue_compat.SQLiteAckQueue`의 `auto_resume`이 만료된 lease만 되돌리도록 변경: 다른 프로세스가 처리 중인 작업을 풀지 않는다. 재시작 때 모두 되돌리려면 `resume_live_leases=True`를 준다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- `persistqueue_compat`의 `get(id=...)`이 이미 끝났거나 지워진 작업이면 block이어도 바로 `Empty`로 변경 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- `persistqueue_compat`이 ack를 기다리며 기억하는 claim을 10000개로 제한: 넘으면 만료된 것부터 잊고 그 claim의 ack는 `UnknownClaimError`다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 호출한 스레드가 같은 `Database`에 연 트랜잭션 때문에 막힌 Python 호출은 `busy_timeout_s`를 기다리지 않고 바로 `BusyError`(자동 commit SQL은 `SqlError`)로 변경 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 승인된 commit의 손실이 확정된 파일의 쓰기 열기를 거절: 공개 C API는 `101` `DB_INVALID`, Python은 `DatabaseOpenError`이며 읽기 열기는 이전 세대로 열린다. 손실을 받아들이고 이어 쓰는 열기는 내부 API(`database_open_accepting_loss`)에만 있다. ([#271](https://github.com/woonyong-choi/streamlite/issues/271))
- `database_create`가 같은 경로를 만드는 다른 핸들이 있으면 `LOCKED`를 줄 수 있고, 게시 뒤 정리에 실패하면 게시한 이름을 거둠: 생성 도중 죽은 프로세스가 남긴 `.partial`은 다음 생성이 자동 정리한다. ([#271](https://github.com/woonyong-choi/streamlite/issues/271))
- macOS에서 내구 동기화에 `F_FULLFSYNC` 사용: 장치 쓰기 캐시까지 비우므로 commit 지연이 늘어난다(측정 예: 자동 commit INSERT 평균 약 0.5~0.8ms에서 7~9ms). ([#271](https://github.com/woonyong-choi/streamlite/issues/271))
- `SUBSTR`이 음수·0 시작과 음수 길이에서 SQLite와 같은 결과를 냄 ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- 기본 키를 옮기는 `UPDATE`(`SET id = id + 1` 등)가 다른 행의 옛 키를 받는 경우에도 적용: 수정 전에는 기본 키 중복 오류였다. ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- SQL 식 재귀 한도 도입: 식 트리 높이 512, 식 중첩 128, 질의 중첩 16, 결합 단계 64를 넘으면 `LIMIT`(분류 4, 코드 15) 오류다. 예전에 받던 일부 깊은 SQL(괄호 128단계 이상, 괄호와 연산자 사슬을 합친 높이가 512를 넘는 식)도 거절한다. ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- 식 평가의 중간 TEXT·BLOB 값과 작업 buffer를 질의 메모리 한도에 계상: 큰 중간값을 만드는 문장은 메모리 예산 오류(분류 5, 코드 24)가 될 수 있다. ([#272](https://github.com/woonyong-choi/streamlite/issues/272))

### 수정

- 긴 큐 이름의 경계 처리 수정
- free-list 경계 처리 수정
- 음수이거나 범위를 넘는 시계 값에서 큐 연산이 모두 `LIMIT`이 되던 문제 수정: 유효한 값을 한 번도 읽지 못한 핸들만 1970년으로 진행하지 않고 `LIMIT`으로 거절한다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 보존 기간이 긴 멱등성 키 뒤의 만료된 키가 purge에서 정리되지 않던 문제 수정 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- commit·rollback 없이 참조를 잃은 Python `Transaction`이 writer를 쥔 채 남던 문제 수정: 수거 때 rollback하고 `Database`·`Queue`는 수거나 인터프리터 종료 때 닫는다. 인터프리터 종료 때는 다른 스레드가 쓰고 있을 수 있어 트랜잭션을 rollback하지 않는다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 종료자가 끝낸 Python `Transaction`의 다음 `execute`·큐 호출이 자동 commit으로 실행되던 문제 수정: 값이 NULL인 트랜잭션·DB·큐 핸들을 끝난 상태로 보고 `InvalidArgumentError`(큐는 `ValueError`)로 거절한다. ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- Python `Queue.put`·`update`가 `bytearray`·`memoryview`를 받고 `str`은 `TypeError`로 거절하도록 수정 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- `persistqueue_compat`의 `clear_acked_data`가 `clear_ack_failed`일 때 `keep_latest`를 무시하고 1024를 넘는 `max_delete`에서 실패하던 문제 수정 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 끝낸 C 핸들을 표시 검사로 잠그기 전에 `INVALID`로 거절하고 `streamlite_txn_failed`가 mutex 안에서 읽도록 수정 ([#273](https://github.com/woonyong-choi/streamlite/issues/273))
- 죽은 writer가 남긴 게시 표시 때문에 읽기가 게시 timeout까지 기다리던 문제 수정 ([#271](https://github.com/woonyong-choi/streamlite/issues/271))
- 512개 이상 컬럼의 `CREATE TABLE`이 게시된 뒤 열리지 않던 문제 수정: 새 테이블은 511컬럼까지 만들고 넘으면 거절한다. ([#270](https://github.com/woonyong-choi/streamlite/issues/270))
- `DROP TABLE`의 페이지 수집이 긴 overflow 체인에서 목록 범위를 넘겨 쓰던 문제와 손상된 overflow 크기로 큰 메모리를 선할당하던 문제 수정 ([#270](https://github.com/woonyong-choi/streamlite/issues/270))
- 유일 색인 위반을 기본 키 중복과 구분해 "유일 색인의 값이 중복됩니다"로 알림(코드 11 유지) ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- 기본 키에 `NULL`을 대입하는 `UPDATE`를 NOT NULL 위반으로 거절 ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- `CREATE INDEX`의 닫는 괄호 뒤에 남은 토큰을 문법 오류로 거절 ([#272](https://github.com/woonyong-choi/streamlite/issues/272))
- `GROUP BY`의 식 대응이 서로 다른 서브쿼리를 같은 식으로 보던 문제 수정 ([#272](https://github.com/woonyong-choi/streamlite/issues/272))

### 제거

- 더 이상 쓰지 않는 이전 ABI 실험 게이트 제거

## 0.2.0 - 2026-10-19

### 추가

- 배포에서 제외되는 벤치마크 계측 모드 추가 ([#240](https://github.com/woonyong-choi/totedb/pull/240))
- 단일 writer 비용 진단과 평가 근거 추가 ([#234](https://github.com/woonyong-choi/totedb/pull/234))

### 변경

- YCSB·큐 비교와 TEXT 범위 조회 개선 ([#227](https://github.com/woonyong-choi/totedb/pull/227))
- 아키텍처와 안전성 문장 개선 ([#237](https://github.com/woonyong-choi/totedb/pull/237))
- 첫 실행과 지원 범위 안내 정리 ([#219](https://github.com/woonyong-choi/totedb/pull/219))
- 조회 조건과 새 핸들 열기 제한 설명 추가 ([#217](https://github.com/woonyong-choi/totedb/pull/217))

## 0.1.0 - 2026-10-17

첫 공개판이다.

### 추가

- PyPI 패키지 배포와 태그 기반 릴리스 워크플로 추가 (#205)
- 여러 프로세스의 같은 파일 쓰기 적용 ([0d8ed17](https://github.com/woonyong-choi/totedb/commit/0d8ed17))
- 쓰는 동안 다른 프로세스의 조회 비차단 적용 ([6e2b94c](https://github.com/woonyong-choi/totedb/commit/6e2b94c))
- Python 생산자와 C worker의 같은 큐 공유 예제 추가 ([58c1e40](https://github.com/woonyong-choi/totedb/commit/58c1e40))
- lease 연장·idempotency 키 보존·payload 상한 옵션 추가 ([b8fc5ee](https://github.com/woonyong-choi/totedb/commit/b8fc5ee))
- 페이지 회계 검사와 큰 값 장애 시험 추가 ([83fa0dc](https://github.com/woonyong-choi/totedb/commit/83fa0dc))
- 결과 열 형식·NULL 가능 정보 전달 추가 ([63df0e2](https://github.com/woonyong-choi/totedb/commit/63df0e2))
- 정수 기본 키 범위 조회 도입 ([3dbf759](https://github.com/woonyong-choi/totedb/commit/3dbf759))
- `INSERT ... ON CONFLICT DO NOTHING` / `DO UPDATE` 지원 ([8a0c813](https://github.com/woonyong-choi/totedb/commit/8a0c813))
- 바깥 열을 참조하는 서브쿼리(상관 서브쿼리) 지원 ([27bbdf1](https://github.com/woonyong-choi/totedb/commit/27bbdf1))
- RIGHT [OUTER] JOIN 지원(첫 결합) ([92dc042](https://github.com/woonyong-choi/totedb/commit/92dc042))
- 컬럼 DEFAULT와 `INSERT ... DEFAULT VALUES` 지원, 기본 키 NOT NULL ([6a2a266](https://github.com/woonyong-choi/totedb/commit/6a2a266))
- 서브쿼리(스칼라·EXISTS·IN)와 WITH, FROM 서브쿼리 지원 ([6a17465](https://github.com/woonyong-choi/totedb/commit/6a17465))
- UNION·UNION ALL·EXCEPT·INTERSECT 지원 ([c955372](https://github.com/woonyong-choi/totedb/commit/c955372))
- INSERT·UPDATE·DELETE RETURNING 지원 ([255be69](https://github.com/woonyong-choi/totedb/commit/255be69))
- INNER·LEFT·CROSS JOIN과 여러 테이블 FROM 지원 ([e905203](https://github.com/woonyong-choi/totedb/commit/e905203))
- 집계 함수와 GROUP BY·HAVING 지원 ([49962e7](https://github.com/woonyong-choi/totedb/commit/49962e7))
- 식 트리·평가기와 SELECT 정렬·제한·중복 제거·별칭 추가 ([eb03dc5](https://github.com/woonyong-choi/totedb/commit/eb03dc5))
- 빈 페이지 목록을 영속하고 새 페이지를 즉시 재사용 ([ed52fc8](https://github.com/woonyong-choi/totedb/commit/ed52fc8))
- 일반 컬럼 B+Tree 색인과 고유 제약 추가 ([efea9f0](https://github.com/woonyong-choi/totedb/commit/efea9f0))
- ACK 큐 Python 어댑터와 공유 라이브러리 빌드 추가 ([164bef8](https://github.com/woonyong-choi/totedb/commit/164bef8))

### 변경

- 행 인코딩을 varint 코드 형식으로 변경 ([a932fa7](https://github.com/woonyong-choi/totedb/commit/a932fa7))
- 행 하나를 꺼낼 때 그 slot의 경계만 검사하도록 개선 ([6790be5](https://github.com/woonyong-choi/totedb/commit/6790be5))
- 기본 키 순서 보조 색인과 색인 순서 실행 도입 ([e18b89f](https://github.com/woonyong-choi/totedb/commit/e18b89f))
- GROUP BY·집계를 해시 집계로 변경 ([806f5c4](https://github.com/woonyong-choi/totedb/commit/806f5c4))
- commit을 fsync 한 번으로 줄이고 끝나지 않은 commit은 열 때 되돌림 ([b0d2234](https://github.com/woonyong-choi/totedb/commit/b0d2234))
- 페이지·overflow 체크섬을 하드웨어 CRC32C로 교체 ([fce7efd](https://github.com/woonyong-choi/totedb/commit/fce7efd))
- page_file에 검증된 페이지 읽기 캐시 추가 ([d039421](https://github.com/woonyong-choi/totedb/commit/d039421))

### 제거

- 옛 엔진과 REPL·HTTP 서버 제거 ([5e6b265](https://github.com/woonyong-choi/totedb/commit/5e6b265))
- 이전 형식 호환 코드 제거 ([c49bb6a](https://github.com/woonyong-choi/totedb/commit/c49bb6a))

### 수정

- 큰 id를 넣은 뒤 자동 id가 오류로 끝나는 문제 수정 ([87e376f](https://github.com/woonyong-choi/totedb/commit/87e376f))
- 표 페이지 목록을 모을 때 목록 끝 밖에 쓰는 문제 수정 ([b40cc06](https://github.com/woonyong-choi/totedb/commit/b40cc06))
- 함수·CAST 결과를 SQLite·PostgreSQL 기준으로 수정 ([216e95c](https://github.com/woonyong-choi/totedb/commit/216e95c))
- 보조 색인 순회의 LIMIT 멈춤 처리 수정 ([7b7a839](https://github.com/woonyong-choi/totedb/commit/7b7a839))
