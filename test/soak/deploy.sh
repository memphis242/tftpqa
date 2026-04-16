#!/bin/bash
set -euo pipefail

echo "=== tftptest chaos monkey deployment ==="

# 1. Install Podman + podman-compose (Oracle Linux 9 / AlmaLinux 9 / Fedora)
if ! command -v podman &>/dev/null; then
    echo "Installing Podman..."
    sudo dnf install -y podman
fi
if ! command -v podman-compose &>/dev/null; then
    echo "Installing podman-compose..."
    sudo dnf install -y podman-compose || pip3 install --user podman-compose
fi

# 2. Create log directory
mkdir -p chaos-logs

# 3. Build container image
echo "Building container image..."
podman-compose build

# 4. Start all 8 monkeys
echo "Starting chaos monkeys..."
podman-compose up -d

# 5. Verify
echo ""
echo "=== Status ==="
podman ps --format "table {{.Names}}\t{{.Status}}\t{{.Created}}"
echo ""
echo "Logs: ./chaos-logs/cm{1-8}.log"
echo "Check: grep FAIL chaos-logs/*.log"
echo "Stop:  podman-compose down"
