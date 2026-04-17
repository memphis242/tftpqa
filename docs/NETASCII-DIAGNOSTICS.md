# Netascii Mode Diagnostics

## Overview

When a TFTP transfer is initiated in **netascii mode**, the tftptest server performs a diagnostic check on the file content to detect potential mode mismatches. If the file contains suspicious bytes — those that would not normally appear in a plain text file — a warning is logged.

## What is Netascii Mode?

Netascii is a text encoding defined in RFC 764 and used by TFTP (RFC 1350) for platform-independent transfer of text files. The key purpose is to normalize line endings across systems:

- **Unix/Linux/macOS** use `LF` (0x0A) for line endings
- **Windows** uses `CR+LF` (0x0D 0x0A) for line endings
- **Legacy Mac OS** (pre-OS X) used bare `CR` (0x0D) for line endings

When transmitting a file, netascii mode results in the following translations to a _"canonical" form_ (i.e., the common format over the wire):
- Bare `LF` → transmitted as `CR+LF`
- Bare `CR` → transmitted as `CR+NUL` (0x0D 0x00)
- `CR+LF` → transmitted as `CR+LF` (unchanged)

When receiving in netascii mode, the reverse translation is applied as appropriate for that host system.

### CR+NUL: The Special Sequence

`CR+NUL` is a legitimate netascii encoding that represents a **bare CR** in the original file. This two-byte sequence is recognized and allowed by the diagnostic:

- **On the wire** (netascii): `0x0D 0x00` (CR followed by NUL)
- **In the file** (after translation): `0x0D` (bare CR)

This allows proper round-trip transfer of files that contain carriage returns without line feeds — a convention from legacy systems or specialized file formats.

### How Does a User Enter a Bare CR?

In modern text editors, bare CR characters are rarely entered intentionally because:

1. Most modern systems (Windows, macOS, Linux) use `LF` or `CR+LF` line endings exclusively
2. Text editors typically don't expose the ability to insert a bare CR without a following LF
3. Bare CR files are mostly artifacts from legacy Mac OS (pre-2001) systems

To create a file with bare CRs, a user would need to:

- Use a specialized hex editor to insert the byte manually
- Use a legacy Mac OS system (which used bare CR as the line ending)
- Use a scripting language or tool that explicitly creates such bytes
- Transfer a file from a legacy system that originally used bare CR

Example in Python:
```python
with open('legacy.txt', 'wb') as f:
    f.write(b'Line 1\rLine 2\rLine 3')  # Three lines separated by bare CR
```

## Allowed Control Characters

The diagnostic allows the following non-printable ASCII characters in netascii files:

| Byte | Name | Reason |
|------|------|--------|
| 0x09 | HT (Horizontal Tab) | Extremely common for indentation in source code and formatted text |
| 0x0A | LF (Line Feed) | Essential for line breaks on Unix/Linux/macOS |
| 0x0B | VT (Vertical Tab) | Occasionally used in formatted documents for logical breaks |
| 0x0C | FF (Form Feed) | Used to indicate page breaks in formatted or printed text |
| 0x0D | CR (Carriage Return) | Essential for Windows line endings and RFC 764 netascii encoding |
| 0x1B | ESC (Escape) | Universal ANSI escape sequences (colors, formatting in terminal output, logs, etc.) |

All other control characters (0x01–0x08, 0x0E–0x1A, 0x1C–0x1F, 0x7F, 0x80–0xFF) are flagged as suspicious.

## Why Other Control Characters Are Rejected

**Archaic or Non-Text Control Bytes:**

- **BEL (0x07)**: Bell/alert — archaic, almost never intentional in text
- **BS (0x08)**: Backspace — not a structural part of text files
- **SO/SI (0x0E–0x0F)**: Shift out/in — ancient encoding switches, never in plain text
- **DLE, DC1–DC4, NAK, SYN, ETB, CAN, EM, SUB (0x10–0x1A)**: All flow control markers or encoding flags from ancient protocols; not found in modern text
- **FS, GS, RS, US (0x1C–0x1F)**: Structural separators (file, group, record, unit separators); these mark data boundaries, not content
- **DEL (0x7F)**: Delete character — archaic
- **0x80–0xFF**: Non-ASCII bytes — indicate binary data or a different character encoding (e.g., UTF-8, Latin-1, etc.), not plain text

**NUL (0x00):**

- A bare NUL is a string terminator in C and many languages
- Its presence in a file almost always indicates binary data or corruption
- Only allowed when immediately preceded by CR (`CR+NUL`), which is the netascii encoding for a bare CR
- If a standalone NUL is encountered, it suggests the file is binary and should not be transferred in netascii mode

## Diagnostic Warnings

When the server detects suspicious bytes during a netascii transfer, it logs a warning **once per transfer** (not per block):

### RRQ (Read Request — Server Sending a File)

```
[WARN] ... FSM: Potential incorrect mode for this transfer — non-printable bytes found in source file
```

This indicates the local file being read contains bytes incompatible with netascii. The client requested netascii mode, but the file appears to be binary or use a non-text encoding.

### WRQ (Write Request — Server Receiving a File)

```
[WARN] ... FSM: Unexpected non-printable or unconventional control bytes found in received data
```

This indicates the incoming data stream (in netascii format) contains suspicious bytes. Either the client sent binary data in netascii mode, or the data was corrupted in transit.

## When to Expect Warnings

### False Positives (Rare)

- Files with ANSI escape sequences (colored terminal output, logs) will NOT trigger warnings because ESC (0x1B) is allowed
- Files with tabs and varied line endings will NOT trigger warnings
- Files transferred from legacy Mac OS systems (with bare CR) will NOT trigger warnings, as `CR+NUL` is allowed

### True Positives (Should Investigate)

- Binary files (executables, images, archives) transferred in netascii mode
- Files with embedded NUL bytes not preceded by CR
- Text files using non-ASCII or multi-byte encodings (UTF-8 with BOM, UTF-16, etc.)

## Recommendations

1. **Use octet mode for binary files**: Always transfer non-text files with `mode octet`
2. **Use netascii for plain text**: Reserve netascii for `.txt`, `.c`, `.py`, `.sh`, `.md`, and similar plain text files
3. **Verify encoding**: If transferring files from non-ASCII sources, ensure they are properly encoded as plain ASCII or UTF-8 (7-bit clean)
4. **Legacy systems**: If receiving files from old Mac OS systems, netascii is the correct mode; the `CR+NUL` warning will NOT occur

## Implementation Details

The diagnostic is implemented in `src/tftp_util.c` via `tftp_util_has_suspicious_text_bytes()`, which scans a buffer byte-by-byte and returns true if any suspicious bytes are found. The check occurs:

- **RRQ**: After reading raw bytes from the file, before octet-to-netascii translation
- **WRQ**: After receiving netascii bytes from the network, before netascii-to-octet translation

The session tracks whether a warning has been issued via a `netascii_warned` flag to ensure at most one warning per transfer.
