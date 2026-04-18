################################################################################
# Makefile for tftptest — A fault-injecting TFTP server for testing
#
# Builds:
#   release  – Optimized for speed (-O2), -Werror enabled, GCC output
#   debug    – Debug build (-Og -g3), no -Werror, GCC output
#   test     – Unit tests via Unity, sanitizers, coverage instrumentation
#
# Static analysis:
#   analyze – Run GCC -fanalyzer, clang --analyze, clang-tidy, and cppcheck
#             on all sources
#
# Coverage:
#   coverage – Run tests then produce HTML coverage report via gcov + gcovr
#
# Usage:
#   make help  # tells you the available targets
################################################################################

# ─── Project Layout ──────────────────────────────────────────────────────────

PROJECT          := tftptest
SRC_DIR          := src
TEST_DIR         := test
INTEGRATION_DIR  := test/integration
SCRIPTS_DIR      := scripts
BUILD_DIR        := build
MEMCHECK_DIR     := build/memcheck

# Valgrind / profilemem knobs (overridable on the command line)
SKIP_LARGE       ?= 0
VALGRIND_TIMEOUT ?= 120
# Expands to --skip-large when SKIP_LARGE=1, empty otherwise
_PROFMEM_SKIP_FLAG = $(if $(filter 1,$(SKIP_LARGE)),--skip-large,)

# Unity (cloned as a git submodule under test/)
UNITY_DIR    := $(TEST_DIR)/Unity
UNITY_SRC    := $(UNITY_DIR)/src

# ─── Toolchain ───────────────────────────────────────────────────────────────

CC           := gcc
CLANG        := clang
CLANG_TIDY   := clang-tidy
CPPCHECK     := cppcheck
GCOV         := gcov
GCOVR        := gcovr
CSPELL       := cspell

# ─── Source Discovery ────────────────────────────────────────────────────────

# Application sources (all .c files under src/)
SRCS         := $(wildcard $(SRC_DIR)/*.c)
# Test sources (all .c files directly in test/, excluding Unity internals)
TEST_SRCS    := $(wildcard $(TEST_DIR)/*.c)

# ─── Common Flags ────────────────────────────────────────────────────────────

# A reasonably thorough set of warnings shared by both GCC and Clang
COMMON_WARNS := \
	-Wall -Wextra -Wpedantic \
	-Wuninitialized -Wshadow \
	-Wdouble-promotion \
	-Wformat=2 -Wformat-signedness -Wformat-truncation \
	-Wundef \
	-fno-common \
	-Wconversion \
	-Wunused-parameter \
	-Wunused-result \
	-Wswitch-default -Wswitch-enum \
	-Wnull-dereference \
	-Wold-style-definition -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
	-Wfloat-equal \
	-Wwrite-strings \
	-Wcast-qual \
	-Wredundant-decls -Wparentheses \
	-Wmissing-include-dirs \
	-Winit-self -Wstrict-aliasing \
	-Wpointer-arith -Wreturn-type \
	-Wstack-protector \
	-Walloca \
	-Wdeprecated-declarations 


# Additional warnings only GCC understands
GCC_EXTRA_WARNS := \
	-Wmaybe-uninitialized \
	-Wlogical-op \
	-Wcast-align=strict \
	-Wformat-overflow=2 -Wformat-truncation=2 \
	-Wduplicated-cond \
	-Wduplicated-branches \
	-Wrestrict \
	-Wjump-misses-init \
	-Wstringop-overflow=4 \
	-Warith-conversion \
	-Warray-bounds=2 \
	-Wuse-after-free=2 \
	-Wimplicit-fallthrough=3 \
	-Walloc-zero -Walloc-size \
	-Wuseless-cast -Wtrampolines \
	-Wimplicit-fallthrough=4 \
	-fdiagnostics-color

# Additional warnings only Clang understands
CLANG_EXTRA_WARNS := \
	-Wformat-overflow \
	-Wassign-enum \
	-Wcast-align \
	-Wconditional-uninitialized \
	-Widiomatic-parentheses \
	-Wunreachable-code-aggressive \
	-Wcomma \
	-Wimplicit-fallthrough \
	-Wover-aligned

# Common defines / feature-test macros
COMMON_DEFS  := -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

# Common includes
COMMON_INC   := -I$(SRC_DIR)

# C standard
C_STD        := -std=c99

# Linker flags (add libs as needed; a TFTP server on Linux will likely need -lpthread)
LDFLAGS      :=
LDLIBS       :=

# ─── Per-Build Configuration ─────────────────────────────────────────────────

# --- Release ---
REL_DIR      := $(BUILD_DIR)/release
REL_CFLAGS   := $(C_STD) -O2 -DNDEBUG -D_FORTIFY_SOURCE=2 $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC) -Werror -fstack-protector-strong -fPIE
REL_LDFLAGS  := -pie -Wl,-z,relro,-z,now
REL_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(REL_DIR)/%.o,$(SRCS))
REL_BIN      := $(REL_DIR)/$(PROJECT)

# --- Debug ---
DBG_DIR      := $(BUILD_DIR)/debug
DBG_CFLAGS   := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_WARNS) -Wno-unused-parameter $(GCC_EXTRA_WARNS) $(COMMON_INC)
DBG_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(DBG_DIR)/%.o,$(SRCS))
DBG_BIN      := $(DBG_DIR)/$(PROJECT)

# --- Test ---
TEST_BUILD_DIR  := $(BUILD_DIR)/test
#	 This flag allows the executable to continue after the sanitizer has discovered
#	 an error, and the executable (likely the unit test executable) may even
#	 return a zero exit status afterwards. This just means that if we want to
#	 properly detect this in a pipeline, we'd want to separately scan the results
#	 of the sanitizers and produce an independent exit status based on that,
#	 for the job overall.
TEST_SANITIZERS := -fsanitize=undefined,address,leak -fstack-protector-strong -fsanitize-recover=all
TEST_CFLAGS     := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC) \
                   -I$(UNITY_SRC) \
                   $(TEST_SANITIZERS) \
                   --coverage -fprofile-arcs -ftest-coverage \
                   -fstack-protector-strong
# Separate flags for Unity (suppress warnings from its macros and implementation)
UNITY_CFLAGS    := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_INC) \
                   -I$(UNITY_SRC) \
                   -Wno-float-equal -Wno-useless-cast \
                   $(TEST_SANITIZERS) \
                   --coverage -fprofile-arcs -ftest-coverage \
                   -fstack-protector-strong

# Flags for test source files (suppress -Wuseless-cast from Unity macro expansion)
TEST_FILE_CFLAGS := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC) \
                   -I$(UNITY_SRC) \
                   -Wno-useless-cast \
                   $(TEST_SANITIZERS) \
                   --coverage -fprofile-arcs -ftest-coverage \
                   -fstack-protector-strong
TEST_LDFLAGS    := $(TEST_SANITIZERS) --coverage
# Application objects for test build (everything except main, so tests supply their own main via Unity)
# If you keep main() in tftptest.c, split it out or guard it with #ifndef UNIT_TEST.
TEST_APP_SRCS   := $(filter-out $(SRC_DIR)/tftptest.c,$(SRCS))
TEST_APP_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(TEST_BUILD_DIR)/%.o,$(TEST_APP_SRCS))
TEST_OBJS       := $(patsubst $(TEST_DIR)/%.c,$(TEST_BUILD_DIR)/%.o,$(TEST_SRCS))
UNITY_OBJS      := $(TEST_BUILD_DIR)/unity.o
TEST_BIN        := $(TEST_BUILD_DIR)/test_$(PROJECT)

# --- Coverage ---
COV_DIR         := $(BUILD_DIR)/coverage

# --- Analysis (separate directory for analyzer artifacts) ---
ANALYSIS_DIR    := $(BUILD_DIR)/analysis

# ─── Clang analysis flags (used during the analyze-only compile pass) ───────

CLANG_ANALYSIS_CFLAGS := $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(CLANG_EXTRA_WARNS) \
			 -Wno-unused-parameter \
			 $(COMMON_INC) \
                          --analyze \
                          -Xanalyzer -analyzer-output=text

GCC_ANALYSIS_CFLAGS   := $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) \
			 -Wno-unused-parameter \
			 $(COMMON_INC) \
                          -fanalyzer \
                          -c -o /dev/null

# ─── clang-tidy flags (lint against Google C++ style, plus all other checks) ─

# Enable every checker and layer Google's style checks on top.
# -warnings-as-errors is intentionally omitted so the build doesn't abort on
# the first hit — we want the full report (minimise false negatives).
CLANG_TIDY_CHECKS := \
	-checks='-*,google-*,clang-analyzer-*,bugprone-*,cert-*,concurrency-*,cppcoreguidelines-*,misc-*,modernize-*,performance-*,portability-*,readability-*'

CLANG_TIDY_FLAGS  := \
	$(CLANG_TIDY_CHECKS) \
	--header-filter='$(SRC_DIR)/.*' \
	--extra-arg=$(C_STD) \
	$(addprefix --extra-arg=,$(COMMON_DEFS)) \
	--extra-arg=-Wno-unused-parameter \
	$(addprefix --extra-arg=,$(COMMON_INC))

# ─── cppcheck flags (maximum depth, all checkers) ───────────────────────────

# Note, cppcheck 2.19.1 (my version as of Apr 5, 2026), does not support C23.
CPPCHECK_FLAGS := \
	--std=c11 \
	--enable=all \
	--inconclusive \
	--force \
	--check-level=exhaustive \
	--max-ctu-depth=16 \
	$(addprefix -D,_POSIX_C_SOURCE=200809L _GNU_SOURCE) \
	-I$(SRC_DIR) \
	--suppress=missingIncludeSystem \
	--error-exitcode=0

################################################################################
#                                 TARGETS
################################################################################

.PHONY: all release debug test buildtest testintegration testnominal profilemem binstats coverage analyze spell clean help fuzz

# Default target
all: debug

# ─── Release ──────────────────────────────────────────────────────────────────

release: $(REL_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "Release Executable \033[35m$< \033[32;1mbuilt\033[0m!"
	@echo "----------------------------------------"

# Also compile with Clang for its extra diagnostics (no binary kept)
release: CLANG_CHECK = clang_release_check

$(REL_BIN): $(REL_OBJS) | clang_release_check
	$(CC) $(REL_CFLAGS) $(REL_LDFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(REL_DIR)/%.o: $(SRC_DIR)/%.c | $(REL_DIR)
	$(CC) $(REL_CFLAGS) -c -o $@ $<

$(REL_DIR):
	@mkdir -p $@

# Clang compile pass for release (diagnostics only, no output kept)
.PHONY: clang_release_check
clang_release_check: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning Clang static analysis\033[0m..."
	@echo
	@for src in $(SRCS); do \
		echo "  [CLANG] $$src"; \
		$(CLANG) $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(CLANG_EXTRA_WARNS) \
		$(COMMON_INC) \
		-Werror -O2 -DNDEBUG -fsyntax-only $$src; \
	done
	@echo "--- Clang diagnostic pass complete ---"

# ─── Debug ────────────────────────────────────────────────────────────────────

debug: $(DBG_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "Debug Executable \033[35m$< \033[32;1mbuilt\033[0m!"
	@echo "----------------------------------------"

$(DBG_BIN): $(DBG_OBJS) | clang_debug_check
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[32mConstructing\033[0m the output executable: $@..."
	@echo
	$(CC) $(DBG_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(DBG_DIR)/%.o: $(SRC_DIR)/%.c | $(DBG_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[36mCompiling\033[0m the source file: $<..."
	@echo
	$(CC) $(DBG_CFLAGS) -c -o $@ $<

$(DBG_DIR):
	@mkdir -p $@

# Clang compile pass for debug (diagnostics only, warnings — no -Werror)
.PHONY: clang_debug_check
clang_debug_check: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning Clang static analysis\033[0m..."
	@echo
	@for src in $(SRCS); do \
		echo "  [CLANG] $$src"; \
		$(CLANG) $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(CLANG_EXTRA_WARNS) \
		$(COMMON_INC) \
		-Og -g3 -fsyntax-only $$src; \
	done
	@echo "--- Clang diagnostic pass complete ---"

# ─── Test ─────────────────────────────────────────────────────────────────────

TEST_RESULTS := $(TEST_BUILD_DIR)/results.txt
COLORIZE     := python3 $(SCRIPTS_DIR)/colorize_unity.py

# Note: ${PIPESTATUS[0]} returns the exit status code of the test binary, so that
#	this target's overall exit status is that of the test, not tee or the
#	colorize script.
test: $(TEST_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mExecuting unit test\033[0m $<..."
	@echo
	./$(TEST_BIN) 2>&1 | tee $(TEST_RESULTS) | $(COLORIZE); \
	exit $${PIPESTATUS[0]}

buildtest: $(TEST_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "Test Executable \033[35m$< \033[32;1mbuilt\033[0m!"
	@echo "----------------------------------------"

$(TEST_BIN): $(TEST_APP_OBJS) $(TEST_OBJS) $(UNITY_OBJS) | $(TEST_BUILD_DIR)
	$(CC) $(TEST_LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_FILE_CFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR)/unity.o: $(UNITY_SRC)/unity.c | $(TEST_BUILD_DIR)
	$(CC) $(UNITY_CFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR):
	@mkdir -p $@

# ─── Integration Tests ────────────────────────────────────────────────────────

INTEGRATION_SCRIPTS := $(wildcard $(INTEGRATION_DIR)/*.py)

testnominal: $(DBG_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mRunning nominal integration test\033[0m..."
	@echo
	python3 $(INTEGRATION_DIR)/test_nominal.py
	@echo "--- Nominal integration test complete ---"

testintegration: $(DBG_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mRunning all integration tests\033[0m..."
	@echo
	@failed=0; \
	for script in $(INTEGRATION_SCRIPTS); do \
		echo; \
		echo "  [INTEGRATION] $$script"; \
		python3 $$script || failed=$$((failed + 1)); \
	done; \
	echo; \
	if [ $$failed -ne 0 ]; then \
		echo -e "--- \033[1;31m$$failed integration test(s) FAILED\033[0m ---"; \
		exit 1; \
	else \
		echo "--- All integration tests passed ---"; \
	fi

# ─── Memory Profile ───────────────────────────────────────────────────────────

$(MEMCHECK_DIR):
	@mkdir -p $@

profilemem: $(DBG_BIN) | $(MEMCHECK_DIR)
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "ERROR: valgrind not found. Install with: sudo dnf install valgrind"; \
		exit 1; \
	fi
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mprofilemem: Valgrind memcheck\033[0m..."
	@echo
	python3 $(INTEGRATION_DIR)/test_nominal.py \
		--server-bin $(DBG_BIN) \
		--server-prefix "valgrind --tool=memcheck --leak-check=full \
			--show-leak-kinds=all --track-origins=yes \
			--log-file=$(abspath $(MEMCHECK_DIR))/memcheck.txt" \
		--timeout $(VALGRIND_TIMEOUT) \
		--stop-timeout 30 \
		$(_PROFMEM_SKIP_FLAG)
	@echo
	@if grep -qE "ERROR SUMMARY: [1-9]" "$(MEMCHECK_DIR)/memcheck.txt"; then \
		echo "ERROR: Valgrind memcheck found memory errors:"; \
		grep "ERROR SUMMARY" "$(MEMCHECK_DIR)/memcheck.txt"; \
		exit 1; \
	else \
		grep "ERROR SUMMARY" "$(MEMCHECK_DIR)/memcheck.txt" || true; \
		echo "--- Valgrind memcheck: clean ---"; \
	fi
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mprofilemem: Valgrind massif\033[0m..."
	@echo
	python3 $(INTEGRATION_DIR)/test_nominal.py \
		--server-bin $(DBG_BIN) \
		--server-prefix "valgrind --tool=massif \
			--massif-out-file=$(abspath $(MEMCHECK_DIR))/massif.out" \
		--timeout $(VALGRIND_TIMEOUT) \
		--stop-timeout 30 \
		$(_PROFMEM_SKIP_FLAG)
	ms_print "$(MEMCHECK_DIR)/massif.out" > "$(MEMCHECK_DIR)/massif.txt"
	@echo
	@echo "----------------------------------------"
	@echo "profilemem complete. Reports:"
	@echo "  $(MEMCHECK_DIR)/memcheck.txt  (Valgrind memcheck)"
	@echo "  $(MEMCHECK_DIR)/massif.txt    (heap profile, human-readable)"
	@echo "  $(MEMCHECK_DIR)/massif.out    (raw massif data)"

# ─── Binary Stats ─────────────────────────────────────────────────────────────
# Shows release binary footprint and hardening flags.
#
# Brief security terms rundown:
#   NX (no-execute):    Stack & heap marked non-executable; prevents code injection.
#                       RW = NX enabled (good), RWE = executable stack (exploitable).
#
#   RELRO:              Read-Only Relocations
#			Relocations read-only after load. PARTIAL = some, FULL = all.
#			Prevents GOT (Global Offset Table) hijacking.
#                       Full RELRO requires -Wl,-z,relro,-z,now at link time.
#
#   PIE:		Position Independent Executable
#			Binary can be loaded at random addresses (ASLR). ET_EXEC = no PIE.
#			Return-oriented exploits become infeasible - fcn addresses change every time.
#
#   FORTIFY_SOURCE:     Runtime bounds checking on libc functions (e.g., memcpy → memcpy_chk).
#                       Requires -D_FORTIFY_SOURCE=2 and optimization (-O1 or higher).
#
# Overall, RELRO + ASLR + PIE makes exploitation orders of magnitude harder at minimal
# performance cost.
binstats: $(REL_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mbinstats:\033[0m $(REL_BIN)"
	@echo
	@echo "--- File size & sections ---"
	@ls -lh $(REL_BIN) | awk '{print "  Total on disk:  " $$5 "  (" $$9 ")"}'
	@size $(REL_BIN)
	@echo
	@echo "--- Security hardening ---"
	@if nm $(REL_BIN) 2>/dev/null | grep -q "__stack_chk_fail"; then \
		echo "  Stack canary:     ENABLED  (-fstack-protector-strong)"; \
	else \
		echo "  Stack canary:     disabled"; \
	fi
	@readelf -l $(REL_BIN) | awk '/GNU_STACK/ { \
		if (index($$0,"RWE")) print "  NX (no-exec):     DISABLED"; \
		else print "  NX (no-exec):     ENABLED"; \
	}'
	@if readelf -l $(REL_BIN) | grep -q GNU_RELRO; then \
		if readelf -d $(REL_BIN) | grep -q BIND_NOW; then \
			echo "  RELRO:            FULL"; \
		else \
			echo "  RELRO:            PARTIAL"; \
		fi; \
	else \
		echo "  RELRO:            none"; \
	fi
	@readelf -h $(REL_BIN) | awk '/^  Type:/ { \
		if ($$2 == "DYN") print "  PIE:              ENABLED"; \
		else print "  PIE:              disabled (ET_EXEC — no -fPIE)"; \
	}'
	@if nm $(REL_BIN) 2>/dev/null | grep -qE " [TW] .*_chk$$"; then \
		echo "  FORTIFY_SOURCE:   some functions fortified"; \
	else \
		echo "  FORTIFY_SOURCE:   not detected"; \
	fi
	@echo
	@echo "--- Build ID ---"
	@readelf -n $(REL_BIN) 2>/dev/null | awk '/Build ID:/ {print "  " $$0}' || echo "  (none)"
	@echo
	@echo "--- Dynamic library dependencies ---"
	@ldd $(REL_BIN) | sed 's/^/  /'
	@echo
	@echo "--- Static library linkage ---"
	@sym_list=$$(nm --defined-only -g $(REL_BIN) 2>/dev/null \
		| awk '$$2 == "T" {print $$3}' \
		| grep -Ev '^(main|tftp_|tftptest_|_)' | sort); \
	if [ -z "$$sym_list" ]; then \
		echo "  none detected"; \
	else \
		echo "$$sym_list" | sed 's/^/  /'; \
	fi
	@echo
	@echo "----------------------------------------"
	@echo "binstats complete."

# ─── Coverage ─────────────────────────────────────────────────────────────────

coverage: test | $(COV_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mGenerating HTML coverage report\033[0m..."
	@echo
	$(GCOVR) \
		--root . \
		--object-directory $(TEST_BUILD_DIR) \
		--filter '$(SRC_DIR)/' \
		--html --html-details \
		--output $(COV_DIR)/index.html \
		--print-summary \
		--decisions \
		--calls
	@echo "=== Coverage report: $(COV_DIR)/index.html ==="

$(COV_DIR):
	@mkdir -p $@

# ─── Static Analysis (run at-will) ──────────────────────────────────────────

analyze: analysis_gcc analysis_clang analysis_clang_tidy analysis_cppcheck
	@echo "=== Static analysis complete ==="
	@echo
	@echo "----------------------------------------"
	@echo -e "Static analysis \033[32;1mcomplete\033[0m"
	@echo "----------------------------------------"

.PHONY: analysis_gcc analysis_clang analysis_clang_tidy analysis_cppcheck

analysis_gcc: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning GCC static analysis\033[0m on all src files..."
	@echo
	@for src in $(SRCS); do \
		echo "  [GCC-ANALYZER] $$src"; \
		$(CC) $(GCC_ANALYSIS_CFLAGS) $$src; \
	done
	@echo "--- GCC -fanalyzer complete ---"

analysis_clang: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning Clang static analysis\033[0m on all src files..."
	@echo
	@for src in $(SRCS); do \
		echo "  [CLANG-ANALYZE] $$src"; \
		$(CLANG) $(CLANG_ANALYSIS_CFLAGS) $$src; \
	done
	@echo "--- Clang --analyze complete ---"

analysis_clang_tidy: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning clang-tidy (Google style + extra checkers)\033[0m on all src files..."
	@echo
	@for src in $(SRCS); do \
		echo "  [CLANG-TIDY] $$src"; \
		$(CLANG_TIDY) $(CLANG_TIDY_FLAGS) $$src --; \
	done
	@echo "--- clang-tidy complete ---"

analysis_cppcheck: | $(ANALYSIS_DIR)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[33mRunning cppcheck (exhaustive analysis)\033[0m on all src files..."
	@echo
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(SRCS) 2>&1
	@echo "--- cppcheck complete ---"

$(ANALYSIS_DIR):
	@mkdir -p $@

# ─── Spell Check ──────────────────────────────────────────────────────────────

spell:
	@echo -e "\033[33mRunning cspell\033[0m on source, docs, and scripts..."
	@$(CSPELL) lint --config cspell.json \
	    "src/**/*.{c,h}"         \
	    "test/test_*.c"          \
	    "scripts/**/*.py"        \
	    "docs/**/*.md"           \
	    "../*.md"                \
	    || true
	@echo "--- spell check complete ---"

# ─── Clean ────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

# ─── Help ─────────────────────────────────────────────────────────────────────

help:
	@echo ""
	@echo "  tftptest Makefile"
	@echo "  ─────────────────────────────────────────────────"
	@echo "  make                  Build debug (default)"
	@echo "  make debug            Build with -Og -g3, no -Werror"
	@echo "  make release          Build with -O2, -Werror"
	@echo "  make test             Build & run Unity unit tests"
	@echo "  make buildtest        Build unit test executable (do not run)"
	@echo "  make testnominal      Run nominal integration test (test/integration/test_nominal.py)"
	@echo "  make testintegration  Run all integration tests (test/integration/*.py)"
	@echo "  make profilemem       Run Valgrind memcheck + massif on nominal integration tests"
	@echo "                          SKIP_LARGE=1    skip 33-64 MB transfers (much faster)"
	@echo "                          VALGRIND_TIMEOUT=N  socket timeout per call (default 120s)"
	@echo "  make binstats         Show release binary stats: size, sections, security, deps"
	@echo "  make coverage         Run tests + generate HTML coverage report"
	@echo "  make analyze          Run GCC -fanalyzer, Clang --analyze, clang-tidy & cppcheck"
	@echo "  make spell            Spell-check source, docs, and scripts (advisory)"
	@echo "  make clean            Remove all build artifacts"
	@echo "  make help             Show this help"
	@echo "  make fuzz             (TODO) Fuzz the server"
	@echo "  make cve_analysis     (TODO) Check for any known CVE patterns"
	@echo ""
	@echo "  Outputs:"
	@echo "    Debug binary     → $(DBG_BIN)"
	@echo "    Release binary   → $(REL_BIN)"
	@echo "    Test binary      → $(TEST_BIN)"
	@echo "    Coverage report  → $(COV_DIR)/index.html"
	@echo ""
