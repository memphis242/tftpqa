## Soak Test Infrastructure (Chaos Monkey 24/7)

For release qualification, all chaos monkey scripts run 24/7 for at least one month in parallel Podman containers on an Oracle Cloud Free Tier ARM instance (4 cores, 24 GB RAM).

## Container System Setup
- `Containerfile`: Multi-stage build — `almalinux/9-base` (builds the tftptest server, then discards its base img) → `almalinux/9-minimal` (runtime: Python 3 + tftptest binary)
- `entrypoint.sh`: Loops a single chaos monkey indefinitely, logging PASS/FAIL with timestamps and run counts; full output dumped only on failure
- `podman-compose.yml`: Multiple services (`chaos-1` through `chaos-n`), each on a unique port, shared log volume, auto-restart
- `deploy.sh`: One-shot VPS bootstrap (install podman, build image, start containers)

## Coverage
~1 million test cycles per month across all monkeys (CM1: random duplication, CM2: random delays, CM3: exhaustive duplication, CM4: endless data, CM5: request floods, CM6: invalid block numbers, CM7: malformed packets, CM8: Sorcerer's Apprentice bug detection).

## Concept of Operations
- SSH into the server manually and inspect the log files.
   - Future Planned: Implement a basic REST API on the server that a desktop application (probably TUI, because I'm built different) makes requests and displays them conveniently for me to browse through.
   - For a quick glance, `grep FAIL chaos-logs/*.log` gives us failures.
- Redeployment (every release): `podman-compose down && git pull && podman-compose build --no-cache && podman-compose up -d`.
