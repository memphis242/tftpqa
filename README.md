# tftptest

A TFTP (RFC 1350) test server with fault simulation capabilities for thorough testing of your TFTP client.

## Basic Usage

### Starting the Server

```bash
tftptest                # Run /w defaults, like port 23069 and "nobody" user
tftptest -p 6969        # Listen on port 6969
tftptest -c config.ini  # Load configuration from file
tftptest -u tftp        # Drop privileges to user 'tftp'
tftptest -vv            # Verbose output (repeat for more)
tftptest -s             # Log to syslog
```

### Configuration file (INI format):

```ini
tftp_port = 23069
ctrl_port = 23070
root_dir = /var/lib/tftpboot
timeout_sec = 5
max_retransmits = 5
log_level = info
fault_whitelist = 0xFFFFFFFFFFFFFFFF
```

### Set fault mode via control channel (UDP):

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
