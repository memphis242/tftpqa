# Software Quality Assurance Principles

## Code Quality & Robustness
- `assert()` throughout the code at any point assumption are made prior to line executions
  - **only** assert on conditions that are 100% in the control of the code, such as calls made to internal functions
  - **do not** assert on external input (user CLI, file inputs, network inputs)
- thorough set of compiler warnings
- multiple compilers
- multiple static analyzers
- unit tests + multiple sanitizers, striving for good _behavioral_ coverage _in addition to_ max possible [MC/DC](https://en.wikipedia.org/wiki/Modified_condition/decision_coverage) coverage
  - no "coverage probing"!!! (i.e., calling a module's functions without any assertions, just to meet coverage metrics)
  - do not modify private members of a module in order to meet coverage or test; the module is a black box, and whatever edge cases aren't caught can be reasoned about rationally
  - chasing the coverage metric should not be the highest priority - this leads to bad behaviors and anti-patterns; instead, test your unit against meaningful behavioral expectations.
  - even if we have 100% line/branch/condition coverage for a function, but there is input we want to make sure it handles, we should add unit tests for those extra cases, even without the coverage metric benefit
- integration tests (nominal/chaos monkey)
- fuzz'ing at the function
- inter-module
- and app level
- manual + multi-agent scans of codebase against common enumerated vulnerabilities
- manual + agent scan of codebase against for best-practice idiomatic constructs in language + library usage
- builds for multiple platforms
- **soak testing**: over-night automated chaos-monkey runs across several server instances, some running /w different instrumentation (e.g., memory profilers)

## Architecture-Level Robustness
- Maximize encapsulation of data/functionality internal to a module
- Modules and the APIs they expose shall be internally orthogonal
