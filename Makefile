CC      = gcc
WARNFLAGS = -Wall -Wextra -Werror
INCLUDES = -Iinclude
# ── sanitizer 기본값 ──
#
# macOS 26(Darwin 25) 이상에서는 ASan 런타임이 초기화 도중 자기 자신을
# 재진입해(AsanInitInternal → ... → malloc → AsanInitFromRtl) spin lock 에
# 걸린다. 프로세스가 main() 에 닿지 못하므로 ASan 을 켠 빌드는 무엇을 하든
# 멈춘다. ToteDB 의 문제가 아니라 플랫폼 문제이고, 빈 main() 으로도 재현된다.
# 그 환경에서는 UBSan 만 켠다. Linux CI 는 아래 기본값대로 둘 다 켠다.
#
#   make SANITIZE=address,undefined   강제로 둘 다
#   make SANITIZE=                    sanitizer 없이
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
ifeq ($(shell test $$(uname -r | cut -d. -f1) -ge 25 && echo yes),yes)
SANITIZE ?= undefined
endif
endif
SANITIZE ?= address,undefined

ifneq ($(strip $(SANITIZE)),)
SANITIZE_FLAGS = -fsanitize=$(SANITIZE)
endif

CFLAGS  = $(WARNFLAGS) -g $(INCLUDES) $(SANITIZE_FLAGS)
LDFLAGS = $(SANITIZE_FLAGS)
LDLIBS  = -lpthread -lm
DEPFLAGS = -MMD -MP
PYTHON ?= python3
PYTEST ?= $(PYTHON) -m pytest
TEST_TIMEOUT ?= 60
FAULT_SEED ?= 1
TEST_RUNNER = $(PYTHON) tests/fault_runner.py --timeout $(TEST_TIMEOUT) --seed $(FAULT_SEED)
.DEFAULT_GOAL := all

SQL_TEMPLATE_CACHE ?= 0
ifneq ($(SQL_TEMPLATE_CACHE),0)
ifneq ($(SQL_TEMPLATE_CACHE),1)
$(error SQL_TEMPLATE_CACHE must be 0 or 1)
endif
endif
CACHE_FLAGS = -UTOTE_SQL_TEMPLATE_CACHE -DTOTE_SQL_TEMPLATE_CACHE=$(SQL_TEMPLATE_CACHE)
override CFLAGS += $(CACHE_FLAGS)

TXN_WORKSPACE ?= 0
ifneq ($(TXN_WORKSPACE),0)
ifneq ($(TXN_WORKSPACE),1)
$(error TXN_WORKSPACE must be 0 or 1)
endif
endif
WORKSPACE_FLAGS = -UTOTE_TXN_WORKSPACE -DTOTE_TXN_WORKSPACE=$(TXN_WORKSPACE)
override CFLAGS += $(WORKSPACE_FLAGS)

ACKQ_CONDITIONAL ?= 0
ifneq ($(ACKQ_CONDITIONAL),0)
ifneq ($(ACKQ_CONDITIONAL),1)
$(error ACKQ_CONDITIONAL must be 0 or 1)
endif
endif
CONDITIONAL_FLAGS = -UTOTE_ACKQ_CONDITIONAL -DTOTE_ACKQ_CONDITIONAL=$(ACKQ_CONDITIONAL)
override CFLAGS += $(CONDITIONAL_FLAGS)

BENCHMARK ?= 0
ifneq ($(BENCHMARK),0)
ifneq ($(BENCHMARK),1)
$(error BENCHMARK must be 0 or 1)
endif
endif
SRC_DIR   = src
ifeq ($(BENCHMARK),1)
BUILD_DIR ?= build-benchmark
ifeq ($(abspath $(BUILD_DIR)),$(abspath build))
$(error BENCHMARK=1 cannot use the normal build directory)
endif
BENCHMARK_FLAGS = -UTOTE_BENCHMARK -DTOTE_BENCHMARK=1
BENCHMARK_OBJ = $(BUILD_DIR)/benchmark.o
else
BUILD_DIR ?= build
BENCHMARK_FLAGS = -UTOTE_BENCHMARK
endif
override CFLAGS += $(BENCHMARK_FLAGS)
SHARED_CFLAGS ?= -O2 -g $(WARNFLAGS) $(INCLUDES)
override SHARED_CFLAGS += $(BENCHMARK_FLAGS) $(CACHE_FLAGS) $(WORKSPACE_FLAGS) $(CONDITIONAL_FLAGS)
BUILD_FINGERPRINT = $(BUILD_DIR)/.build-fingerprint
CONFIG_ID := $(shell printf '%s\n' 'BENCHMARK=$(BENCHMARK)' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'SHARED_CFLAGS=$(SHARED_CFLAGS)' 'LDFLAGS=$(LDFLAGS)' | shasum -a 256 | cut -d' ' -f1)
ifeq ($(shell grep -Fx 'CONFIG_ID=$(CONFIG_ID)' $(BUILD_FINGERPRINT) 2>/dev/null),)
.PHONY: $(BUILD_FINGERPRINT)
endif
.PHONY: FORCE
FORCE:
$(BUILD_FINGERPRINT): FORCE
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'CONFIG_ID=$(CONFIG_ID)' 'BENCHMARK=$(BENCHMARK)' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'SHARED_CFLAGS=$(SHARED_CFLAGS)' 'LDFLAGS=$(LDFLAGS)' > $@.candidate
	@cmp -s $@.candidate $@ || cp $@.candidate $@
	@rm -f $@.candidate

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# 각 테스트 번역 단위도 헤더 의존성을 추적한다.
$(BUILD_DIR)/tests/%.o: tests/%.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

STORAGE_OBJS = $(BENCHMARK_OBJ) $(BUILD_DIR)/storage/record.o $(BUILD_DIR)/storage/record_page.o \
               $(BUILD_DIR)/storage/overflow.o $(BUILD_DIR)/storage/checksum.o $(BUILD_DIR)/storage/value.o $(BUILD_DIR)/storage/catalog.o \
               $(BUILD_DIR)/storage/page_file.o $(BUILD_DIR)/storage/page_cache.o $(BUILD_DIR)/storage/page_workspace.o $(BUILD_DIR)/storage/catalog_store.o \
               $(BUILD_DIR)/storage/pk_tree.o $(BUILD_DIR)/storage/text_tree.o
BASIC_TESTS = test_record test_record_fault test_overflow test_overflow_fault test_value test_value_fault \
              test_catalog test_catalog_fault test_lexer test_lexer_fault test_page_file test_catalog_store \
              test_pk_tree test_text_tree
TEST_BINS = $(addprefix $(BUILD_DIR)/,$(BASIC_TESTS))

CANDIDATE_WAL_TAIL_MODULE ?= $(BUILD_DIR)/storage/candidate_wal_tail.o
CANDIDATE_WAL_OBJS = $(BUILD_DIR)/storage/candidate_wal.o $(BUILD_DIR)/storage/candidate_wal_read.o $(BUILD_DIR)/storage/candidate_wal_format.o $(CANDIDATE_WAL_TAIL_MODULE) $(BUILD_DIR)/storage/candidate_digest.o
$(CANDIDATE_WAL_TAIL_MODULE): src/storage/candidate_wal_tail.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
CANDIDATE_CONTROL_OBJS = $(BUILD_DIR)/storage/candidate_control.o $(BUILD_DIR)/storage/candidate_control_format.o
CANDIDATE_CHECKPOINT_OBJS = $(BUILD_DIR)/storage/candidate_checkpoint.o $(BUILD_DIR)/storage/candidate_checkpoint_format.o
$(BUILD_DIR)/test_candidate_checkpoint: $(BUILD_DIR)/tests/test_candidate_checkpoint.o $(CANDIDATE_CHECKPOINT_OBJS) $(CANDIDATE_CONTROL_OBJS) $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/candidate_digest.o $(BUILD_DIR)/storage/checksum.o
CANDIDATE_CHECKPOINT_REPORT ?= $(BUILD_DIR)/fault/test_candidate_checkpoint.json
.PHONY: test-candidate-checkpoint
test-candidate-checkpoint: $(BUILD_DIR)/test_candidate_checkpoint
	$(TEST_RUNNER) --report $(CANDIDATE_CHECKPOINT_REPORT) -- ./$(BUILD_DIR)/test_candidate_checkpoint
$(BUILD_DIR)/test_candidate_control: $(BUILD_DIR)/tests/test_candidate_control.o $(CANDIDATE_CONTROL_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_CONTROL_REPORT ?= $(BUILD_DIR)/fault/test_candidate_control.json
.PHONY: test-candidate-control
test-candidate-control: $(BUILD_DIR)/test_candidate_control
	$(TEST_RUNNER) --report $(CANDIDATE_CONTROL_REPORT) -- ./$(BUILD_DIR)/test_candidate_control
$(BUILD_DIR)/test_candidate_wal: $(BUILD_DIR)/tests/test_candidate_wal.o $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_WITNESS_MODULE ?= $(BUILD_DIR)/storage/candidate_wal_witness.o
CANDIDATE_SYSTEM_MODULE ?= $(BUILD_DIR)/storage/candidate_wal_system.o
CANDIDATE_WITNESS_CHECKPOINT_MODULE ?= $(BUILD_DIR)/storage/candidate_wal_checkpoint.o
CANDIDATE_WITNESS_OBJS = $(CANDIDATE_WITNESS_MODULE) $(CANDIDATE_SYSTEM_MODULE) $(BUILD_DIR)/storage/candidate_catalog.o $(BUILD_DIR)/storage/candidate_allocation_format.o $(BUILD_DIR)/storage/candidate_page.o $(CANDIDATE_WITNESS_CHECKPOINT_MODULE) $(CANDIDATE_CHECKPOINT_OBJS) $(BUILD_DIR)/storage/candidate_control_format.o $(BUILD_DIR)/storage/candidate_digest.o
$(BUILD_DIR)/test_candidate_wal_checkpoint: $(BUILD_DIR)/tests/test_candidate_wal_checkpoint.o $(CANDIDATE_WITNESS_OBJS) $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_WAL_CHECKPOINT_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_checkpoint.json
CANDIDATE_WAL_CHECKPOINT_ARGS ?=
CANDIDATE_REDO_OBJS = $(BUILD_DIR)/storage/candidate_redo.o $(CANDIDATE_WITNESS_OBJS) $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_OUTCOME_CATALOG_MODULE ?= $(BUILD_DIR)/storage/candidate_catalog.o
CANDIDATE_OUTCOME_MODULE ?= $(BUILD_DIR)/storage/candidate_outcome_witness.o
CANDIDATE_OUTCOME_OBJS = $(CANDIDATE_OUTCOME_MODULE) $(BUILD_DIR)/storage/candidate_result.o $(CANDIDATE_OUTCOME_CATALOG_MODULE) $(filter-out $(BUILD_DIR)/storage/candidate_catalog.o,$(CANDIDATE_REDO_OBJS))
$(BUILD_DIR)/test_candidate_outcome_witness: $(BUILD_DIR)/tests/test_candidate_outcome_witness.o $(CANDIDATE_OUTCOME_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_OUTCOME_REPORT ?= $(BUILD_DIR)/fault/test_candidate_outcome_witness.json
CANDIDATE_RECOVERY_MODULE ?= $(BUILD_DIR)/storage/candidate_recovery.o
CANDIDATE_RECOVERY_TERMINAL_MODULE ?= $(BUILD_DIR)/storage/candidate_recovery_terminal.o
CANDIDATE_RECOVERY_OBJS = $(CANDIDATE_RECOVERY_MODULE) $(CANDIDATE_RECOVERY_TERMINAL_MODULE) $(BUILD_DIR)/storage/candidate_recovery_state.o $(CANDIDATE_OUTCOME_OBJS) $(CANDIDATE_CONTROL_OBJS) $(BUILD_DIR)/storage/candidate_allocation.o $(BUILD_DIR)/storage/candidate_wal_identity.o
$(BUILD_DIR)/test_candidate_checkpoint_state: $(BUILD_DIR)/tests/test_candidate_checkpoint_state.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_STATE_REPORT ?= $(BUILD_DIR)/fault/test_candidate_checkpoint_state.json
.PHONY: test-candidate-checkpoint-state
test-candidate-checkpoint-state: $(BUILD_DIR)/test_candidate_checkpoint_state
	$(TEST_RUNNER) --report $(CANDIDATE_STATE_REPORT) -- ./$(BUILD_DIR)/test_candidate_checkpoint_state
$(BUILD_DIR)/test_candidate_outcome_rebuild: $(BUILD_DIR)/tests/test_candidate_outcome_rebuild.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_REBUILD_REPORT ?= $(BUILD_DIR)/fault/test_candidate_outcome_rebuild.json
.PHONY: test-candidate-outcome-rebuild
test-candidate-outcome-rebuild: $(BUILD_DIR)/test_candidate_outcome_rebuild
	$(TEST_RUNNER) --report $(CANDIDATE_REBUILD_REPORT) -- ./$(BUILD_DIR)/test_candidate_outcome_rebuild
$(BUILD_DIR)/test_candidate_outcome_rebuild_limits: $(BUILD_DIR)/tests/test_candidate_outcome_rebuild_limits.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_REBUILD_LIMIT_PREFIX ?= $(BUILD_DIR)/fault/test_candidate_outcome_rebuild_limits
.PHONY: test-candidate-outcome-rebuild-limits
test-candidate-outcome-rebuild-limits: $(BUILD_DIR)/test_candidate_outcome_rebuild_limits
	$(TEST_RUNNER) --report $(CANDIDATE_REBUILD_LIMIT_PREFIX)-capacity.json -- ./$(BUILD_DIR)/test_candidate_outcome_rebuild_limits capacity
	$(TEST_RUNNER) --report $(CANDIDATE_REBUILD_LIMIT_PREFIX)-space.json -- ./$(BUILD_DIR)/test_candidate_outcome_rebuild_limits space
.PHONY: test-candidate-outcome-rebuild-empty
test-candidate-outcome-rebuild-empty: $(BUILD_DIR)/test_candidate_outcome_rebuild_limits
	$(TEST_RUNNER) --report $(CANDIDATE_REBUILD_LIMIT_PREFIX)-empty.json -- ./$(BUILD_DIR)/test_candidate_outcome_rebuild_limits empty
$(BUILD_DIR)/test_candidate_recovery_wal: $(BUILD_DIR)/tests/test_candidate_recovery_wal.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_RECOVERY_WAL_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_wal.json
.PHONY: test-candidate-recovery-wal
test-candidate-recovery-wal: $(BUILD_DIR)/test_candidate_recovery_wal
	$(TEST_RUNNER) --report $(CANDIDATE_RECOVERY_WAL_REPORT) -- ./$(BUILD_DIR)/test_candidate_recovery_wal
$(BUILD_DIR)/test_candidate_aborted_result: $(BUILD_DIR)/tests/test_candidate_aborted_result.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_ABORTED_RESULT_REPORT ?= $(BUILD_DIR)/fault/test_candidate_aborted_result.json
.PHONY: test-candidate-aborted-result
test-candidate-aborted-result: $(BUILD_DIR)/test_candidate_aborted_result
	$(TEST_RUNNER) --report $(CANDIDATE_ABORTED_RESULT_REPORT) -- ./$(BUILD_DIR)/test_candidate_aborted_result
$(BUILD_DIR)/test_candidate_recovery: $(BUILD_DIR)/tests/test_candidate_recovery.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_RECOVERY_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery.json
.PHONY: test-candidate-recovery
test-candidate-recovery: $(BUILD_DIR)/test_candidate_recovery
	$(TEST_RUNNER) --report $(CANDIDATE_RECOVERY_REPORT) -- ./$(BUILD_DIR)/test_candidate_recovery
$(BUILD_DIR)/test_candidate_recovery_terminal: $(BUILD_DIR)/tests/test_candidate_recovery_terminal.o $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_TERMINAL_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_terminal.json
.PHONY: test-candidate-recovery-terminal
test-candidate-recovery-terminal: $(BUILD_DIR)/test_candidate_recovery_terminal
	$(TEST_RUNNER) --report $(CANDIDATE_TERMINAL_REPORT) -- ./$(BUILD_DIR)/test_candidate_recovery_terminal
CANDIDATE_RECOVERY_PUBLISH_OBJS ?= $(BUILD_DIR)/storage/candidate_recovery_publish.o $(BUILD_DIR)/storage/candidate_recovery_inventory.o \
	$(BUILD_DIR)/storage/candidate_recovery_groups.o $(BUILD_DIR)/storage/candidate_recovery_checkpoint.o
CANDIDATE_PUBLISH_TEST_OBJS = $(BUILD_DIR)/tests/test_candidate_recovery_publish_fixture.o $(BUILD_DIR)/tests/test_candidate_recovery_publish_scenario.o
$(BUILD_DIR)/test_candidate_recovery_publish: $(BUILD_DIR)/tests/test_candidate_recovery_publish.o $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/test_candidate_recovery_publish_faults: $(BUILD_DIR)/tests/test_candidate_recovery_publish_faults.o $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/test_candidate_recovery_publish_source: $(BUILD_DIR)/tests/test_candidate_recovery_publish_source.o $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/test_candidate_recovery_publish_reads: $(BUILD_DIR)/tests/test_candidate_recovery_publish_reads.o $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_PUBLISH_FIXTURE_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_publish_fixture.o
$(BUILD_DIR)/test_candidate_recovery_publish_counters: $(BUILD_DIR)/tests/test_candidate_recovery_publish_counters.o \
	$(CANDIDATE_PUBLISH_FIXTURE_OBJS) $(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_PUBLISH_TIMEOUT ?= 900
CANDIDATE_PUBLISH_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_publish
CANDIDATE_PUBLISH_CASES ?= f1-commit f1-abort f2-single f2-fallback f3-highwater f4-shared stale-root-carrier stale-root-alloc \
	stale-root-carrier-retry stale-root-alloc-retry source-view
CANDIDATE_PUBLISH_GATES ?= basic selection empty resolved sparse boundary segments faults
CANDIDATE_PUBLISH_SOURCE_CASES ?= scan-lsn read-lsn pred-spoof delta-chain selection-repair cross-fallback redo-apply moved-end tail-15 \
	append-mutate-a append-mutate-b strict-open
# 고밀도 용량 사례는 수 분이 걸리므로 기본은 비우고 필요할 때만 지정한다.
CANDIDATE_PUBLISH_DENSE_CASES ?=
CANDIDATE_PUBLISH_SOURCE_DENSE_CASES ?=
CANDIDATE_PUBLISH_DENSE_TIMEOUT ?= 1200
CANDIDATE_PUBLISH_COUNTER_CASES ?= gid-default gid-max-2 gid-max-1 gid-max gid-zero-need gid-one-need gen-max gen-max-1 \
	gen-max-1-fallback gen-max-2-fallback
CANDIDATE_PUBLISH_READ_CASES ?= image-calibrate delta-calibrate image-header image-crc image-total delta-header delta-crc delta-total \
	image-nomem dependency-fallback wal-header-pinned wal-header-unpinned wal-crc-pinned wal-crc-unpinned wal-trailer-pinned \
	wal-total-pinned wal-total-unpinned wal-version-pinned wal-uuid-pinned wal-io-header-pinned wal-io-frame-pinned wal-invalid-pinned
CANDIDATE_PUBLISH_READ_PLACE ?=
.PHONY: test-candidate-recovery-publish test-candidate-recovery-publish-source test-candidate-recovery-publish-counters \
	test-candidate-recovery-publish-reads
test-candidate-recovery-publish: $(BUILD_DIR)/test_candidate_recovery_publish $(BUILD_DIR)/test_candidate_recovery_publish_faults \
	test-candidate-recovery-publish-source test-candidate-recovery-publish-counters test-candidate-recovery-publish-reads
	@for c in $(CANDIDATE_PUBLISH_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish $$c || exit 1; done
	@for c in $(CANDIDATE_PUBLISH_DENSE_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_DENSE_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish $$c || exit 1; done
	@for c in $(CANDIDATE_PUBLISH_GATES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-gate-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish_faults $$c || exit 1; done
test-candidate-recovery-publish-source: $(BUILD_DIR)/test_candidate_recovery_publish_source
	@for c in $(CANDIDATE_PUBLISH_SOURCE_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-source-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish_source $$c || exit 1; done
	@for c in $(CANDIDATE_PUBLISH_SOURCE_DENSE_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_DENSE_TIMEOUT) \
		--seed $(FAULT_SEED) --report $(CANDIDATE_PUBLISH_REPORT)-source-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish_source $$c \
		|| exit 1; done
test-candidate-recovery-publish-counters: $(BUILD_DIR)/test_candidate_recovery_publish_counters
	@for c in $(CANDIDATE_PUBLISH_COUNTER_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-counter-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish_counters $$c || exit 1; done
test-candidate-recovery-publish-reads: $(BUILD_DIR)/test_candidate_recovery_publish_reads
	@for c in $(CANDIDATE_PUBLISH_READ_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_PUBLISH_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_PUBLISH_REPORT)-read-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_publish_reads $$c $(CANDIDATE_PUBLISH_READ_PLACE) \
		|| exit 1; done
CANDIDATE_BUFFER_OBJS ?= $(BUILD_DIR)/storage/candidate_buffer.o $(BUILD_DIR)/storage/candidate_buffer_scan.o \
	$(BUILD_DIR)/storage/candidate_buffer_flush.o $(BUILD_DIR)/storage/candidate_buffer_checkpoint.o \
	$(BUILD_DIR)/storage/candidate_buffer_produce.o
CANDIDATE_BUFFER_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_buffer.o $(BUILD_DIR)/tests/test_candidate_buffer_flush.o \
	$(BUILD_DIR)/tests/test_candidate_buffer_checkpoint.o $(BUILD_DIR)/tests/test_candidate_buffer_repair.o \
	$(BUILD_DIR)/tests/test_candidate_buffer_source.o \
	$(BUILD_DIR)/tests/test_candidate_buffer_fixture.o $(BUILD_DIR)/tests/test_candidate_buffer_fixture_pages.o
$(BUILD_DIR)/test_candidate_buffer: $(CANDIDATE_BUFFER_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) $(CANDIDATE_BUFFER_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_BUFFER_TIMEOUT ?= 900
CANDIDATE_BUFFER_REPORT ?= $(BUILD_DIR)/fault/test_candidate_buffer
CANDIDATE_BUFFER_CASES ?= budget pinned lease shared-leaf membership terminal redirty fpi checkpoint-order checkpoint-active \
	close-reentry stale-base append-mutate checkpoint-dangling-version swap-proof swap-control callback-complete-group
.PHONY: test-candidate-buffer
test-candidate-buffer: $(BUILD_DIR)/test_candidate_buffer
	@for c in $(CANDIDATE_BUFFER_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_BUFFER_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_BUFFER_REPORT)-$$c.json -- ./$(BUILD_DIR)/test_candidate_buffer $$c || exit 1; done
.PHONY: test-candidate-outcome-witness
test-candidate-outcome-witness: $(BUILD_DIR)/test_candidate_outcome_witness
	$(TEST_RUNNER) --report $(CANDIDATE_OUTCOME_REPORT) -- ./$(BUILD_DIR)/test_candidate_outcome_witness
$(CANDIDATE_OUTCOME_CATALOG_MODULE): src/storage/candidate_catalog.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
$(CANDIDATE_OUTCOME_MODULE): src/storage/candidate_outcome_witness.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
CANDIDATE_SEMANTIC_MODULE ?= $(BUILD_DIR)/storage/candidate_semantic_seed.o
CANDIDATE_SEMANTIC_OBJS = $(CANDIDATE_SEMANTIC_MODULE) $(CANDIDATE_OUTCOME_OBJS)
CANDIDATE_SEMANTIC_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_semantic_fixture.o $(BUILD_DIR)/tests/test_candidate_semantic_compare.o
$(CANDIDATE_SEMANTIC_MODULE): src/storage/candidate_semantic_seed.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
$(BUILD_DIR)/test_candidate_semantic_seed: $(BUILD_DIR)/tests/test_candidate_semantic_seed.o $(CANDIDATE_SEMANTIC_TEST_OBJS) \
	$(CANDIDATE_SEMANTIC_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/test_candidate_semantic_boundary: $(BUILD_DIR)/tests/test_candidate_semantic_boundary.o $(CANDIDATE_SEMANTIC_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_SEMANTIC_REPORT_PREFIX ?= $(BUILD_DIR)/fault/test_candidate_semantic
.PHONY: test-candidate-semantic
test-candidate-semantic: $(BUILD_DIR)/test_candidate_semantic_seed $(BUILD_DIR)/test_candidate_semantic_boundary
	$(TEST_RUNNER) --report $(CANDIDATE_SEMANTIC_REPORT_PREFIX)-seed-all.json -- ./$(BUILD_DIR)/test_candidate_semantic_seed all
	$(TEST_RUNNER) --report $(CANDIDATE_SEMANTIC_REPORT_PREFIX)-boundary-all.json -- ./$(BUILD_DIR)/test_candidate_semantic_boundary all
# 규칙 prerequisite는 읽는 즉시 펼쳐지므로 CANDIDATE_SEMANTIC_MODULE 정의 뒤에 둔다.
CANDIDATE_VIEW_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_view.o $(BUILD_DIR)/tests/test_candidate_recovery_view_fixture.o
$(BUILD_DIR)/test_candidate_recovery_view: $(CANDIDATE_VIEW_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS) $(CANDIDATE_SEMANTIC_MODULE)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_VIEW_TIMEOUT ?= 600
CANDIDATE_VIEW_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_view
CANDIDATE_VIEW_CASES ?= resolved recovery preview graph nonselected cut access faults durable oom owner inputs metadata observe semantic
.PHONY: test-candidate-recovery-view
test-candidate-recovery-view: $(BUILD_DIR)/test_candidate_recovery_view
	@for c in $(CANDIDATE_VIEW_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_VIEW_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_VIEW_REPORT)-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_view $$c || exit 1; done
CANDIDATE_CLOSURE_OBJS = $(BUILD_DIR)/storage/candidate_recovery_continue.o $(BUILD_DIR)/storage/candidate_recovery_closure.o \
	$(BUILD_DIR)/storage/candidate_wal_witness_prune.o
CANDIDATE_CLOSURE_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_closure.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_dependency.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_fixture.o $(BUILD_DIR)/tests/test_candidate_recovery_view_fixture.o
$(BUILD_DIR)/test_candidate_recovery_closure: $(CANDIDATE_CLOSURE_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_CLOSURE_OBJS) $(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_CLOSURE_TIMEOUT ?= 1200
CANDIDATE_CLOSURE_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_closure
CANDIDATE_CLOSURE_CASES ?= cycles-resolved cycles-preview identity reservation dependency active live prefix segments faults oom
.PHONY: test-candidate-recovery-closure
test-candidate-recovery-closure: $(BUILD_DIR)/test_candidate_recovery_closure
	@for c in $(CANDIDATE_CLOSURE_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_CLOSURE_TIMEOUT) --seed $(FAULT_SEED) \
		--report $(CANDIDATE_CLOSURE_REPORT)-$$c.json -- ./$(BUILD_DIR)/test_candidate_recovery_closure $$c || exit 1; done
CANDIDATE_CLOSURE_BOUNDARY_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_closure_boundary.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_boundary_fixture.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_fixture.o $(BUILD_DIR)/tests/test_candidate_recovery_view_fixture.o
$(BUILD_DIR)/test_candidate_recovery_closure_boundary: $(CANDIDATE_CLOSURE_BOUNDARY_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_CLOSURE_OBJS) $(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_CLOSURE_BOUNDARY_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_closure_boundary
CANDIDATE_CLOSURE_BOUNDARY_CASES ?= healthy-pair foreign-empty foreign-suffix unpublished-abort expired-abort slot-reuse
.PHONY: test-candidate-recovery-closure-boundary
test-candidate-recovery-closure-boundary: $(BUILD_DIR)/test_candidate_recovery_closure_boundary
	@for c in $(CANDIDATE_CLOSURE_BOUNDARY_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_CLOSURE_TIMEOUT) \
		--seed $(FAULT_SEED) --report $(CANDIDATE_CLOSURE_BOUNDARY_REPORT)-$$c.json -- \
		./$(BUILD_DIR)/test_candidate_recovery_closure_boundary $$c || exit 1; done
CANDIDATE_CLOSURE_TRANSPORT_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_closure_transport.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_transport_fixture.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_dependency.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_fixture.o $(BUILD_DIR)/tests/test_candidate_recovery_view_fixture.o
$(BUILD_DIR)/test_candidate_recovery_closure_transport: $(CANDIDATE_CLOSURE_TRANSPORT_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_CLOSURE_OBJS) $(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_CLOSURE_TRANSPORT_TIMEOUT ?= 1200
CANDIDATE_CLOSURE_TRANSPORT_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_closure_transport
CANDIDATE_CLOSURE_TRANSPORT_CASES ?= healthy short-padding padding header frame io reentry captured images
.PHONY: test-candidate-recovery-closure-transport
test-candidate-recovery-closure-transport: $(BUILD_DIR)/test_candidate_recovery_closure_transport
	@for c in $(CANDIDATE_CLOSURE_TRANSPORT_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_CLOSURE_TRANSPORT_TIMEOUT) \
		--seed $(FAULT_SEED) --report $(CANDIDATE_CLOSURE_TRANSPORT_REPORT)-$$c.json -- \
		./$(BUILD_DIR)/test_candidate_recovery_closure_transport $$c || exit 1; done
CANDIDATE_CLOSURE_CALLBACKS_TEST_OBJS ?= $(BUILD_DIR)/tests/test_candidate_recovery_closure_callbacks.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_transport_fixture.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_dependency.o \
	$(BUILD_DIR)/tests/test_candidate_recovery_closure_fixture.o $(BUILD_DIR)/tests/test_candidate_recovery_view_fixture.o
$(BUILD_DIR)/test_candidate_recovery_closure_callbacks: $(CANDIDATE_CLOSURE_CALLBACKS_TEST_OBJS) $(CANDIDATE_PUBLISH_TEST_OBJS) \
	$(CANDIDATE_CLOSURE_OBJS) $(CANDIDATE_RECOVERY_PUBLISH_OBJS) $(CANDIDATE_RECOVERY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_CLOSURE_CALLBACKS_REPORT ?= $(BUILD_DIR)/fault/test_candidate_recovery_closure_callbacks
CANDIDATE_CLOSURE_CALLBACKS_CASES ?= read-healthy visit-healthy visit-stop scan-healthy scan-stop read-reject read-reject-pinned \
	read-move visit-reject-stop visit-move-stop scan-reject scan-reject-stop scan-move-stop first-owner-true first-owner-false
.PHONY: test-candidate-recovery-closure-callbacks
test-candidate-recovery-closure-callbacks: $(BUILD_DIR)/test_candidate_recovery_closure_callbacks
	@for c in $(CANDIDATE_CLOSURE_CALLBACKS_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_CLOSURE_TRANSPORT_TIMEOUT) \
		--seed $(FAULT_SEED) --report $(CANDIDATE_CLOSURE_CALLBACKS_REPORT)-$$c.json -- \
		./$(BUILD_DIR)/test_candidate_recovery_closure_callbacks $$c || exit 1; done
CANDIDATE_WAL_READ_BOUNDARY_OBJS ?= $(CANDIDATE_WAL_OBJS)
$(BUILD_DIR)/test_candidate_wal_read_boundary: $(BUILD_DIR)/tests/test_candidate_wal_read_boundary.o \
	$(CANDIDATE_WAL_READ_BOUNDARY_OBJS) $(BUILD_DIR)/storage/checksum.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_WAL_READ_BOUNDARY_TIMEOUT ?= 1200
CANDIDATE_WAL_READ_BOUNDARY_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_read_boundary
CANDIDATE_WAL_READ_BOUNDARY_CASES ?= unpinned pinned
.PHONY: test-candidate-wal-read-boundary
test-candidate-wal-read-boundary: $(BUILD_DIR)/test_candidate_wal_read_boundary
	@for c in $(CANDIDATE_WAL_READ_BOUNDARY_CASES); do $(PYTHON) tests/fault_runner.py --timeout $(CANDIDATE_WAL_READ_BOUNDARY_TIMEOUT) \
		--seed $(FAULT_SEED) --report $(CANDIDATE_WAL_READ_BOUNDARY_REPORT)-$$c.json -- \
		./$(BUILD_DIR)/test_candidate_wal_read_boundary $$c || exit 1; done
$(BUILD_DIR)/test_candidate_wal_identity: $(BUILD_DIR)/tests/test_candidate_wal_identity.o $(BUILD_DIR)/storage/candidate_wal_identity.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
CANDIDATE_IDENTITY_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_identity.json
.PHONY: test-candidate-wal-identity
test-candidate-wal-identity: $(BUILD_DIR)/test_candidate_wal_identity
	$(TEST_RUNNER) --report $(CANDIDATE_IDENTITY_REPORT) -- ./$(BUILD_DIR)/test_candidate_wal_identity
$(BUILD_DIR)/test_candidate_redo: $(BUILD_DIR)/tests/test_candidate_redo.o $(CANDIDATE_REDO_OBJS) $(BUILD_DIR)/storage/candidate_allocation.o
CANDIDATE_REDO_REPORT ?= $(BUILD_DIR)/fault/test_candidate_redo.json
.PHONY: test-candidate-redo
test-candidate-redo: $(BUILD_DIR)/test_candidate_redo
	$(TEST_RUNNER) --report $(CANDIDATE_REDO_REPORT) -- ./$(BUILD_DIR)/test_candidate_redo
.PHONY: test-candidate-wal-checkpoint
test-candidate-wal-checkpoint: $(BUILD_DIR)/test_candidate_wal_checkpoint
	$(TEST_RUNNER) --report $(CANDIDATE_WAL_CHECKPOINT_REPORT) -- ./$(BUILD_DIR)/test_candidate_wal_checkpoint $(CANDIDATE_WAL_CHECKPOINT_ARGS)
$(CANDIDATE_WITNESS_MODULE): src/storage/candidate_wal_witness.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
$(CANDIDATE_WITNESS_CHECKPOINT_MODULE): src/storage/candidate_wal_checkpoint.c $(BUILD_FINGERPRINT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
$(BUILD_DIR)/test_candidate_wal_witness: $(BUILD_DIR)/tests/test_candidate_wal_witness.o $(CANDIDATE_WITNESS_OBJS) $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o
$(BUILD_DIR)/test_candidate_wal_system: $(BUILD_DIR)/tests/test_candidate_wal_system.o $(CANDIDATE_WITNESS_OBJS) $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_SYSTEM_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_system.json
.PHONY: test-candidate-wal-system
test-candidate-wal-system: $(BUILD_DIR)/test_candidate_wal_system
	$(TEST_RUNNER) --report $(CANDIDATE_SYSTEM_REPORT) -- ./$(BUILD_DIR)/test_candidate_wal_system
$(BUILD_DIR)/test_candidate_wal_files: $(BUILD_DIR)/tests/test_candidate_wal_files.o $(CANDIDATE_WAL_OBJS) $(BUILD_DIR)/storage/checksum.o

CANDIDATE_WAL_TEST_TARGETS = $(addprefix test-candidate-wal-,0 1 2 3 4 5 6 7 8 9 10 11)
CANDIDATE_WAL_REPORT_PREFIX ?= $(BUILD_DIR)/fault/test_candidate_wal
.PHONY: test-candidate-wal $(CANDIDATE_WAL_TEST_TARGETS)
test-candidate-wal: $(CANDIDATE_WAL_TEST_TARGETS)

$(CANDIDATE_WAL_TEST_TARGETS): test-candidate-wal-%: $(BUILD_DIR)/test_candidate_wal
	$(TEST_RUNNER) --report $(CANDIDATE_WAL_REPORT_PREFIX)-$*.json -- ./$(BUILD_DIR)/test_candidate_wal "" $*

.PHONY: test-candidate-wal-witness test-candidate-wal-files
CANDIDATE_WITNESS_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_witness.json
test-candidate-wal-witness: $(BUILD_DIR)/test_candidate_wal_witness
	$(TEST_RUNNER) --report $(CANDIDATE_WITNESS_REPORT) -- ./$(BUILD_DIR)/test_candidate_wal_witness

CANDIDATE_WAL_FILES_REPORT ?= $(BUILD_DIR)/fault/test_candidate_wal_files.json
test-candidate-wal-files: $(BUILD_DIR)/test_candidate_wal_files
	$(TEST_RUNNER) --report $(CANDIDATE_WAL_FILES_REPORT) -- ./$(BUILD_DIR)/test_candidate_wal_files

CANDIDATE_PAGE_OBJS = $(BUILD_DIR)/storage/candidate_page.o $(BUILD_DIR)/storage/candidate_result.o $(BUILD_DIR)/storage/candidate_digest.o
$(BUILD_DIR)/test_candidate_page: $(BUILD_DIR)/tests/test_candidate_page.o $(CANDIDATE_PAGE_OBJS) $(BUILD_DIR)/storage/checksum.o $(BUILD_DIR)/storage/record.o
$(BUILD_DIR)/test_candidate_result_wal: $(BUILD_DIR)/tests/test_candidate_result_wal.o $(CANDIDATE_PAGE_OBJS) $(BUILD_DIR)/storage/candidate_catalog.o $(CANDIDATE_WAL_OBJS) $(CANDIDATE_WITNESS_OBJS) $(BUILD_DIR)/storage/checksum.o
$(BUILD_DIR)/test_candidate_catalog: $(BUILD_DIR)/tests/test_candidate_catalog.o $(BUILD_DIR)/storage/candidate_catalog.o $(CANDIDATE_PAGE_OBJS) $(BUILD_DIR)/storage/checksum.o
CANDIDATE_CATALOG_REPORT ?= $(BUILD_DIR)/fault/test_candidate_catalog.json
.PHONY: test-candidate-catalog
test-candidate-catalog: $(BUILD_DIR)/test_candidate_catalog
	$(TEST_RUNNER) --report $(CANDIDATE_CATALOG_REPORT) -- ./$(BUILD_DIR)/test_candidate_catalog
$(BUILD_DIR)/test_candidate_allocation: $(BUILD_DIR)/tests/test_candidate_allocation.o $(BUILD_DIR)/storage/candidate_allocation.o $(BUILD_DIR)/storage/candidate_allocation_format.o $(BUILD_DIR)/storage/candidate_page.o $(BUILD_DIR)/storage/checksum.o
CANDIDATE_ALLOCATION_REPORT ?= $(BUILD_DIR)/fault/test_candidate_allocation.json
.PHONY: test-candidate-allocation
test-candidate-allocation: $(BUILD_DIR)/test_candidate_allocation
	$(TEST_RUNNER) --report $(CANDIDATE_ALLOCATION_REPORT) -- ./$(BUILD_DIR)/test_candidate_allocation
CANDIDATE_PAGE_REPORT_PREFIX ?= $(BUILD_DIR)/fault/test_candidate_page
.PHONY: test-candidate-pages
test-candidate-pages: $(BUILD_DIR)/test_candidate_page $(BUILD_DIR)/test_candidate_result_wal
	$(TEST_RUNNER) --report $(CANDIDATE_PAGE_REPORT_PREFIX)-codec.json -- ./$(BUILD_DIR)/test_candidate_page
	$(TEST_RUNNER) --report $(CANDIDATE_PAGE_REPORT_PREFIX)-wal.json -- ./$(BUILD_DIR)/test_candidate_result_wal

$(BUILD_DIR)/test_record: $(BUILD_DIR)/tests/test_record.o $(BUILD_DIR)/storage/record.o $(BUILD_DIR)/storage/record_page.o
$(BUILD_DIR)/test_overflow: $(BUILD_DIR)/tests/test_overflow.o $(BUILD_DIR)/storage/overflow.o $(BUILD_DIR)/storage/checksum.o $(BUILD_DIR)/storage/record_page.o
$(BUILD_DIR)/test_overflow_file: $(BUILD_DIR)/tests/test_overflow_file.o $(BUILD_DIR)/storage/overflow.o $(BUILD_DIR)/storage/checksum.o
$(BUILD_DIR)/test_value: $(BUILD_DIR)/tests/test_value.o $(BUILD_DIR)/storage/value.o
$(BUILD_DIR)/storage/value_varchar_hook.o: src/storage/value.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=value_test_malloc -c -o $@ $<

$(BUILD_DIR)/test_value_varchar: $(BUILD_DIR)/tests/test_value_varchar.o $(BUILD_DIR)/storage/value_varchar_hook.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_value_sqlite: $(BUILD_DIR)/tests/test_value_sqlite.o $(BUILD_DIR)/storage/value.o
$(BUILD_DIR)/test_value_sqlite: LDLIBS += -lsqlite3
$(BUILD_DIR)/test_record_fault: $(BUILD_DIR)/tests/test_record_fault.o $(BUILD_DIR)/storage/record_fault.o $(BUILD_DIR)/storage/record_page_fault.o
$(BUILD_DIR)/test_overflow_fault: $(BUILD_DIR)/tests/test_overflow_fault.o $(BUILD_DIR)/storage/overflow_fault.o $(BUILD_DIR)/storage/checksum.o
$(BUILD_DIR)/test_value_fault: $(BUILD_DIR)/tests/test_value_fault.o $(BUILD_DIR)/storage/value_fault.o
$(BUILD_DIR)/test_catalog: $(BUILD_DIR)/tests/test_catalog.o $(BUILD_DIR)/storage/catalog.o
$(BUILD_DIR)/test_catalog_fault: $(BUILD_DIR)/tests/test_catalog_fault.o $(BUILD_DIR)/storage/catalog_fault.o
$(BUILD_DIR)/test_lexer: $(BUILD_DIR)/tests/test_lexer.o $(BUILD_DIR)/sql/lexer.o
$(BUILD_DIR)/test_lexer_fault: $(BUILD_DIR)/tests/test_lexer_fault.o $(BUILD_DIR)/sql/lexer_fault.o
$(BUILD_DIR)/test_page_file: $(BUILD_DIR)/tests/test_page_file.o $(STORAGE_OBJS)
$(BUILD_DIR)/test_catalog_store: $(BUILD_DIR)/tests/test_catalog_store.o $(STORAGE_OBJS)
$(BUILD_DIR)/test_pk_tree: $(BUILD_DIR)/tests/test_pk_tree.o $(BUILD_DIR)/storage/pk_tree.o
$(BUILD_DIR)/test_text_tree: $(BUILD_DIR)/tests/test_text_tree.o $(BUILD_DIR)/storage/text_tree.o

$(BUILD_DIR)/test_result: $(BUILD_DIR)/tests/test_result.o $(BUILD_DIR)/sql/result.o $(BUILD_DIR)/storage/value.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_result_fault: $(BUILD_DIR)/tests/test_result_fault.o $(BUILD_DIR)/sql/result_fault.o $(BUILD_DIR)/storage/value.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/sql/result_fault.o: src/sql/result.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=result_test_malloc -Dcalloc=result_test_calloc -Drealloc=result_test_realloc -Dfree=result_test_free -c -o $@ $<

test-result: $(BUILD_DIR)/test_result $(BUILD_DIR)/test_result_fault
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_result.json -- ./$(BUILD_DIR)/test_result
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_result_fault.json -- ./$(BUILD_DIR)/test_result_fault

$(TEST_BINS) $(BUILD_DIR)/test_overflow_file $(BUILD_DIR)/test_value_sqlite $(BUILD_DIR)/test_candidate_wal $(BUILD_DIR)/test_candidate_wal_witness $(BUILD_DIR)/test_candidate_wal_files:
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_candidate_page $(BUILD_DIR)/test_candidate_result_wal $(BUILD_DIR)/test_candidate_catalog $(BUILD_DIR)/test_candidate_control $(BUILD_DIR)/test_candidate_allocation $(BUILD_DIR)/test_candidate_wal_system $(BUILD_DIR)/test_candidate_checkpoint $(BUILD_DIR)/test_candidate_wal_checkpoint $(BUILD_DIR)/test_candidate_redo:
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/storage/record_fault.o: src/storage/record.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=record_test_malloc -c -o $@ $<

$(BUILD_DIR)/storage/record_page_fault.o: src/storage/record_page.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dcalloc=record_test_calloc -c -o $@ $<

$(BUILD_DIR)/storage/overflow_fault.o: src/storage/overflow.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=overflow_test_malloc -c -o $@ $<

$(BUILD_DIR)/storage/value_fault.o: src/storage/value.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=value_test_malloc -c -o $@ $<

$(BUILD_DIR)/storage/catalog_fault.o: src/storage/catalog.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=catalog_test_malloc -Drealloc=catalog_test_realloc -Dfree=catalog_test_free -c -o $@ $<

$(BUILD_DIR)/sql/lexer_fault.o: src/sql/lexer.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=lexer_test_malloc -Dfree=lexer_test_free -c -o $@ $<

test: $(TEST_BINS)
	@set -e; for binary in $(TEST_BINS); do \
		$(TEST_RUNNER) --report "$(BUILD_DIR)/fault/$${binary##*/}.json" -- "$$binary"; \
	done

test-overflow-file: $(BUILD_DIR)/test_overflow_file
	@directory=$$(mktemp -d "$(BUILD_DIR)/overflow.XXXXXX") && \
	trap 'rmdir "$$directory" 2>/dev/null || true' EXIT HUP INT TERM && \
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_overflow_file.json -- ./$(BUILD_DIR)/test_overflow_file "$$directory/roundtrip.db"

test-value-sqlite: $(BUILD_DIR)/test_value_sqlite
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_value_sqlite.json -- ./$(BUILD_DIR)/test_value_sqlite

test-value-varchar: $(BUILD_DIR)/test_value_varchar
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_value_varchar.json -- ./$(BUILD_DIR)/test_value_varchar

test-runner:
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_runner.json -- $(PYTEST) -q tests/test_fault_runner.py tests/test_fault_oracle.py --basetemp=$(BUILD_DIR)/runner-tests -p no:cacheprovider

$(BUILD_DIR)/test_fault_runtime: $(BUILD_DIR)/tests/test_fault_runtime.o $(BUILD_DIR)/tests/fault_runtime.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/fault_fixture: $(BUILD_DIR)/tests/fault_fixture.o $(BUILD_DIR)/tests/fault_runtime.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-fault: $(BUILD_DIR)/test_fault_runtime $(BUILD_DIR)/fault_fixture
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_fault_runtime.json -- ./$(BUILD_DIR)/test_fault_runtime $(BUILD_DIR)/fault/runtime.trace
	FAULT_FIXTURE=$(abspath $(BUILD_DIR)/fault_fixture) $(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_fault_fixture.json -- $(PYTEST) -q tests/test_fault_fixture.py --basetemp=$(BUILD_DIR)/fault/fixture-tests -p no:cacheprovider

# SQL 실행기(#198). 파일마다 한 역할이며 공유 타입은 src/sql/database_exec.h에 있다
DATABASE_EXEC_NAMES = database database_prepare database_cache database_workspace sql_statement_clone database_expr database_subquery database_ddl database_insert database_select \
                      database_group database_join database_update database_check
DATABASE_EXEC_OBJS = $(addprefix $(BUILD_DIR)/sql/,$(addsuffix .o,$(DATABASE_EXEC_NAMES)))
DATABASE_EXEC_FAULT_OBJS = $(addprefix $(BUILD_DIR)/sql/,$(addsuffix _fault.o,$(DATABASE_EXEC_NAMES)))
DATABASE_OBJS = $(STORAGE_OBJS) $(BUILD_DIR)/storage/heap_store.o $(BUILD_DIR)/storage/transaction.o \
                $(DATABASE_EXEC_OBJS) $(BUILD_DIR)/sql/database_txn.o \
                $(BUILD_DIR)/sql/sql_statement.o $(BUILD_DIR)/sql/sql_eval.o $(BUILD_DIR)/sql/lexer.o $(BUILD_DIR)/sql/result.o
DATABASE_TESTS = test_sql_statement test_heap_store test_database test_database_sqlite test_database_fault test_database_bounded_open test_primary_index
DATABASE_BINS = $(addprefix $(BUILD_DIR)/,$(DATABASE_TESTS))
DATABASE_AUDIT_OBJS = $(patsubst $(BUILD_DIR)/%.o,$(BUILD_DIR)/audit/%.o,$(DATABASE_OBJS))

$(BUILD_DIR)/test_sql_statement: $(BUILD_DIR)/tests/test_sql_statement.o $(BUILD_DIR)/sql/sql_statement_fault.o $(BUILD_DIR)/sql/lexer.o $(BUILD_DIR)/storage/value_statement_fault.o
$(BUILD_DIR)/test_heap_store: $(BUILD_DIR)/tests/test_heap_store.o $(STORAGE_OBJS) $(BUILD_DIR)/storage/heap_store_test.o
$(BUILD_DIR)/test_database: $(BUILD_DIR)/tests/test_database.o $(DATABASE_OBJS)
$(BUILD_DIR)/bench_secondary_index: $(BUILD_DIR)/tests/bench_secondary_index.o $(DATABASE_OBJS)
$(BUILD_DIR)/bench_secondary_index: LDLIBS += -lsqlite3
$(BUILD_DIR)/bench_secondary_index:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/bench_primary_range: $(BUILD_DIR)/tests/bench_primary_range.o $(DATABASE_OBJS)
$(BUILD_DIR)/bench_primary_range: LDLIBS += -lsqlite3
$(BUILD_DIR)/bench_primary_range:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/bench_free_space: $(BUILD_DIR)/tests/bench_free_space.o $(DATABASE_OBJS)
$(BUILD_DIR)/bench_free_space: LDLIBS += -lsqlite3
$(BUILD_DIR)/bench_free_space:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD_DIR)/test_database_sqlite: $(BUILD_DIR)/tests/test_database_sqlite.o $(DATABASE_OBJS)
$(BUILD_DIR)/test_primary_index: $(BUILD_DIR)/tests/test_primary_index.o $(DATABASE_OBJS)
$(BUILD_DIR)/test_database_fault: $(BUILD_DIR)/tests/test_database_fault.o $(filter-out $(DATABASE_EXEC_OBJS) $(BUILD_DIR)/storage/heap_store.o $(BUILD_DIR)/sql/sql_statement.o $(BUILD_DIR)/storage/value.o,$(DATABASE_OBJS)) $(DATABASE_EXEC_FAULT_OBJS) $(BUILD_DIR)/storage/heap_store_fault.o $(BUILD_DIR)/sql/database_statement_fault.o $(BUILD_DIR)/storage/value_database_fault.o
$(BUILD_DIR)/test_database_bounded_open: $(BUILD_DIR)/tests/test_database_bounded_open.o $(BUILD_DIR)/tests/alloc_audit.o $(DATABASE_AUDIT_OBJS)
$(BUILD_DIR)/test_database_cache: $(BUILD_DIR)/tests/test_database_cache.o $(BUILD_DIR)/tests/alloc_audit.o $(DATABASE_AUDIT_OBJS)
$(BUILD_DIR)/test_database_cache: LDLIBS += -lsqlite3
$(BUILD_DIR)/test_database_cache:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_database_workspace: $(BUILD_DIR)/tests/test_database_workspace.o $(BUILD_DIR)/tests/alloc_audit.o $(DATABASE_AUDIT_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) -lsqlite3

test-database-workspace: $(BUILD_DIR)/test_database_workspace
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_database_workspace.json -- ./$(BUILD_DIR)/test_database_workspace

.PHONY: test-database-workspace

test-database-cache: $(BUILD_DIR)/test_database_cache
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_database_cache.json -- ./$(BUILD_DIR)/test_database_cache

.PHONY: test-database-cache
$(DATABASE_BINS):
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/audit/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=audit_malloc -Dcalloc=audit_calloc -Drealloc=audit_realloc -Dfree=audit_free -c -o $@ $<

$(BUILD_DIR)/sql/sql_statement_fault.o: src/sql/sql_statement.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=sqlstmt_test_malloc -Dcalloc=sqlstmt_test_calloc -Dfree=sqlstmt_test_free -c -o $@ $<

$(BUILD_DIR)/storage/value_statement_fault.o: src/storage/value.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=sqlstmt_test_malloc -Dcalloc=sqlstmt_test_calloc -Dfree=sqlstmt_test_free -c -o $@ $<

$(BUILD_DIR)/sql/database_statement_fault.o: src/sql/sql_statement.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=db_test_malloc -Dcalloc=db_test_calloc -Dfree=db_test_free -c -o $@ $<

$(BUILD_DIR)/storage/value_database_fault.o: src/storage/value.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=db_test_malloc -Dcalloc=db_test_calloc -Dfree=db_test_free -c -o $@ $<

$(BUILD_DIR)/storage/heap_store_test.o: src/storage/heap_store.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=hs_test_malloc -Dcalloc=hs_test_calloc -Drealloc=hs_test_realloc -c -o $@ $<

$(DATABASE_EXEC_FAULT_OBJS): $(BUILD_DIR)/sql/%_fault.o: src/sql/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=db_test_malloc -Dcalloc=db_test_calloc -Drealloc=db_test_realloc -Dfree=db_test_free -c -o $@ $<

$(BUILD_DIR)/storage/heap_store_fault.o: src/storage/heap_store.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=db_test_malloc -Dcalloc=db_test_calloc -Drealloc=db_test_realloc -Dfree=db_test_free -c -o $@ $<

test-database: $(DATABASE_BINS)
	@set -e; for binary in $(DATABASE_BINS); do \
		$(TEST_RUNNER) --report "$(BUILD_DIR)/fault/$${binary##*/}.json" -- "$$binary"; \
	done

$(BUILD_DIR)/sql/session_test.o: src/sql/session.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dcalloc=st_calloc -Dfree=st_free -c -o $@ $<

$(BUILD_DIR)/audit/sql/session_test.o: src/sql/session.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Dmalloc=audit_malloc -Dcalloc=audit_calloc -Drealloc=audit_realloc -Dfree=audit_free -DSESSION_TEST_HOOK -c -o $@ $<

$(BUILD_DIR)/test_session: $(BUILD_DIR)/tests/test_session.o $(BUILD_DIR)/sql/session_test.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_session_audit: $(BUILD_DIR)/tests/test_session_audit.o $(BUILD_DIR)/tests/alloc_audit.o $(BUILD_DIR)/audit/sql/session_test.o $(DATABASE_AUDIT_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_result_ledger: $(BUILD_DIR)/tests/test_result_ledger.o $(BUILD_DIR)/tests/alloc_audit.o $(BUILD_DIR)/audit/sql/result.o $(BUILD_DIR)/storage/value.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_heap_ledger: $(BUILD_DIR)/tests/test_heap_ledger.o $(BUILD_DIR)/tests/alloc_audit.o $(BUILD_DIR)/audit/storage/heap_store.o $(BUILD_DIR)/audit/storage/record.o $(filter-out $(BUILD_DIR)/storage/record.o,$(STORAGE_OBJS))
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-session: $(BUILD_DIR)/test_session $(BUILD_DIR)/test_session_audit $(BUILD_DIR)/test_result_ledger $(BUILD_DIR)/test_heap_ledger
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_session.json -- ./$(BUILD_DIR)/test_session
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_session_audit.json -- ./$(BUILD_DIR)/test_session_audit
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_result_ledger.json -- ./$(BUILD_DIR)/test_result_ledger
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_heap_ledger.json -- ./$(BUILD_DIR)/test_heap_ledger

$(BUILD_DIR)/test_transaction: $(BUILD_DIR)/tests/test_transaction.o $(BUILD_DIR)/storage/transaction.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_transaction_fault: $(BUILD_DIR)/tests/test_transaction_fault.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_database_txn: $(BUILD_DIR)/tests/test_database_txn.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_session_txn: $(BUILD_DIR)/tests/test_session_txn.o $(BUILD_DIR)/sql/session.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-transaction: $(BUILD_DIR)/test_transaction $(BUILD_DIR)/test_transaction_fault $(BUILD_DIR)/test_database_txn $(BUILD_DIR)/test_session_txn
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_transaction.json -- ./$(BUILD_DIR)/test_transaction
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_transaction_fault.json -- ./$(BUILD_DIR)/test_transaction_fault
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_database_txn.json -- ./$(BUILD_DIR)/test_database_txn
	$(TEST_RUNNER) --report $(BUILD_DIR)/fault/test_session_txn.json -- ./$(BUILD_DIR)/test_session_txn

$(BUILD_DIR)/session_probe: $(BUILD_DIR)/tests/session_probe.o $(BUILD_DIR)/sql/session.o $(DATABASE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# SQLite 3.51.0과 같은 입력으로 두 연결을 비교한다. 시스템 SQLite 버전과 상관없이 기준 버전 소스를 받아
# (sha256 확인) 공유 라이브러리로 빌드하고 파이썬 sqlite3 모듈이 그것을 쓰게 한다. 네트워크가 필요해 test-all에 넣지 않는다
SQLITE_DIFF_DIR ?= $(BUILD_DIR)/sqlite-session-diff
SQLITE_REF_VERSION := 3510000
SQLITE_REF_URL := https://www.sqlite.org/2025/sqlite-amalgamation-$(SQLITE_REF_VERSION).zip
SQLITE_REF_SHA256 := 1caf7116f2910600d04473ad69d37ec538fa62fa36adccd37b5e0e43647c98be
SQLITE_REF_DIR := $(BUILD_DIR)/sqlite-ref

$(SQLITE_REF_DIR)/libsqlite3.so.0:
	mkdir -p $(SQLITE_REF_DIR)
	curl -sSfL -o $(SQLITE_REF_DIR)/src.zip $(SQLITE_REF_URL)
	echo "$(SQLITE_REF_SHA256)  $(SQLITE_REF_DIR)/src.zip" | sha256sum -c --quiet -
	unzip -qo -d $(SQLITE_REF_DIR) $(SQLITE_REF_DIR)/src.zip
	gcc -O2 -fPIC -shared -Wl,-soname,libsqlite3.so.0 -o $@ \
		$(SQLITE_REF_DIR)/sqlite-amalgamation-$(SQLITE_REF_VERSION)/sqlite3.c -lm -lpthread

test-sqlite-session-diff-harness:
	$(PYTEST) -q tests/test_sqlite_session_diff.py --basetemp=$(BUILD_DIR)/sqlite-session-diff-harness -p no:cacheprovider

test-sqlite-session-diff: $(BUILD_DIR)/session_probe test-sqlite-session-diff-harness $(SQLITE_REF_DIR)/libsqlite3.so.0
	LD_LIBRARY_PATH=$(abspath $(SQLITE_REF_DIR)) $(PYTHON) tests/sqlite_session_diff.py \
		--probe ./$(BUILD_DIR)/session_probe --workdir $(SQLITE_DIFF_DIR)

test-all: test test-overflow-file test-value-sqlite test-value-varchar test-database test-database-cache test-database-workspace test-transaction test-session test-ack-queue test-ack-queue-python

clean:
	rm -rf $(BUILD_DIR)

.PHONY: test-runner test-fault test-result test-overflow-file test-value-sqlite test-value-varchar test-sqlite-session-diff test-sqlite-session-diff-harness

-include $(STORAGE_OBJS:.o=.d)
-include $(CANDIDATE_WAL_OBJS:.o=.d)
-include $(CANDIDATE_PAGE_OBJS:.o=.d)
-include $(CANDIDATE_CONTROL_OBJS:.o=.d)
-include $(BUILD_DIR)/storage/candidate_catalog.d
-include $(BUILD_DIR)/storage/candidate_allocation.d
-include $(BUILD_DIR)/storage/candidate_allocation_format.d
-include $(CANDIDATE_WITNESS_MODULE:.o=.d)
-include $(CANDIDATE_OUTCOME_CATALOG_MODULE:.o=.d)
-include $(CANDIDATE_OUTCOME_MODULE:.o=.d)
-include $(CANDIDATE_SEMANTIC_MODULE:.o=.d)
-include $(CANDIDATE_WAL_TAIL_MODULE:.o=.d)
-include $(BUILD_DIR)/storage/candidate_wal_system.d
-include $(BUILD_DIR)/storage/candidate_checkpoint.d
-include $(BUILD_DIR)/storage/candidate_checkpoint_format.d
-include $(BUILD_DIR)/storage/candidate_wal_checkpoint.d
-include $(BUILD_DIR)/storage/candidate_redo.d
-include $(BUILD_DIR)/storage/candidate_outcome_witness.d
-include $(BUILD_DIR)/storage/candidate_recovery.d $(BUILD_DIR)/storage/candidate_recovery_state.d
-include $(BUILD_DIR)/storage/candidate_recovery_terminal.d
-include $(CANDIDATE_RECOVERY_PUBLISH_OBJS:.o=.d)
-include $(CANDIDATE_CLOSURE_OBJS:.o=.d) $(BUILD_DIR)/storage/candidate_wal_identity.d
-include $(wildcard $(BUILD_DIR)/tests/*.d $(BUILD_DIR)/storage/*_fault.d $(BUILD_DIR)/sql/lexer*.d)
-include $(BUILD_DIR)/sql/result.d $(BUILD_DIR)/sql/result_fault.d
-include $(DATABASE_OBJS:.o=.d) $(DATABASE_EXEC_FAULT_OBJS:.o=.d) $(BUILD_DIR)/sql/sql_statement_fault.d $(BUILD_DIR)/storage/heap_store_test.d
-include $(DATABASE_AUDIT_OBJS:.o=.d)
-include $(BUILD_DIR)/sql/database_statement_fault.d

-include $(BUILD_DIR)/storage/transaction.d

.PHONY: test-database test-transaction test-session

.PHONY: all test test-all clean

# 선택형 ACK 큐(#140). 큐 파일이 없으면 무시한다.
-include src/queue/queue.mk

# 기본 대상: DB와 큐를 묶은 공유 라이브러리(파이썬 바인딩이 쓴다)
all: $(TOTEDB_SHARED_LIB)

$(shell find $(BUILD_DIR) -name '*.o' 2>/dev/null): $(BUILD_FINGERPRINT)

.PHONY: test-benchmark
test-benchmark:
	$(PYTHON) -m unittest discover -s tests -p test_benchmark_build.py
	$(MAKE) BENCHMARK=1 BUILD_DIR=build-benchmark build-benchmark/test_benchmark
	./build-benchmark/test_benchmark
	$(MAKE) BENCHMARK=1 BUILD_DIR=build-benchmark build-benchmark/test_totedb
	./build-benchmark/test_totedb

$(BUILD_DIR)/benchmark_test.o: src/benchmark.c src/benchmark.h $(BUILD_FINGERPRINT)
	$(CC) $(CFLAGS) -Dclock_gettime=tb_test_clock -c -o $@ $<

$(BUILD_DIR)/test_benchmark: $(BUILD_DIR)/tests/test_benchmark.o $(BUILD_DIR)/benchmark_test.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: counter-driver
counter-driver: $(BUILD_DIR)/counter_driver

.PHONY: common-cost-driver
common-cost-driver: $(BUILD_DIR)/common_cost_driver

.PHONY: pending-query-driver
pending-query-driver: $(BUILD_DIR)/pending_query_driver

.PHONY: pending-query-fault-driver
pending-query-fault-driver: $(BUILD_DIR)/test_pending_query_native

$(BUILD_DIR)/pending_query_session.o: src/sql/session.c $(BUILD_FINGERPRINT)
	$(CC) $(CFLAGS) $(DEPFLAGS) -DSESSION_TEST_HOOK -c -o $@ $<

$(BUILD_DIR)/test_pending_query_native: $(BUILD_DIR)/tests/test_pending_query_native.o $(BUILD_DIR)/queue/ack_queue_probe.o $(BUILD_DIR)/pending_query_session.o $(DATABASE_OBJS) $(BUILD_DIR)/queue/ack_queue_sql.o $(BUILD_DIR)/queue/ack_queue_conditional.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(BUILD_DIR)/pending_query_session.d $(BUILD_DIR)/tests/test_pending_query_native.d

$(BUILD_DIR)/pending_query_driver: bench/pending_query_driver.c include/queue/ack_queue.h include/sql/session.h $(TOTEDB_SHARED_LIB) $(BUILD_FINGERPRINT)
	$(CC) $(SHARED_CFLAGS) -o $@ $< -L$(dir $(TOTEDB_SHARED_LIB)) -ltotedb -Wl,-rpath,$(abspath $(dir $(TOTEDB_SHARED_LIB)))

$(BUILD_DIR)/common_cost_driver: bench/common_cost_driver.c src/benchmark.h src/sql/database_cache.h src/sql/database_workspace.h $(TOTEDB_SHARED_LIB) $(BUILD_FINGERPRINT)
	$(CC) $(SHARED_CFLAGS) -o $@ $< -L$(dir $(TOTEDB_SHARED_LIB)) -ltotedb -Wl,-rpath,$(abspath $(dir $(TOTEDB_SHARED_LIB)))

$(BUILD_DIR)/counter_driver: bench/counter_driver.c src/benchmark.h include/totedb.h $(TOTEDB_SHARED_LIB) $(BUILD_FINGERPRINT)
	$(CC) $(SHARED_CFLAGS) -o $@ $< -L$(dir $(TOTEDB_SHARED_LIB)) -ltotedb -lsqlite3 -Wl,-rpath,$(abspath $(dir $(TOTEDB_SHARED_LIB)))

.PHONY: ycsb-build ycsb-test
ycsb-build:
	$(PYTHON) bench/ycsb/run.py build

ycsb-test:
	$(PYTHON) bench/ycsb/run.py test

.PHONY: queue-contention-test
queue-contention-test: $(TOTEDB_SHARED_LIB)
	$(PYTHON) -m unittest discover -s tests -p test_queue_contention.py -v
