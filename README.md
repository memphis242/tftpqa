# tftptest

[tftptest-demo-test-sequence.webm](https://github.com/user-attachments/assets/afadd98a-8425-4d14-82e0-cc1af4b6a31c)

This is a TFTP (RFC 1350) _Test_ Server that allows developers to exhaustively integration test TFTP clients against a variety of fault scenarios (see full list at the bottom).

This server can also operate as a standard, nominal TFTP server. Note that for simplicity, this server will only answer to a single client at a time, and can be configured to answer to a _specific_ client as well (see `allowed_client_ip` config file option below), among _many_ other test configuration knobs.

## Basic Usage

### Build

- There are no external library dependencies
- Environment:
  - POSIX system
  - gcc is the default compiler
  - clang used for extra static analysis

```bash
# At any point, to print out a useful list of available make targets
make help

# To build release target
make release
```

### Starting the Server

Note, CLI options _override_ the config file's corresponding options.

```bash
# Run /w defaults: port 23069, "nobody" user, WARN lvl logging
tftptest

# Listen on port 6969
tftptest -p 6969
tftptest --port 6969

# Load configuration from file
tftptest -c config.ini
tftptest --config config.ini

# Drop privileges to user 'tftp-user'
tftptest -u tftp-user
tftptest --user tftp-user

# Verbosity control
tftptest -v   # ≥ INFO
tftptest -vv  # ≥ DEBUG
tftptest -vvv # ≥ TRACE (max logging)

# Enable logging to syslog
tftptest -s
tftptest --syslog

# Restrict session TID ports to a range (useful for packet capture filtering)
tftptest -r 50000:50100
tftptest --tid-range 50000:50100
```

### Configuration file (INI format):

Note, CLI options _override_ the config file's corresponding options.

```ini
# tftptest server port setup (there are two)
tftp_port = 23069
ctrl_port = 23070

# blank lines are fine # so are inline comments

# tftptest general setup
root_dir = /var/lib/tftpboot # the chroot() jail and where all files go to / come from
run_as_user = tftpuser # the default user to drop privileges to
timeout_sec = 5 # per-packet timeout
max_retransmits = 5 # max retries of DATA or ACK's

# logging
log_level = info # there's trace > debug > info > warn > error > fatal

# allowed fault simulations
fault_whitelist = 0xFFFFFFFFFFFFFFFF # bit masks of allowed fault modes (see table at TODO) - 0 for no fault simulations allowed

# Extra protections against malicious attackers
allowed_client_ip = 192.168.0.24    # 0 for no restrictions, specific IP otherwise
max_abandoned_sessions = 10         # Lock out all requests from a particular IP address after this many timed-out sessions (0 = unlimited)

# TID port range for session sockets (useful for packet capture filtering)
tid_port_range = 50000-50100        # Restrict session TIDs to ports 50000-50100 (omit for OS-assigned ephemeral)

# WRQ DoS protection
max_wrq_file_size = 500000000       # Per-file size limit in bytes (0 = unlimited)
max_wrq_session_bytes = 10000000000 # Cumulative WRQ bytes for entire server run (0 = unlimited)
max_wrq_duration_sec = 60           # Per-WRQ transaction maximum duration (0 = unlimited)
max_wrq_file_count = 500            # Max files written in this server run (0 = unlimited)
min_disk_free_bytes = 20000000000   # Reject WRQ if free disk < this (0 = no check)
wrq_enabled = 1                     # If false, reject all WRQ with ACCESS_VIOLATION
```

### Setting the Fault Simulation Mode (Two Options):

#### UDP Control Channel:

Server will listen on a "control" port to go off-nominal and into a particular fault simulation mode.  

From a separate terminal, for simple local-host control:

```bash
echo "SET_FAULT FAULT_RRQ_TIMEOUT" | nc -u localhost 23070
echo "GET_FAULT" | nc -u localhost 23070
echo "RESET" | nc -u localhost 23070
```

#### Test sequence file (`-t`):

Instead of controlling faults at runtime via UDP, you can specify a sequence of fault modes in a file, almost like a script. When a sequence file is specified, the UDP control channel is disabled and the server steps through the sequence automatically, shutting down gracefully when complete.

```bash
tftptest -t test_sequence.txt # Or...
tftptest --sequence test_sequence.txt
```

Sequence file format (one entry per line, `#` comments, blank lines ignored):

```
# Fields: mode=FAULT_NAME [param=N] [count=N]
# - mode: required (full enum name, case-insensitive)
# - param: optional, default 0
# - count: optional, default 1 (sessions this fault covers)

mode=FAULT_NONE              count=2             # 2 nominal transfers
mode=FAULT_RRQ_TIMEOUT       count=1             # then a timeout for the subsequent transfer session
mode=FAULT_CORRUPT_DATA      param=5   count=2   # then corrupt block 5 for 2 sessions
mode=FAULT_SLOW_RESPONSE     param=3000          # then apply 3 second delay for every response, 1 session
```

## Fault Simulation Modes

All 33 modes are supported in the test sequence file. Modes marked with `param` use the `param=N` field in the sequence entry.

| `FAULT_NAME` | Description |
|---|---|
| `FAULT_NONE` | Normal operation; no fault applied |
| **Timeout faults** | |
| `FAULT_RRQ_TIMEOUT` | No response to RRQ at all |
| `FAULT_WRQ_TIMEOUT` | No response to WRQ at all |
| `FAULT_MID_TIMEOUT_NO_DATA` | Stop sending DATA mid-transfer (`param` = block# at which to stop) |
| `FAULT_MID_TIMEOUT_NO_ACK` | Stop sending ACK mid-transfer (`param` = block# at which to stop) |
| `FAULT_MID_TIMEOUT_NO_FINAL_DATA` | Suppress the final short DATA packet (last block of RRQ) |
| `FAULT_MID_TIMEOUT_NO_FINAL_ACK` | Suppress the final ACK (after last DATA received on WRQ) |
| **Error response faults** | |
| `FAULT_FILE_NOT_FOUND` | Respond to RRQ with a FILE\_NOT\_FOUND error |
| `FAULT_PERM_DENIED_READ` | Respond to RRQ with ACCESS\_VIOLATION |
| `FAULT_PERM_DENIED_WRITE` | Respond to WRQ with ACCESS\_VIOLATION |
| `FAULT_SEND_ERROR_READ` | Send ERROR mid-transfer during RRQ (`param` = error code, 0–7) |
| `FAULT_SEND_ERROR_WRITE` | Send ERROR mid-transfer during WRQ (`param` = error code, 0–7) |
| **Duplicate faults** | |
| `FAULT_DUP_FINAL_DATA` | Re-send the final DATA packet (last block of RRQ) |
| `FAULT_DUP_FINAL_ACK` | Re-send the final ACK (WRQ completion) |
| `FAULT_DUP_MID_DATA` | Duplicate DATA at a specific block during RRQ (`param` = block#) |
| `FAULT_DUP_MID_ACK` | Duplicate ACK at a specific block during WRQ — Sorcerer's Apprentice (`param` = block#) |
| **Sequence / skip faults** | |
| `FAULT_SKIP_DATA` | Withhold DATA for one block during RRQ (`param` = block#) |
| `FAULT_SKIP_ACK` | Withhold ACK for one block during WRQ (`param` = block#) |
| `FAULT_OOO_DATA` | Send two adjacent DATA blocks out of order during RRQ (`param` = first block# of the swapped pair) |
| `FAULT_OOO_ACK` | Send two adjacent ACKs out of order during WRQ (`param` = first block# of the swapped pair) |
| **Invalid field faults** | |
| `FAULT_INVALID_BLOCK_DATA` | Send DATA with a wrong block number during RRQ (`param` = bogus block# to use) |
| `FAULT_INVALID_BLOCK_ACK` | Send ACK with a wrong block number during WRQ (`param` = bogus block# to use) |
| `FAULT_DATA_TOO_LARGE` | Send a DATA payload larger than 512 bytes |
| `FAULT_DATA_LEN_MISMATCH` | Send a DATA packet shorter than its declared length |
| `FAULT_INVALID_OPCODE_READ` | Send a packet with an invalid opcode during RRQ |
| `FAULT_INVALID_OPCODE_WRITE` | Send a packet with an invalid opcode during WRQ |
| `FAULT_INVALID_ERR_CODE_READ` | Send ERROR with an out-of-range code during RRQ (`param` = error code value) |
| `FAULT_INVALID_ERR_CODE_WRITE` | Send ERROR with an out-of-range code during WRQ (`param` = error code value) |
| **TID faults** | |
| `FAULT_WRONG_TID_READ` | Send packets from an unexpected source port during RRQ |
| `FAULT_WRONG_TID_WRITE` | Send packets from an unexpected source port during WRQ |
| **Timing faults** | |
| `FAULT_SLOW_RESPONSE` | Delay every response (`param` = delay in milliseconds) |
| **Payload faults** | |
| `FAULT_CORRUPT_DATA` | Flip bits in the DATA payload at a specific block (`param` = block#) |
| `FAULT_TRUNCATED_PKT` | Send a packet shorter than the minimum valid size |
| **Protocol violation faults** | |
| `FAULT_BURST_DATA` | Send N DATA packets in a row without waiting for ACK (`param` = burst count) |

## Diagnostics

### Netascii Mode Content Warnings

When a file is transferred in **netascii mode**, the server performs a diagnostic check to detect potential mode mismatches. If the file contains non-printable or unconventional control bytes that would not normally appear in plain text, a warning is logged:

- **RRQ (server reading)**: `FSM: Potential incorrect mode for this transfer — non-printable bytes found in source file`
- **WRQ (server writing)**: `FSM: Unexpected non-printable or unconventional control bytes found in received data`

This single-per-transfer warning helps identify cases where a file claimed to be text (netascii) is actually binary, which can cause corruption or unexpected behavior.

For details on what bytes are allowed, why certain control characters are rejected, and how to handle legacy files, see **[docs/NETASCII-DIAGNOSTICS.md](docs/NETASCII-DIAGNOSTICS.md)**.

## Future Plans

- Release `0.1.0` will be a functional product that has been thorougly reviewed, well tested, and demonstrated to work as intended.
- Release `0.2.0` will be a maximized unit test, soak test, and integration tested product.
- Release `0.3.0` will be a performance-optimized product (without sacrificing code quality and security).
- Release `0.4.0` will be CI-pipelined product.
- Release `0.5.0` will be post-comparison /w `tftp-hpa` and `atftpd`.
- Release `1.0.0` will be the first feature-complete release.
- Release `2.0.0` will be a multi-client upgrade to this test server.
- Release `2.1.0` will be an rpm-package distribution release.

## A Note On AI Assistance
This project was _written_ through a combination of me and LLMs.

However, _writing_ code is distinct from designing a software product, setting the ground rules for its development, and performing
the analysis, review, and iteration that take the product closer to an ideal goal. I take 100% credit for the latter. I have also done
plenty of direct, manual coding on this project, but opted to let the LLMs take a whirl here and there, giving me the ability to expand
my perspective and spend more mental energy on the program's architecture, user-friendliness, internal module's APIs and interactions,
the test and quality assurance suite I wanted to set up, and more. Furthermore, all code has been pain-stakingly reviewed by me, many times over.

With that said, for transparency, any commit whose content was primarily AI generated (but still reviewed by me) is cleared marked as such. AI
assistance _can_ be a tool for good, and getting greater things done, if we use it well. I have come to believe that after being a long-time
skeptic.
