## Soak Test Infrastructure (Chaos Monkey 24/7)

For release qualification, all 8 chaos monkey scripts (`scripts/chaos_monkey{1-8}.py`) run 24/7 for at least one month in parallel Podman containers on an Oracle Cloud Free Tier ARM instance (4 cores, 24 GB RAM).

**Container setup:**
- `Containerfile`: Multi-stage build — `almalinux/9-base` (builder, discarded) → `almalinux/9-minimal` (runtime: Python 3 + tftptest binary)
- `entrypoint.sh`: Loops a single chaos monkey indefinitely, logging PASS/FAIL with timestamps and run counts; full output dumped only on failure
- `podman-compose.yml`: 8 services (`chaos-1` through `chaos-8`), each on a unique port, shared log volume, auto-restart
- `deploy.sh`: One-shot VPS bootstrap (install podman, build image, start containers)

**Coverage:** ~1 million test cycles per month across all 8 monkeys (CM1: random duplication, CM2: random delays, CM3: exhaustive duplication, CM4: endless data, CM5: request floods, CM6: invalid block numbers, CM7: malformed packets, CM8: Sorcerer's Apprentice bug detection).

**Operations:** SSH + log files. `grep FAIL chaos-logs/*.log` for failures. Redeploy per release: `podman-compose down && git pull && podman-compose build --no-cache && podman-compose up -d`.
