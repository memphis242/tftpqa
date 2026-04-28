#!/usr/bin/env python3
"""
Chaos monkey integration test #2: random delays before each ACK/DATA.

During RRQ (GET), the client waits a random amount of time before sending each
ACK, up to just under the server's receive timeout.

During WRQ (PUT), the client waits the same random delay before sending each
DATA packet.

The transfer must still complete successfully — the server must be patient
enough to wait for a slow client without timing out.

Test matrix (matches test_nominal.py):
  RRQ: small (~100 B), medium (~50 KB), large (65535×512 B), extralarge (65536×512+100 B)
  WRQ: small (~100 B), medium (~50 KB), large (65535×512 B), extralarge (65536×512+100 B)

Usage:
  python3 scripts/chaos_monkey2.py [--port PORT] [--server-bin PATH]
                                   [--max-delay-sec N] [--seed N]
                                   [--skip-large]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import random
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

DEFAULT_MAX_DELAY_SEC = TIMEOUT_SEC - 0.5  # Stay just under server timeout

# ---------------------------------------------------------------------------
# TFTP packet helpers
# ---------------------------------------------------------------------------

class TFTPError(Exception):
    def __init__(self, code: int, msg: str):
        super().__init__(f"TFTP error {code}: {msg}")
        self.code = code
        self.msg  = msg


def _build_rrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_RRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _build_wrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_WRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _build_ack(block: int) -> bytes:
    return struct.pack("!HH", TFTP_OP_ACK, block & 0xFFFF)


def _parse_data(pkt: bytes) -> tuple[int, bytes]:
    if len(pkt) < 4:
        raise ValueError(f"DATA packet too short ({len(pkt)} bytes)")
    opcode, block = struct.unpack("!HH", pkt[:4])
    if opcode == TFTP_OP_ERROR:
        msg = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
        raise TFTPError(block, msg)
    if opcode != TFTP_OP_DATA:
        raise ValueError(f"Expected DATA (opcode 3), got opcode {opcode}")
    return block, pkt[4:]


def _parse_ack(pkt: bytes) -> int:
    if len(pkt) < 4:
        raise ValueError(f"ACK packet too short ({len(pkt)} bytes)")
    opcode, block = struct.unpack("!HH", pkt[:4])
    if opcode == TFTP_OP_ERROR:
        msg = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
        raise TFTPError(block, msg)
    if opcode != TFTP_OP_ACK:
        raise ValueError(f"Expected ACK (opcode 4), got opcode {opcode}")
    return block


# ---------------------------------------------------------------------------
# Slow TFTP clients
# ---------------------------------------------------------------------------

def slow_tftp_get(host: str, port: int, filename: str,
                  max_delay_sec: float) -> tuple[bytes, float]:
    """
    Download via RRQ, inserting a random delay before each ACK.
    Returns (file_bytes, total_delay_injected_seconds).
    """
    total_delay = 0.0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        sock.sendto(_build_rrq(filename), (host, port))

        data           = bytearray()
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
                            f"Timed out waiting for DATA block {expected_block}"
                        )
                    if tid is None:
                        sock.sendto(_build_rrq(filename), (host, port))
                    else:
                        sock.sendto(_build_ack(expected_block - 1), tid)

            if tid is None:
                tid = addr
            elif addr != tid:
                continue

            block, payload = _parse_data(pkt)

            if block != (expected_block & 0xFFFF):
                sock.sendto(_build_ack(block), tid)
                continue

            data.extend(payload)

            # Inject delay before sending ACK
            delay = random.uniform(0.0, max_delay_sec)
            time.sleep(delay)
            total_delay += delay

            sock.sendto(_build_ack(block), tid)
            expected_block += 1

            if len(payload) < TFTP_BLOCK_SIZE:
                break

        return bytes(data), total_delay
    finally:
        sock.close()


def slow_tftp_put(host: str, port: int, filename: str, content: bytes,
                  max_delay_sec: float) -> float:
    """
    Upload via WRQ, inserting a random delay before each DATA send.
    Returns total_delay_injected_seconds.
    """
    total_delay = 0.0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        sock.sendto(_build_wrq(filename), (host, port))

        tid = None
        for attempt in range(MAX_RETRIES):
            try:
                pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                break
            except socket.timeout:
                if attempt == MAX_RETRIES - 1:
                    raise TimeoutError("Timed out waiting for ACK 0")
                sock.sendto(_build_wrq(filename), (host, port))
        tid = addr
        if _parse_ack(pkt) != 0:
            raise ValueError("Expected ACK 0")

        block  = 1
        offset = 0

        while True:
            chunk    = content[offset : offset + TFTP_BLOCK_SIZE]
            data_pkt = struct.pack("!HH", TFTP_OP_DATA, block & 0xFFFF) + chunk

            # Inject delay before sending DATA
            delay = random.uniform(0.0, max_delay_sec)
            time.sleep(delay)
            total_delay += delay

            for attempt in range(MAX_RETRIES):
                sock.sendto(data_pkt, tid)
                try:
                    while True:
                        pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                        if addr != tid:
                            continue
                        ack_block = _parse_ack(pkt)
                        if ack_block == (block & 0xFFFF):
                            break
                    break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(f"Timed out waiting for ACK {block}")

            offset += len(chunk)
            block  += 1

            if len(chunk) < TFTP_BLOCK_SIZE:
                break

        return total_delay
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Test file generation
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


def make_content(size: int) -> bytes:
    pattern   = bytes(range(256))
    full_reps = size // 256
    tail      = size % 256
    return pattern * full_reps + pattern[:tail]


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
# RRQ test cases
# ---------------------------------------------------------------------------

def test_slow_rrq_small(host, port, root, max_delay):
    fname = "small.bin"
    expected_md5 = generate_test_file(root / fname, 100)
    data, total_delay = slow_tftp_get(host, port, fname, max_delay)
    assert len(data) == 100,              f"Expected 100 bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_rrq_medium(host, port, root, max_delay):
    fname = "medium.bin"
    size  = 50 * 1024
    expected_md5 = generate_test_file(root / fname, size)
    data, total_delay = slow_tftp_get(host, port, fname, max_delay)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_rrq_large(host, port, root, max_delay):
    fname = "large.bin"
    size  = 65535 * TFTP_BLOCK_SIZE
    expected_md5 = generate_test_file(root / fname, size)
    data, total_delay = slow_tftp_get(host, port, fname, max_delay)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_rrq_extralarge(host, port, root, max_delay):
    fname = "extralarge.bin"
    size  = 65536 * TFTP_BLOCK_SIZE + 100
    expected_md5 = generate_test_file(root / fname, size)
    data, total_delay = slow_tftp_get(host, port, fname, max_delay)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return f"total delay injected: {total_delay:.1f}s"


# ---------------------------------------------------------------------------
# WRQ test cases
# ---------------------------------------------------------------------------

def test_slow_wrq_small(host, port, root, max_delay):
    fname   = "upload_small.bin"
    content = make_content(100)
    total_delay = slow_tftp_put(host, port, fname, content, max_delay)
    written = (root / fname).read_bytes()
    assert len(written) == 100, f"Expected 100 bytes on disk, got {len(written)}"
    assert written == content,  "Content mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_wrq_medium(host, port, root, max_delay):
    fname   = "upload_medium.bin"
    size    = 50 * 1024
    content = make_content(size)
    total_delay = slow_tftp_put(host, port, fname, content, max_delay)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_wrq_large(host, port, root, max_delay):
    fname   = "upload_large.bin"
    size    = 65535 * TFTP_BLOCK_SIZE
    content = make_content(size)
    total_delay = slow_tftp_put(host, port, fname, content, max_delay)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return f"total delay injected: {total_delay:.1f}s"


def test_slow_wrq_extralarge(host, port, root, max_delay):
    fname   = "upload_extralarge.bin"
    size    = 65536 * TFTP_BLOCK_SIZE + 100
    content = make_content(size)
    total_delay = slow_tftp_put(host, port, fname, content, max_delay)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return f"total delay injected: {total_delay:.1f}s"


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
        description="Chaos monkey #2: random delays before each ACK (RRQ) and DATA (WRQ)"
    )
    parser.add_argument("--port",          type=int,   default=23069)
    parser.add_argument("--server-bin",    type=str,   default=None)
    parser.add_argument("--max-delay-sec", type=float, default=DEFAULT_MAX_DELAY_SEC,
                        help=f"Max delay in seconds before each ACK/DATA (default {DEFAULT_MAX_DELAY_SEC:.1f})")
    parser.add_argument("--seed",          type=int,   default=None)
    parser.add_argument("--skip-large",    action="store_true",
                        help="Skip large/extralarge tests (very slow with delays)")
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    binary    = args.server_bin or find_server_binary()
    host      = "127.0.0.1"
    port      = args.port
    max_delay = max(0.0, args.max_delay_sec)

    print(f"Server binary:    {binary}")
    print(f"TFTP port:        {port}")
    print(f"Max delay:        {max_delay:.2f}s per ACK/DATA")
    if args.seed is not None:
        print(f"Random seed:      {args.seed}")
    print()

    with tempfile.TemporaryDirectory(prefix="tftpqa_chaos2_") as tmpdir:
        root = Path(tmpdir)
        print(f"Test root dir:    {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as _server:
            results = []

            print("=== RRQ (Read) Tests — random delay before each ACK ===")
            results.append(run_test("RRQ small (100 B)",    test_slow_rrq_small,      host, port, root, max_delay))
            results.append(run_test("RRQ medium (50 KB)",   test_slow_rrq_medium,     host, port, root, max_delay))
            if not args.skip_large:
                results.append(run_test("RRQ large (65535×512 B)",       test_slow_rrq_large,      host, port, root, max_delay))
                results.append(run_test("RRQ extralarge (65536×512+100 B)", test_slow_rrq_extralarge, host, port, root, max_delay))
            else:
                print("  RRQ large ... SKIPPED")
                print("  RRQ extralarge ... SKIPPED")

            print()
            print("=== WRQ (Write) Tests — random delay before each DATA ===")
            results.append(run_test("WRQ small (100 B)",    test_slow_wrq_small,      host, port, root, max_delay))
            results.append(run_test("WRQ medium (50 KB)",   test_slow_wrq_medium,     host, port, root, max_delay))
            if not args.skip_large:
                results.append(run_test("WRQ large (65535×512 B)",       test_slow_wrq_large,      host, port, root, max_delay))
                results.append(run_test("WRQ extralarge (65536×512+100 B)", test_slow_wrq_extralarge, host, port, root, max_delay))
            else:
                print("  WRQ large ... SKIPPED")
                print("  WRQ extralarge ... SKIPPED")

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

        if passed < total:
            sys.exit(1)


if __name__ == "__main__":
    main()
