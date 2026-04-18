#!/usr/bin/env python3
"""
Chaos monkey integration test #8: Sorcerer's Apprentice (SA) bug test.

Verifies the server does NOT have the Sorcerer's Apprentice bug:

RRQ variant:
  After each DATA block arrives, send ACK N twice (duplicate ACK).
  Track how many times each block number is seen arriving from the server.
  Assert: every block received exactly once (no duplicate DATA induced).
  Assert: final content MD5 matches.

WRQ variant:
  Send each DATA block twice (duplicate DATA).
  Assert: file written to disk has the exact expected size and content.
  (SA bug would cause extra data to be appended.)

Test matrix: small/medium/large/extralarge × RRQ + WRQ (8 tests).

Usage:
  python3 scripts/chaos_monkey8.py [--port PORT] [--server-bin PATH]
                                   [--skip-large]
"""

from __future__ import annotations

import argparse
import collections
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

TIMEOUT_SEC = 10
MAX_RETRIES = 5
SOCK_RCVBUF = 1 << 20

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
# SA-test TFTP clients
# ---------------------------------------------------------------------------

def sa_tftp_get(host: str, port: int, filename: str) -> tuple[bytes, dict[int, int]]:
    """
    Download via RRQ.  After receiving each DATA block, send its ACK *twice*.
    Returns (file_bytes, block_receive_counts) where the dict maps
    block_number → how many times that block was received.
    A correct server should have every value == 1.
    """
    block_counts: dict[int, int] = collections.defaultdict(int)
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
            block_counts[block] += 1

            if block != (expected_block & 0xFFFF):
                # Out-of-order or SA-induced duplicate DATA — ACK it and skip
                sock.sendto(_build_ack(block), tid)
                continue

            data.extend(payload)

            # Send ACK twice to trigger potential SA bug
            ack = _build_ack(block)
            sock.sendto(ack, tid)
            sock.sendto(ack, tid)

            expected_block += 1

            if len(payload) < TFTP_BLOCK_SIZE:
                # Drain any duplicate DATA the server might (incorrectly) send
                # after the duplicate ACK, with a short window.
                sock.settimeout(1.0)
                while True:
                    try:
                        extra_pkt, extra_addr = sock.recvfrom(TFTP_MAX_PACKET + 4)
                        if extra_addr != tid:
                            continue
                        extra_block, _ = _parse_data(extra_pkt)
                        block_counts[extra_block] += 1
                    except socket.timeout:
                        break
                break

        return bytes(data), dict(block_counts)
    finally:
        sock.close()


def sa_tftp_put(host: str, port: int, filename: str, content: bytes) -> int:
    """
    Upload via WRQ.  Send each DATA block twice.
    Returns the number of extra DATA packets sent (== number of blocks).
    """
    extras = 0
    sock   = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
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

            for attempt in range(MAX_RETRIES):
                # First attempt: send DATA twice to trigger SA bug if present
                if attempt == 0:
                    sock.sendto(data_pkt, tid)
                    sock.sendto(data_pkt, tid)
                    extras += 1
                else:
                    sock.sendto(data_pkt, tid)

                # Drain ACKs until we get the one for this block
                try:
                    while True:
                        pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                        if addr != tid:
                            continue
                        ack_block = _parse_ack(pkt)
                        if ack_block == (block & 0xFFFF):
                            break
                        # Stale ACK — discard
                    break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(f"Timed out waiting for ACK {block}")

            offset += len(chunk)
            block  += 1

            if len(chunk) < TFTP_BLOCK_SIZE:
                break

        return extras
    finally:
        sock.close()


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

def test_sa_rrq(host, port, root, fname, size):
    expected_md5 = generate_test_file(root / fname, size)
    data, counts = sa_tftp_get(host, port, fname)

    # Content check
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"

    # SA bug check: no block should appear more than once
    dup_blocks = {b: c for b, c in counts.items() if c > 1}
    assert not dup_blocks, \
        f"SA bug detected — duplicate DATA blocks received: {dup_blocks}"

    return f"0 duplicate DATA blocks; {len(counts)} unique blocks received"


def test_sa_rrq_small(host, port, root):
    return test_sa_rrq(host, port, root, "small.bin", 100)


def test_sa_rrq_medium(host, port, root):
    return test_sa_rrq(host, port, root, "medium.bin", 50 * 1024)


def test_sa_rrq_large(host, port, root):
    return test_sa_rrq(host, port, root, "large.bin", 65535 * TFTP_BLOCK_SIZE)


def test_sa_rrq_extralarge(host, port, root):
    return test_sa_rrq(host, port, root, "extralarge.bin", 65536 * TFTP_BLOCK_SIZE + 100)


# ---------------------------------------------------------------------------
# WRQ test cases
# ---------------------------------------------------------------------------

def test_sa_wrq(host, port, root, fname, size):
    content = make_content(size)
    extras  = sa_tftp_put(host, port, fname, content)
    written = (root / fname).read_bytes()

    assert len(written) == size, \
        f"SA bug detected — expected {size} bytes on disk, got {len(written)}"
    assert written == content, \
        "Content mismatch (file may contain doubled data)"

    return f"{extras} extra DATA packet(s) sent; file size exact match"


def test_sa_wrq_small(host, port, root):
    return test_sa_wrq(host, port, root, "upload_small.bin", 100)


def test_sa_wrq_medium(host, port, root):
    return test_sa_wrq(host, port, root, "upload_medium.bin", 50 * 1024)


def test_sa_wrq_large(host, port, root):
    return test_sa_wrq(host, port, root, "upload_large.bin", 65535 * TFTP_BLOCK_SIZE)


def test_sa_wrq_extralarge(host, port, root):
    return test_sa_wrq(host, port, root, "upload_extralarge.bin", 65536 * TFTP_BLOCK_SIZE + 100)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_server_binary() -> str:
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir.parent.parent / "build" / "debug"   / "tftptest",
        script_dir.parent.parent / "build" / "release" / "tftptest",
    ]
    for p in candidates:
        if p.is_file() and os.access(p, os.X_OK):
            return str(p)
    sys.exit("Could not find tftptest binary. Run `make debug` from the repo root first.")


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Chaos monkey #8: Sorcerer's Apprentice — "
            "verify server sends no duplicate DATA (RRQ) and writes no extra bytes (WRQ)"
        )
    )
    parser.add_argument("--port",       type=int, default=23069)
    parser.add_argument("--server-bin", type=str, default=None)
    parser.add_argument("--skip-large", action="store_true",
                        help="Skip large/extralarge tests")
    args = parser.parse_args()

    binary = args.server_bin or find_server_binary()
    host   = "127.0.0.1"
    port   = args.port

    print(f"Server binary:  {binary}")
    print(f"TFTP port:      {port}")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_chaos8_") as tmpdir:
        root = Path(tmpdir)
        print(f"Test root dir:  {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as _server:
            results = []

            print("=== RRQ Tests — ACK sent twice; expect 0 duplicate DATA ===")
            results.append(run_test("RRQ small (100 B)",    test_sa_rrq_small,      host, port, root))
            results.append(run_test("RRQ medium (50 KB)",   test_sa_rrq_medium,     host, port, root))
            if not args.skip_large:
                results.append(run_test("RRQ large (65535×512 B)",          test_sa_rrq_large,      host, port, root))
                results.append(run_test("RRQ extralarge (65536×512+100 B)", test_sa_rrq_extralarge, host, port, root))
            else:
                print("  RRQ large ... SKIPPED")
                print("  RRQ extralarge ... SKIPPED")

            print()
            print("=== WRQ Tests — DATA sent twice; expect exact file size on disk ===")
            results.append(run_test("WRQ small (100 B)",    test_sa_wrq_small,      host, port, root))
            results.append(run_test("WRQ medium (50 KB)",   test_sa_wrq_medium,     host, port, root))
            if not args.skip_large:
                results.append(run_test("WRQ large (65535×512 B)",          test_sa_wrq_large,      host, port, root))
                results.append(run_test("WRQ extralarge (65536×512+100 B)", test_sa_wrq_extralarge, host, port, root))
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
