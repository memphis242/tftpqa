#!/usr/bin/env python3
"""
Chaos monkey integration test #6: mid-transfer invalid block number.

Transfers pivot_block blocks correctly, then injects a packet whose block
number is replaced with a completely random uint16 that is guaranteed to
differ from both the expected value and the previous block.

RRQ variant: correct ACKs until pivot_block, then send ACK with bad block
             number → expect ERROR(4) ILLEGAL_OP from the server.
WRQ variant: correct DATA until pivot_block, then send DATA with bad block
             number → expect ERROR(4) ILLEGAL_OP from the server.

Both variants then verify the server is still responsive by completing a
small legitimate RRQ.

Usage:
  python3 scripts/chaos_monkey6.py [--port PORT] [--server-bin PATH]
                                   [--seed N] [--pivot-block N]
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

TFTP_ERRC_ILLEGAL_OP = 4

TFTP_BLOCK_SIZE = 512
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE

TIMEOUT_SEC = 10
MAX_RETRIES = 5
SOCK_RCVBUF = 1 << 20

MEDIUM_SIZE = 50 * 1024  # ~100 blocks — enough room for a pivot

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


def _bad_block(expected: int, prev: int) -> int:
    """Return a random uint16 that differs from both expected and prev."""
    while True:
        v = random.randint(0, 0xFFFF)
        if v != (expected & 0xFFFF) and v != (prev & 0xFFFF):
            return v


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
# RRQ bad-block test
# ---------------------------------------------------------------------------

def test_cm6_rrq_bad_block(host: str, port: int, root: Path, pivot_block: int):
    """
    Transfer correctly until pivot_block, then send a bad ACK block number.
    Expect ERROR(4) ILLEGAL_OP.
    """
    fname        = "medium_rrq.bin"
    generate_test_file(root / fname, MEDIUM_SIZE)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    bad_block_sent = None

    try:
        sock.sendto(_build_rrq(fname), (host, port))

        tid            = None
        expected_block = 1
        injected       = False

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
                        sock.sendto(_build_rrq(fname), (host, port))
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

            if block == (pivot_block & 0xFFFF) and not injected:
                # Inject a bad ACK block number
                bad_block_sent = _bad_block(expected_block + 1, block)
                sock.sendto(struct.pack("!HH", TFTP_OP_ACK, bad_block_sent), tid)
                injected = True
                # Now expect ERROR(4) from the server
                try:
                    err_pkt, _ = sock.recvfrom(TFTP_MAX_PACKET + 4)
                    opcode      = struct.unpack("!H", err_pkt[:2])[0]
                    if opcode != TFTP_OP_ERROR:
                        raise AssertionError(
                            f"Expected ERROR(4) after bad ACK, got opcode {opcode}"
                        )
                    code = struct.unpack("!HH", err_pkt[:4])[1]
                    assert code == TFTP_ERRC_ILLEGAL_OP, \
                        f"Expected ERROR code 4 (ILLEGAL_OP), got {code}"
                    return (f"pivot={pivot_block}, bad_ack_block={bad_block_sent} → "
                            f"ERROR({code}) as expected")
                except socket.timeout:
                    raise AssertionError(
                        f"Server did not send ERROR after bad ACK at pivot block {pivot_block}"
                    )
            else:
                sock.sendto(_build_ack(block), tid)

            expected_block += 1

            if len(payload) < TFTP_BLOCK_SIZE:
                break

        raise AssertionError(
            f"Transfer completed without triggering bad-block error "
            f"(pivot={pivot_block}, file may be too short)"
        )
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# WRQ bad-block test
# ---------------------------------------------------------------------------

def test_cm6_wrq_bad_block(host: str, port: int, root: Path, pivot_block: int):
    """
    Transfer correctly until pivot_block, then send a DATA with a bad block number.
    Expect ERROR(4) ILLEGAL_OP.
    """
    fname   = "medium_wrq.bin"
    content = make_content(MEDIUM_SIZE)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    bad_block_sent = None

    try:
        sock.sendto(_build_wrq(fname), (host, port))

        tid = None
        for attempt in range(MAX_RETRIES):
            try:
                pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                break
            except socket.timeout:
                if attempt == MAX_RETRIES - 1:
                    raise TimeoutError("Timed out waiting for ACK 0")
                sock.sendto(_build_wrq(fname), (host, port))
        tid = addr
        if _parse_ack(pkt) != 0:
            raise ValueError("Expected ACK 0")

        block  = 1
        offset = 0

        while offset <= len(content):
            chunk = content[offset : offset + TFTP_BLOCK_SIZE]

            if block == pivot_block:
                # Inject a bad block number in this DATA packet
                bad_block_sent = _bad_block(block, block - 1)
                bad_pkt = struct.pack("!HH", TFTP_OP_DATA, bad_block_sent) + chunk
                sock.sendto(bad_pkt, tid)
                # Expect ERROR(4)
                try:
                    err_pkt, _ = sock.recvfrom(TFTP_MAX_PACKET + 4)
                    opcode      = struct.unpack("!H", err_pkt[:2])[0]
                    if opcode != TFTP_OP_ERROR:
                        raise AssertionError(
                            f"Expected ERROR(4) after bad DATA block number, got opcode {opcode}"
                        )
                    code = struct.unpack("!HH", err_pkt[:4])[1]
                    assert code == TFTP_ERRC_ILLEGAL_OP, \
                        f"Expected ERROR code 4 (ILLEGAL_OP), got {code}"
                    return (f"pivot={pivot_block}, bad_data_block={bad_block_sent} → "
                            f"ERROR({code}) as expected")
                except socket.timeout:
                    raise AssertionError(
                        f"Server did not send ERROR after bad DATA at pivot block {pivot_block}"
                    )

            data_pkt = struct.pack("!HH", TFTP_OP_DATA, block & 0xFFFF) + chunk

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

        raise AssertionError(
            f"Transfer completed without triggering bad-block error "
            f"(pivot={pivot_block}, file may be too short)"
        )
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Recovery probe
# ---------------------------------------------------------------------------

def test_cm6_server_still_responsive(host: str, port: int, root: Path):
    """Small legitimate RRQ after the bad-block injection — server must respond."""
    fname        = "probe.bin"
    size         = 100
    expected_md5 = generate_test_file(root / fname, size)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        sock.sendto(_build_rrq(fname), (host, port))

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
                            f"Timed out waiting for DATA block {expected_block}"
                        )
                    if tid is None:
                        sock.sendto(_build_rrq(fname), (host, port))
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

            received.extend(payload)
            sock.sendto(_build_ack(block), tid)
            expected_block += 1

            if len(payload) < TFTP_BLOCK_SIZE:
                break

        assert len(received) == size,               f"Expected {size} bytes, got {len(received)}"
        assert md5_bytes(bytes(received)) == expected_md5, "MD5 mismatch"
        return "server responsive after bad-block injection"
    finally:
        sock.close()


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
        description="Chaos monkey #6: mid-transfer invalid block number → ERROR(4) ILLEGAL_OP"
    )
    parser.add_argument("--port",         type=int, default=23069)
    parser.add_argument("--server-bin",   type=str, default=None)
    parser.add_argument("--seed",         type=int, default=None)
    parser.add_argument("--pivot-block",  type=int, default=None,
                        help="Block at which to inject the bad block number (default: random 1–5)")
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    pivot_block = args.pivot_block if args.pivot_block is not None else random.randint(1, 5)
    if pivot_block < 1:
        sys.exit("--pivot-block must be >= 1")

    binary = args.server_bin or find_server_binary()
    host   = "127.0.0.1"
    port   = args.port

    print(f"Server binary:  {binary}")
    print(f"TFTP port:      {port}")
    print(f"Pivot block:    {pivot_block}")
    if args.seed is not None:
        print(f"Random seed:    {args.seed}")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_chaos6_") as tmpdir:
        root = Path(tmpdir)
        print(f"Test root dir:  {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as _server:
            results = []

            print(f"=== Bad Block Number Tests (pivot at block {pivot_block}) ===")
            results.append(run_test(
                f"WRQ bad block at pivot={pivot_block} → ERROR(4)",
                test_cm6_wrq_bad_block, host, port, root, pivot_block
            ))
            results.append(run_test(
                "Server still responsive after WRQ bad-block (small RRQ)",
                test_cm6_server_still_responsive, host, port, root
            ))
            results.append(run_test(
                f"RRQ bad block at pivot={pivot_block} → ERROR(4)",
                test_cm6_rrq_bad_block, host, port, root, pivot_block
            ))
            results.append(run_test(
                "Server still responsive after RRQ bad-block (small RRQ)",
                test_cm6_server_still_responsive, host, port, root
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

        if passed < total:
            sys.exit(1)


if __name__ == "__main__":
    main()
