# Research: Vulnerabilities Relevant to File I/O in tftptest

This document captures CWEs and CVEs that informed the design of file I/O
hardening in tftptest. It is a research summary — for the user policy and log
semantics, see `FILE-PERMISSIONS.md`.

## File Permissions Handling

### CWEs

- **CWE-276 — Incorrect Default Permissions**
  Newly created files should not silently inherit mode defaults that are more
  liberal than the user expects (e.g., world-readable when user wanted 0644).
  Addressed here by giving the user an explicit `new_file_mode` config knob
  (default `0666`) and verifying post-create via `fstat` that the filesystem
  honored the requested mode.
  <https://cwe.mitre.org/data/definitions/276.html>

- **CWE-277 — Insecure Inherited Permissions.**
  The process umask silently strips bits on create. Addressed by a startup
  diagnostic that compares `umask & ~new_file_mode` and emits a WARN when the
  umask reduces the user-configured mode. We deliberately do **not** call
  `umask(0)` — the user controls the process umask (via systemd `UMask=`,
  the invoking shell, etc.) and we respect that intent rather than override it.
  <https://cwe.mitre.org/data/definitions/277.html>

- **CWE-278 / CWE-279 / CWE-281 — Insecure / Incorrect / Improper Preserved Permissions.**
  When copying or rewriting files some tools "preserve" an upstream mode that
  is inappropriate for the destination context. Addressed by always applying
  the fixed `new_file_mode` on create (no preservation from any source) and by
  rejecting setuid/setgid/sticky bits in the configured value at config-load
  time.
  <https://cwe.mitre.org/data/definitions/278.html>,
  <https://cwe.mitre.org/data/definitions/279.html>,
  <https://cwe.mitre.org/data/definitions/281.html>

- **CWE-280 — Improper Handling of Insufficient Permissions.**
  Previously the WRQ path mapped `EACCES` to `ACCESS_VIOLATION` but the RRQ
  path silently folded every open failure into `FILE_NOT_FOUND`. Addressed by
  mapping errors consistently on both paths, and by logging the underlying
  reason (`not world-readable (mode=0%04o)`, etc.) so an user can diagnose
  the refusal without guesswork.
  <https://cwe.mitre.org/data/definitions/280.html>

- **CWE-378 — Creation of Temporary File With Insecure Permissions.**
  Not directly applicable — we do not create temp files — but the same
  "explicit mode + `O_EXCL` + post-create `fstat` verification" pattern is the
  canonical mitigation and is reused in our WRQ create path.
  <https://cwe.mitre.org/data/definitions/378.html>

- **CWE-552 — Files or Directories Accessible to External Parties.**
  Addressed by making the user's intent the gate: we refuse to serve
  anything not marked world-readable (RRQ) and refuse to overwrite anything
  not marked world-writable (WRQ). A file whose permissions don't say
  "anyone can read this" stays unreadable to the TFTP client.
  <https://cwe.mitre.org/data/definitions/552.html>

- **CWE-732 — Incorrect Permission Assignment for Critical Resource.**
  Addressed by the post-create `fstat` verification: if umask, mount options
  (e.g. `nosuid`), or an SELinux/AppArmor policy strips bits from the
  requested mode, a WARN fires so the user knows the configured intent
  was silently relaxed.
  <https://cwe.mitre.org/data/definitions/732.html>

- **CWE-250 — Execution with Unnecessary Privileges.**
  Mitigated separately by the existing chroot + privilege drop
  (`tftp_util_chroot_and_drop`), which is a prerequisite for this work making
  sense: without a drop to `nobody` and a chroot jail, a hostile filename
  could still reach privileged parts of the filesystem regardless of mode
  checks.
  <https://cwe.mitre.org/data/definitions/250.html>

### CVEs — lessons

- **CVE-2011-2199 (tftpd-hpa)** and **CVE-2019-11365 (atftpd)** — classic
  buffer overflow / input validation bugs in TFTP daemons. Not directly
  permission-related, but a reminder that any code path touching
  attacker-controlled input (filename, mode string) must be defensive.
  Relevance here: the new config-parsing path for `new_file_mode` is a small
  new input surface and is written with explicit bounds and strict octal
  parsing (rejects trailing garbage, out-of-range values, and setuid bits).

- **CVE-2009-2957 / CVE-2009-2958 (dnsmasq TFTP)** — heap overflow reachable
  over UDP. Lesson: chroot + privilege drop are necessary but not sufficient;
  hardening the file-access path beyond the jail boundary is worthwhile.

### Best-practice rules applied

- **Explicit mode on `O_CREAT`** rather than inherited defaults.
- **`fstat(fd)`** on the already-opened fd, not `stat(path)` before open
  (CERT POS35-C, FIO01-C).
- **Refuse setuid/setgid files** at read/write time
  (`st.st_mode & (S_ISUID | S_ISGID)`). A test TFTP server has no business
  serving or overwriting privileged executables.
- **Reject setuid/setgid/sticky in the configured `new_file_mode`** — the
  parser rejects any bits in `07000` with an `ERR` log.
- **Warn on umask-stripped mode** rather than silently applying a weaker
  permission set.

## File Symlinks Handling

### CWEs

- **CWE-22 — Path Traversal.**
  A malicious filename (`../../etc/passwd`) escaping the intended directory.
  Mitigated primarily by the existing chroot jail. Listed here because
  symlinks are a cousin of traversal attacks (a symlink inside the jail
  can be abused to pivot to an unexpected target).
  <https://cwe.mitre.org/data/definitions/22.html>

- **CWE-59 — Improper Link Resolution Before File Access ("Link Following").**
  If a path component is a symlink and we follow it, the target of our I/O
  can be redirected by whoever controls the link. Addressed by `O_NOFOLLOW`
  on every `open()` (fails with `ELOOP` if the final component is a symlink)
  plus a "refuse non-regular files" check on the opened fd.
  <https://cwe.mitre.org/data/definitions/59.html>

- **CWE-61 — UNIX Symbolic Link ("Symlink") Following.**
  The classic symlink-target attack. Same mitigation as CWE-59. Our posture
  is to **refuse** symlinks rather than attempt to validate them — simpler,
  race-free, and immune to the symlink-chain bypass patterns catalogued in
  the rsync CVEs below.
  <https://cwe.mitre.org/data/definitions/61.html>

- **CWE-73 — External Control of File Name or Path.**
  The client supplies the filename. Mitigated by chroot + filename validation
  (`tftp_util_is_valid_filename_char` rejects path separators) + the new
  symlink/non-regular refusals.
  <https://cwe.mitre.org/data/definitions/73.html>

- **CWE-243 — Creation of chroot Jail Without Changing Working Directory.**
  Already mitigated: `tftp_util_chroot_and_drop` does `chdir("/")` after
  `chroot(".")`. Relevant here because a sloppy chroot setup would make
  symlink refusal largely meaningless.
  <https://cwe.mitre.org/data/definitions/243.html>

- **CWE-367 — Time-of-Check / Time-of-Use (TOCTOU) Race Condition.**
  If you `stat` a path, then `open` it, an attacker can swap the path between
  the two calls. Addressed by doing every permission check on an
  already-opened fd via `fstat` — never `stat`-then-open. Combined with
  `O_NOFOLLOW` rejecting symlinks at the open itself, there is no
  check-then-act window to race.
  <https://cwe.mitre.org/data/definitions/367.html>

### CVEs — lessons

- **CVE-2024-12087 (rsync, `--inc-recursive` path traversal via symlinks).**
  De-duplication checks were scoped per-file-list, letting a malicious server
  write outside the client's destination via symlinks in intermediate path
  components. Lesson: symlink checks must account for every path component,
  not just the final one — which is why we also benefit from being inside a
  chroot (intermediate components can't escape the jail) rather than relying
  on `O_NOFOLLOW` alone.

- **CVE-2024-12088 (rsync `--safe-links` bypass via chained symlinks).**
  `--safe-links` didn't recurse through chained symlinks, so
  `A → B → ../etc/passwd` bypassed the check. Lesson: validating symlinks
  correctly is notoriously hard; **refusing symlinks entirely** (our design
  choice) sidesteps the whole class of bugs.

- **CVE-2024-12747 (rsync symlink TOCTOU race).**
  Attacker flips filesystem state between rsync's symlink check and its file
  access. Lesson: the `O_NOFOLLOW` flag must be on the same `open()` call
  that will be used for I/O — any separate check step is racy.

- **Historical Linux tftpd `../` traversal** (pre-chroot era) — reinforces
  that a proper chroot (which we have) is load-bearing for any network file
  server.

### Best-practice rules applied

- **`O_NOFOLLOW`** on every `open()` — fails with `ELOOP` if the final path
  component is a symlink. Final-component-only protection is acceptable here
  because we are inside a chroot (intermediate components always resolve
  within the jail) and we also refuse non-regular files.
- **`O_CLOEXEC`** prevents fd leakage if the server ever spawns a child
  process in the future.
- **`O_CREAT | O_EXCL`** in the create path gives atomic "create only if
  absent," closing the pre-create permission race (CERT FIO45-C) where an
  attacker pre-creates the target with liberal permissions and waits for the
  server to open and write into it.
- **Refuse symlinks rather than validate them.** CVE-2024-12088 is the
  canonical case study for why validation is a trap.

### Out of scope (noted for a future pass)

- **`openat()` with a long-lived root directory `dirfd`** would close the gap
  that an _intermediate-path_ symlink would reference a file outside of the
  chroot jail.
- Recall that `O_NOFOLLOW` only applies if the symlink is the basename of the
  filepath, but if a symlink is before the basename portion, the symlink is _still_
  followed (although, the chroot jail will end up blocking anyways)
- An example scenario is:
    - client-uploads symlink: `/tftproot/outsidelink` pointing to `/home/myfriend/`
    - tftp root directory is set at `/tftproot/`
    - without a chroot jail, that client _could_ then do a subsequent
      `PUT my_key /outsidelink/.ssh/authorized_keys` which would resolve to
      `/home/myfriend/.ssh/authorized_keys`
    - attacker has now made themselves one of the authorized users for SSH
- the `openat()` fix hasn't been implemented for the time being because the
  chroot jail does address the above scenario, and the added complexity is not
  welcome at the moment
