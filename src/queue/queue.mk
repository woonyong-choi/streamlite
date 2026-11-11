# 선택형 ACK 큐(#140) 빌드 규칙. Makefile이 -include로 읽는다. DB 코어 빌드에는 큐가 포함되지 않는다.
# 큐는 세션(session.o)으로 문장을 실행한다.
# DB 코어(DATABASE_OBJS)는 큐 객체를 링크하지 않는다. 큐를 쓰는 바이너리만 QUEUE_OBJS를 더한다.

QUEUE_OBJS = $(BUILD_DIR)/queue/ack_queue.o $(BUILD_DIR)/queue/ack_queue_sql.o $(BUILD_DIR)/queue/ack_queue_conditional.o
QUEUE_TESTS = test_ack_queue test_ack_queue_fault test_streamlite
QUEUE_BINS = $(addprefix $(BUILD_DIR)/,$(QUEUE_TESTS))

$(QUEUE_BINS): $(BUILD_DIR)/%: $(BUILD_DIR)/tests/%.o $(QUEUE_OBJS) $(BUILD_DIR)/sql/session.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-ack-queue: $(QUEUE_BINS) $(BUILD_DIR)/test_ack_queue_conditional
	@set -e; for binary in $(QUEUE_BINS) $(BUILD_DIR)/test_ack_queue_conditional; do \
		$(TEST_RUNNER) --report "$(BUILD_DIR)/fault/$${binary##*/}.json" -- "$$binary"; \
	done

-include $(QUEUE_OBJS:.o=.d) $(addprefix $(BUILD_DIR)/tests/,$(addsuffix .d,$(QUEUE_TESTS)))

.PHONY: test-ack-queue

$(BUILD_DIR)/queue/ack_queue_probe.o: src/queue/ack_queue.c $(BUILD_FINGERPRINT)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=ackq_probe_malloc -c -o $@ $<

$(BUILD_DIR)/queue/ack_queue_conditional_probe.o: src/queue/ack_queue_conditional.c $(BUILD_FINGERPRINT)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=ackq_probe_malloc -c -o $@ $<

$(BUILD_DIR)/queue/ack_queue_sql_probe.o: src/queue/ack_queue_sql.c $(BUILD_FINGERPRINT)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dsession_exec=ackq_probe_session_exec -c -o $@ $<

$(BUILD_DIR)/test_ack_queue_conditional: $(BUILD_DIR)/tests/test_ack_queue_conditional.o $(BUILD_DIR)/queue/ack_queue_probe.o $(BUILD_DIR)/queue/ack_queue_conditional_probe.o $(BUILD_DIR)/queue/ack_queue_sql_probe.o $(BUILD_DIR)/sql/session.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-ack-queue-conditional: $(BUILD_DIR)/test_ack_queue_conditional
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_ack_queue_conditional.json -- ./$<

-include $(BUILD_DIR)/queue/ack_queue_probe.d $(BUILD_DIR)/queue/ack_queue_conditional_probe.d $(BUILD_DIR)/queue/ack_queue_sql_probe.d $(BUILD_DIR)/tests/test_ack_queue_conditional.d
.PHONY: test-ack-queue-conditional

# Python 어댑터가 쓰는 공유 라이브러리. 센티널(sanitizer)을 켜지 않은 최적화 빌드이며 DB 소스와 큐 소스를 한 번에 묶는다.
STREAMLITE_SHARED_EXT = $(if $(filter Darwin,$(UNAME_S)),dylib,so)
STREAMLITE_SHARED_LIB = $(BUILD_DIR)/lib/libstreamlite.$(STREAMLITE_SHARED_EXT)
STREAMLITE_SHARED_SRCS = $(patsubst $(BUILD_DIR)/%.o,$(SRC_DIR)/%.c,$(DATABASE_OBJS)) \
                    src/sql/session.c src/queue/ack_queue.c src/queue/ack_queue_sql.c src/queue/ack_queue_conditional.c src/capi/streamlite.c

$(STREAMLITE_SHARED_LIB): $(STREAMLITE_SHARED_SRCS) $(wildcard include/*.h include/*/*.h) $(wildcard src/queue/*.h src/sql/*.h src/storage/*.h) src/benchmark.h $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(SHARED_CFLAGS) -fPIC -shared -o $@ $(STREAMLITE_SHARED_SRCS) $(LDLIBS)

# Python 시험은 pytest가 필요하다. STREAMLITE_PYTHON으로 pytest가 있는 인터프리터를 고른다.
STREAMLITE_PYTHON ?= $(PYTHON)

# 여러 언어 예제(#201)의 C worker. 공유 라이브러리와 같은 최적화 빌드로 공개 헤더 streamlite.h만 쓴다
OUTBOX_CONSUMER = $(BUILD_DIR)/outbox_consumer

$(OUTBOX_CONSUMER): examples/outbox/consumer.c include/streamlite.h $(STREAMLITE_SHARED_LIB)
	$(CC) -O2 -g $(WARNFLAGS) -Iinclude -o $@ $< -L$(dir $(STREAMLITE_SHARED_LIB)) -lstreamlite -Wl,-rpath,$(abspath $(dir $(STREAMLITE_SHARED_LIB)))

test-ack-queue-python: $(STREAMLITE_SHARED_LIB) $(OUTBOX_CONSUMER)
	STREAMLITE_LIB=$(abspath $(STREAMLITE_SHARED_LIB)) OUTBOX_CONSUMER=$(abspath $(OUTBOX_CONSUMER)) PYTHONPATH=python $(STREAMLITE_PYTHON) -m pytest -q tests/test_ack_queue_python.py tests/test_ack_queue_synthetic_contract.py tests/test_database_python.py tests/test_queue_lifecycle.py tests/test_persistqueue_compat.py tests/test_multiprocess.py tests/test_outbox_example.py

# Python 생산자와 C worker가 같은 큐를 쓰는 예제를 실행한다
example-outbox: $(STREAMLITE_SHARED_LIB) $(OUTBOX_CONSUMER)
	STREAMLITE_LIB=$(abspath $(STREAMLITE_SHARED_LIB)) PYTHONPATH=python $(STREAMLITE_PYTHON) examples/outbox/run.py --consumer $(OUTBOX_CONSUMER)

# 새 엔진·SQLite·PostgreSQL 3자 SQL 차분. SQLDIFF_DSN이 있으면 PostgreSQL까지 반드시 비교한다(psycopg 필요).
test-sql-diff: $(STREAMLITE_SHARED_LIB)
	STREAMLITE_LIB=$(abspath $(STREAMLITE_SHARED_LIB)) PYTHONPATH=python $(STREAMLITE_PYTHON) tests/sql_diff.py \
		$(if $(SQLDIFF_DSN),--dsn $(SQLDIFF_DSN) --require-postgres)

.PHONY: test-ack-queue-python test-sql-diff example-outbox
