# File Permissions Policy

tftpqa enforces an intentional file-permissions policy on every TFTP read (RRQ)
and write (WRQ). The goal is that the user controls which files in the TFTP root
directory are exposed to clients. Any violations are upheld and reported.

## Summary

| Operation | Server policy |
|---|---|
| **GET (RRQ)** | Serve a file only if it is a regular, non-setuid, world-readable file (`S_IROTH`). |
| **PUT (WRQ) — overwriting existing** | Allow only if the existing file is regular, non-setuid, and world-writable (`S_IWOTH`). |
| **PUT (WRQ) — creating new** | Open with `O_CREAT \| O_EXCL` and a configurable mode (`new_file_mode`, default `0666`). Post-create `fstat` verifies the filesystem honored the requested mode (wasn't overridden by umask). |
| **Symlinks** (any path) | Refused via `O_NOFOLLOW`; the server never follows a symlink as the final path component. |
| **Non-regular files** (dir, fifo, block, char, socket) | Refused. |
| **Setuid / setgid files** | Refused for both RRQ and WRQ. |

#### Why are setuid files auto-rejected?
It would be highly unusual to have any setuid programs in the TFTP root directory;
but _suppose_ there were.
- we probably shouldn't serve the file up to get GET'd
- a malicious attacker could overwrite the file to whatever they want, and anyone
  on the server host system that executes the file later on could trigger whatever
  the intended result is.

## The `new_file_mode` config knob

```ini
# Mode for newly created files (WRQ). Parsed as OCTAL (base 8).
# Default: 0666 (world-readable + world-writable, so a later PUT can
# overwrite and a later GET can retrieve).
new_file_mode = 0666
```

Rules enforced by the parser:

- Parsed strictly as octal (base 8). `0666`, `666`, and `0666   ` are all valid.
- Trailing garbage is rejected. `0666abc` fails.
- Values above `0777` are rejected.
- **Setuid (`S_ISUID`), setgid (`S_ISGID`), and sticky (`S_ISVTX`) bits are
  rejected at config-load** (i.e., any bit in `07000`). This is a hard refusal,
  logged at ERR level, but the server continues /w the the default `0666` mode.
- Note that if any errors are detected during cfg parsing, defaults are fallen
  back to and a warning is issued post-parse processing.

## Umask Interaction

Recall that umask is a Linux/UNIX process attribute that determines which permission
bits should be "turned off" (i.e., masked away) when new files or directories are
created by the process, potentially overriding a requested, more permissive,
permission set. For example, if one does `open( fname, flags, 0666 )` but the
umask is `0022` (a very common umask setting), the resulting permission attributes
of the file will actually be `0666 & ~0022 = 0644`.

tftpqa does **not** override the process umask. When the server starts, it reads
the current umask and logs a `WARN` if the umask would strip bits from the
configured `new_file_mode`:

```
[WARN] Process umask 0022 strips bits from configured new_file_mode 0666;
       newly created files will have mode 0644. To preserve the configured
       mode, set UMask=0 in the service unit (or unset it from the invoking
       shell).
```

#### _The Rationale_
- the user deploys this server inside their environment (via some init something
  like systemd)
- overriding the umask from within the program would be a surprise that fights
  the user's system configuration, which may be undesirable
- the diagnostic logging is there so that the user can reconcile the two (e.g.,
  set their init-system's umask to `0011` or perhaps call `umask` in a script
  before starting the server)
- this is _different_ from the popular `tftp-hpa` `tftpd` server, that _does_ by
  default do a `umask()` call to override the process's `umask()` - user has to
  pass in `--permissive / -p` to prevent that

As an example, in systemd:

```ini
[Service]
ExecStart=/usr/local/bin/tftpqa -c /etc/tftpqa.conf
UMask=0000
```

Or in a shell:

```bash
umask 0011 # only turn off execute bits for group/world and allow read/write
```

## Log Lines to Expect

| Condition | Level | Example |
|---|---|---|
| Symlink refusal (ELOOP on open) | WARN | `FSM: Refusing to follow symlink for RRQ: 'foo'` |
| Non-regular file | WARN | `FSM: Refusing RRQ for non-regular file 'bar' (mode=040755)` |
| Setuid/setgid file | WARN | `FSM: Refusing RRQ for setuid/setgid file 'baz' (mode=104755)` |
| RRQ target not world-readable | INFO | `FSM: RRQ refused; 'secret.txt' is not world-readable (mode=0640)` |
| WRQ target not world-writable | INFO | `FSM: WRQ refused; 'ro.txt' is not world-writable (mode=0644)` |
| Post-create mode mismatch | WARN | `FSM: Created 'new.txt' with mode 0644 but requested 0666 (umask, filesystem policy, or SELinux stripped bits)` |
| Umask strips configured mode at startup | WARN | `Process umask 0022 strips bits from configured new_file_mode 0666; ...` |
| `open`, `fstat`, or `fdopen` system-call failure | ERR | `FSM: fstat on 'x' failed: ENOENT (2) : No such file or directory` |

User-visible refusals (INFO) are intentionally quieter than security events
(WARN/ERR) — an INFO is "the user set it up this way, this is working as configured,"
whereas a WARN is "something unexpected happened that you should probably know about."

## Wire-level error codes

All refusals send the client a TFTP ERROR packet:

- `ACCESS_VIOLATION` (code 2) for permission and symlink refusals.
- `FILE_NOT_FOUND` (code 1) for `ENOENT` on RRQ.
- `DISK_FULL` (code 3) for `ENOSPC` on WRQ.
- `NOT_DEFINED` (code 0) for unexpected system-call failures (`fstat`, `fdopen`, etc.).

## Implementation notes

- All permission checks operate on an already-opened file descriptor (`fstat(fd, ...)`),
  never on a path (`stat(path, ...)`).
  - This eliminates the TOCTOU window where an attacker could swap a path
    between check and open.
- Every `open()` uses `O_NOFOLLOW | O_CLOEXEC`.
  - `O_NOFOLLOW` prevents a symbolic link from being pursued (if the symlink is
    the basename)
  - symbolic links don't break a chroot-jail, but this feature just provides that
    extra layer of paranoid protection
  - `O_CLOEXEC` prevents the file from remaining open across an `execve()`, although
    this server does **not** `execve()` (nor should it **ever**); but it's a low-cost
    extra layer of paranoid protection against future non-sense
- The WRQ `open()` path additionally uses `O_EXCL`
  - atomically makes sure that the file doesn't already exist before trying to
    create it, or error-exits out /w `EEXIST`
  - this is as opposed to checking if a file exists separately from creating it,
    leading to a classic TOCTOU scenario:
    - program checks whether the file exists or not independent of creation (e.g., `stat()`)
    - sees that the file doesn't exist and does an `open(..., OCREAT ...)`
    - before file is created, attacker creates a file with desired permissions (e.g., `0777`)
    - `open(..., O_CREAT ...)` call succeeds, but file will have attacker's permissions
      because `O_CREAT` on its own will ignore the `mode` argument if the file
      already exists
  - in case of `EEXIST`, we will try one more time in case another process
    created a temporary file before our first `open()`
      - likely won't work but it's an attempt to satisfy the user, and is low-cost,
        so, why not
- The permission check runs once at session kickoff, before any DATA blocks are
  sent or received — so a transfer that starts will complete without additional
  permission re-checks mid-stream.

For the CWE / CVE background behind these choices, see
[`RESEARCH-VULNERABILITIES.md`](./RESEARCH-VULNERABILITIES.md).
