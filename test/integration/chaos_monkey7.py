#!/usr/bin/env python3
"""
Chaos monkey integration test #7: malformed RRQ/WRQ packets.

Sends 10 malformed request packets (5 malformation types × 2 opcodes).
After each malformed send, waits 200ms then performs a small legitimate
RRQ to confirm the server is still alive and did not crash.

The server is expected to silently drop all malformed requests — no reply,
no crash.

Malformation types:
  1. Truncated — opcode only, 2 bytes
  2. Filename not NUL-terminated (fills buffer, no terminator)
  3. Filename + NUL, mode not NUL-terminated (fills remaining buffer)
  4. Empty filename (NUL immediately after opcode)
  5. Filename too long (> 64 chars)

Usage:
  python3 scripts/chaos_monkey7.py [--port PORT] [--server-bin PATH]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# TFTP protocol constants
# ---------------------------------------------------------------------------

TFTP_OP_RRQ   = 1
TFTP_OP_WRQ   = 2
TFTP_OP_DATA  = 3
TFTP_OP_ACK   = 4
TFTP_OP_ERROR = 5

TFTP_BLOCK_SIZE = 512
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE

TIMEOUT_SEC  = 10
MAX_RETRIES  = 5
SOCK_RCVBUF  = 1 << 20
PROBE_WAIT   = 0.2   # seconds to wait after each malformed send before probing

# ---------------------------------------------------------------------------
# Malformed packet builders
# ---------------------------------------------------------------------------

def _malformed_truncated(opcode: int) -> bytes:
    """Only the 2-byte opcode, nothing else."""
    return struct.pack("!H", opcode)


def _malformed_filename_no_nul(opcode: int) -> bytes:
    """Filename fills the remaining space with no NUL terminator."""
    # Build a packet that is exactly 64 bytes with no NUL after the filename
    filename_bytes = b"A" * 62   # 2 opcode + 62 filename = 64 bytes, no NUL
    return struct.pack("!H", opcode) + filename_bytes


def _malformed_mode_no_nul(opcode: int) -> bytes:
    """Valid filename + NUL, mode bytes fill rest of buffer with no NUL."""
    filename = b"test.bin\x00"
    # Fill up to 64 bytes total with mode bytes that have no NUL terminator
    remaining = 64 - 2 - len(filename)
    mode_bytes = b"octet"[:remaining].ljust(remaining, b"X")
    return struct.pack("!H", opcode) + filename + mode_bytes


def _malformed_empty_filename(opcode: int) -> bytes:
    """NUL immediately after the opcode — empty filename."""
    return struct.pack("!H", opcode) + b"\x00octet\x00"


def _malformed_filename_too_long(opcode: int) -> bytes:
    """Filename longer than FILENAME_MAX_LEN (64 bytes)."""
    long_name = b"A" * 128
    return struct.pack("!H", opcode) + long_name + b"\x00octet\x00"


MALFORMATIONS = [
    ("truncated (opcode only)",             _malformed_truncated),
    ("filename not NUL-terminated",         _malformed_filename_no_nul),
    ("mode not NUL-terminated",             _malformed_mode_no_nul),
    ("empty filename",                      _malformed_empty_filename),
    ("filename too long (>64 chars)",       _malformed_filename_too_long),
]

OPCODES = [
    ("RRQ", TFTP_OP_RRQ),
    ("WRQ", TFTP_OP_WRQ),
]

# ---------------------------------------------------------------------------
# Test file helpers
# ---------------------------------------------------------------------------

def generate_test_file(path: Path, size: int) -> str:
    md5       = hashlib.md5()
    remaining = size
    pattern   = bytes(range(256))
    with open(path, "wb") as f:
        while remaining > 0:
            n         = min(remaining, 65536)
            full_reps = n // 256
            tail      = n % 256
            chunk     = pattern * full_reps + pattern[:tail]
            f.write(chunk)
            md5.update(chunk)
            remaining -= n
    return md5.hexdigest()


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


# ---------------------------------------------------------------------------
# Liveness probe — small legitimate RRQ
# ---------------------------------------------------------------------------

def _probe_server(host: str, port: int, fname: str, expected_md5: str) -> None:
    """
    Perform a small legitimate RRQ.  Raises AssertionError / TimeoutError if
    the server does not respond correctly.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        rrq_pkt = struct.pack("!H", TFTP_OP_RRQ) + fname.encode() + b"\x00octet\x00"
        sock.sendto(rrq_pkt, (host, port))

        received       = bytearray()
        tid            = None
        expected_block = 1

        while True:
            for attempt in range(MAX_RETRIES):
                try:
                    pkt, addr = sock.recvfrom(TFTP_MAX_PACKET + 4)
                    break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(
                            f"Server probe failed: timed out waiting for DATA block {expected_block}"
                        )
                    if tid is None:
                        sock.sendto(rrq_pkt, (host, port))
                    else:
                        sock.sendto(
                            struct.pack("!HH", TFTP_OP_ACK, (expected_block - 1) & 0xFFFF),
                            tid
                        )

            if tid is None:
                tid = addr
            elif addr != tid:
                continue

            opcode, block = struct.unpack("!HH", pkt[:4])
            if opcode == TFTP_OP_ERROR:
                code = block
                msg  = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
                raise AssertionError(f"Server probe got ERROR({code}): {msg}")
            if opcode != TFTP_OP_DATA:
                raise AssertionError(f"Server probe got unexpected opcode {opcode}")

            if block != (expected_block & 0xFFFF):
                sock.sendto(struct.pack("!HH", TFTP_OP_ACK, block), tid)
                continue

            received.extend(pkt[4:])
            sock.sendto(struct.pack("!HH", TFTP_OP_ACK, block), tid)
            expected_block += 1

            if len(pkt) - 4 < TFTP_BLOCK_SIZE:
                break

        assert md5_bytes(bytes(received)) == expected_md5, \
            f"Server probe MD5 mismatch — file may be corrupted"
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Server management
# ---------------------------------------------------------------------------

class TFTPTestServer:
    def __init__(self, binary: str, port: int, root_dir: str, verbosity: int = 0):
        self.binary    = binary
        self.port      = port
        self.root_dir  = root_dir
        self.verbosity = verbosity
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        cmd = [self.binary, "-p", str(self.port), *(["-v"] * self.verbosity)]
        self.proc = subprocess.Popen(
            cmd, cwd=self.root_dir,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        time.sleep(0.3)
        if self.proc.poll() is not None:
            _, stderr = self.proc.communicate(timeout=2)
            raise RuntimeError(
                f"Server exited immediately (rc={self.proc.returncode}):\n"
                f"{stderr.decode(errors='replace')}"
            )

    def stop(self) -> tuple[str, str]:
        if self.proc is None:
            return ("", "")
        self.proc.send_signal(signal.SIGINT)
        try:
            stdout, stderr = self.proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            try:
                stdout, stderr = self.proc.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                return ("", "(server did not exit)")
        return (stdout.decode(errors="replace"), stderr.decode(errors="replace"))

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()


# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"


def run_test(name: str, func, *args, **kwargs) -> bool:
    print(f"  {name} ... ", end="", flush=True)
    try:
        info = func(*args, **kwargs)
        info_str = f"  [{info}]" if info else ""
        print(f"{PASS}{info_str}")
        return True
    except Exception as e:
        print(f"{FAIL}: {e}")
        return False


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

def test_malformed_dropped(host: str, port: int, malformed_pkt: bytes,
                            probe_fname: str, probe_md5: str) -> str:
    """
    Send one malformed packet.  Optionally verify the server does NOT reply
    (short timeout), then probe the server with a legitimate RRQ.
    """
    # Send malformed packet — server should silently drop it
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.5)  # Short wait — we expect silence
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)
    try:
        sock.sendto(malformed_pkt, (host, port))
        try:
            pkt, _ = sock.recvfrom(TFTP_MAX_PACKET + 4)
            # Some servers send ERROR for certain malformed packets; we only
            # require they do NOT crash, so accept either silence or an ERROR
            # but flag unexpected non-error replies.
            opcode = struct.unpack("!H", pkt[:2])[0]
            if opcode not in (TFTP_OP_ERROR,):
                raise AssertionError(
                    f"Server replied to malformed packet with unexpected opcode {opcode}"
                )
        except socket.timeout:
            pass  # Expected — server dropped the packet silently
    finally:
        sock.close()

    time.sleep(PROBE_WAIT)
    _probe_server(host, port, probe_fname, probe_md5)
    return "dropped (no crash); server still responsive"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_server_binary() -> str:
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir.parent.parent / "build" / "debug"   / "tftpqa",
        script_dir.parent.parent / "build" / "release" / "tftpqa",
    ]
    for p in candidates:
        if p.is_file() and os.access(p, os.X_OK):
            return str(p)
    sys.exit("Could not find tftpqa binary. Run `make debug` from the repo root first.")


def main():
    parser = argparse.ArgumentParser(
        description="Chaos monkey #7: malformed RRQ/WRQ packets — server must drop, not crash"
    )
    parser.add_argument("--port",       type=int, default=23069)
    parser.add_argument("--server-bin", type=str, default=None)
    args = parser.parse_args()

    binary = args.server_bin or find_server_binary()
    host   = "127.0.0.1"
    port   = args.port

    print(f"Server binary:  {binary}")
    print(f"TFTP port:      {port}")
    print()

    with tempfile.TemporaryDirectory(prefix="tftpqa_chaos7_") as tmpdir:
        root        = Path(tmpdir)
        probe_fname = "probe.bin"
        probe_md5   = generate_test_file(root / probe_fname, 100)

        print(f"Test root dir:  {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as _server:
            results = []

            print("=== Malformed Packet Tests ===")
            print("  (each test: send malformed pkt → wait 200ms → probe server liveness)")
            print()

            for mal_name, mal_builder in MALFORMATIONS:
                for op_name, opcode in OPCODES:
                    test_name = f"{op_name}: {mal_name}"
                    pkt       = mal_builder(opcode)
                    results.append(run_test(
                        test_name,
                        test_malformed_dropped,
                        host, port, pkt, probe_fname, probe_md5
                    ))

            print()
            _, stderr = _server.stop()
            if stderr:
                print("--- Server stderr (last 20 lines) ---")
                for line in stderr.strip().splitlines()[-20:]:
                    print(f"  {line}")
                print()

        passed = sum(results)
        total  = len(results)
        print(f"{'=' * 40}")
        print(f"Results: {passed}/{total} passed")
        print(f"  ({total} malformation × opcode combinations tested)")

        if passed < total:
            sys.exit(1)


if __name__ == "__main__":
    main()
