################################################################################
# Makefile for tftptest — A fault-injecting TFTP server for testing
#
# Builds:
#   release  – Optimized for speed (-O2), -Werror enabled, GCC output
#   debug    – Debug build (-Og -g3), no -Werror, GCC output
#   test     – Unit tests via Unity, sanitizers, coverage instrumentation
#
# Static analysis:
#   analysis – Run GCC -fanalyzer and clang --analyze on all sources
#
# Coverage:
#   coverage – Run tests then produce HTML coverage report via gcov + gcovr
#
# Usage:
#   make                  # defaults to debug
#   make release
#   make debug
#   make test             # build & run unit tests
#   make coverage         # build tests w/ coverage, run, generate HTML report
#   make analysis         # static analysis only (no binary produced)
#   make clean
################################################################################

# ─── Project Layout ──────────────────────────────────────────────────────────

PROJECT      := tftptest
SRC_DIR      := src
TEST_DIR     := test
SCRIPTS_DIR  := scripts
BUILD_DIR    := build

# Unity (cloned as a git submodule under test/)
UNITY_DIR    := $(TEST_DIR)/Unity
UNITY_SRC    := $(UNITY_DIR)/src

# ─── Toolchain ───────────────────────────────────────────────────────────────

CC           := gcc
CLANG        := clang
GCOV         := gcov
GCOVR        := gcovr

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
	-Wformat=2 -Wformat-signedness \
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
	-fdiagnostics-color

# Additional warnings only Clang understands
CLANG_EXTRA_WARNS := \
	-Wassign-enum \
	-Wcast-align \
	-Wformat-overflow -Wformat-truncation \
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
C_STD        := -std=c23

# Linker flags (add libs as needed; a TFTP server on Linux will likely need -lpthread)
LDFLAGS      :=
LDLIBS       :=

# ─── Per-Build Configuration ─────────────────────────────────────────────────

# --- Release ---
REL_DIR      := $(BUILD_DIR)/release
REL_CFLAGS   := $(C_STD) -O2 -DNDEBUG $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC) -Werror
REL_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(REL_DIR)/%.o,$(SRCS))
REL_BIN      := $(REL_DIR)/$(PROJECT)

# --- Debug ---
DBG_DIR      := $(BUILD_DIR)/debug
DBG_CFLAGS   := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC)
DBG_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(DBG_DIR)/%.o,$(SRCS))
DBG_BIN      := $(DBG_DIR)/$(PROJECT)

# --- Test ---
TEST_BUILD_DIR  := $(BUILD_DIR)/test
# Sanitizers appropriate for a network server: UBSan, ASan, LSan (bundled w/ ASan on Linux)
# Also add -fstack-protector-strong for extra runtime hardening.
TEST_SANITIZERS := -fsanitize=undefined,address -fno-sanitize-recover=all
TEST_CFLAGS     := $(C_STD) -Og -g3 $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) $(COMMON_INC) \
                   -I$(UNITY_SRC) \
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

# ─── Clang analysis flags (used during the analysis-only compile pass) ───────

CLANG_ANALYSIS_CFLAGS := $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(CLANG_EXTRA_WARNS) \
			 $(COMMON_INC) \
                          --analyze \
                          -Xanalyzer -analyzer-output=text

GCC_ANALYSIS_CFLAGS   := $(C_STD) $(COMMON_DEFS) $(COMMON_WARNS) $(GCC_EXTRA_WARNS) \
			 $(COMMON_INC) \
                          -fanalyzer \
                          -c -o /dev/null

################################################################################
#                                 TARGETS
################################################################################

.PHONY: all release debug test coverage analysis clean help

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
	$(CC) $(REL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

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

test: $(TEST_BIN)
	@echo
	@echo "----------------------------------------"
	@echo -e "\033[35mExecuting unit test\033[0m $<..."
	@echo
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_APP_OBJS) $(TEST_OBJS) $(UNITY_OBJS) | $(TEST_BUILD_DIR)
	$(CC) $(TEST_LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR)/unity.o: $(UNITY_SRC)/unity.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(TEST_BUILD_DIR):
	@mkdir -p $@

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

analysis: analysis_gcc analysis_clang
	@echo "=== Static analysis complete ==="
	@echo
	@echo "----------------------------------------"
	@echo -e "Static analysis \033[32;1mcompletebuilt\033[0m"
	@echo "----------------------------------------"

.PHONY: analysis_gcc analysis_clang

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

$(ANALYSIS_DIR):
	@mkdir -p $@

# ─── Clean ────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

# ─── Help ─────────────────────────────────────────────────────────────────────

help:
	@echo ""
	@echo "  tftptest Makefile"
	@echo "  ─────────────────────────────────────────────────"
	@echo "  make              Build debug (default)"
	@echo "  make debug        Build with -Og -g3, no -Werror"
	@echo "  make release      Build with -O2, -Werror"
	@echo "  make test         Build & run Unity unit tests"
	@echo "  make coverage     Run tests + generate HTML coverage report"
	@echo "  make analysis     Run GCC -fanalyzer & Clang --analyze"
	@echo "  make clean        Remove all build artifacts"
	@echo "  make help         Show this help"
	@echo ""
	@echo "  Outputs:"
	@echo "    Debug binary     → $(DBG_BIN)"
	@echo "    Release binary   → $(REL_BIN)"
	@echo "    Test binary      → $(TEST_BIN)"
	@echo "    Coverage report  → $(COV_DIR)/index.html"
	@echo ""
