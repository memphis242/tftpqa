# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**tftptest** is a TFTP (Trivial File Transfer Protocol) test server designed to simulate TFTP faults and edge cases for thorough testing of TFTP client implementations. It is intentionally kept simple and focused on this single purpose.

The repository contains subdirectories for multiple language implementations (`c/`, `cpp/`, `python/`), but **only the C implementation is active**. Occasional Python utility scripts may be placed in the `*/scripts/` of a language implementation.

### Development Priorities (in order)
1. **Functionality**: Get the application working correctly
2. **Quality Assurance**: Address all items in `SOFTWARE-QA-MANIFESTO.md`
3. **Security**: Harden against cybersecurity threats per `SECURITY-PLAN.md`
4. **Performance**: Optimize per `PERFORMANCE-MANIFESTO.md`

## Build Commands

Work in the `c/` directory. The comprehensive Makefile supports:

```bash
make                # Build debug binary (default)
make debug          # Debug build: -Og -g3, no -Werror
make release        # Release build: -O2 -DNDEBUG -Werror
make test           # Build & run unit tests (Unity framework + sanitizers)
make coverage       # Run tests + generate HTML coverage report (gcov/gcovr)
make analyze        # Static analysis: GCC -fanalyzer, Clang --analyze, clang-tidy, cppcheck
make clean          # Remove build artifacts
make help           # Show Makefile targets
```

**Compiler features:**
- GCC + Clang (release and debug builds trigger Clang diagnostic pass)
- C23 standard (`-std=c23`)
- Comprehensive warning set shared by both compilers, plus compiler-specific extra warnings
- UBSan + ASan in test builds
- Coverage instrumentation in test builds

## Code Architecture

### Module Structure

The C implementation is organized into focused modules:

- **`tftptest.c`** — Main server entry point
  - Signal handling (e.g., `SIGINT` for graceful shutdown)
  - Server socket setup on a configurable port (default 23069)
  - Main loop accepting new client connections
  - Delegates each session to the FSM

- **`tftp_fsm.c`** — TFTP Finite State Machine
  - Handles individual read (RRQ) and write (WRQ) transfer sessions
  - Manages state transitions (DETERMINE_RQ → RRQ_DATA/RRQ_ACK/etc. or WRQ_DATA/WRQ_ACK/etc.)
  - File I/O and socket communication for the session
  - Internal session state encapsulated in `struct TFTP_FSM_Session_S`
  - (Mostly stubbed; expand as implementation proceeds)

- **`tftp_pkt.c`** — Packet Encoding/Decoding and Validation
  - Validates incoming TFTP requests (RRQ/WRQ packets)
  - Decodes/encodes TFTP packets
  - (Mostly stubbed; expand as implementation proceeds)

- **`tftp_err.c`** — Error Message Table
  - The internal API for error handling uses an enumeration of the error
  - Functions return that enumeration, and upstream callers check accordingly
  - Errors can be printed using the enum as an index into the error message table
  - (Not yet implemented; expand as implementation proceeds)

- **`tftp_log.c`** — Logging
  - Centralized logging interface
  - (Mostly stubbed; plan includes syslog integration)

- **`tftp_util.c`** — Utility Functions
  - Shared helper functions for string handling, assertions, etc.

- **`tftp_parsecfg.c`** — Configuration File Parsing
  - Parses configuration files to configure the server (e.g., port, allowed fault simulations, etc.)

- **`tftptest_common.h`** — Common Constants
  - Shared constants across the application - use `constexpr` instead of `#define` whenever possible
  - Examples: `FILENAME_MAX_LEN`, `FILENAME_MIN_LEN`, `TFTP_MODE_*_LEN`

- **`tftptest_faultmode.h`** — Fault Mode Definitions
  - Enums and structures for the ~30+ test fault cases (timeouts, file not found, permission errors, packet corruption, etc.)

### Key Design Principles

- **Modular**: Each module handles a specific concern; internal state is file-scoped
- **Encapsulation**: Modules expose only necessary APIs via header files; implementation details are private
- **Orthogonal APIs**: Module APIs do not overlap; clear boundaries between responsibilities
- **Single Client**: Only one TFTP client connection is handled at a time (by design)

### Build Outputs

```
c/build/debug/tftptest          # Debug binary (default)
c/build/release/tftptest        # Release binary
c/build/test/test_tftptest      # Unit test binary
c/build/coverage/index.html     # Coverage report
```

## Quality Assurance Standards

Per `SOFTWARE-QA-MANIFESTO.md`:

- **Compiler & Warnings**: Thorough warning set, multiple compilers (GCC + Clang)
- **Static Analysis**: GCC -fanalyzer, Clang --analyze, clang-tidy (Google style + extra checks), cppcheck (exhaustive)
- **Unit Tests**: Unity framework with sanitizers (UBSan, ASan, LSan)
- **Coverage**: MC/DC coverage targets (use `make coverage`)
- **Sanitizers**: ASan + UBSan in test builds; LSan bundled with ASan on Linux
- **Assertions**: Use `assert()` throughout to document assumptions
- **Fuzzing**: Planned multi-layer fuzzing (in-process and network-level)
- **Manual Review**: Codebase scanned for common enumerated vulnerabilities and idiomatic best practices

## Security Assurance Strategy

Per `SECURITY-PLAN.md`:

1. **Multi-Layer Fuzz Testing**
   - In-process fuzzing (AFL++ / libFuzzer with ASan + UBSan)
   - Network-level fuzzing with realistic TFTP traffic
   - Seed corpus includes valid/invalid packets, edge cases, malformed payloads

2. **CVE/CWE Analysis**
   - Reference CWE (Common Weakness Enumeration) lists
   - Review CVEs from `atftpd`, `tftpd-hpa`
   - Test system call abuse vectors (e.g., `chroot()` jail correctness)

3. **Agentic Security Analysis**
   - AI agents scan for vulnerabilities and write security-focused test cases

4. **Chaos Monkey Testing**
   - Nightly automated runs against fault-injecting TFTP clients with various instrumentation

## Performance Strategy

Per `PERFORMANCE-MANIFESTO.md`:

- **Function-level**: Benchmark functions, choose fastest approach (ties broken by space efficiency)
- **Architecture-level**: Minimize steps/jumps, simplify, tighten while preserving encapsulation

Performance optimization is deferred until functionality and QA are complete.

## Code Style & Conventions

- **C Standard**: C23 (`-std=c23`)
- **Feature Test Macros**: `_POSIX_C_SOURCE=200809L`, `_GNU_SOURCE`
- **Assertions**: Liberal use of `assert()` to validate assumptions
- **Function Comments**: Doxygen-style headers for public APIs; implementation details in code
- **Includes**: Standard headers first, then system headers, then internal headers
- **Line-Scope Variables**: File-scope (static) for module internals; only public functions exported
- **Error Codes**: Enum-based return codes (not errno); bitfield-friendly (e.g., `TFTP_FSM_RC` with 0x0001, 0x0002, etc.)

## Testing & Coverage

**Running Tests:**
```bash
make test                        # Build & run unit tests
make coverage                    # Run tests + open coverage report
```

**Test Framework:** Unity (included as git submodule under `c/test/Unity`)

**Sanitizers Active in Tests:** UBSan, ASan, memory coverage instrumentation

**Future:** Function-level fuzz harnesses, network-level fuzzing harnesses

## Important: Work Only in `c/`

- All active development is in the `c/` directory
- Do not create or edit files in `cpp/` or `python/` directories unless explicitly requested
- Any Python utility scripts belong in `c/scripts/` (e.g., test automation, API scripts)

## Guiding Documents

Read these for context before making architectural or scope decisions:

- `README.md` — High-level project goals (in-progress)
- `SOFTWARE-QA-MANIFESTO.md` — QA principles and test strategy
- `SECURITY-PLAN.md` — Security hardening strategy and vulnerability analysis plan
- `PERFORMANCE-MANIFESTO.md` — Performance optimization principles
