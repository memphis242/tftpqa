# Software Quality Assurance Principles

## Code Quality & Robustness
- code readability, at least at a basic level (before people's stylistic preferences),
  is very important for maintainability and quality
  - whatever your style, be consistent
  - minimize how much readers have to track at any given moment (e.g., minimize necessary indentation, #ifdef madness, etc.)
  - don't aggressively abbreviate to the point that nobody knows what your abbreviations are

- `assert()` throughout the code at any point assumption are made, especially
  prior to lines that execute based on those assumptions
  - **only** assert on conditions that are 100% in the control of the code, such
    as calls made to internal functions or internal state
  - **do not** assert on external input (user CLI, file inputs, network inputs)
    until after it has been checked/sanitized - after which that "input" is
    considered internally controlled

- respect `const` correctness

- do not discard the return values of system calls (except maybe printf.. cuz
  that'd really clutter up the code and it's highly unlikely it matters too much)
- do not return a single value for multiple possible events (e.g., all errors cause
  -1 to be returned); many-to-one mapping obscures what happened to the caller

- thorough set of compiler warnings
- multiple compilers
- multiple static analyzers

- unit tests + multiple sanitizers, striving for good _behavioral_ coverage
  _in addition to_ max possible [MC/DC](https://en.wikipedia.org/wiki/Modified_condition/decision_coverage)
  - no "coverage probing"!!! (i.e., calling a module's functions without any
    assertions, just to meet coverage metrics)
  - do not modify private members of a module in order to meet coverage or test;
    the module is a black box; if you can't modify private members via public
    interfaces, neither can the rest of your code or other apps (unless you're
    trying to handle solar storms lol)
  - chasing the coverage metric should not be the highest priority - this leads
    to bad behaviors and anti-patterns; instead, test your unit against meaningful
    behavioral expectations.
  - even if we have 100% line/branch/condition coverage for a function, but there
    is input we want to make sure it handles, we should add unit tests for those
    extra cases, even without the coverage metric benefit

- if doing any dynamic memory allocation, have memory profiler instrumentation
  (e.g., valgrind, dr.memory) for integration tests that catch memory bugs not
  caught elsewhere
  - not for unit tests because slowing down unit tests discourages good behavior
    (running unit tests often)

- integration tests (nominal/chaos monkey)

- fuzzing at the function lvl
- fuzzing inter-module lvl
- fuzzing app lvl

- manual + multi-agent scans of codebase against common enumerated vulnerabilities
- manual + agent scan of codebase against for best-practice idiomatic constructs in language + library usage

- builds for multiple platforms

- **soak testing**: over-night automated chaos-monkey runs across several server instances, some running /w different instrumentation (e.g., memory profilers)

## Architecture-Level Robustness
- Maximize encapsulation of data/functionality internal to a module
- Modules and the APIs they expose shall be orthogonal
