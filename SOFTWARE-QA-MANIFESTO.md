# Software Quality Assurance Principles

## Code Quality & Robustness
- `assert()` throughout the code at any point assumption are made prior to line executions
- thorough set of compiler warnings
- multiple compilers
- multiple static analyzers
- unit tests + multiple sanitizers, striving for good _behavioral_ coverage _in addition to_ max possible [MC/DC](https://en.wikipedia.org/wiki/Modified_condition/decision_coverage) coverage
- integration tests (nominal/chaos monkey)
- fuzz'ing at the function
- inter-module
- and app level
- manual + multi-agent scans of codebase against common enumerated vulnerabilities
- manual + agent scan of codebase against for best-practice idiomatic constructs in language + library usage
- builds for multiple platforms
- over-night automated chaos-monkey runs across several server instances, some running /w different instrumentation (e.g., memory profilers)

## Architecture-Level Robustness
- Maximize encapsulation of data/functionality internal to a module
- Modules and the APIs they expose shall be internally orthogonal
