#!/usr/bin/env python3
"""
Chaos monkey integration test #1: random packet duplication.

During RRQ (GET), the client randomly sends 2–20 duplicate ACK packets after
each normal ACK, simulating a lossy or misbehaving network that echoes packets.

During WRQ (PUT), the client randomly sends 2–20 duplicate DATA packets after
each normal DATA send, simulating the same.

Both directions must complete successfully — the server is expected to handle
RFC 1350 duplicate packets (re-ACK on duplicate DATA, re-DATA on duplicate ACK)
without corrupting the transfer.

Test matrix (matches test_nominal.py):
  RRQ: small (~100 B), medium (~50 KB), large (65535×512 B), extralarge (65536×512+100 B)
  WRQ: small (~100 B), medium (~50 KB), large (65535×512 B), extralarge (65536×512+100 B)

Usage:
  python3 scripts/chaos_monkey1.py [--port PORT] [--server-bin PATH]
                                   [--dup-probability P] [--seed N]
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
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE   # DATA header (4) + payload (512)

TIMEOUT_SEC  = 10
MAX_RETRIES  = 5
SOCK_RCVBUF  = 1 << 20  # 1 MB receive buffer

DEFAULT_DUP_PROBABILITY = 0.30
MIN_DUP_COUNT           = 1
MAX_DUP_COUNT           = 20

# ---------------------------------------------------------------------------
# TFTP packet builders / parsers
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
    """Return (block_number, payload) or raise TFTPError / ValueError."""
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
    """Return block_number or raise TFTPError / ValueError."""
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
# Chaos helpers
# ---------------------------------------------------------------------------

class DupStats:
    """Tracks duplication events across a single transfer."""

    def __init__(self):
        self.events  = 0   # Number of packets that triggered duplication
        self.extras  = 0   # Total extra copies sent

    def record(self, n: int) -> None:
        self.events += 1
        self.extras += n

    def __str__(self) -> str:
        return f"{self.events} dup event(s), {self.extras} extra packet(s) sent"


def _maybe_dup(sock: socket.socket, pkt: bytes, dest: tuple,
               prob: float, stats: DupStats) -> None:
    """With probability `prob`, send pkt to dest an additional 2–20 times."""
    if random.random() >= prob:
        return
    n = random.randint(MIN_DUP_COUNT, MAX_DUP_COUNT)
    for _ in range(n):
        sock.sendto(pkt, dest)
    stats.record(n)


# ---------------------------------------------------------------------------
# Chaos TFTP client — RRQ with duplicate ACKs
# ---------------------------------------------------------------------------

def chaos_tftp_get(host: str, port: int, filename: str,
                   dup_prob: float = DEFAULT_DUP_PROBABILITY) -> tuple[bytes, DupStats]:
    """
    Download a file via RRQ (octet mode).
    After sending each ACK, randomly send 2–20 extra copies.
    Returns (file_bytes, DupStats).
    """
    stats = DupStats()
    sock  = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        sock.sendto(_build_rrq(filename), (host, port))

        data           = bytearray()
        tid            = None      # server's ephemeral (address, port)
        expected_block = 1

        while True:
            # Receive next DATA with retry / re-request on timeout
            for attempt in range(MAX_RETRIES):
                try:
                    pkt, addr = sock.recvfrom(TFTP_MAX_PACKET + 4)
                    break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(
                            f"Timed out waiting for DATA block {expected_block} "
                            f"after {MAX_RETRIES} retries"
                        )
                    # Re-send last ACK (or re-send RRQ for block 1)
                    if tid is None:
                        sock.sendto(_build_rrq(filename), (host, port))
                    else:
                        sock.sendto(_build_ack(expected_block - 1), tid)

            # Lock onto server TID from first reply
            if tid is None:
                tid = addr
            elif addr != tid:
                continue  # Packet from wrong source — ignore

            block, payload = _parse_data(pkt)

            if block != (expected_block & 0xFFFF):
                # Duplicate or out-of-order DATA — re-ACK and discard
                sock.sendto(_build_ack(block), tid)
                continue

            data.extend(payload)

            # Send ACK, then randomly send extra copies
            ack = _build_ack(block)
            sock.sendto(ack, tid)
            _maybe_dup(sock, ack, tid, dup_prob, stats)

            expected_block += 1

            if len(payload) < TFTP_BLOCK_SIZE:
                break   # Short DATA == end of transfer

        return bytes(data), stats
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Chaos TFTP client — WRQ with duplicate DATA
# ---------------------------------------------------------------------------

def chaos_tftp_put(host: str, port: int, filename: str, content: bytes,
                   dup_prob: float = DEFAULT_DUP_PROBABILITY) -> DupStats:
    """
    Upload a file via WRQ (octet mode).
    After sending each DATA packet, randomly send 2–20 extra copies.
    Returns DupStats.
    """
    stats = DupStats()
    sock  = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        sock.sendto(_build_wrq(filename), (host, port))

        # Wait for ACK 0
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
            raise ValueError(f"Expected ACK 0, got ACK {_parse_ack(pkt)}")

        block  = 1
        offset = 0

        while True:
            chunk    = content[offset : offset + TFTP_BLOCK_SIZE]
            data_pkt = struct.pack("!HH", TFTP_OP_DATA, block & 0xFFFF) + chunk

            for attempt in range(MAX_RETRIES):
                sock.sendto(data_pkt, tid)

                # Randomly duplicate DATA on the first send attempt only
                if attempt == 0:
                    _maybe_dup(sock, data_pkt, tid, dup_prob, stats)

                # Drain incoming ACKs until we get the one matching this block.
                # The server may send multiple ACKs (one per duplicate DATA received),
                # and stale ACKs from previous blocks may still be in flight.
                try:
                    while True:
                        pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                        if addr != tid:
                            continue
                        ack_block = _parse_ack(pkt)
                        if ack_block == (block & 0xFFFF):
                            break
                        # Stale or duplicate ACK from a previous block — discard
                    break  # Got the correct ACK; advance to next block
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(f"Timed out waiting for ACK {block}")
                    # Fall through to retry (resend DATA without extra duplication)

            offset += len(chunk)
            block  += 1

            if len(chunk) < TFTP_BLOCK_SIZE:
                break   # Short DATA sent == transfer complete

        return stats
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Test file generation
# ---------------------------------------------------------------------------

def generate_test_file(path: Path, size: int) -> str:
    """Create a test file of exact size with deterministic content.
    Returns the hex MD5 digest."""
    md5       = hashlib.md5()
    remaining = size
    pattern   = bytes(range(256))
    with open(path, "wb") as f:
        while remaining > 0:
            n          = min(remaining, 65536)
            full_reps  = n // 256
            tail       = n % 256
            chunk      = pattern * full_reps + pattern[:tail]
            f.write(chunk)
            md5.update(chunk)
            remaining -= n
    return md5.hexdigest()


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def make_content(size: int) -> bytes:
    """Build deterministic in-memory content matching generate_test_file."""
    pattern = bytes(range(256))
    full_reps = size // 256
    tail      = size % 256
    return pattern * full_reps + pattern[:tail]


# ---------------------------------------------------------------------------
# Server management
# ---------------------------------------------------------------------------

class TFTPTestServer:
    """Manages the tftptest server process lifecycle."""

    def __init__(self, binary: str, port: int, root_dir: str, verbosity: int = 0):
        self.binary    = binary
        self.port      = port
        self.root_dir  = root_dir
        self.verbosity = verbosity
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        cmd = [
            self.binary,
            "-p", str(self.port),
            *(["-v"] * self.verbosity),
        ]
        self.proc = subprocess.Popen(
            cmd,
            cwd=self.root_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
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
    """Run a test; print PASS + stats or FAIL + reason. Returns True on pass."""
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

def test_chaos_rrq_small(host, port, root, dup_prob):
    fname = "small.bin"
    expected_md5 = generate_test_file(root / fname, 100)
    data, stats  = chaos_tftp_get(host, port, fname, dup_prob)
    assert len(data) == 100,              f"Expected 100 bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return stats


def test_chaos_rrq_medium(host, port, root, dup_prob):
    fname = "medium.bin"
    size  = 50 * 1024
    expected_md5 = generate_test_file(root / fname, size)
    data, stats  = chaos_tftp_get(host, port, fname, dup_prob)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return stats


def test_chaos_rrq_large(host, port, root, dup_prob):
    fname = "large.bin"
    size  = 65535 * TFTP_BLOCK_SIZE   # exactly fills blocks 1–65535; server sends empty EOF
    expected_md5 = generate_test_file(root / fname, size)
    data, stats  = chaos_tftp_get(host, port, fname, dup_prob)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return stats


def test_chaos_rrq_extralarge(host, port, root, dup_prob):
    fname = "extralarge.bin"
    size  = 65536 * TFTP_BLOCK_SIZE + 100   # block number wraps past 65535
    expected_md5 = generate_test_file(root / fname, size)
    data, stats  = chaos_tftp_get(host, port, fname, dup_prob)
    assert len(data) == size,               f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"
    return stats


# ---------------------------------------------------------------------------
# WRQ test cases
# ---------------------------------------------------------------------------

def test_chaos_wrq_small(host, port, root, dup_prob):
    fname   = "upload_small.bin"
    content = make_content(100)
    stats   = chaos_tftp_put(host, port, fname, content, dup_prob)
    written = (root / fname).read_bytes()
    assert len(written) == 100,       f"Expected 100 bytes on disk, got {len(written)}"
    assert written == content,        "Content mismatch"
    return stats


def test_chaos_wrq_medium(host, port, root, dup_prob):
    fname   = "upload_medium.bin"
    size    = 50 * 1024
    content = make_content(size)
    stats   = chaos_tftp_put(host, port, fname, content, dup_prob)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return stats


def test_chaos_wrq_large(host, port, root, dup_prob):
    fname   = "upload_large.bin"
    size    = 65535 * TFTP_BLOCK_SIZE
    content = make_content(size)
    stats   = chaos_tftp_put(host, port, fname, content, dup_prob)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return stats


def test_chaos_wrq_extralarge(host, port, root, dup_prob):
    fname   = "upload_extralarge.bin"
    size    = 65536 * TFTP_BLOCK_SIZE + 100
    content = make_content(size)
    stats   = chaos_tftp_put(host, port, fname, content, dup_prob)
    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content,   "Content mismatch"
    return stats


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_server_binary() -> str:
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir.parent / "build" / "debug"   / "tftptest",
        script_dir.parent / "build" / "release" / "tftptest",
    ]
    for p in candidates:
        if p.is_file() and os.access(p, os.X_OK):
            return str(p)
    sys.exit("Could not find tftptest binary. Run `make debug` from the c/ directory first.")


def main():
    parser = argparse.ArgumentParser(
        description="Chaos monkey #1: random duplicate ACK (RRQ) and DATA (WRQ) injection"
    )
    parser.add_argument("--port",            type=int,   default=23069,
                        help="TFTP port (default 23069)")
    parser.add_argument("--server-bin",      type=str,   default=None,
                        help="Path to tftptest binary")
    parser.add_argument("--dup-probability", type=float, default=DEFAULT_DUP_PROBABILITY,
                        help=f"Per-packet duplication probability 0–1 (default {DEFAULT_DUP_PROBABILITY})")
    parser.add_argument("--seed",            type=int,   default=None,
                        help="Random seed for reproducible runs (default: unpredictable)")
    parser.add_argument("--skip-large",      action="store_true",
                        help="Skip large/extralarge tests (~64 MB files, slower with chaos)")
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    binary = args.server_bin or find_server_binary()
    host   = "127.0.0.1"
    port   = args.port
    prob   = max(0.0, min(1.0, args.dup_probability))

    print(f"Server binary:    {binary}")
    print(f"TFTP port:        {port}")
    print(f"Dup probability:  {prob:.0%}  ({MIN_DUP_COUNT}–{MAX_DUP_COUNT} extra copies per event)")
    if args.seed is not None:
        print(f"Random seed:      {args.seed}")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_chaos1_") as tmpdir:
        root = Path(tmpdir)
        print(f"Test root dir:    {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as _server:
            results = []

            print("=== RRQ (Read) Tests — duplicate ACKs injected by client ===")
            results.append(run_test("RRQ small (100 B)",
                                    test_chaos_rrq_small,  host, port, root, prob))
            results.append(run_test("RRQ medium (50 KB)",
                                    test_chaos_rrq_medium, host, port, root, prob))
            if not args.skip_large:
                results.append(run_test("RRQ large (65535×512 B)",
                                        test_chaos_rrq_large,      host, port, root, prob))
                results.append(run_test("RRQ extralarge (65536×512+100 B)",
                                        test_chaos_rrq_extralarge, host, port, root, prob))
            else:
                print("  RRQ large ... SKIPPED")
                print("  RRQ extralarge ... SKIPPED")

            print()
            print("=== WRQ (Write) Tests — duplicate DATA injected by client ===")
            results.append(run_test("WRQ small (100 B)",
                                    test_chaos_wrq_small,  host, port, root, prob))
            results.append(run_test("WRQ medium (50 KB)",
                                    test_chaos_wrq_medium, host, port, root, prob))
            if not args.skip_large:
                results.append(run_test("WRQ large (65535×512 B)",
                                        test_chaos_wrq_large,      host, port, root, prob))
                results.append(run_test("WRQ extralarge (65536×512+100 B)",
                                        test_chaos_wrq_extralarge, host, port, root, prob))
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
