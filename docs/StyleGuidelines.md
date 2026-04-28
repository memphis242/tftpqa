# Style Guidelines

This document defines the coding conventions and style practices for the tftpqa project.

## Naming Conventions

### Module and Function Prefixes

- `tftp_`
    - TFTP protocol-specific module/function
    - Examples:
        - `tftp_fsm.c`
        - `tftp_pkt.c`
        - `tftp_err.c`

- `tftpqa_`
    - App-specific (non-protocol) module/function
    - Examples:
        - `tftpqa_log.c`
        - `tftpqa_parsecfg.c`
        - `tftpqa_util.c`
        - `tftpqa_whitelist.c`
        - `tftpqa_ctrl.c`
        - `tftpqa_seq.c`

#### Test Files

- Test source files: `test/test_MODULE.c`

### Static Variables

Prefix `static` variables with `s_`.

### Case Conventions

- use lowercase + underscores for module names, functions, and variables
- use UPPER_CASE for macro constants
- use PascalCase for data types

### Name Choice
- lean towards brevity over long names, as long as the meaning is still clear
    - `rc` over `return_code`
    - `err` over `error`
    - `val` over `value`
    - `img` over `image`
    - `mgr` over `manager`

### Network Byte Order (NBO)

When working with network-order data, include `_nbo` suffix to make the byte order explicit:

```c
uint32_t ip_nbo;                    // Network byte order
uint32_t ip_host = ntohl(ip_nbo);   // Host byte order
```

## Code Style

### Spacing
- make the code "vim-able" - allow empty lines for '{' / '}' navigation
- almost always, empty line before `if`, `for`, `while`
- function arguments and parameter lists
    - 1 argument: no space around parentheses is fine
    - 2+ arguments: add space after `(` and before `)` for readability
    - If 2+ parameters, consider putting each on a separate line, but keep the
      first argument on the same line as the function name

```c
// One argument — fine either way
tftpqa_log_init(false);
tftpqa_log_init( false );

// Two arguments — add space for readability
tftpqa_util_set_recv_timeout( sfd, timeout );

// Many arguments — line break for each (keep first on same line)
int tftpqa_util_create_udp_socket_in_range(uint16_t port_min,
                                             uint16_t port_max,
                                             struct sockaddr_in *bound_addr);
```

### Comments

- Document return codes in function header comments
- Keep function header comments succinct - don't expose internal details
- Prefer good names and clear code over comments
- Don't reference the current task/issue in code comments; use commit messages instead
- Only add comments when the _why_ is non-obvious (hidden constraint, subtle invariant, workaround for a specific bug)
- Never document _what_ the code does; well-named identifiers already do that

**Good comment** — Explains why:
```c
// Lazy-allocate blacklist on first block() call to avoid memory overhead
// for servers with no blocked IPs
if ( s_blacklist.addrs_nbo == NULL ) { ... }
```

**Bad comment** — Explains what (obvious from code):
```c
// Check if blacklist is NULL  ← This is obvious, don't write it
if ( s_blacklist.addrs_nbo == NULL ) { ... }
```

### Include Order

Standard headers → System headers → Internal headers. Blank line between groups.

```c
// Standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// System headers (POSIX, Linux)
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// Internal headers
#include "tftpqa_log.h"
#include "tftp_pkt.h"
```

## Terminology

- **"fault simulation"** or **"fault emulation"** is more appropriate than _"fault injection"_
