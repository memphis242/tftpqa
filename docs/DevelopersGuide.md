# tftpqa Developer's Guide

## Overview & Development Priorities

This guide consolidates quality assurance, security, and performance principles for tftpqa development. The project prioritizes
in this order:

1. **Functionality**: Get the application/feature working correctly in a user-friendly manner
2. **Quality Assurance**: Ensure robustness through comprehensive testing, static analysis, code quality standards, and
                          software engineering best practices
3. **Security**: Harden against cybersecurity threats through threat analysis and protections within the code and UX
4. **Performance**: Optimize speed and space efficiency within the above constraints

---

## Part 1: Software Quality Assurance Principles

### Code Quality & Robustness

#### Readability and Maintainability
- Code readability, at least at a basic level (before stylistic preferences), is crucial for maintainability and quality
  - Whatever your style, be consistent throughout the codebase
  - Minimize how much readers need to track at any given moment (e.g., minimize indentation, `#ifdef` madness, etc.)
  - Avoid aggressively abbreviating identifiers to the point where nobody understands them

#### Assertions
- Use `assert()` throughout the code at any point assumptions are made, especially prior to lines that execute based on those assumptions
  - **Only** assert on conditions that are 100% under code control, such as calls to internal functions or internal state checks
  - **Do not** assert on external input (user CLI, file inputs, network inputs) until after it has been checked/sanitized;
               once sanitized, that "input" is considered internally controlled

#### Type Safety & Correctness
- Respect `const` correctness throughout the codebase
- Do not discard return values of system calls (except perhaps `printf`, which would clutter code and isn't necessarily a fatal error case)
- Do not return a single value for multiple possible events (e.g., all errors returning `-1`)
  - many-to-one mapping obscures what happened to the caller

#### Compiler & Static Analysis
- Employ a thorough (but carefully thought-through) set of compiler warnings
- Use multiple compilers (GCC primary, Clang diagnostic pass, potentially MSVC in the future)
- Run multiple static analyzers (GCC -fanalyzer, Clang --analyze, clang-tidy, cppcheck)

#### Testing & Coverage
- Unit tests with multiple sanitizers (ASan, UBSan, LSan), striving for good **behavioral** coverage in addition to maximum [MC/DC](https://en.wikipedia.org/wiki/Modified_condition/decision_coverage)
  - **Avoid coverage probing**: do not call functions without assertions just to meet coverage metrics
  - Do not modify private members of a module to meet coverage or test expectations; treat modules as black boxes and only access public interfaces
    - if a piece of internal code cannot be reached publicly through any input combination, it's likely dead code or requires
      mocking system call failure
  - Chasing coverage metrics should not be the highest priority; test against meaningful behavior expectations instead
    - (100% coverage does not mean your unit tests are good)
  - Even with 100% line/branch/condition coverage, if there is a realistic input to cover that happens with high probability,
    add it to the test suite!
  - _TODO: Develop a plan for handling syscall mocking in unit tests_

- Instrument integration tests with memory profilers (e.g., valgrind) to catch memory bugs
  - Do not use memory profilers for unit tests as they slow down test runs and discourage frequent testing

- Integration tests (nominal paths, chaos monkey scenarios)
- Fuzzing at function, inter-module, and application levels

#### Code Review & Analysis
- Manual (i.e., HUMAN) review of all changes
- Agentic AI scans of codebase
- Reviews should check code against common enumerated vulnerabilities (CWE/CVE)

#### Build & Deployment
- Build for multiple platforms
- **Soak testing**: for new releases, 24/7/4 (24 hours a day, 7 days a week, 4 weeks) automated chaos-monkey runs across multiple
                    server instances, some with different instrumentation (memory profilers, sanitizers, etc.)

### Architecture-Level Robustness

- **Maximize encapsulation**: Keep data/functionality internal to a module; expose only necessary interfaces
- **Orthogonal APIs**: Modules and their exposed APIs shall not have hidden dependencies or overlapping responsibilities

---

## Part 2: Software Security Development Plan

tftpqa is a network-facing TFTP server. In addition to standard quality assurance practices, the following security measures are employed:

### 1. Multi-Layer Fuzz Testing

#### In-Process Function Fuzzing
- Fuzz internal functions using `AFL++` or `libFuzzer` with ASan + UBSan
- Use test harnesses similar to unit tests for individual functions
- Build comprehensive seed corpora including:
  - Minimal RRQ/WRQ packets
  - Valid RRQ/WRQ with different mode strings and filenames
  - Invalid mode strings and filenames
  - Long filenames, path traversals
  - RRQ/WRQ with missing NUL terminators
  - Valid ERR packets with different error codes
  - ERR packets with missing NUL terminators and invalid error codes
  - Valid config file option combinations and invalid combinations
  - ACK packets with edge block numbers
  - DATA packets with varying lengths including edge sizes
  - Duplicate, truncated, and malformed packets
  - RRQ/WRQ/ERR packets with invalid ASCII encodings

#### Application-Level Fuzzing
- Fuzz the application as a whole via a network harness (TBD)

### 2. CVE/CWE Record Analysis

- Reference the [CWE (Common Weakness Enumeration) lists](https://cwe.mitre.org/data/index.html) and scan the codebase against each enumerated item
- Analyze any CVEs found against similar TFTP servers (e.g., `atftpd`, `tftpd-hpa`)
- Deep analysis of all system calls to enumerate attack vectors:
  - Example: For `chroot()` jail, verify correct usage (privilege drop after `chroot()`, close file descriptors pointing outside jail before `chroot()`, etc.)
  - Write automated test harnesses that replicate each identified attack vector

### 3. Agentic Security Analysis

- Use AI agents to scan source code for security vulnerabilities
- Generate and test different test cases against the codebase

### 4. Chaos Monkey Testing

- Nightly runs with chaos monkey clients on multiple server instances
- Each instance runs with different instrumentation (sanitizers, memory profilers, etc.)

### 5. Protect Against Application Misuse

- The application must prevent itself from being misused in ways that attackers can exploit
- Fault simulation modes must not result in harm to the host system or connecting TFTP clients

### 6. Dependency Scans

- If external library dependencies are added in the future, perform regular vulnerability scans on those libraries

---

## Part 3: Performance Principles

Software quality and robustness take precedence over performance. However, within those constraints, performance is optimized in this order: **speed first, then space efficiency**.

### Module/Function-Level Performance

- Benchmark every method/function and compare against alternative approaches that produce identical results
- The fastest approach wins; if performance is tied, choose the most space-efficient option
- Use data structures optimized for the specific use case

### Architecture-Level Performance

- Minimize the number of steps/jumps needed to complete any action
- Simplify and tighten the architecture whenever possible while prioritizing encapsulation

---
