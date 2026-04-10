# Software Security Development Plan
In addition to my usual quality-assurance combo (multiple compilers, high warning levels, multiple static analyzers, multiple sanitizers, linters, unit tests with good coverage, runs with valgrind/dr.memory, etc.), since I'm building a network facing application here, I'll add on three measures of cybersecurity assurance /w this application:

1. **Multi-Layer Fuzz Testing**
- Fuzzing the internal functions of the application _in-process_
    - `AFL++` or `libFuzzer` /w asan + ubsan
    - Individual functions at a time, tested /w a test harness similar to unit tests
- Fuzzing the application as a whole via a network harness (TBD)
- Some important things to keep in mind are that the seed corpora should include:
    - minimal RRQ/WRQ
    - valid RRQ/WRQ /w different mode strings and filenames
    - invalid RRQ/WRQ mode strings
    - invalid RRQ/WRQ filenames
    - RRQ/WRQ /w long filenames
    - RRQ/WRQ /w filenames that include traversals
    - RRQ/WRQ /w missing nul terminators
    - valid ERR packets /w different error codes
    - ERR packets /w missing nul terminators
    - ERR packets /w invalid error codes
    - valid cfg file option combinations
    - invalid cfg file options
    - ACK Packets /w edge block numbers
    - DATA packets /w varying lengths, including at edge sizes
    - duplicate packets
    - truncated packets
    - RRQ/WRQ/ERR packets /w invalid ASCII encodings

2. **CVE/CWE Record Analysis**
- Referencing the [CWE (Common Weakness Enumeration) lists](https://cwe.mitre.org/data/index.html), develop either manual or automated tooling to scan the codebase against each enumerated item.
- Look at any and all CVEs that were found against `atftpd`, `tftpd-hpa`
- Take a deep look at all your system calls and enumerate the ways they can be abused by an attacker. With that list, iterate on the design and write automated test harnesses that replicate that attack vector.
    - For example, /w a `chroot()` jail, make sure the app is doing it right (e.g., dropping privileges after `chroot()`, closing any open file descriptors prior to `chroot()` that point outside of the jail, etc.).

3. **Agentic Security Analysis**
- There is no denying that AI agents have advanced enough to be valuable tools in analyzing source code for security vulnerabilities, so simply running an agent for a long period of time against the code base to:
    - scan the code for vulnerabilities
    - try to write different tests to check the code base against

4. **Chaos Monkey Instances**
- Nightly runs against a chaos monkey client on multiple server instances, each /w different instrumentation (sanitizers, memory profilers, etc.).

5. **Protect Against Application Misuse**
- The application should do its best to prevent itself from getting used in a way that a malicious attacker can take advantage of.
- The fault simulation modes this TFTP test server supports should not result in harm to the host system or the connecting TFTP client.
