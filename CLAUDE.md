# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**tftptest** is a TFTP (Trivial File Transfer Protocol) test server designed to simulate TFTP faults and edge cases for thorough testing of TFTP client implementations. It is intentionally kept simple and focused on this single purpose.

The repository contains subdirectories for multiple language implementations (`c/`, `cpp/`, `python/`), but **only the C implementation is active**. Occasional Python utility scripts may be placed in the `*/scripts/` of a language implementation.

### Current Status

**~60% complete** with core TFTP functionality working end-to-end:
- RRQ (read requests) in octet and netascii modes ✅
- WRQ (write requests) in octet and netascii modes ✅
- File transfers up to 65536×512 bytes (block number wrap-around) ✅
- Error handling and timeout robustness ✅
- Security hardening (chroot + privilege drop) ✅
- Control channel for fault injection ✅
- 66 passing unit tests, Python integration test suite for nominal transfers
- Remaining: ~25 parameterized fault injection modes (in progress)

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
- C99 standard (`-std=c99`)
- Comprehensive warning set shared by both compilers, plus compiler-specific extra warnings
- UBSan + ASan in test builds
- Coverage instrumentation in test builds

## Code Architecture

### Module Structure

The C implementation is organized into focused modules:

- **`tftptest.c`** — Main server entry point (complete)
  - Signal handling (`SIGINT` for graceful shutdown)
  - Server socket setup on configurable port (default 23069)
  - CLI argument parsing: `-c config`, `-p port`, `-u user`, `-v` (verbosity), `-s` (syslog)
  - Main loop accepting and dispatching client requests
  - Chroot + privilege drop after socket setup
  - Delegates each session to the FSM

- **`tftp_fsm.c`** — TFTP Finite State Machine (functional for RRQ/WRQ)
  - End-to-end RRQ and WRQ session handling
  - State machine: DETERMINE_RQ → RRQ_DATA/RRQ_ACK/etc. or WRQ_DATA/WRQ_ACK/etc.
  - File I/O (fopen, fread, fwrite), socket communication (sendto, recvfrom)
  - RFC 1350 timeout + retransmit logic (configurable max_retransmits)
  - TID validation (source address/port match enforcement)
  - Netascii mode translation support
  - Fault injection framework (partially implemented)
  - Session state in file-scoped static `struct TFTP_FSM_Session_S`

- **`tftp_pkt.c`** — Packet Encoding/Decoding (complete per RFC 1350)
  - `TFTP_PKT_RequestIsValid()` — full RRQ/WRQ validation (opcode, filename, mode)
  - `TFTP_PKT_ParseRequest()` — extract opcode, filename, mode (zero-copy)
  - `TFTP_PKT_BuildData()`, `TFTP_PKT_BuildAck()`, `TFTP_PKT_BuildError()` — outgoing packets
  - `TFTP_PKT_ParseAck()`, `TFTP_PKT_ParseData()`, `TFTP_PKT_ParseError()` — incoming packets
  - Big-endian encoding/decoding, proper packet bounds checking

- **`tftp_err.c`** — Error Message Table (complete)
  - `tftp_err_str(enum TFTP_Err err)` — returns human-readable error strings
  - Enum covers all 8 RFC 1350 error codes + internal errors

- **`tftp_log.c`** — Logging (complete with syslog support)
  - `tftp_log_init()` — set min log level, optionally enable syslog
  - `tftp_log()` — printf-style logging with timestamp and level prefix
  - Syslog integration via `openlog()/vsyslog()`
  - Log levels: TRACE, DEBUG, INFO, WARN, ERR, FATAL

- **`tftp_util.c`** — Utility Functions (complete)
  - `tftp_util_create_ephemeral_udp_socket()` — UDP socket on ephemeral port
  - `tftp_util_set_recv_timeout()` — `SO_RCVTIMEO` wrapper
  - `tftp_util_is_valid_filename_char()` — printable ASCII validation
  - `tftp_util_octet_to_netascii()` — RFC 764 translation (bare LF→CRLF, bare CR→CR+NUL)
  - `tftp_util_netascii_to_octet()` — reverse translation
  - `tftp_util_chroot_and_drop()` — chdir + chroot + setgid + setuid + verify

- **`tftp_parsecfg.c`** — Configuration File Parsing (complete)
  - INI parser: `tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg)`
  - Supports: port, ctrl_port, root_dir, log_level, timeout_sec, max_retransmits, max_requests, fault_whitelist
  - CLI flags override config file values

- **`tftptest_ctrl.c`** — Control Channel (functional)
  - UDP socket on `ctrl_port` (default: TFTP port + 1)
  - Text protocol: `SET_FAULT <mode> [param]`, `GET_FAULT`, `RESET`
  - Fault whitelist enforcement

- **`tftptest_common.h`** — Common Constants
  - `FILENAME_MAX_LEN = 64`, `FILENAME_MIN_LEN = 1`
  - `TFTP_RQST_MIN_SZ`, `TFTP_DATA_MAX_SZ`, `TFTP_BLOCK_DATA_SZ = 512`

- **`tftptest_faultmode.h`** — Fault Mode Definitions
  - Enums for ~25+ parameterized fault injection modes (timeout, duplicate, error, invalid block#, wrong TID, etc.)

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

**Implemented:**
- ✅ **Compiler & Warnings**: Comprehensive warning set, both GCC and Clang, `-Werror` in release builds
- ✅ **Static Analysis**: GCC -fanalyzer, Clang --analyze, clang-tidy (Google style), cppcheck
- ✅ **Unit Tests**: 66 tests with Unity framework, comprehensive behavioral coverage
- ✅ **Sanitizers**: ASan + UBSan active in test builds, LSan for memory leaks
- ✅ **Assertions**: Corrected per guidelines — only on code-controlled conditions
- ✅ **Integration Tests**: Python test suite for nominal RRQ/WRQ transfers
- ✅ **Functional Coverage**: RRQ/WRQ end-to-end, block wrap-around, error handling, timeout robustness

**In Progress:**
- 🔄 **Coverage**: MC/DC targets via `make coverage` (baseline established)
- 🔄 **Fault Injection Tests**: ~25 parameterized modes (framework in place, modes pending)

**Planned:**
- 📋 **Fuzzing**: In-process (AFL++/libFuzzer) and network-level fuzzing harnesses
- 📋 **Manual Review**: CWE/CVE analysis, idiomatic construct audit
- 📋 **Chaos Monkey**: Nightly automated runs against fault-injecting clients

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

- **C Standard**: C99 (`-std=c99`)
- **Feature Test Macros**: `_POSIX_C_SOURCE=200809L`, `_GNU_SOURCE`
- **Assertions**: Per `SOFTWARE-QA-MANIFESTO.md`, assert **only** on code-controlled conditions:
  - ✅ Internal function results, system calls we control, enum bounds
  - ❌ External input (network, files, CLI args), public function parameters (validate instead)
  - Pattern: validate public parameters at entry → return error → assert on internals only
- **Function Comments**: Doxygen-style headers for public APIs; implementation details in code
- **Includes**: Standard headers first, then system headers, then internal headers
- **File-scope Variables**: File-scoped (static) for module internals; only public functions exported
- **Error Codes**: Enum-based return codes (not errno); bitfield-friendly (e.g., `TFTP_FSM_RC` with 0x0001, 0x0002, etc.)

## Testing & Coverage

**Unit Tests (66 passing):**
```bash
make test       # Build & run all unit tests with sanitizers
make coverage   # Run tests + generate HTML coverage report (gcov/gcovr)
```

**Integration Tests:**
```bash
python3 c/scripts/test_nominal.py  # Nominal TFTP transfers (RRQ/WRQ, multiple file sizes)
```
- Tests: small (100B), medium (50KB), large (65535×512B), extralarge (65536×512+100B) files
- Validates end-to-end transfers, block number wrap-around, MD5 checksums
- Server startup/shutdown lifecycle management
- Performance: ~4.3s for 65536 blocks (512B each) without verbose logging

**Test Framework:** Unity (git submodule at `c/test/Unity`)

**Sanitizers Active:** UBSan, ASan, LSan (memory coverage) in test builds

**Future Test Additions:**
- Parameterized fault injection test suite (~25 modes)
- Function-level fuzz harnesses
- Network-level fuzzing with malformed TFTP packets
- Chaos monkey testing (nightly runs)

## Implementation Phases

The project follows a phased roadmap (see `build/debug/tftptest` executable and integration tests):

| Phase | Scope | Status |
|-------|-------|--------|
| 0 | Foundation (logging, errors, utilities, config) | ✅ Complete |
| 1 | Packet module (RFC 1350 encoding/decoding) | ✅ Complete |
| 2 | RRQ end-to-end (normal operation, octet mode) | ✅ Complete |
| 3 | Netascii mode for RRQ | ✅ Complete |
| 4 | WRQ end-to-end (octet + netascii) | ✅ Complete |
| 5 | Error handling & timeout robustness | ✅ Complete |
| 6 | Control channel for fault injection | ✅ Complete |
| 7-10 | Fault injection modes (timeouts, duplicates, errors, protocol violations) | 🔄 In Progress |
| 11 | Large files & edge cases (block wrap-around) | ✅ Complete |
| 12 | Security hardening (chroot + privilege drop) | ✅ Complete |
| 13 | CLI arguments | ✅ Complete |
| 14 | Comprehensive test suite & cleanup | 🔄 In Progress |

**Next Steps:** Complete remaining fault injection modes (~25 parameterized faults), then run full QA suite (fuzz, chaos monkey, coverage targets).

## Important Implementation Notes

### File I/O and Jailing
- `tftp_util_chroot_and_drop()` is called after socket setup, before main loop
- All file I/O happens within the chrooted directory (enforced by the jail)
- Running as `nobody` by default; override with `-u user` CLI flag

### Large File Transfers
- Block numbers are `uint16_t` (0–65535), wrap to 0 at block 65536
- Transfer of exactly N×512 bytes must be followed by 0-byte DATA packet for EOF
- Python test suite validates 65536×512 byte transfers with MD5 checksum

### Socket Performance
- UDP receive buffer set to 1MB to prevent packet loss on rapid transfers
- Timeout set to 10s in integration tests (increase for slow networks)
- Without `-vv` flags, 65536 blocks transfers in ~4.3 seconds

## Guiding Documents

Read these for context before making architectural or scope decisions:

- `README.md` — High-level project goals
- `SOFTWARE-QA-MANIFESTO.md` — QA principles and assertion guidelines (recently updated)
- `SECURITY-PLAN.md` — Security hardening strategy and vulnerability analysis plan
- `PERFORMANCE-MANIFESTO.md` — Performance optimization principles

## Extra Notes for AI
- Do **NOT** ever try to `git commit` or `git push` changes! I will handle that.
- Do **NOT** use "fault injection" phrasing anywhere - use "fault simulation" or "fault emulation" instead.
