# 빌드와 시험

README의 빠른 시작에 담지 않은 빌드 변수, 시험 대상, 외부 엔진과의 비교 방법, 플랫폼 문제를 적는다.

## 빌드

| 항목 | 내용 |
|---|---|
| 언어 | C11, `-Wall -Wextra -Werror` |
| 도구 | GNU Make, C11을 지원하는 GCC 또는 Clang. `CC` 기본값은 `gcc`, Clang은 `make CC=clang`으로 선택 |
| 런타임 의존성 | POSIX 파일 I/O와 pthread, libm |
| Python 바인딩 | 표준 라이브러리 ctypes |
| 시험 의존성 | `libsqlite3-dev`, `sqlite3` CLI, pytest |

`make all`은 DB와 큐를 묶은 공유 라이브러리 `build/lib/libstreamlite.so`(macOS에서는 `.dylib`)를 `-O2`로 만든다. 이 라이브러리는 sanitizer를 켜지 않는다. Python 바인딩의 라이브러리 탐색 순서와 존재하지만 로딩에 실패한 후보의 처리는 [설정](configuration.md#우선순위)에 있다.

시험 바이너리는 `SANITIZE` 변수로 sanitizer를 고른다. 기본값은 `address,undefined`이고 Darwin 25 이상에서는 `undefined`다. `BUILD_DIR`를 바꾸면 sanitizer 설정이 다른 빌드를 따로 둘 수 있다.

```sh
make test-all
make SANITIZE=thread BUILD_DIR=build-tsan test-transaction test-session test-ack-queue
make SANITIZE= BUILD_DIR=build-nosan test-database
```

## 시험 대상

| 대상 | 검사하는 것 |
|---|---|
| `make test` | 행 인코딩, overflow, 값, 카탈로그, lexer, 페이지 파일, 카탈로그 저장, 기본 키·TEXT 색인 트리 |
| `make test-overflow-file` | overflow 체인의 파일 왕복 |
| `make test-value-sqlite` | 값 비교·변환을 시스템 SQLite와 대조 |
| `make test-value-varchar` | VARCHAR 경계 값과 UTF-8 판정 |
| `make test-database` | SQL 문장 parser, heap 저장소, 문장 실행, SQLite 대조, I/O·할당 실패 주입, 메모리 한도 열기, 기본 키 색인 |
| `make test-database-cache` | 내부 SQL template의 binding·스키마·퇴출·할당 실패·메모리 소유권과 SQLite 결과 대조 |
| `make test-database-workspace` | 거래별 최신 COW 사본·catalog 재사용, 네 조건의 SQLite 결과, rollback·할당/I/O 실패·삭제/재사용·결과 수명 |
| `make test-transaction` | 트랜잭션 상태 기계, writer 경합, commit 결과, 실패 주입 |
| `make test-session` | 세션 메모리 예산과 결과 수명 |
| `make test-ack-queue` | ACK 큐의 C API와 실패 주입 |
| `make test-ack-queue-python` | Python 바인딩의 DB·트랜잭션·큐 계약과 선택형 수명·등록 실패·Busy close |
| `make test-all` | 위 대상 전체 |
| `make test-runner` | 시험 실행기와 실패 판정기 |
| `make test-fault` | 실패 주입 런타임 |
| `make test-result` | 결과 버퍼 인코딩과 할당 실패 |
| `make test-benchmark` | 별도 계측 ON/OFF 빌드와 C API 계측 회귀. `test-all`에 포함되지 않는다. |

시험 실행기 [fault_runner.py](../tests/fault_runner.py)로 감싼 대상은 실행 결과와 시드 등을 `build/fault/`의 JSON으로 남긴다. `BUILD_DIR`를 바꾸면 이 경로도 해당 디렉터리 아래의 `fault/`로 바뀐다. CI는 기본 `build/fault/`를 sanitizer별 산출물로 올린다.

모든 시험이 이 실행기를 거치는 것은 아니다. `make test-ack-queue-python`은 pytest를 직접 호출하므로 결과를 콘솔과 CI 작업 로그에서 확인한다. `make test-benchmark`의 직접 실행 항목도 콘솔에 결과를 남긴다. 이 실행들의 결과와 시드가 모두 `build/fault/`에 JSON으로 모인다고 가정하지 않는다.

### SQL template cache

`SQL_TEMPLATE_CACHE`는 내부 비교 빌드의 설정이며 값은 0과 1이다. 기본값 0은 현행 준비 경로를 사용하고 1은 [공통 실행 계약](design/streamlite.md#공통-실행-계약)의 cache를 활성화한다. 설정이 바뀌면 fingerprint가 객체와 공유 라이브러리를 다시 만든다. 이는 제품 기본값 변경이나 성능 채택이 아니다.

```sh
make SQL_TEMPLATE_CACHE=1 test-database-cache test-database test-session test-transaction
make SQL_TEMPLATE_CACHE=0 all
```

`test-database-cache`는 기본 빌드에서도 내부 제어로 OFF와 ON을 대조하며 `test-all`에 포함한다. [database_cache.h](../src/sql/database_cache.h)의 제어·통계는 시험용 내부 인터페이스다. 공개 prepared handle이나 Python 설정으로 노출하지 않는다. cache hit도 binding 검사와 실행별 AST 복제를 수행하므로 lexer/parser를 건너뛴다는 사실만으로 지연·메모리 개선을 주장하지 않는다.

### Transaction workspace

`TXN_WORKSPACE`는 0과 1을 받는 빌드 설정이며 기본값은 1이다(#262). 1은 [변경 누적과 실패 경계](design/streamlite.md#변경-누적과-실패-경계)의 catalog 재사용과 private COW page 보유를 켜고, 0은 이전처럼 문장마다 페이지를 바로 쓰는 비교 빌드다. 거래가 모은 페이지 사본과 작업 catalog·목록이 한도(`DATABASE_WORKSPACE_BYTES`, 8MiB)를 넘으면 모은 사본을 그때 파일에 쓰고(사본 없이 쓰는 경로와 같은 새 페이지 쓰기) 거래를 이어 간다. 그래서 큰 거래가 한도 때문에 실패하지 않고 사본 이득만 줄어든다. 설정 변경은 객체와 공유 라이브러리 fingerprint에 반영한다. 파일 형식·내구 barrier와 기존 단일 writer 경계는 유지한다.

```sh
make TXN_WORKSPACE=0 SQL_TEMPLATE_CACHE=1 test-database-workspace test-transaction test-session test-ack-queue
make TXN_WORKSPACE=1 SQL_TEMPLATE_CACHE=0 all
```

`test-database-workspace`는 기본 빌드에서도 내부 제어로 cache/workspace 네 조건을 검사하며 `test-all`에 포함한다. [database_workspace.h](../src/sql/database_workspace.h)의 제어·통계는 내부 시험용이며 Python·공개 C ABI에 추가하지 않는다. 작업이 열려 있으면 설정 변경과 resident ledger의 새 연결을 거절한다. 한도는 [시험용 설정](design/streamlite.md#공통-개선의-시험용-한도)을 따르며 query·result의 기존 한도와 전체 RSS를 뜻하지 않는다. 문장별 heap·색인 계산과 사본 탐색·복사 비용은 남는다. 시험의 쓰기 횟수·예약 최고치와 실패 주입은 성능 채택이나 물리 전원 장애의 증거가 아니다.

### Conditional queue SQL

`ACKQ_CONDITIONAL`은 0과 1을 받는 내부 비교 설정이며 기본값은 0이다. 1은 [조건부 claim·ACK 경로](design/streamlite.md#선택형-수명과-조건부-갱신)를 선택한다. 큐 미사용 SQL 코어에는 전용 hook을 추가하지 않는다. 공개 C/Python 설정·파일 형식·단일 writer 경계와 내구 barrier는 바꾸지 않는다.

```sh
make ACKQ_CONDITIONAL=1 test-ack-queue
make ACKQ_CONDITIONAL=0 test-ack-queue-conditional
```

`test-ack-queue`는 `test-ack-queue-conditional`의 바이너리도 실행한다. 이 시험은 내부 제어로 cache/workspace 네 조건과 기존·조건부 경로를 함께 대조한다. queue SQL과 payload 할당의 시험 hook, 실제 파일 I/O 호출, query/result 예약을 관측한다. 정상·빈 후보·부분 배치·중복/stale/FAILED·상한·재개방·결과 수명·취소·payload OOM·query 예산 거절·writer timeout을 검사하며 시험 관측을 비계측 성능으로 해석하지 않는다.

E3 비교의 baseline·candidate와 각 계측 ON은 같은 소스와 cache/workspace 설정으로 별도 빌드한다. 조건부 사전 등록은 정확한 `STREAMLITE_ACKQ_CONDITIONAL=0`과 `=1`, 계측 플래그만 정규화하며 다른 차이는 거절한다. 원 cache/workspace 비교의 `validated_builds`와 수집·채택 기준은 유지한다. 실제 통과한 시험·소스·바이너리 해시를 담은 eligibility를 구현 commit에 연결해 등록한 뒤 별도 수집한다. 이 내부 구현이나 회귀만으로 채택하지 않는다.

### 비교용 WAL 파일

[후보 control](../include/storage/candidate_control.h)의 codec·실파일 시험은 두 slot의 CRC/형식·단조 상태·필수 의존성 verifier와 fallback을 검사한다. verifier는 시험용 합성 oracle이며 checkpoint/WAL/data 정합이나 일반 복구를 구현했다고 표시하지 않는다. 파일은 별도 합성 경로에 남기고 기존 DB를 열거나 변환하지 않는다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-control TEST_TIMEOUT=120 CANDIDATE_CONTROL_REPORT=build-regression/fault/control-check.json
```

[control 시험](../tests/test_candidate_control.c)은 slot의 8192개 절단·8192개 단일 byte 손상과 재서명 반례, 파일 길이·UUID/backend·slot 위치·highwater 퇴행, 짧은/0바이트 I/O·EINTR·읽기/동기화 실패·OOM을 대조한다. 의존성 CORRUPT와 IO를 구분하고 fallback을 보존한 세대 증가·미확정 쓰기 뒤 재열기·cwd/경로 교체·재진입·fork, 쓰기 전·쓰기 뒤/fsync 전·fsync 뒤/응답 전 종료와 성공 확인 뒤 SIGKILL을 확인한다. generation/checkpoint ID 소진은 LIMIT다. allocator callback이 입력 options를 바꿔도 입장 때 복사한 UUID/backend/verifier를 사용한다. control의 파일 동기화와 실제 DB의 복구 활성화·내구 결과 판정은 별도다.

[candidate WAL](../include/storage/candidate_wal.h)은 내부 비교용 format1 transport다. 기본 라이브러리·SQL·ACK의 object 목록에는 넣지 않는다. [codec](../src/storage/candidate_wal_format.c)은 record의 길이·padding·CRC32C·UUID·LSN을 검사하고 [파일 모듈](../src/storage/candidate_wal.c)은 4MiB segment와 명시적 force·재시작 scan·tail 수리를 제공한다. transport는 record 본문과 거래 의미를 판정하지 않는다. 아래의 witness·본문 reader·metadata catalog가 각각 검증하며 내구 결과 판정과 일반 복구 활성화는 아직 구현 대상이다.

```sh
make BUILD_DIR=build-regression test-candidate-wal TEST_TIMEOUT=120 CANDIDATE_WAL_REPORT_PREFIX=build-regression/fault/wal-check
```

[전용 시험](../tests/test_candidate_wal.c)은 작은 두 번째 frame의 360개 절단 지점과 원본을 남긴 별도 수리 파일, checksum을 다시 맞춘 잘못된 필드·기록 순서, segment 경계·누락·owner 역행, 짧은 I/O·EINTR·0바이트·동기화/할당 실패를 검사한다. 실제 자식 process는 force 직전·직후 종료와 성공 확인 뒤 SIGKILL을 구분한다. 합성 fixture를 지우지 않으며 기본 prefix에 process ID를 넣어 기존 파일 덮어쓰기를 피한다. 목표 작업의 반복 검증은 별도 prefix·report로 보존한다.

Make target은 절단 지점을 30개씩 12개 실행으로 나누며 모두 성공해야 통과한다. 첫 실행에는 나머지 경계·실패·process 시험도 포함한다. 반복 실행은 `CANDIDATE_WAL_REPORT_PREFIX`에 아직 없는 report 접두사를 지정한다. 각 native 실행은 90초, runner는 위 명령에서 120초로 제한한다. 파일 수리에 필요한 fsync가 느린 호스트에서 360개를 한 process에 몰아 실패하는 경우에도 입력을 빼거나 한도를 늘려 통과시키지 않는다. 각 report와 부분 fixture를 남긴다.

open은 파일을 바꾸지 않는다. 마지막 불완전 tail이 있으면 append/force를 거절하고 호출자가 원본을 보존한 뒤 명시적으로 수리해야 한다. 완전 길이의 checksum 손상과 중간 손상은 CORRUPT로 차단한다. 실패한 append/force 또는 scan 오류 뒤 handle은 poison이며 close/reopen이 필요하다. scan visitor가 중간에 멈춘 경우는 poison과 구분한다.

이 계층의 durable LSN은 확인한 파일 barrier 범위다. COMMIT 종류의 frame이 있다는 사실만으로 APPLIED를 반환하지 않는다. owner epoch 검사는 숫자의 역행을 막으며 새 소유권 인수·사망 복구를 제공하지 않는다. SQL·callback 재실행, page writeback·checkpoint·MVCC·실제 writer·장치 전원 차단과 lost-write 보장은 이 시험 범위가 아니다. 고정 record buffer와 callback allocator의 계상도 전체 앱 RSS·프로세스 간 128MiB 상한 구현으로 확대하지 않는다.

[파일 소유권 시험](../tests/test_candidate_wal_files.c)은 create/reopen·회전·tail 수리에서 상대/절대 경로와 cwd 변경을 대조한다. 실제 fsync fd의 inode가 원 파일과 같은지 검사하고 동명의 다른 UUID 파일을 보존한다. 같은 UUID·bytes로 바꾼 segment와 force 중 교체는 IO 뒤 poison이며 내구 LSN이 그대로여야 한다. 반복 scan·성공/실패 close의 열린 fd 수도 대조한다. transport는 디렉터리와 최대 64개 segment의 fd를 보유하며 외부 파일 변경은 지원하지 않는다.

```sh
make BUILD_DIR=build-regression test-candidate-wal-files TEST_TIMEOUT=120 CANDIDATE_WAL_FILES_REPORT=build-regression/fault/wal-files-check.json
```

반복 실행은 아직 없는 report 경로를 지정한다. 이 시험은 파일 barrier 대상과 오류 뒤 상태를 확인하며 거래 복구·실제 writer·성능 우위를 입증하지 않는다.

[witness 분석기](../include/storage/candidate_wal_witness.h)는 같은 파일을 읽어 transaction chain·terminal, image/delta group과 결과 참조를 대조한다. [전용 시험](../tests/test_candidate_wal_witness.c)은 열린 A 중 B의 commit, A의 abort 뒤에도 남는 완성 group, 미완성 group·18개 record 경계, 외부 CRC를 다시 맞춘 손상 34개, 중복 ID/terminal·교차 group, 빈/두 chunk 참조·한도/OOM·뒤쪽 읽기 오류를 확인한다.

```sh
make BUILD_DIR=build-regression test-candidate-wal-witness TEST_TIMEOUT=120 CANDIDATE_WITNESS_REPORT=build-regression/fault/witness-check.json
```

기본 fixture prefix는 process ID를 포함하며 원본을 지우지 않는다. 반복 실행은 `CANDIDATE_WITNESS_REPORT`에 아직 없는 report 경로를 지정해 앞선 로그도 보존한다. native 90초·runner 120초를 유지한다. 분석은 파일과 transaction 상태를 고치지 않고 전체 scan 실패 시 부분 표를 반환하지 않는다. COMMIT witness는 본문 복원이나 APPLIED가 아니다. 기본 모드는 ALLOC/FREE·system·checkpoint·epoch/ID 예약을 거절한다. [내부 분석 계약](design/streamlite.md#checkpoint와-회수-순서)의 두 모드와 남은 복구 의무를 함께 확인한다.

[system 시험](../tests/test_candidate_wal_system.c)은 내부 옵션 `enable_system_groups`를 켜고 실제 WAL의 metadata image와 ALLOC/FREE·ID_RESERVE·REQUEST_EPOCH를 대조한다. group 내 owner/chain·개수/CRC·세대·root/node·LSN,31개 record의 모든 prefix, 활성 요청의 floor 차단과 ABORT 뒤 구조 보존,64개 image 경계와4096개 event 한도·OOM·뒤쪽 읽기 실패·options 사본을 검사한다. 합성 파일의 force/reopen과 원시 event 결과를 확인하며 checkpoint·전체 할당 이력·reader/fallback 참조 해제·번호 발급·redo·복구 활성화는 구현하지 않는다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-wal-system TEST_TIMEOUT=120 CANDIDATE_SYSTEM_REPORT=build-regression/fault/system-check.json
```

기본 라이브러리에 연결하지 않으며 반복 실행에는 새 report 경로를 지정한다. 기존 object/binary를 보존하는 비교 실행은 `CANDIDATE_WITNESS_MODULE`로 새 object 경로를 사용하고 재사용하는 transport·format·checksum·page·metadata object를 각각 Make의 `--old-file`로 고정한다. 하나를 빠뜨려도 재빌드될 수 있으므로 실행 전후 원 해시를 대조한다. [기본 witness 시험](../tests/test_candidate_wal_witness.c)과 [결과 본문 시험](../tests/test_candidate_result_wal.c)은 새 소스로 별도 binary를 만들어 함께 확인한다.

[page codec과 본문 reader](../include/storage/candidate_page.h)는 일반 version/overflow page의 slot·generation·길이와 결과 bytes를 검사한다. 아래 target은 [codec 시험](../tests/test_candidate_page.c)과 [WAL/page fixture](../tests/test_candidate_result_wal.c)를 각각 실행한다.

```sh
make BUILD_DIR=build-regression test-candidate-pages TEST_TIMEOUT=120 CANDIDATE_PAGE_REPORT_PREFIX=build-regression/fault/pages-check
```

codec 시험은 page의 8192개 절단·8192개 단일 byte 손상, CRC를 다시 맞춘 29개 잘못된 구조와 1024개 탐색 입력, slot/inline/1MiB 경계·cycle·세대·OOM/loader 실패·소유권을 확인한다. 기존 record codec의 NULL·한글·내장 NUL·9000B BLOB도 그대로 복원한다. digest는 알려진 예제와 padding 경계·1MiB를 포함한 135개 입력으로 대조한다. 탐색에서 유효한 page는 다시 encode해 정확히 같은 bytes인지 확인하며, 이 유한 입력을 전체 가능한 손상의 증명으로 쓰지 않는다.

WAL fixture는 1MiB 응답의 version/overflow 131page에 metadata root/node 2page를 더한 133page·4physical group·16결과 참조를 force/reopen한다. 완전한 단일 COMMIT witness와 WAL barrier를 확인한 뒤 새 합성 data 파일에 image를 복사하고 fsync/reopen한다. catalog에서 요청 ID·operation digest로 raw outcome을 찾고 terminal 참조를 witness와 대조한 뒤, 그 VersionRef로 본문 bytes를 읽는다. 이 data 파일은 일반 DB control·할당 이력·system 예약/epoch 전이와 복구 활성화가 없는 시험 파일이다. 미완료·abort 거래의 일반 재생이나 APPLIED 판정으로 확대하지 않는다. 반복 실행은 아직 없는 report 접두사를 사용하며 합성 파일을 보존한다.

[요청 metadata 시험](../tests/test_candidate_catalog.c)은 root/node의 절단·손상·예약/형식, nonce 0·빈 결과·세 상태, 최대 4epoch/4096개·epoch당 1024개, 중복 ID·내용 불일치·없는/만료/미래/다른 UUID를 확인한다. 전체 91개 page의 각 loader 실패와 OOM에는 catalog를 노출하지 않으며 소유 할당 해제도 대조한다. 4096개 표를 fsync/close/reopen해 읽지만 이 표 자체는 결과 본문이나 내구 게시를 증명하지 않는다. 형식과 반환 의미는 [page와 WAL 불변식](design/streamlite.md#page와-wal의-형식-불변식)에 있다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-catalog TEST_TIMEOUT=120 CANDIDATE_CATALOG_REPORT=build-regression/fault/catalog-check.json
```

[할당 snapshot 시험](../tests/test_candidate_allocation.c)은 type 5 root/node의 절단·단일 byte 손상·재서명 구조, 정렬/세대/LSN·root/node LIVE 자기 포함, 희소 표와 최대 65,536개 기록을 검사한다. 메모리 전이는 LIVE/FREE·새 번호·재사용·세대 소진과 다중 변경 중 실패의 전체 불변을 대조한다. loader/OOM·입력 options 변경·소유 해제, 별도 합성 파일의 fsync/close/reopen과 COW 0LSN도 확인한다. 최대 fixture는 326개 metadata page이며 512MiB의 실제 data를 적재한 시험은 아니다. 파일과 로그를 보존하며 native 90초·runner 120초를 유지한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-allocation TEST_TIMEOUT=120 CANDIDATE_ALLOCATION_REPORT=build-regression/fault/allocation-check.json
```

이 모듈은 기본 라이브러리·SQL·ACK에 연결하지 않는다. 원시 할당 기록과 메모리 batch 성공은 durable ALLOC/FREE·ID 예약/epoch·checkpoint/WAL/data 정합·참조 해제·실제 회수·일반 복구의 증거가 아니다. 반복 실행에는 아직 없는 report 경로를 사용한다. [형식과 전이 경계](design/streamlite.md#page와-wal의-형식-불변식)를 함께 확인한다.

[checkpoint 시험](../tests/test_candidate_checkpoint.c)은 control anchor와 실제 CHECKPOINT_BEGIN/CHUNK/END의 완전성을 대조한다. 두 slot에 실제 checkpoint 부분 verifier를 연결해 digest 불량만 이전 anchor로 fallback하고 UNSUPPORTED·LIMIT·OOM은 차단한다. 선언된 dirty/transaction 표의 정렬·중복·CSN·owner·redo/terminal 위치, chunk 순서/길이·digest·부분 prefix, interleaved 일반 record, allocator 입력 변경과 END 뒤 I/O 실패의 부분 결과 차단을 확인한다. 최대8192개 dirty/4096개 transaction은 두 WAL segment에 걸친다. 시험의 거래 상태는 합성 snapshot이며 실제 terminal record와 data page의 전체 이력 검증은 아니다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-checkpoint TEST_TIMEOUT=120 CANDIDATE_CHECKPOINT_REPORT=build-regression/fault/checkpoint-check.json
```

native90초·runner120초와 원 파일/report를 보존한다. 재사용 control·transport·format·digest·checksum object와 fingerprint는 각각 `--old-file`로 고정하고 전후 해시를 확인한다. 일반 재현 명령은 해당 build 영역의 기존 파일을 보호하지 않으므로 보존할 실행에는 이 pin과 아직 없는 report 경로가 필요하다. 이 target은 기본 library에 연결하지 않으며 WAL reader는 append·force·tail 수리·로그 회수를 하지 않는다. [checkpoint 형식과 미완료 복구 경계](design/streamlite.md#checkpoint와-회수-순서)를 함께 확인한다.

[checkpoint 이력 대조 시험](../tests/test_candidate_wal_checkpoint.c)은 명시적 checkpoint-aware 진입점에서 실제 WAL의 BEGIN 시점 거래와 dirty PageRef·완성 group 위치를 확인한다. 원시 snapshot reader가 받아들이는 phantom·누락 활성 거래·위조 terminal/owner/highwater·dirty 위치를 거절한다. 정상 거래와 system 갱신의 chunk 사이 삽입, 새 owner의 복구 ABORT, 두 scan 사이의 CRC를 다시 맞춘 BEGIN/CHUNK/END 변경, END 뒤 오류·세 할당 지점 OOM·입력 사본을 검사한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-wal-checkpoint TEST_TIMEOUT=120 CANDIDATE_WAL_CHECKPOINT_REPORT=build-regression/fault/checkpoint-history-check.json
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-wal-checkpoint TEST_TIMEOUT=120 CANDIDATE_WAL_CHECKPOINT_ARGS=max CANDIDATE_WAL_CHECKPOINT_REPORT=build-regression/fault/checkpoint-history-max.json
```

일반 profile과 `max` profile은 각각 native90초·runner120초를 적용한다. 최대 입력은 실제8192개 image·4096개 거래·96chunk가17개 WAL segment에 걸치며64개 활성 거래와 한도를 넘는 입력도 별도로 대조한다. 최대 입력의 root 값은 capacity 시험용 선언이며 유효 DB를 연 증거가 아니다. 최고 소유 요청량2,108,056B는 반환 시1,681,736B로 줄며 transport·stack·allocator/RSS는 별도다. 기존 binary·object/fingerprint를 보호할 때는 새 `CANDIDATE_WITNESS_MODULE` 경로와 모든 재사용 dependency의 `--old-file` pin·전후 해시를 함께 사용한다. 구 options 구조로 컴파일한 object를 새 witness에 연결하지 않고 현재 header에 맞춘 시험 object를 사용한다. 기존 `analyze`의 기본/system 동작은 유지하며 data redo·결과 복원·일반 복구 활성화는 이 target의 범위가 아니다.

[물리 redo 시험](../tests/test_candidate_redo.c)은 실제 WAL과 새 data 파일에서 image·delta, PageRef/previousLSN·group 끝, ABORT/미완료 거래의 구조 보존과 미완성 group 배제를 확인한다. 정상·중간 결과에서의 재실행,14개 위치의 torn page, CRC를 다시 맞춘 잘못된 UUID/형식/ID/generation/pageLSN, 모든 작은 record prefix와 불완전 tail, read/부분 write/sync/WAL force 실패, 다중 scan 변경·OOM·입력 사본을 대조한다. 쓰기 도중과 data fsync 전후의 실제 자식 process 종료5곳에서 재실행한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-redo TEST_TIMEOUT=120 CANDIDATE_REDO_REPORT=build-regression/fault/redo-check.json
```

native90초·runner120초를 유지하고 새 target/report를 사용한다. 이 시험은 callback에 열린 test fd를 연결하며 WAL durable LSN이 pageLSN 이상인 뒤에만 pwrite하는지 확인한다. 복원한 allocation/catalog snapshot은 기존 owning reader로 다시 읽고520개 page·두 segment도 대조한다. workspace7,866,584B와 close 뒤0은 transport·stack·allocator/RSS와 별도다. 전체65536-ID 한도 채움이나 물리 power-cut·DB 활성화의 증거로 쓰지 않는다. 보호할 이전 object/binary가 있으면 `CANDIDATE_WITNESS_MODULE`과 `CANDIDATE_WITNESS_CHECKPOINT_MODULE`에 새 경로를 지정하고 모든 재사용 dependency/fingerprint를 `--old-file`로 고정해 전후 해시를 대조한다. 기본 library·SQL·ACK에는 연결하지 않는다.

[결과 대조 시험](../tests/test_candidate_outcome_witness.c)은 물리 redo로 복원한 metadata와 전체 보존 WAL·실제 결과 본문을 연결한다. COMMIT·ABORT·미완료 admission과 빈 결과, 1MiB 결과·16chunk를 대조한다. metadata 단독 reader가 받아들이는 위조 상태·terminal 위치·digest·VersionRef·owner·상한·ID·내용과 오래된 ADMITTED 표를 구분한다. 서로 다른 VersionRef를 가리키도록 frame/거래 CRC를 다시 맞춘 chunk, 누락된 표·실제 REQUEST_EPOCH와 만료, 미동기화 재열기·terminal 없는 prefix·불완전 tail, allocator 입력 변경·각 OOM·본문 뒤쪽 I/O 실패를 검사한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-outcome-witness TEST_TIMEOUT=120 CANDIDATE_OUTCOME_REPORT=build-regression/fault/outcome-check.json
```

native90초·runner120초와 아직 없는 report 경로를 사용한다. 기존 object/binary를 보존하려면 모든 재사용 dependency/fingerprint를 `--old-file`로 고정하고 `CANDIDATE_OUTCOME_CATALOG_MODULE`에 새 object 경로를 지정한다. witness/helper도 위의 명시적 module 옵션으로 보호한다. 전체 WAL force는 caller가 수행하며 이 target은 파일 수리·ABORT append·metadata 재작성·복구 membership·DB 활성화를 제공하지 않는다. 선택 checkpoint의 allocation root는 시험용 선언이고 전체 유효 DB의 증거가 아니다. [영속 요청 결과와 일반 복구의 경계](design/streamlite.md#영속-요청-id와-결과-조회)를 함께 확인한다.

[배타 복구 시험](../tests/test_candidate_recovery.c)은 기존 data·WAL·control 파일의 잠금을 보유한 상태에서 실제 checkpoint 검증·redo·할당과 참조·terminal·결과 대조를 연결한다. 정상 snapshot과 1MiB 결과, 재열기 추가 쓰기 0회, 유효 FREE·세대 증가 재할당을 확인한다. dangling 참조·version 순환·근거 없는 FREE/세대·누락된 할당·잘못된 root·future frozen CSN, 미완료 거래/물리 묶음·부분 tail은 성공 handle을 반환하지 않는다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-recovery TEST_TIMEOUT=120 CANDIDATE_RECOVERY_REPORT=build-regression/fault/recovery-check.json
```

native90초·runner120초와 새 report 경로를 사용한다. 이전 자료를 보존할 때는 위와 같이 모든 재사용 dependency/fingerprint를 `--old-file`로 고정하고 전후 hash를 확인한다. 시험은 세 파일의 배타 잠금·fork·부분 write/short I/O·read/fsync 실패·각 OOM·callback 재진입·inode 교체, 쓰기 도중과 fsync 전후의 실제 process 종료 뒤 재실행을 대조한다. 최신 control 의존성 CORRUPT의 fallback과 UNSUPPORTED의 차단도 구분한다. fixture 생성에만 합성 control verifier를 쓰며 복구 진입점은 실제 검증기를 사용한다. 이 입력의 소유 요청량 최고8,926,728B는 stack·allocator overhead·RSS와 별도다. 기존 파일 생성의 내구성과 handle 호출 직렬화는 caller가 보장한다. 이 경계는 resolved snapshot만 열며 미완료 상태의 내구 정리·새 owner epoch·SQL catalog·DB 쓰기 활성화를 제공하지 않는다.

[terminal 정리 시험](../tests/test_candidate_recovery_terminal.c)은 같은 배타 파일 소유권 아래 전체 WAL 검증·부분 transport tail 수리·ABORT append/force·재검증을 실행한다. admission만 남은 거래, 교차 거래·부분 결과 chunk·기존 COMMIT 보존, 같은 owner 재실행과 오래된 owner 거절을 확인한다. 부분 ENOSPC·prefix/final force 실패·truncate 실패·모든10할당 지점 OOM과 실제 process 종료3곳의 원 입력과 중간 WAL을 남긴다. data/control bytes는 전후 동일하고 결과표는 계속 STALE로 남긴다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-regression test-candidate-recovery-terminal TEST_TIMEOUT=120 CANDIDATE_TERMINAL_REPORT=build-regression/fault/terminal-check.json
```

native90초·runner120초와 새 report 경로를 사용한다. 보호할 object가 있으면 `CANDIDATE_RECOVERY_MODULE`과 `CANDIDATE_RECOVERY_TERMINAL_MODULE`에 새 object를 지정하고 모든 재사용 dependency/fingerprint를 `--old-file`로 고정한다. WAL 보존 사본은 segment inventory의 접두사 밖에 둔다. 이 target의 terminal receipt는 data/root/outcome의 정합·새 checkpoint 게시·DB 활성화나 제품 APPLIED 판정이 아니다.

[버퍼 시험](../tests/test_candidate_buffer.c)은 별도 `test-candidate-buffer` target으로 실행한다. 기본 DB 라이브러리에는 연결하지 않는다. 기본 case 목록은 대표 경로이며 전체 고장·OOM·중단 검증과 같지 않다. `CANDIDATE_BUFFER_CASES`로 사례를 지정하고, 반복할 때에는 사용하지 않은 report 접두사를 준다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build-buffer-check SANITIZE=undefined \
  CFLAGS="-Wall -Wextra -Werror -g -Iinclude -fsanitize=undefined -fno-sanitize-recover=undefined" \
  test-candidate-buffer \
  CANDIDATE_BUFFER_REPORT=build-buffer-check/fault/buffer-check \
  CANDIDATE_BUFFER_CASES="budget pinned lease shared-leaf membership terminal redirty fpi checkpoint-order checkpoint-active"
```

기존 산출물을 보존하는 실행은 새 binary·object 경로를 쓰고 모든 재사용 dependency와 fingerprint를 각각 `--old-file`로 고정한다. 실행 전후 해시를 대조한다. `CANDIDATE_BUFFER_OBJS`와 `CANDIDATE_BUFFER_TEST_OBJS`, 기존 recovery 모듈 옵션으로 별도 객체를 연결할 수 있다. 새 모듈은 `-Werror`·fatal UBSan으로 컴파일하고, 재사용 객체의 기존 계측 조건은 별도로 기록한다.

frame·전체 요청 예산, pin/lease·NO-STEAL·완성 group 원자 수용, callback 중 여러 page의 재변경, 첫 image와 body delta, fuzzy checkpoint의 dirty·거래 표와 게시 순서는 [버퍼 계약](design/streamlite.md#영속-요청-id와-결과-조회)을 따른다. native 검사와 원시 WAL/data/control·syscall trace의 독립 검사는 서로 다른 증거다. 중단 전 파일과 복구 후 파일, syscall 시도와 성공, CHUNK와 END의 실제 record·LSN·fd를 구분해 보존해야 한다. 이 correctness 검증을 실제 독립 writer, bounded WAL 재사용, 제품 통합이나 COW/WAL 성능 채택으로 해석하지 않는다.

[소유 복구 view 시험](../tests/test_candidate_recovery_view.c)은 선택형 `test-candidate-recovery-view` target으로 실행한다. 기본 DB 라이브러리와 기본 시험 목록은 유지한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build/recovery-view-check SANITIZE=undefined \
  CFLAGS="-Wall -Wextra -Werror -g -Iinclude -Itests -fsanitize=undefined -fno-sanitize-recover=undefined" \
  LDFLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined" \
  test-candidate-recovery-view \
  CANDIDATE_VIEW_REPORT=build/recovery-view-check/fault/recovery-view
```

`CANDIDATE_VIEW_CASES`로 사례를 선택하고 `CANDIDATE_VIEW_TIMEOUT`으로 각 runner의 제한 시간을 정한다. 새 build 경로와 사용하지 않은 report 접두사를 사용한다. report에는 사례 이름이 접미사로 붙는다. `CANDIDATE_VIEW_TEST_OBJS`를 재정의할 수 있으며, 기존 객체를 재사용하면 각 dependency/fingerprint를 `--old-file`과 전후 hash로 보호한다.

기본 사례는 실제 후보 복구·결과 조회, strict/preview·force 경계, 선택 결과 밖 LIVE version, codec을 통과하는 참조 순환, 주입한 fsync EIO·손상된 pinned 읽기, OOM·입력 snapshot·loader 종료 뒤 metadata 조회를 검사한다. 의미 관측 읽기의 대조는 checkpoint 읽기를 구분하며 redo·핀·전체 복구의 총비용은 별도로 남는다. 이 target은 최초 메모리 view 후보를 검증한다. 이어 처리와 의미 이력 정리는 아래 선택형 target으로 검사하며, 내구 seed·실제 공간 재사용·독립 writer·제품 성능 검증은 후속이다.

[정리 시험](../tests/test_candidate_recovery_closure.c), [경계 시험](../tests/test_candidate_recovery_closure_boundary.c), [transport 시험](../tests/test_candidate_recovery_closure_transport.c), [callback 시험](../tests/test_candidate_recovery_closure_callbacks.c), [raw 읽기 경계 시험](../tests/test_candidate_wal_read_boundary.c)과 [identity 시험](../tests/test_candidate_wal_identity.c)은 별도 선택형 target으로 실행한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build/recovery-closure-check SANITIZE=undefined \
  CFLAGS="-Wall -Wextra -Werror -g -Iinclude -Itests -fsanitize=undefined -fno-sanitize-recover=undefined" \
  LDFLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined" \
  test-candidate-recovery-closure test-candidate-recovery-closure-boundary \
  test-candidate-recovery-closure-transport test-candidate-recovery-closure-callbacks \
  test-candidate-wal-read-boundary test-candidate-wal-identity
```

각 target의 report 접두사와 사례 목록은 Makefile의 `CANDIDATE_CLOSURE_REPORT`·`CANDIDATE_CLOSURE_CASES`, `CANDIDATE_CLOSURE_BOUNDARY_REPORT`·`CANDIDATE_CLOSURE_BOUNDARY_CASES`, `CANDIDATE_CLOSURE_TRANSPORT_REPORT`·`CANDIDATE_CLOSURE_TRANSPORT_CASES`, `CANDIDATE_CLOSURE_CALLBACKS_REPORT`·`CANDIDATE_CLOSURE_CALLBACKS_CASES`, `CANDIDATE_WAL_READ_BOUNDARY_REPORT`·`CANDIDATE_WAL_READ_BOUNDARY_CASES`, `CANDIDATE_IDENTITY_REPORT`로 지정한다. 재실행에는 사용하지 않은 build·report 경로를 준다. 복구 view와 transport를 함께 바꿨으면 이 선택형 시험만으로 기존 WAL·checkpoint·redo·복구·buffer 회귀를 대신하지 않는다. raw 경계 시험의 IO는 잘못된 읽기 주소의 기존 오류 분류이며 실제 디스크 장애나 전원 차단을 증명하지 않는다.

[물리 권위 시험](../tests/test_candidate_recovery_physical.c)은 별도 선택형 `test-candidate-recovery-physical` target으로 실행한다.

```sh
UBSAN_OPTIONS=halt_on_error=1 make BUILD_DIR=build/recovery-physical-check SANITIZE=undefined \
  CFLAGS="-Wall -Wextra -Werror -g -Iinclude -Itests -fsanitize=undefined -fno-sanitize-recover=undefined" \
  LDFLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined" \
  test-candidate-recovery-physical \
  CANDIDATE_PHYSICAL_REPORT=build/recovery-physical-check/fault/recovery-physical
```

`CANDIDATE_PHYSICAL_CASES`로 사례를, `CANDIDATE_PHYSICAL_TIMEOUT`으로 runner 제한 시간을 정한다. fixture는 기존 view fixture와 같이 `build/` 아래 PID·serial 이름과 배타 생성 파일을 쓰므로 이전 산출물을 지우지 않는다. 물리 증명은 view 관측과 suffix 검증에 포함되므로 이 target만으로 view·closure·redo·복구·publish·buffer 회귀를 대신하지 않는다. 이 target은 국소 seeded preview/redo 후보의 검증이며 buffer 연결·내구 seed·reader 수명·실제 WAL 회수·writer 검증이 아니다.

### 공통 비용 수집

[사전 계획](design/streamlite.md#공통-비용-수집의-사전-계획)에 따라 E0–E3를 비교한다. `common-cost-driver`는 기존 공유 라이브러리의 내부 제어로 cache/workspace를 각각 켠다. 공개 API·배포 기본값은 바꾸지 않는다. 아래 명령의 `PLAN_COMMIT`은 그 계획과 수집기를 고정한 commit이고 `PREFIX`는 기존 실행 영역에서 사용하지 않은 파일 접두사다. `.registration.json`과 `.raw.jsonl`, manifest·DB·로그를 접두사별로 보존한다.

```sh
make BENCHMARK=0 SQL_TEMPLATE_CACHE=0 TXN_WORKSPACE=0 common-cost-driver
make BENCHMARK=1 SQL_TEMPLATE_CACHE=0 TXN_WORKSPACE=0 BUILD_DIR=build-benchmark common-cost-driver
python3 bench/common_cost.py register --phase cache --plan-commit "$PLAN_COMMIT" --registration "$PREFIX.registration.json"
python3 bench/common_cost.py collect --registration "$PREFIX.registration.json" --prefix "$PREFIX"
python3 bench/common_cost.py verify --registration "$PREFIX.registration.json" --prefix "$PREFIX"
python3 bench/common_cost.py analyze --registration "$PREFIX.registration.json" --prefix "$PREFIX"
```

수집·분석 interpreter에는 NumPy 1.26.4·SciPy 1.13.1이 필요하며 등록 정보가 실제 버전을 고정한다. 실행 단계는 `cache`와 `conditional`만 있다. 기존 eager/lazy 수명 기록은 등록한 source revision에 남아 있으며 현재 수집기는 옛 binding을 실행하지 않는다. `--phase conditional`은 별도 `--candidate-build`, `--candidate-on-build`, `--eligibility`를 요구한다. 후보가 없거나 baseline과 같은 binary면 비교를 거절한다. eligibility는 최소 ID·lease/fence·중복/stale/FAILED·cancel/OOM·실패 readback·reopen 검증 근거를 연결하며 구현 전에는 정식 E3를 실행하지 않는다.

eligibility JSON의 `minimum_id`, `lease_fence`, `duplicate_stale_failed`, `cancel_oom`, `failure_readback`, `reopen`은 통과했을 때만 true로 기록한다. `source_commit`은 후보 검증 commit이고 `build_source_commits`와 `library_hashes`는 각각 `off`, `on`, `candidate`, `candidate_on`의 commit과 SHA-256을 담는다. `artifacts`는 실제 검증 파일 경로와 SHA-256의 대응이다. 등록은 실제 파일·binary hash와 commit 존재를 확인하고, 수집 직전 다시 검사한다. 이 표시는 필요한 실패 검증을 새로 수행하거나 미구현 후보를 통과시킨다는 뜻이 아니다.

`--control` 등록은 16행·8작업·작은 본문의 수집기 검증용이며 효과 판단에 쓰지 않는다. [수집기 시험](../tests/test_common_cost.py)은 정상 결과·잘못된 변경·누락 지표·자료 변조·실패 뒤 중단/DB 보존·binary 변경·순서/대응·분석 재생성과 Python 수명을 검사한다. 시작 전·fixture·worker·최종 case·검증·보존·manifest의 시간 초과와 남은 worker 시간도 대조한다. 별도 DB 장애 주입 시험을 대신하지 않는다. 한도 초과는 완성된 raw가 있어도 campaign 실패이며 `verify`와 `analyze`가 거절한다. 실패한 접두사는 재사용하지 않고 부분 DB·로그·manifest를 보존한다.

정식 수집은 clean 작업본과 현재 수집기·계획을 함께 담은 `PLAN_COMMIT`의 조상을 요구한다. 수집기를 수리했으면 그 변경을 포함한 commit으로 새 등록을 만들고, 과거 등록·raw를 덮어쓰지 않는다. 수집 시간·완료/실패/미실행의 경계는 [사전 계획](design/streamlite.md#공통-비용-수집의-사전-계획)에 있다. 결과 보고는 원자료의 전체 완료와 동일한 분석 JSON을 확인한 뒤 수행한다.

[자료 검증기](../bench/common_cost_artifacts.py)는 등록 일정에서 필수 raw·worker stdout/stderr·cache fixture 생성 로그/DB·case별 DB gzip 경로를 도출한다. 누락·다른 경로·alias·symlink·중복 JSON key·잘못된 digest를 거부하며, 각 gzip의 원래 DB hash·해제 크기·저장 크기와 fixture hash도 대조한다. 같은 접두사의 늦은 분석 출력이나 collect 로그를 필수 집합으로 편입하지 않는다. 새 `collect`는 API로 전달한 등록도 `<prefix>.registration.json`에 보존하고 다른 내용이 있으면 거절한다. 검증기 도입 전 control API에서 등록 파일 없이 보존한 형식만 예외로 허용한다. 정식 자료와 새 등록은 등록 JSON까지 필수다. checksum은 전체 장부 교체를 인증하지 않는다. [자료 회귀](../tests/test_common_cost_artifacts.py)는 세 단계의 정상·누락·경로·gzip 메타데이터와 과거 source 검사를 대조한다.

schema 2 분석은 `preflight_records`, `formal_records`, `diagnostic_records`를 구분한다. `instrumented_records`는 진단 수와 같다. 비계측 block -1, 비계측 정식 범위, 계측 block -2만 허용하며 Python 수명·조건부 분석에도 적용한다. 기존 schema 1 결과의 잘못된 표본 수나 누락 필드는 원본 그대로 보존한다.

기본 `verify`·`analyze`는 현재 소스와 등록 hash가 다르면 거절한다. 과거 결과에는 아래의 명시적 재현 경로를 사용한다. `SOURCE_COMMIT`은 등록한 기준 파일 목록·hash와 모두 일치하는 신뢰할 수 있는 로컬 조상 commit이다. 파일 목록은 해당 commit에서 도출하므로 새 검증기 도입 전의 다섯 파일과 도입 후의 여섯 파일을 구분한다. 이 경로는 그 commit의 분석 Python 코드를 메모리에서 실행하므로 검토한 이력에만 사용한다. 강화한 현재 자료 coverage·gzip 검사와 기존 순서·정확성·완료·시간 한도 검사를 통과한 뒤 분석하며 `register`·`collect`에는 이 옵션을 허용하지 않는다.

```sh
python3 bench/common_cost.py verify --registration "$PREFIX.registration.json" --prefix "$PREFIX" --source-commit "$SOURCE_COMMIT"
python3 bench/common_cost.py analyze --registration "$PREFIX.registration.json" --prefix "$PREFIX" --source-commit "$SOURCE_COMMIT"
python3 bench/common_cost.py analyze --registration "$PREFIX.registration.json" --prefix "$PREFIX" --source-commit "$SOURCE_COMMIT" --correct-metadata
```

처음 두 명령은 보존한 입력을 검증하고 당시 schema의 원 JSON을 재현한다. 마지막 명령은 원 분석의 효과·구간·판정을 유지하면서 schema 2의 표본 수와 `metadata_correction`을 추가한다. 이 필드에는 기준 commit·등록 분석 hash·등록 내용 hash와 현재 수집기·분석기·자료 검증기의 hash가 있다. 검증기 도입 전 두 처리기 hash를 기록한 수정 JSON도 원본 그대로 보존한다. 결과는 stdout으로 내보내며 보존 파일은 직접 고치지 않는다. 수정 결과를 저장할 때는 사용하지 않은 파일을 선택하고 원 분석을 덮어쓰지 않는다. 새 수집이나 다른 내구성·backend의 성능 근거로 사용하지 않는다.

### 요청 단위 내부 계측

정상 빌드는 `BENCHMARK=0`이며 계측 매크로의 인자와 선언은 전처리 때 사라진다. 런타임 환경 변수로 켤 수 없다. `BENCHMARK=1`만 별도 `build-benchmark`에서 계측 소스를 링크한다. 이 모드에 `BUILD_DIR=build`를 주면 실패한다. 사용자 지정 디렉터리를 재사용해도 모드·컴파일러·플래그 fingerprint가 바뀌면 객체와 공유 라이브러리를 다시 만든다. 실제 플래그는 해당 디렉터리의 `.build-fingerprint`와 Make 출력에 남는다.

```sh
make test-benchmark
make BENCHMARK=0 SANITIZE= counter-driver
python3 bench/counter_run.py --build-dir build --output .local/runs/counter-off --n 12
make BENCHMARK=1 SANITIZE= counter-driver
python3 bench/counter_run.py --build-dir build-benchmark --output .local/runs/counter-on --n 12
python3 tools/verify_benchmark.py build/lib/libstreamlite.so
python3 tools/verify_benchmark.py build-benchmark/lib/libstreamlite.so --enabled
```

macOS에서는 검사할 라이브러리의 확장자를 `.dylib`로 바꾼다. output 디렉터리는 새 경로여야 한다. 실패한 DB도 보존한다. 이 작은 실행은 정확성 검사이며 성능 결론의 근거가 아니다. 두 엔진 모두 같은 단일 행 스키마와 단일 클라이언트로 매 증가마다 명시적 begin/update/commit을 실행하고 영향 행 수, 최종 값, 닫았다 다시 연 영속 값을 검사한다. SQLite prepare-each와 prepared-reuse는 별도 구성이다. 입력 생성·DB 생성·JSON 출력은 측정 밖이다. StreamLite의 결과 wire 인코딩은 exec에 포함되며 결과 해석·해제와 SQLite changes/reset/finalize 비용은 별도로 남긴다.

계측 범위는 C API exec·begin·commit·rollback·queue put/get/ack 전체이며 검증 실패와 EMPTY 반환도 포함한다. 내부에는 어휘 분석·구문 분석, 실행 부모, 카탈로그 복제·save/load, 게시·commit, 정수 키 lookup·heap page 수집·batch finish, 핸들 mutex 획득, 파일 lock 획득과 개별 pread/pwrite/fsync/flock 호출이 있다. ROW_INDEX는 이 coarse 범위만 포함하며 전체 행 처리 비용을 뜻하지 않는다. 결과는 스레드별 고정 버퍼에 쌓이고 요청 밖에서 drain한다. 불완전한 시계·스택·overflow·drop은 카운터로 남는다.

inclusive 단계 시간은 자식을 포함한다. exclusive만 자식 시간을 제외하므로 부모와 자식을 더하면 안 된다. CPU 시간과 I/O wall 시간을 합쳐 100%로 만들지 않는다. wall−CPU는 미분류 잔여이며 OS off-CPU나 잠금 시간으로 단정하지 않는다. I/O bytes는 커널 호출에 수락된 양이지 물리 장치 전송량이 아니다. SQLite 내부 단계는 측정하지 않았으므로 unknown이며 0비용이 아니다.

wheel과 sdist 재빌드는 환경의 Make 모드 변수를 차단하고 패키징 전용 경로에서 `BENCHMARK=0 SANITIZE=`로 강제 재빌드한다. 복사 전후와 설치 smoke에서 `tb_bench` 정의·참조·TLS 심볼을 검사하여 계측 라이브러리의 배포를 거절한다.

### 여러 프로세스 outbox 예제

[run.py](../examples/outbox/run.py)는 Python 생산자가 주문과 작업을 함께 저장하고 C worker가 출고 기록과 ACK를 함께 commit하는 예제다. 외부 출고 API를 호출하지 않고 DB의 출고 행으로 처리 결과를 확인한다. Python 3.9 이상과 위 C 빌드 도구를 준비하고 저장소 루트에서 실행한다.

```sh
make BENCHMARK=0 SANITIZE= all build/outbox_consumer
STREAMLITE_LIB=build/lib/libstreamlite.so PYTHONPATH=python python3 examples/outbox/run.py --consumer build/outbox_consumer --dir build/outbox-example --orders 30 --workers 3 --lease-ms 800
```

macOS에서는 `STREAMLITE_LIB`의 확장자를 `.dylib`로 바꾼다. `--dir`에는 예제 전용 경로만 준다. 스크립트는 그 안의 기존 `shop.streamlite`를 삭제하므로 보관할 DB 경로를 주지 않는다. 기본 입력은 주문 30건·worker 3개·lease 800밀리초다. 위 명령은 기본값을 명시하며 DB를 `build/outbox-example`에 남긴다.

실행 중 worker 하나를 claim 직후 SIGKILL한다. 나머지 worker가 lease 만료 뒤 해당 주문을 재전달받아 처리한다. 출력 끝의 `[검증]` 행에서 출고 30건·중복 0·누락 0·큐 acked 30과 재전달을 확인하고, 마지막 `검증 통과` 및 종료 코드 0을 확인한다. worker별 처리 건수와 재전달 대상은 실행마다 다를 수 있다.

스크립트는 강제 종료한 worker를 회수하고 나머지 worker가 끝날 때까지 기다린 뒤 DB를 다시 열어 검증하고 종료한다. 정상 완료 뒤 별도 서버 종료 명령은 없다. `make example-outbox`도 같은 스크립트를 실행하지만 `--dir`을 주지 않아 OS 임시 경로를 만든다. 경로를 지정하려면 위 직접 실행 명령을 쓴다. 자동 회귀시험은 `make test-ack-queue-python`에 포함된 [test_outbox_example.py](../tests/test_outbox_example.py)다. README의 ACK 생략 재생과 달리 실제 프로세스를 종료하는 시험이다.

## 다른 엔진과 비교

같은 SQL을 이 엔진, SQLite, PostgreSQL에 실행해 행과 오류 종류를 비교한다. 사례는 `tests/sql_diff_cases.py`에 있다. 엔진마다 의미가 다른 사례는 사례 안에 차이를 적고 `DIFF`로 보고한다.

```sh
make test-sql-diff
make test-sql-diff SQLDIFF_DSN=postgresql://user:password@localhost:5432/sqldiff
```

`SQLDIFF_DSN`이 없으면 PostgreSQL 비교를 건너뛴다. `SQLDIFF_DSN`을 주면 PostgreSQL에 연결하지 못할 때 실패로 끝난다. PostgreSQL 비교에는 Python 패키지 `psycopg`와 `C` 정렬 규칙으로 만든 데이터베이스가 필요하다.

두 연결이 같은 파일을 번갈아 쓰는 동작은 SQLite 3.51.0과 비교한다. 이 대상은 SQLite 소스를 내려받아 sha256을 확인한 뒤 공유 라이브러리로 빌드하므로 네트워크가 필요하다.

```sh
make SANITIZE= BUILD_DIR=build-sqlite test-sqlite-session-diff
```

### 성능 측정

`bench/sqlite_workload.py`는 같은 SQL을 이 엔진과 시스템 SQLite에 주고, 문장마다 결과를 대조하면서 시간과 메모리를 잰다.

측정 항목은 자동 commit 삽입, 기본 키·구간·조건·전체 스캔 조회, 혼합 부하, 강제 종료 뒤 다시 열기다. 측정 조건(seed, 행 수, 반복 수, 내구성 설정)은 `bench/sqlite_workload.json`에 있다. 결과가 하나라도 다르면 실패로 끝난다. 시간 수치는 통과·실패 판정에 쓰지 않는다.

```sh
python3 bench/sqlite_workload.py --smoke
python3 bench/sqlite_workload.py --output build/bench/sqlite_workload.json
python3 bench/sqlite_workload.py --output build/bench/sqlite_workload.json --analyze
```

공식 YCSB 0.17.0은 같은 Java/JNI SQL 바인딩으로 StreamLite와 SQLite에 A~F를 실행한다(2026-10-18 확인). Java 21, Python 3.9 이상, C 컴파일러와 SQLite 개발 헤더가 필요하다. 고정 JAR 다운로드에 네트워크를 사용한다.

```sh
make ycsb-build
make ycsb-test
```

이 시험은 양쪽의 결정적 연결 검사와 공식 Client의 축소 matrix를 검사한다. 성능 합격선은 두지 않는다. 별도 성능 수집과 원자료 재분석은 [바인딩 안내](../bench/ycsb/README.md)에 있다. [YCSB 동등 조건 측정 결과](experiments/ycsb-sqlite/report.md)는 고정한 자동 commit 설정의 240회 비교이며, WAL·준비 문장 캐시를 튜닝한 DB 전체 성능을 뜻하지 않는다.

독립 연결의 큐 쓰기 경합은 [큐 쓰기 경합 결과](experiments/queue-contention/report.md)에 있다. 공식 YCSB와 별개인 enqueue·drain·mixed·outbox 부하를 SQLite DELETE/FULL·WAL/FULL과 비교한다. 연결마다 worker를 하나씩 두며 payload·lease·fence·저장 결과를 대조한다.

```sh
make SANITIZE= all
python3 -m unittest discover -s tests -p test_queue_contention.py -v
python3 bench/queue_contention/run.py verify --output docs/experiments/queue-contention
```

새 실측은 [Linux 계획](experiments/queue-contention/linux-design.md)의 소스와 환경에서 실행한다. 실행 ID는 새로 정하고 기존 자료를 덮어쓰지 않는다. 분석·검증에는 native library가 필요하므로 먼저 빌드한다.

Issue #232의 비용 가설 진단은 이미 빌드한 두 라이브러리를 사용한다. 아래 output은 존재하지 않아야 한다. 빌드를 모두 끝낸 뒤 실행하며 기존 자료를 덮어쓰지 않는다. 정식 비교와 native trace는 별개이며, 비Linux·도구 및 권한 부재는 `unavailable`로 보존한다.

```sh
python3 -m unittest discover -s tests -p test_queue_diagnose.py -v
python3 bench/queue_contention/diagnose.py compare \
  --baseline-lib "$BASELINE_LIB" --candidate-lib "$CANDIDATE_LIB" \
  --output .local/runs/queue-cost-compare --jobs 4096 --repetitions 5 --workers 1,8
python3 bench/queue_contention/diagnose.py trace \
  --library "$CANDIDATE_LIB" --engine streamlite \
  --output .local/runs/queue-cost-trace --jobs 4096 --workers 8
```

작은 정확성 smoke는 compare의 `--jobs 12 --repetitions 1 --workers 1`로 실행한다. 성능 비교에 사용하지 않는다. 조건과 채택 기준은 [Linux 후속 계획](experiments/queue-contention/linux-design.md#issue-232-후속-비용-진단과-전후-비교)에 있다.

TEXT 기본 키 범위 개선의 전후 비교는 [TEXT 색인 구간 순회 결과](experiments/text-range/report.md)에 있다. 고정 소스의 기존 공식 E를 같은 VM에서 전후로 실행하며 [Linux 계획](experiments/text-range/linux-design.md)에 조건과 순서를 둔다.

```sh
RUN_ID=20261019T031416Z-b590d61-check ./docs/experiments/text-range/run.sh verify
RUN_ID=20261019T031416Z-b590d61-check ./docs/experiments/text-range/run.sh analyze
```

## CI

`.github/workflows/ci.yml`은 Ubuntu에서 아래 작업을 실행한다.

| 작업 | 명령 |
|---|---|
| `test` (address, undefined) | `make SANITIZE=<sanitizer> test-all test-runner test-fault test-result` 뒤 별도 `make SANITIZE=<sanitizer> test-benchmark` |
| `test` (thread) | `make SANITIZE=thread test-transaction test-session test-ack-queue` |
| `pg-compat` | PostgreSQL 18 서비스에 `make test-sql-diff`를 실행한 뒤 `make test-sqlite-session-diff` |
| `actionlint` | 워크플로 파일 검사 |
| `queue-contention` | Ubuntu 24.04에서 공개 Python 큐 API·동시 실행·오류 보존·원자료 재현 검사 |
| `ycsb` | Ubuntu 24.04·Java 21에서 `make ycsb-build ycsb-test`, 양쪽 바인딩·공식 Client·원자료 재분석 검사 |

CI의 SQL 비교·워크플로 검사와 릴리스의 Linux 보조 job은 Ubuntu 24.04로 고정한다. sanitizer 시험은 Ubuntu 22.04를 유지한다. 결과 파일 업로드·다운로드 액션은 Node.js 24 런타임을 사용한다.

옛 기준 source·ABI를 고정해 비교하던 label 실험 job(`local-db-queue-experiment`, `release-cost-experiment`, `benchmark-comparison`, `queue-cost-experiment`)은 제거했다. 기존 실험 기록은 기록된 source revision에 남아 있으며 새 측정이 아니다. 새 다중 writer·COW/WAL 정리·복구 전체 비용 비교는 아직 끝나지 않았다.

태그 push와 패키징 파일을 바꾼 PR에서는 `.github/workflows/release.yml`이 실행된다([패키지 배포](#패키지-배포)).

## 패키지 배포

Python 패키지 `streamlite`는 아직 PyPI에 게시하지 않았다. 게시 전에는 저장소에서 `pip install .`로 설치하거나 직접 빌드한 wheel을 설치한다. 공유 라이브러리를 ctypes로 열기 때문에 Python 버전과 무관한 `py3-none-{플랫폼}` wheel을 플랫폼마다 하나씩 만든다.

| 파일 | 역할 |
|---|---|
| `pyproject.toml` | 패키지 정보, 버전 출처(`python/streamlite/__init__.py`), cibuildwheel 설정 |
| `setup.py` | `make SANITIZE= all`로 만든 `libstreamlite`를 패키지 폴더에 넣고 wheel 태그 결정 |
| `MANIFEST.in` | sdist에 C 소스와 `Makefile`을 넣어 설치 때 다시 빌드 |
| `tests/package_smoke.py` | 별도 설치 환경에서 패키지 안의 라이브러리로 DB·큐 사용 확인 |

Python 바인딩의 라이브러리 탐색은 [설정](configuration.md#우선순위)을 따른다.

로컬에서 확인하려면 아래처럼 빌드해 설치한다.

```sh
python3 -m build
python3 -m venv build/package-env
build/package-env/bin/pip install dist/*.whl
STREAMLITE_ROOT="$PWD"
cd build
package-env/bin/python "$STREAMLITE_ROOT/tests/package_smoke.py"
cd "$STREAMLITE_ROOT"
```

### 릴리스 순서

1. `python/streamlite/__init__.py`의 `__version__`, `include/streamlite.h`의 `STREAMLITE_VERSION`, `CHANGELOG.md`의 `## {버전} - {날짜}` 절을 같은 버전으로 맞춘다.
2. 기본 브랜치에 머지한 뒤 `v{버전}` 태그를 push한다.
3. `.github/workflows/release.yml`이 버전 일치를 검사하고 sdist와 wheel(Linux x86_64·aarch64 manylinux_2_28, macOS arm64 11.0 이상)을 빌드·설치·검사한다.
4. PyPI Trusted Publishing(`pypi` 환경)으로 게시한 뒤 변경 기록 절을 본문으로 GitHub Release를 만들고 배포 파일을 첨부한다.

패키징 파일을 바꾼 PR에서는 같은 워크플로가 3단계까지만 실행한다. 업로드한 배포 파일을 다시 내려받아 `twine check`로 검사한 뒤에만 게시 job을 시작한다. Windows와 macOS x86_64 wheel은 만들지 않는다. macOS x86_64는 sdist로 설치한다.

## macOS에서 ASan 바이너리가 멈추는 문제

macOS 26(Darwin 25)과 Apple clang 17에서는 ASan 바이너리가 `main()`에 닿기 전에 멈춘다. ASan 런타임 초기화가 자기 자신을 다시 부르는 문제이며 빈 `main()`을 `cc -fsanitize=address`로 빌드해도 같은 증상이 나온다. 스택은 `AsanInitInternal`, `InitializeShadowMemory`, `get_dyld_hdr`, `dyld_shared_cache_iterate_text_swift`, `malloc`, `__sanitizer_mz_malloc`를 거쳐 다시 `AsanInitFromRtl`로 들어가 spin lock에서 멈춘다. UBSan만 켠 빌드는 정상 동작한다.

`Makefile`은 Darwin 25 이상에서 `SANITIZE` 기본값을 `undefined`로 낮춘다. ASan을 켠 시험은 Linux나 저장소의 Dev Container(`.devcontainer/`)에서 실행한다.

## README 그림

[README 그림 원본](assets/transaction-queue.diagramplay)과 [DB 실행 관측값](assets/transaction-queue.trace.json)이 현재 입력이다. 도구 기준은 [DiagramPlay `0f35b9b`](https://github.com/woonyong-choi/diagramplay/tree/0f35b9b1c400355e769f8302a3f3f348a07d69f5)과 Node.js 26.9.0이다. `DIAGRAMPLAY_DIR`은 해당 커밋의 준비된 도구 작업본을 가리킨다. 원본 검사만 하려면 StreamLite 루트에서 아래 명령을 실행한다.

```sh
node "$DIAGRAMPLAY_DIR/src/cli.js" check docs/assets/transaction-queue.diagramplay --strict
```

README는 화면 폭에 따라 움직이는 [기본 SVG](assets/transaction-queue.svg)와 [좁은 SVG](assets/transaction-queue-narrow.svg)를 고른다. 시나리오는 [넓은 HTML 재생 화면](assets/transaction-queue.html)과 [좁은 HTML 재생 화면](assets/transaction-queue-narrow.html)에서 선택한다. SVG와 HTML은 서로 다른 생성물이며, 원본의 장면 값과 trace 관측값을 함께 대조한다. `.torso`와 Torso `14e57b7`의 `--static` 명령은 현재 움직이는 SVG의 재생성 절차가 아니므로 사용하지 않는다.

GitHub의 HTML 링크는 재생 화면이 아니라 소스보기다. 장면을 선택하려면 저장소를 내려받은 뒤 `docs/assets/transaction-queue.html` 또는 `transaction-queue-narrow.html`을 로컬 브라우저에서 연다. `storage-publication`과 `queue-lifecycle`의 기본·좁은 HTML도 같은 방식으로 연다. GitHub에서 움직이는 SVG가 보이는 것과 HTML의 장면 선택 기능은 구분한다.

재생성은 StreamLite 루트에서 `node tools/render-figures.mjs`로 한다. 큐 2개는 `buildFigure(source, {strict: true, baseDir})`의 기본 폭으로 빌드하고 `toSvg(result, {name})`·`toHtml(result, name)`으로 내보낸다. 좁은 배치는 고정 도구의 내부 `src/build.js`에 있는 `reflowFigure`로 같은 기준 시간표를 유지한다. 원본의 장면 값을 trace와 대조하고 정지 출력으로 덮어쓰지 않는다. 7개 원본과 22개 생성물의 정확한 옵션·환경·바이트 대조는 [아키텍처의 재생성](architecture.md#그림-원본과-재생성)을 따른다.

## pending 조회 수집

[사전 계획](design/streamlite.md#pending-조회-수집의-사전-계획)은 실제 persist-queue 1.1.0, 현재 호환 계층과 native 목록 API를 같은 최종 소비 결과로 비교한다. `bench/pending_query_plan.json`은 현재 macOS 작업본의 고정 binary 경로·hash를 사용한다. 다른 호스트의 재빌드는 같은 등록으로 덮어쓰지 않고 별도 계획·환경으로 다룬다. `register --control`은 9행·32B body·4개 page의 36건 대조이며 정식 효과 판단에 쓰지 않는다.

```sh
make BUILD_DIR=build-regression/conditional-baseline-on BENCHMARK=1 pending-query-driver
make BUILD_DIR=build-regression pending-query-fault-driver
build/check-env/bin/python bench/pending_query.py register --control --prefix build/pending-control
build/check-env/bin/python bench/pending_query.py collect --prefix build/pending-control
build/check-env/bin/python bench/pending_query.py verify --prefix build/pending-control
build/check-env/bin/python bench/pending_query.py analyze --prefix build/pending-control
```

정식 수집은 계획·수집기를 함께 담은 commit 뒤의 clean 작업본에서 실행한다. `PLAN_COMMIT`에는 그 전체 SHA를 지정한다. 등록 후 수집 전에는 source·binary·환경을 바꾸지 않는다. 등록·실패 접두사가 이미 있으면 새 접두사를 사용하며 기존 파일을 덮어쓰지 않는다.

```sh
build/check-env/bin/python bench/pending_query.py register --plan-commit "$PLAN_COMMIT" --prefix build/pending-formal
build/check-env/bin/python bench/pending_query.py collect --prefix build/pending-formal
build/check-env/bin/python bench/pending_query.py verify --prefix build/pending-formal
build/check-env/bin/python bench/pending_query.py analyze --prefix build/pending-formal
```

`<prefix>.registration.json`은 source·설정·일정·binary·환경, `.fixtures.json`은 첫 조회 전 데이터 hash, `.raw.jsonl`은 case별 관측, `.manifest.json`은 완료·실패·미실행·파일 hash·시간을 담는다. 같은 접두사의 fixture 디렉터리와 stdout/stderr도 유일한 실행 증거이므로 보존한다. analyzer는 원자료에서 처리한 통계를 stdout으로 내며, 저장한 JSON을 고쳐 수치를 맞추지 않는다. 등록한 수집 코드가 바뀌면 기존 실행을 새 코드의 표본으로 읽지 않고 원래 코드에서 재현한다.

`verify`는 등록 계획·일정에서 필수 raw·등록 JSON·fixture 데이터·생성 로그·각 worker와 native 진단 로그를 계산해 hash 목록의 누락부터 거부한다. 기재한 파일은 허용된 절대 정규 경로여야 하며 alias·symlink·중복 JSON key와 잘못된 digest를 거부한다. fixture 등록 두 사본의 경로·데이터 hash도 일치해야 한다. 수집 때 남은 SQLite WAL/SHM과 native lock은 별도로 hash를 확인한다. 수집 뒤 저장한 분석 출력은 필수 수집 자료로 추가하지 않는다. 이 검사는 자료의 완전성과 변경을 확인하며, 전체 장부를 교체하는 행위에 대한 인증은 제공하지 않는다.

[수집기 회귀](../tests/test_pending_query.py)는 정상 결과·손상 digest·행 수·계측 혼입·변경된 입력·시간 초과·실패 보존, 혼합 상태·page 전환·수명·변환/serializer 오류를 검사한다. [native 실패 대조](../tests/test_pending_query_native.c)는 같은 합성 fixture를 인자로 받아 payload malloc·session reserve 실패 뒤 부분 결과·누수와 다음 조회를 확인한다. 제품 코드·현재 파일·기본 backend와 소비 프로젝트는 이 명령으로 교체하지 않는다.

[첫 판정](design/streamlite.md#pending-조회-수집의-첫-판정)은 계획/source `c0cb4216`에서 등록한 전체 1,728건(정식 1,440건)을 사용한다. 보존 자료가 있는 이 로컬 작업본에서는 위 `verify`·`analyze`의 접두사에 `build/task19-pending-formal`을 지정하면 같은 분석을 재생성한다. 새 효과를 수집할 때는 기존 접두사를 재사용하지 않는다. 한 page의 표본 내 메모리·지연 지지, 전체 순차 소비의 시간 비용, caller 전체 축적과 실제 소비자 미교체를 함께 읽는다.

## 기본키 규모 실측

[#158 재개 조건](design/streamlite.md#기본키-규모-실측의-재개-조건)은 기존 seed137 입력·120초 적재 제한과 세 규모의3회 반복을 유지한다. [수집기](../bench/primary_key_scale.py)는 현재 작업본의 고정된 OFF 라이브러리와 build fingerprint가 필요하다. 새 driver가 없을 때만 첫 명령으로 빌드하며, 이미 있는 실행 파일은 덮지 않는다. 등록·수집은 계획을 담은 clean commit 다음에 실행하고 새 prefix를 사용한다.

```sh
build/check-env/bin/python bench/primary_key_scale.py build
build/check-env/bin/python bench/primary_key_scale.py register --prefix build/primary-key-formal --plan-commit HEAD
build/check-env/bin/python bench/primary_key_scale.py collect --prefix build/primary-key-formal
build/check-env/bin/python bench/primary_key_scale.py verify --prefix build/primary-key-formal
build/check-env/bin/python bench/primary_key_scale.py analyze --prefix build/primary-key-formal > build/primary-key-analysis.json
```

`register --control`은24행의 수집기 대조만 등록한다. 정식 결과로 합치지 않는다. `verify`는 등록 일정에서 필수 자료를 도출하고 hash·행 수·조회 순서·독립 기본키 기대값·스캔 양성 대조·재열기를 검사한다. timeout의 부분 입력도 실제 행 수로 보존하지만 요청 규모 완료는 아니다. SQLite의 직접 스캔 행 수는 기록하지 않는다. 기존 `sqlite_workload.py`의 전체 혼합·복구 캠페인과 다른 경계이며, 그 과거 raw나 실패 표본을 교체하지 않는다.

[기본키 규모 첫 판정](design/streamlite.md#기본키-규모-실측의-첫-판정)은 계획/source `ff3af84d`에서 등록한9case를 모두 완료했다. 보존 자료가 있는 이 작업본에서 `verify`·`analyze`의 접두사에 `build/task21-primary-key-formal`을 지정하면 같은 분석을 재생성한다. 원자료는 로컬에 보존했고 공개 발행은 별도다. 새 표본은 기존 접두사를 재사용하지 않는다. 이번 결과는 현재 단일 writer COW의 기본키 규모 검증이며 새 backend의 채택 근거와 구분한다.
