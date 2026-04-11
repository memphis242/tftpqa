# tftptest

A TFTP (RFC 1350) test server that behaves like a nominal TFTP server but through messages on a control port, it will simulate a chosen fault as part of thorough testing of your TFTP client. This server will only answer to a single client at a time, and can be configured to answer to a specific client.

## Basic Usage

### Starting the Server

Note, CLI options override config file.

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

tftptest -s                  # Log to syslog
tftptest --syslog            # Log to syslog
```

### Configuration file (INI format):

Note, CLI options override config file.

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
# WRQ DoS protection
max_wrq_file_size = 500000000       # Per-file size limit in bytes (0 = unlimited)
max_wrq_session_bytes = 10000000000 # Cumulative WRQ bytes for entire server run (0 = unlimited)
max_wrq_duration_sec = 60           # Per-WRQ transaction maximum duration (0 = unlimited)
max_wrq_file_count = 500            # Max files written in this server run (0 = unlimited)
min_disk_free_bytes = 20000000000   # Reject WRQ if free disk < this (0 = no check)
wrq_enabled = 1                     # If false, reject all WRQ with ACCESS_VIOLATION
```

### Set fault mode via control channel (UDP):

Server will listen on a "control" port to go off-nominal and into a particular fault simulation mode.  

From a separate terminal, for simple local-host control:

```bash
echo "SET_FAULT FAULT_RRQ_TIMEOUT" | nc -u localhost 23070
echo "GET_FAULT" | nc -u localhost 23070
echo "RESET" | nc -u localhost 23070
```

## Fault Simulation Modes

The server supports 33 parameterized fault injection modes for testing TFTP client robustness:

1. RRQ timeout
2. WRQ timeout
3. File not found (RRQ)
4. Permission denied (RRQ)
5. Permission denied (WRQ)
6. Mid-transfer timeout: no DATA on read
7. Mid-transfer timeout: no ACK on write
8. Mid-transfer timeout: no final ACK on write
9. Mid-transfer timeout: no final DATA on read
10. Duplicate final DATA packet (read)
11. Duplicate final ACK packet (write)
12. Duplicate DATA packet mid-transfer (read)
13. Duplicate ACK packet mid-transfer (write) [Sorcerer's Apprentice]
14. Error packet mid-transfer with code 0-7 (read)
15. Error packet mid-transfer with code 0-7 (write)
16. Skip ACK for block N (write)
17. Skip DATA for block N (read)
18. DATA packets out-of-order (read)
19. ACK packets out-of-order (write)
20. Invalid block number in ACK (write)
21. Invalid block number in DATA (read)
22. Invalid DATA packet size (too large)
23. Invalid DATA packet size (payload mismatch)
24. Invalid opcode in packet (read)
25. Invalid opcode in packet (write)
26. Invalid error code in ERROR packet (read)
27. Invalid error code in ERROR packet (write)
28. Wrong transfer ID: incorrect source port (read)
29. Wrong transfer ID: incorrect source port (write)
30. Slow/delayed response (param: delay in ms)
31. Corrupt DATA payload (param: block number)
32. Truncated packet (send packet shorter than minimum valid size)
33. Burst DATA: send multiple DATA packets without waiting for ACK (param: burst count)
