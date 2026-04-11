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

# blank lines are fine

# TFTP root directory (make sure the TFTP user the server drops to has permissions)
root_dir = /var/lib/tftpboot # inline comments are fine
timeout_sec = 5
max_retransmits = 5
log_level = info # there's trace > debug > info > warn > error > fatal
fault_whitelist = 0xFFFFFFFFFFFFFFFF # bit masks of allowed fault modes (see table at TODO)
allowed_client_ip = 192.168.0.24 # 0 for no restrictions, specific IP otherwise
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
6. Large files: 65535 × 512 bytes (read)
7. Large files: 65535 × 512 bytes (write)
8. File size exact multiple of 512 (read)
9. File size exact multiple of 512 (write)
10. Mid-transfer timeout: no DATA on read
11. Mid-transfer timeout: no ACK on write
12. Mid-transfer timeout: no final ACK on write
13. Mid-transfer timeout: no final DATA on read
14. Duplicate final DATA packet (read)
15. Duplicate final ACK packet (write)
16. Duplicate DATA packet mid-transfer (read)
17. Duplicate ACK packet mid-transfer (write) [Sorcerer's Apprentice]
18. Error packet mid-transfer with code 0-7 (read)
19. Error packet mid-transfer with code 0-7 (write)
20. Skip ACK for block N (write)
21. Skip DATA for block N (read)
22. DATA packets out-of-order (read)
23. ACK packets out-of-order (write)
24. Invalid block number in ACK (write)
25. Invalid block number in DATA (read)
26. Invalid DATA packet size (too large)
27. Invalid DATA packet size (payload mismatch)
28. Invalid opcode in packet (read)
29. Invalid opcode in packet (write)
30. Invalid error code in ERROR packet (read)
31. Invalid error code in ERROR packet (write)
32. Wrong transfer ID: incorrect source port (read)
33. Wrong transfer ID: incorrect source port (write)
