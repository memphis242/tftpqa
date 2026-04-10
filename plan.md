● Now let me save the key design decisions to memory for future sessions.

  Recalled 1 memory, wrote 2 memories (ctrl+o to expand)

● The plan is written. Here's the high-level summary:

  15 phases, each producing something testable:

  ┌───────┬──────────────────────────────────────────────────────────────────────────────┐
  │ Phase │                                     Goal                                     │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ Pre   │ Fix 4 existing bugs (htons, inverted validation, filename len, static)       │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 0     │ Foundation: logging, errors, utilities, INI config parser, first unit tests  │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 1     │ Packet module: all RFC 1350 encode/decode/validate functions                 │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 2     │ RRQ end-to-end (octet) — first working transfer                              │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 3     │ Netascii CR/LF translation for RRQ                                           │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 4     │ WRQ end-to-end (octet + netascii)                                            │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 5     │ Error handling and timeout robustness                                        │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 6     │ UDP control channel + expanded fault enum (~21 values + params)              │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 7-10  │ Fault injection: timeouts → duplicates → error packets → protocol violations │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 11    │ Large files + edge cases (block# wrap, exact 512 multiples)                  │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 12    │ Security: chroot + privilege drop                                            │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 13    │ CLI argument parsing                                                         │
  ├───────┼──────────────────────────────────────────────────────────────────────────────┤
  │ 14    │ Full test suite, static analysis clean, coverage, integration scripts        │
  └───────┴──────────────────────────────────────────────────────────────────────────────┘

● User approved Claude's plan
  ⎿  Plan saved to: ~/.claude/plans/bubbly-prancing-codd.md · /plan to edit
     Implementation Plan: tftptest TFTP Fault-Injection Server

     Context

     The tftptest server is ~5-10% complete. The skeleton exists (main loop, FSM state enum, packet validation stub,
     fault mode enum) but nearly all logic is unimplemented. Four supporting modules (tftp_err, tftp_log, tftp_util,
     tftp_parsecfg) are empty stubs. No unit tests exist yet.

     This plan takes the project from its current state to a fully working TFTP test server with ~33 fault injection
     cases, proper security hardening, and comprehensive tests. Each phase produces something testable.

     ---
     Pre-Implementation: Fix Existing Bugs

     Before any new code, fix these in the existing codebase:

     1. tftptest.c:257 — sin_port = port missing htons(). Binds to wrong port.
     2. tftptest.c:152 — TFTP_PKT_RequestIsValid(...) needs ! negation. Currently skips valid packets.
     3. tftp_fsm.c:40 — MAX_FNAME_LEN = 1024 conflicts with tftptest_common.h FILENAME_MAX_LEN = 64. Remove local
     constant, use FILENAME_MAX_LEN from tftptest_common.h.
     4. tftp_fsm.c:75 — TFTP_FSM_Session missing static. Add it for encapsulation.

     ---
     Phase 0: Foundation Layer (Logging, Errors, Utilities, Config)

     Goal: Stand up infrastructure modules so everything built on top has proper logging, error reporting, config, and
     helpers. make debug compiles cleanly with real logging calls. make test runs with at least one passing test.

     tftp_log.h / tftp_log.c

     - Log level enum: LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERR, LOG_FATAL
     - tftp_log_init(bool use_syslog, enum LogLevel min_level) — opens syslog if requested
     - tftp_log(enum LogLevel, const char *fmt, ...) — writes to stderr always, syslog if enabled, filtered by min
     level
     - tftp_log_shutdown() — closes syslog
     - Implementation: va_list/vfprintf/vsyslog, file-scope static config state, map log levels to syslog priorities

     tftp_err.h / tftp_err.c

     - Error enum: TFTP_ERR_NONE, TFTP_ERR_SOCKET, TFTP_ERR_BIND, TFTP_ERR_FILE_OPEN, TFTP_ERR_FILE_READ,
     TFTP_ERR_FILE_WRITE, TFTP_ERR_TIMEOUT, TFTP_ERR_PROTOCOL, TFTP_ERR_CONFIG, TFTP_ERR_ALLOC, etc.
     - const char *tftp_err_str(enum TFTP_Err err) — static string table indexed by enum, with assert() bounds check

     tftp_util.h / tftp_util.c

     - int tftp_util_create_ephemeral_udp_socket(struct sockaddr_in *bound_addr) — creates UDP socket, binds to
     INADDR_ANY:0, returns fd
     - int tftp_util_set_recv_timeout(int sfd, unsigned int timeout_sec) — wraps setsockopt(SO_RCVTIMEO)
     - bool tftp_util_is_valid_filename_char(char c) — character validation helper

     tftp_parsecfg.h / tftp_parsecfg.c

     - Config struct:
     struct TFTPTest_Config {
         uint16_t tftp_port;       // default 23069
         uint16_t ctrl_port;       // default tftp_port + 1
         char root_dir[PATH_MAX];
         enum LogLevel log_level;
         unsigned int timeout_sec; // default 1
         unsigned int max_retransmits; // default 5
         size_t max_requests;      // default 10000
         uint64_t fault_whitelist; // bitmask of allowed fault modes
     };
     - void tftp_parsecfg_defaults(struct TFTPTest_Config *cfg) — fill with defaults
     - int tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg) — INI parser: read lines, skip # comments
     and blanks, split on first =, trim whitespace, match known keys, validate ranges

     tftptest.c updates

     - Replace all TODO: Syslog comments with tftp_log() calls
     - Wire config parsing at startup

     First test file: c/test/test_tftptest.c

     - tftp_err_str() returns non-null for all enum values
     - tftp_parsecfg_defaults() produces sane values
     - TFTP_PKT_RequestIsValid() (the stub — proves test harness works)

     ---
     Phase 1: Packet Module (RFC 1350)

     Goal: Complete packet encoding, decoding, and validation. Pure functions, highly unit-testable.

     tftp_pkt.h — expand with:

     - Opcode constants: TFTP_OP_RRQ=1, TFTP_OP_WRQ=2, TFTP_OP_DATA=3, TFTP_OP_ACK=4, TFTP_OP_ERR=5
     - TFTP error codes: TFTP_ERRC_NOT_DEFINED=0 through TFTP_ERRC_NO_SUCH_USER=7
     - Functions:
       - bool TFTP_PKT_RequestIsValid(const uint8_t *buf, size_t sz) — add const
       - int TFTP_PKT_ParseRequest(const uint8_t *buf, size_t sz, uint16_t *opcode, const char **filename, const char
     **mode) — zero-copy extraction
       - size_t TFTP_PKT_BuildData(uint8_t *out, size_t out_cap, uint16_t block_num, const uint8_t *data, size_t
     data_len)
       - size_t TFTP_PKT_BuildAck(uint8_t *out, size_t out_cap, uint16_t block_num)
       - size_t TFTP_PKT_BuildError(uint8_t *out, size_t out_cap, uint16_t error_code, const char *errmsg)
       - int TFTP_PKT_ParseAck(const uint8_t *buf, size_t sz, uint16_t *block_num)
       - int TFTP_PKT_ParseData(const uint8_t *buf, size_t sz, uint16_t *block_num, const uint8_t **data, size_t
     *data_len)
       - int TFTP_PKT_ParseError(const uint8_t *buf, size_t sz, uint16_t *error_code, const char **errmsg)

     tftp_pkt.c — implement all above

     - RequestIsValid: all 7 checks from existing TODO comments (opcode, filename NUL-terminated, valid chars, no path
     separators, non-empty + alphanumeric, mode is "netascii"/"octet" case-insensitive, mode NUL-terminated)
     - Builders: big-endian opcodes/block numbers via htons() + memcpy()

     tftptest_common.h — add constants:

     - TFTP_DATA_MAX_SZ = 516 (2 + 2 + 512)
     - TFTP_ACK_SZ = 4
     - TFTP_ERR_HDR_SZ = 4
     - TFTP_BLOCK_DATA_SZ = 512

     c/test/test_tftp_pkt.c — comprehensive tests:

     - Valid RRQ/WRQ in octet/netascii (case-insensitive)
     - Reject: wrong opcode, missing NUL terminators, filename too long, empty filename, path separators, unsupported
     mode, packet too short, non-printable chars
     - Build/Parse round-trips for DATA, ACK, ERROR
     - Edge: DATA with 0 bytes (final), DATA with exactly 512 bytes

     ---
     Phase 2: RRQ End-to-End (Normal Operation, Octet Mode)

     Goal: A client can download a file. First end-to-end working path.

     tftp_fsm.h — update signature:

     - TFTP_FSM_KickOff(uint8_t *rqbuf, size_t rqsz, struct sockaddr_in *peer_addr, const struct TFTPTest_Config *cfg)

     tftp_fsm.c — implement the FSM:

     - Add session fields: struct sockaddr_in peer_addr, uint16_t block_num, unsigned int retries, bool is_last_block,
     enum { MODE_OCTET, MODE_NETASCII } transfer_mode
     - TFTP_FSM_KickOff():
       a. Parse request with TFTP_PKT_ParseRequest()
       b. If WRQ: send ERROR "not implemented yet", return
       c. For RRQ: open file (fopen in "rb"), send ERROR file-not-found if missing
       d. Create ephemeral UDP socket via tftp_util_create_ephemeral_udp_socket()
       e. Switch-based FSM loop:
           - DETERMINE_RQ → RRQ_DATA
         - RRQ_DATA: fread up to 512 bytes, build DATA, sendto peer. If < 512 → RRQ_FIN_DATA, else → RRQ_ACK
         - RRQ_ACK: recvfrom with timeout, validate ACK + block number. Timeout → retransmit (decrement retries).
     Retries exhausted → ERR. ACK matches → increment block → RRQ_DATA
         - RRQ_FIN_DATA: Wait for final ACK, then → IDLE
         - ERR: Send ERROR, cleanup, return error code
         - IDLE: Close file + socket, return success
       f. TID validation on every recvfrom: source addr/port must match peer. Mismatch → send ERROR code 5 to wrong
     sender, continue waiting.

     tftptest.c — update main loop:

     - Pass peer_addr and config to TFTP_FSM_KickOff()
     - Wire config values for port, max_requests, timeout

     Testing:

     - Manual: download files of various sizes with tftp/atftp, verify with diff/md5sum
     - Unit tests for file-reading helper if extracted
     - Test TFTP_FSM_CleanExit under ASan

     ---
     Phase 3: Netascii Mode for RRQ

     Goal: RRQ works with proper CR/LF translation.

     tftp_util.h / tftp_util.c — add:

     - size_t tftp_util_octet_to_netascii(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, bool
     *pending_cr) — pure translation function
     - Translation rules: \n → \r\n, bare \r → \r\0, \r\n passes through
     - Must handle split-across-block-boundaries (the pending_cr state)

     tftp_fsm.c — when mode is netascii, route file reads through the translation layer

     c/test/test_tftp_util.c — unit tests:

     - \n → \r\n, \r → \r\0, \r\n passthrough
     - Translation across buffer boundaries (last byte is \r)
     - Empty input, no special chars (passthrough)

     ---
     Phase 4: WRQ End-to-End (Octet + Netascii)

     Goal: Client can upload files in both modes.

     tftp_fsm.c — add WRQ path:

     - DETERMINE_RQ → If WRQ: open file for writing, send ACK block 0 → WRQ_DATA
     - WRQ_DATA: recvfrom with timeout, validate DATA, check block number. Write to file (reverse-translate if
     netascii). Send ACK. If data < 512 → WRQ_FIN_ACK. Else increment block.
     - WRQ_FIN_ACK: Session complete → IDLE
     - Handle duplicate DATA: re-send ACK, don't re-write
     - Overwrite existing files per RFC 1350

     tftp_util.c — add:

     - size_t tftp_util_netascii_to_octet(...) — reverse translation

     Testing:

     - Upload files, verify with diff
     - Edge: zero-byte file, file exactly N×512 bytes (final empty DATA block)
     - Reverse netascii translation unit tests

     ---
     Phase 5: Error Handling and Timeout Robustness

     Goal: All RFC 1350 error conditions handled correctly.

     tftp_fsm.c:

     - TID validation on every recvfrom (already in Phase 2, solidify here)
     - Configurable timeout + max retransmits from config
     - File errors: permission denied → ERROR 2, disk full → ERROR 3
     - Receiving ERROR from client: log and abort session
     - Unexpected opcode: ERROR 4

     Testing:

     - Send packets from wrong port with nc, verify ERROR 5 response
     - Test timeout + retransmit counting

     ---
     Phase 6: Control Channel

     Goal: Test harness can set fault mode via UDP.

     tftptest.c:

     - Create second UDP socket on ctrl_port (config, default = TFTP port + 1)
     - Between sessions: non-blocking recvfrom on control socket
     - Text-based protocol:
       - SET_FAULT <mode> [param]\n → OK <mode> [param]\n or ERR <reason>\n
       - GET_FAULT\n → FAULT <mode> [param]\n
       - RESET\n → OK 0\n
     - Validate against fault whitelist from config

     tftptest_faultmode.h — expand enum to ~21 specific values:

     FAULT_NONE = 0,
     FAULT_RRQ_TIMEOUT,           // Item 1: No response to RRQ
     FAULT_WRQ_TIMEOUT,           // Item 2: No response to WRQ
     FAULT_FILE_NOT_FOUND,        // Item 3
     FAULT_PERM_DENIED_READ,      // Item 4
     FAULT_PERM_DENIED_WRITE,     // Item 5
     FAULT_MID_TIMEOUT_NO_DATA,   // Item 10: Stop sending DATA mid-transfer (RRQ)
     FAULT_MID_TIMEOUT_NO_ACK,    // Item 11: Stop sending ACK mid-transfer (WRQ)
     FAULT_MID_TIMEOUT_NO_FINAL_ACK,  // Item 12
     FAULT_MID_TIMEOUT_NO_FINAL_DATA, // Item 13
     FAULT_DUP_FINAL_DATA,        // Item 14 (param: n/a)
     FAULT_DUP_FINAL_ACK,         // Item 15
     FAULT_DUP_MID_DATA,          // Item 16 (param: block#)
     FAULT_DUP_MID_ACK,           // Item 17 (param: block#) — Sorcerer's Apprentice
     FAULT_SEND_ERROR_READ,       // Item 18 (param: error code 0-7)
     FAULT_SEND_ERROR_WRITE,      // Item 19 (param: error code 0-7)
     FAULT_SKIP_ACK,              // Item 20 (param: block#)
     FAULT_SKIP_DATA,             // Item 21 (param: block#)
     FAULT_OOO_DATA,              // Item 22 (param: block# to swap)
     FAULT_OOO_ACK,               // Item 23 (param: block# to swap)
     FAULT_INVALID_BLOCK_ACK,     // Item 24 (param: bad block#)
     FAULT_INVALID_BLOCK_DATA,    // Item 25 (param: bad block#)
     FAULT_DATA_TOO_LARGE,        // Item 26
     FAULT_DATA_LEN_MISMATCH,     // Item 27
     FAULT_INVALID_OPCODE_READ,   // Item 28
     FAULT_INVALID_OPCODE_WRITE,  // Item 29
     FAULT_INVALID_ERR_CODE_READ, // Item 30 (param: invalid code)
     FAULT_INVALID_ERR_CODE_WRITE,// Item 31 (param: invalid code)
     FAULT_WRONG_TID_READ,        // Item 32
     FAULT_WRONG_TID_WRITE,       // Item 33
     FAULT_MODE_COUNT

     Parameterized faults use the [param] field in the control protocol. Faults without params ignore it.

     Testing:

     - c/test/test_control_channel.c — send control messages, verify ACK responses
     - Integration script: set fault mode, run client, check behavior

     ---
     Phase 7: Fault Injection — Timeouts (Items 1, 2, 10-13)

     Goal: 6 timeout fault modes working.

     tftp_fsm.c:

     - Check active fault mode at FSM entry and at each send point
     - Helper: static bool fault_should_suppress_send(enum TFTPTest_FaultMode mode, enum TFTP_FSM_State state, uint16_t
      block_num, uint16_t param)
     - FAULT_RRQ_TIMEOUT / FAULT_WRQ_TIMEOUT: return immediately after parsing, no response
     - FAULT_MID_TIMEOUT_NO_DATA: stop sending after param blocks (or block 3 if no param)
     - FAULT_MID_TIMEOUT_NO_ACK: stop ACKing after param blocks
     - FAULT_MID_TIMEOUT_NO_FINAL_ACK: suppress final ACK only
     - FAULT_MID_TIMEOUT_NO_FINAL_DATA: suppress final <512-byte DATA

     Testing: For each fault, set mode, attempt transfer, verify client times out.

     ---
     Phase 8: Fault Injection — Duplicates (Items 14-17)

     Goal: Duplicate packet faults including Sorcerer's Apprentice.

     tftp_fsm.c:

     - After normal sendto, check fault mode. If duplicate requested for this block, sendto again.
     - FAULT_DUP_FINAL_DATA/FAULT_DUP_FINAL_ACK: duplicate the last packet
     - FAULT_DUP_MID_DATA/FAULT_DUP_MID_ACK: duplicate at param block number

     ---
     Phase 9: Fault Injection — Error Packets (Items 18-19)

     Goal: Server sends ERROR packets with specified error codes.

     tftp_fsm.c:

     - FAULT_SEND_ERROR_READ: After first successful DATA/ACK, send ERROR with param error code instead of next DATA
     - FAULT_SEND_ERROR_WRITE: After first DATA received, send ERROR with param error code instead of next ACK

     ---
     Phase 10: Fault Injection — Protocol Violations (Items 20-33)

     Goal: All remaining fault modes.

     tftp_fsm.c:

     - Pattern: fault_inject_modify_outgoing(sendbuf, &pkt_len, &dest_addr, state, block_num, fault_mode, fault_param)
     — mutates packet/address/length based on fault mode
     - Sequence faults (20-23): Skip sending, reorder blocks
     - Invalid block# (24-25): Overwrite block number in outgoing packet with param value
     - Invalid data size (26-27): Pad DATA beyond 512, or truncate
     - Invalid opcode (28-29): Overwrite opcode bytes with invalid value (e.g., 9)
     - Invalid error code (30-31): Send ERROR with code > 7
     - Wrong TID (32-33): Send from a different ephemeral socket

     Testing: Each fault needs at least one integration test verifying client sees expected behavior.

     ---
     Phase 11: Large Files and Edge Cases (Items 6-9)

     Goal: Block number wrap (>65535 blocks) and exact-multiple-of-512 files.

     tftp_fsm.c:

     - block_num is uint16_t — natural wrap at 65535→0
     - After last full 512-byte read, if next fread returns 0: send 0-byte DATA to signal completion

     Testing: Files of 512, 1024, 33553920 bytes (65535×512). Verify with diff.

     ---
     Phase 12: Security Hardening (chroot + Privilege Drop)

     Goal: Server can start as root, chroot into TFTP root, drop privileges.

     tftp_util.c — add:

     - int tftp_util_chroot_and_drop(const char *dir, const char *user):
       a. chdir(root_dir)
       b. chroot(".")
       c. chdir("/")
       d. setgid(nobody_gid), setuid(nobody_uid) via getpwnam()
       e. assert(getuid() != 0) after drop
       f. Close any FDs pointing outside the jail

     tftptest.c — call after config parse and socket setup, before main loop

     Testing: Run in Podman as root, verify cannot access files outside jail, running as nobody.

     ---
     Phase 13: CLI Arguments

     Goal: Server accepts CLI args for config path, verbosity, port override.

     tftptest.c — getopt() parsing:

     - -c <config_file> — config path
     - -v / -vvv — increase verbosity
     - -p <port> — override TFTP port
     - -h — help text
     - CLI flags override config file values

     ---
     Phase 14: Comprehensive Test Suite and Cleanup

     Goal: Full coverage, static analysis clean, sanitizer clean.

     Tasks:

     - make analyze — fix all findings
     - make coverage — identify uncovered branches, write tests
     - Valgrind run: valgrind --leak-check=full ./build/test/test_tftptest
     - Integration test script in c/scripts/ (Python):
       a. Start server
       b. Set each fault mode via control channel
       c. Run TFTP client transfer
       d. Verify expected behavior
       e. Repeat for all ~33 fault cases
     - Verify make release compiles with zero warnings (-Werror)
     - Include guards on all headers
     - Doxygen comments on all public functions

     ---
     Verification

     After each phase:
     1. make debug — compiles cleanly with both GCC and Clang
     2. make test — all unit tests pass under ASan + UBSan
     3. make analyze — no new warnings from static analyzers

     After Phase 14:
     4. make coverage — review MC/DC coverage report
     5. make release — zero warnings with -Werror
     6. Full integration test suite passes
     7. Valgrind clean

● Plan approved. Let me start with the pre-implementation bug fixes. I'll tackle all 4 in parallel since they're
  independent.
