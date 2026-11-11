# 설정

StreamLite 0.3.0의 flat C API와 Python API에는 설정 파일이 없다. 예를 들어 `Database("app.streamlite", create=False)`는 기존 파일만 열며 read-only로 여는 옵션이 아니다. API 선언은 [streamlite.h](../include/streamlite.h), Python 구현은 [database.py](../python/streamlite/database.py)와 [queue.py](../python/streamlite/queue.py)에 있다.

## 우선순위

API 인자는 해당 호출이나 핸들에 적용하며 환경 변수가 덮어쓰지 않는다. Python의 공유 라이브러리 탐색만 아래 순서를 사용한다.

1. `STREAMLITE_LIB`에 지정한 파일을 찾는다.
2. 설치 패키지 폴더의 `libstreamlite.dylib` 또는 `libstreamlite.so`를 찾는다.
3. 저장소 `build/lib/`의 같은 파일을 찾는다.

macOS는 `.dylib`, 그 밖은 `.so` 이름을 쓴다. 후보가 없으면 다음 경로로 넘어가지만, 존재하는 파일을 로딩하다 실패하면 그 예외가 전파된다. 모든 후보를 찾지 못하면 `OSError`다. 탐색 구현은 [_native.py](../python/streamlite/_native.py)에 있다.

`STREAMLITE_LIB`는 Python이 공유 라이브러리를 탐색할 때 처음 시도하는 파일의 경로 문자열이다. 기본값은 미지정이며 API 인자를 바꾸는 환경 변수가 아니다.

## 항목

아래 API 인자에 대응하는 환경 변수는 없다.

### 데이터베이스 열기

| 키 | 타입 | 기본값 | 뜻 |
|---|---|---|---|
| `busy_timeout_ms` | C `uint32_t`, 밀리초 | `0`, 기다리지 않음 | C `streamlite_open_ex`의 BUSY 재시도 기간 |
| `busy_timeout_s` | Python `float`, 초 | `5.0` | Python의 BUSY 재시도 기간 |
| `create` | C `int`, Python `bool` | C 필수 인자, Python `True` | 파일이 없으면 생성 |
| `durability` | C `uint32_t`(`STREAMLITE_DURABILITY_*`), Python `"normal"`·`"full"`·`"off"` | NORMAL | commit 동기화 단계([안전성](safety.md#동기화-단계)) |
| `path` | C 문자열, Python 경로 | 없음, 필수 | DB 파일 경로 |

`Database` 생성자의 `streamlite_open` 호출은 한 번이며 `busy_timeout_s`로 재시도하지 않는다. 열기는 다른 연결·프로세스의 쓰기 거래를 기다리지 않으므로 그 거래 중에도 곧바로 열린다(근거는 [오류](errors.md#104-streamlite_db_locked)). 이미 설치된 큐를 `install=True`로 여는 것도 필요한 색인이 모두 있으면 쓰기 거래 없이 열린다. 이 값은 `Database.execute`의 SQL BUSY, `begin`, 큐 열기와 트랜잭션 인자를 받지 않은 큐 호출의 BUSY 재시도에 쓰인다. 명시적 `Transaction.execute`와 `txn=`을 받은 큐 호출은 자동 재시도하지 않는다. 초기 대기는 0.5밀리초이고 두 배씩 늘려 최대 20밀리초로 둔다. 운영체제 스케줄링과 호출 시간 때문에 실제 경과가 timeout을 넘지 않는다는 보장은 아니다.

Flat C API의 `streamlite_open`은 writer 경합을 호출자에게 반환한다. `streamlite_open_ex`의 `busy_timeout_ms`가 0이 아니면 `streamlite_begin`, 트랜잭션 없이 부른 큐 변경 연산과 자동 commit 변경 문장의 BUSY를 그 기간까지 db mutex 밖에서 다시 시도한다(0.5밀리초에서 20밀리초까지 두 배씩). 트랜잭션 안의 문장·큐 호출은 다시 시도하지 않는다. Python `Database`는 C에 0을 넘기고 자기 `busy_timeout_s`로 재시도한다. 동기화 단계 NORMAL·FULL의 commit은 `DURABLE`을, OFF는 `VISIBLE`을 달성한다. 이 API에는 read-only, isolation, 세션 풀·메모리 예산을 선택하는 인자가 없다. Core 헤더의 별도 옵션을 Python `Database` 옵션으로 전달할 수 없다.

### 큐 열기

옵션은 열린 큐 핸들의 값이며 파일에 저장한 큐 전역 설정이 아니다. 여러 worker와 재개방 호출의 옵션 일치는 호출자가 관리한다.

| 키 | 타입 | 기본값 | 뜻 |
|---|---|---|---|
| `clock` | C callback, Python callable | `NULL` 또는 `None` | 실시간 시계 대신 밀리초 절대 시각 제공 |
| `clock_ctx` | C `void *` | `NULL` | C clock에 전달하는 문맥 |
| `idem_retention_ms` | `uint64_t`, 밀리초 | `604800000`, 7일 | Purge 후 키 보존 기간 |
| `install` | C `int`, Python `bool` | C 필수 인자, Python `True` | 큐 테이블이나 색인이 없으면 설치. 모두 있으면 쓰기 거래를 열지 않음 |
| `max_attempts` | `uint32_t` | `0`, 무제한 | claim 시도 횟수 상한 |
| `max_payload_bytes` | C `size_t`, Python `int` | `1048576`, 1MiB | payload 한 건의 상한 |
| `name` | 문자열 | 없음, 필수 | `[A-Za-z0-9_]{1,40}` |

C의 `options=NULL` 또는 0으로 초기화한 옵션은 기본값이다. C의 `idem_retention_ms=0`은 기본 7일이고 `UINT64_MAX`는 purge 뒤 키를 보존하지 않는다. Python도 `idem_retention_ms=0`은 기본 7일이고, 키를 보존하지 않으려면 `None`을 주며 내부에서 이 sentinel로 변환한다. 일반 보존 기간의 최대는 `3162240000000`밀리초다.

C의 `max_payload_bytes=0`은 기본 1MiB다. Python은 0 이하를 거절한다. 설정 가능한 최대는 `1073741824`바이트지만 payload는 큐 테이블의 한 행에 저장하므로 아래 행 저장 한도(TEXT·BLOB 합 63MiB, `idem`·`note` 포함)를 넘는 `put`·`update`는 `LIMIT`이다. 이름, 키와 reason의 NUL 경계는 [데이터](data.md#sql-값과-바인딩)에 있다. 음수나 범위를 넘는 Python 숫자는 unsigned ctypes 변환 전에 모든 인자에서 일관되게 검사하지 않으므로 자동 보정되는 입력으로 취급하지 않는다.

### 큐 연산 인자

| 키 | 타입 | 기본값 | 뜻 |
|---|---|---|---|
| `after_id` | `uint64_t` | `0` | 목록에서 이 ID보다 큰 행만 조회, 최대 `INT64_MAX` |
| `idem_key` | UTF-8 문자열 | Python `None`, C `NULL` | 미지정 시 중복 제거 없음, 지정 시 1~128바이트 |
| `include_failed` | bool | `False` | purge에 FAILED 포함 |
| `item_id` | `uint64_t` | Python `None`, C `0` | 다음 작업 claim, 양수는 특정 작업 |
| `lease_ms` | `uint64_t`, 밀리초 | 없음, 필수 | `get`·`extend`의 기간, 1~31622400000 |
| `limit` | C `size_t`, Python `int` | `list_items` 100, 정리 연산 1024 | C 허용 범위 1~1024 |
| `page_size` | Python `int` | `100` | `iter_items`의 한 쪽 크기 |
| `reason` | UTF-8 문자열 | Python `''` | fail·nack 이유, 최대 200바이트, 빈 값 허용 |
| `retry_delay_ms` | `uint64_t`, 밀리초 | Python `0`, C 필수 인자 | nack 후 대기, 0~31622400000 |
| `with_payload` | bool | `False` | 목록에 payload 복사 |

Python `list_items`는 큰 limit을 1024로 줄인다. `recover`, `release_leases`, `purge`는 줄이지 않고 C의 범위 검사를 받는다. `iter_items`는 `list_items`를 반복한다. 실시간 clock의 후퇴는 lease 대기를 늘릴 수 있다. `extend`는 기존 만료에 더하지 않고 `현재 시각 + lease_ms`로 바꾼다. 시각 덧셈 overflow나 `INT64_MAX` 초과는 LIMIT이다.

### SQL 입력과 결과 한도

| 항목 | 기본값 또는 고정값 | 공개 설정 |
|---|---|---|
| SQL 입력 실효 한도 | 1MiB, UTF-8 바이트 | Flat C·Python 변경 옵션 없음 |
| SQL 식·질의 재귀 | 식 트리 높이 512, 식 중첩 128, 질의 중첩 16, 결합 단계 64 | 없음 |
| C 인코딩 결과 전체 | 64MiB, `67108864`바이트 | `streamlite_set_max_result_bytes(size_t)` |
| 저장할 한 행의 TEXT·BLOB 합 | 63MiB, `66060288`바이트 | 없음 |
| 내부 결과 행 | 기본 64MiB, 셀 배열과 값 바이트 합 | Flat C·Python 변경 옵션 없음 |
| 내부 builder 결과 | 기본 256MiB, metadata와 행 합 | Flat C·Python 변경 옵션 없음 |
| 결과 열 수·이름 | 최대 1024개·이름 256바이트 | 없음 |
| 명령 tag·오류 메시지 | 최대 32바이트·1024바이트 | 없음 |
| 보조 색인 수 | DB 전체 1024개, 테이블당 64개. 큐 하나가 4개 사용 | 없음 |

행 저장 한도는 결과 한도에서 정했다. INSERT·UPDATE·`ON CONFLICT DO UPDATE`가 만들 행의 TEXT·BLOB 합이 63MiB를 넘으면 아무것도 반영하지 않고 `LIMIT`(분류 4, 코드 54) 오류다. 그래서 저장한 행 하나는 내부 결과 행 한도(64MiB)와 기본 C 인코딩 결과 상한(64MiB, 열 이름·셀 머리 몫 1MiB 남김) 안에서 언제나 SELECT로 돌아온다. 0.3.0 개발 중에는 64MiB 이상 값의 INSERT·UPDATE가 성공하고 SELECT만 실패했다. 결과 cap을 줄이거나 큰 행 여러 개를 한 결과로 읽으면 그 결과는 여전히 `LIMIT`일 수 있으므로 `WHERE`나 `LIMIT`로 나눠 읽는다.

보조 색인 한도는 DB 전체 1024개, 테이블 하나에 64개다. 큐 하나는 큐 테이블 색인 3개(`due`·`idem`·`lease_until`)와 키 테이블 유일 색인 1개를 쓰므로, 사용자 색인이 없으면 큐를 256개까지 만들 수 있다. 0.3.0 개발 중에는 DB 전체 한도가 64개라 큐 16개 뒤 큐 설치와 사용자 `CREATE INDEX`가 실패했다. 카탈로그 형식은 색인 수를 u32로 쓰므로 형식은 그대로이며, 전체 한도가 64개이던 빌드는 64개를 넘는 파일을 열 때 파일을 바꾸지 않고 손상(`DB_CORRUPT`)으로 거절한다.

재귀 한도를 넘는 문장은 실행 전에 `LIMIT`(분류 4, 코드 15) 오류다. 0.3.0부터 괄호 128단계 이상이나 괄호와 연산자 사슬을 합친 높이가 512를 넘는 식처럼 예전에 받던 일부 깊은 SQL도 거절하며 값의 근거는 [sql_statement.h](../include/sql/sql_statement.h)에 있다. SQL 입력은 [database.c](../src/sql/database.c)의 bounded 기본값을 적용한다. Lexer 자체의 16MiB 한도가 flat API의 실효 기본값은 아니다. 세션 풀의 무제한 설정도 SQL 입력이나 결과를 무제한으로 만들지 않는다. 내부 결과 한도는 [result.h](../include/sql/result.h)에 있다.

C 결과 cap은 DB별이 아닌 프로세스 전역 atomic 값이다. 결과를 만들기 전에 설정하고 `streamlite_max_result_bytes()`로 읽는다. `SIZE_MAX/2`보다 큰 값은 그 값으로 줄이며 `0`은 무제한이 아니라 0바이트 cap이다. Python 공개 생성자에는 대응 옵션이 없다. Cap은 전체 프로세스 메모리 상한이 아니며, 실패가 commit 뒤에도 발생할 수 있다는 점은 [오류](errors.md#실패-뒤-상태)에 있다.

## 전체 예

새 DB 경로에서 Python 기본값을 모두 명시하는 예다. Lease는 기본값이 없으므로 이 예에는 claim 호출을 넣지 않는다.

```python
from streamlite import Database, Queue

with Database("defaults.streamlite", create=True, busy_timeout_s=5.0) as db:
	queue = Queue(db, "jobs", install=True, max_attempts=0,
		max_payload_bytes=1048576, idem_retention_ms=604800000, clock=None)
	queue.close()
```

같은 기본 큐 옵션을 C에서는 `streamlite_queue_open(db, "jobs", 1, NULL, &queue)`로 지정한다. 실행 파일과 헤더의 버전은 `streamlite_version()`과 `STREAMLITE_VERSION`으로 비교할 수 있지만, Python 로더가 자동으로 버전 일치를 강제하지는 않는다.
