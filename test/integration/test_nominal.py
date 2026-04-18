#!/usr/bin/env python3
"""
Integration tests for nominal TFTP transfers (RRQ and WRQ, octet mode).

Test cases:
  RRQ:
    - Small file   (~100 bytes, fits in one DATA packet)
    - Medium file  (~50 KB, spans many blocks)
    - Large file   (65535 x 512 = 33,553,920 bytes -- block number edge case)
    - Extra-large  (65536 x 512 + 100 bytes -- block number wraps past 0)
    - No read permission (S_IROTH not set) -- expect ACCESS_VIOLATION
    - Non-existent file -- expect FILE_NOT_FOUND

  WRQ:
    - Small file   (~100 bytes)
    - Medium file  (~50 KB)
    - Large file   (65535 x 512 bytes -- block number edge case)
    - Extra-large  (65536 x 512 + 100 bytes -- block number wraps past 0)
    - Existing file without world-write permission -- expect ACCESS_VIOLATION
    - Existing world-writable file -- expect successful overwrite

  All WRQs that create a new file verify that the resulting file's permission bits
  match the server's effective new_file_mode (default 0o666 & ~umask).

Requires: the tftptest server binary at build/debug/tftptest (run `make debug`
          first from the repo root).

Usage:
  python3 test/integration/test_nominal.py [--port PORT] [--server-bin PATH]
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
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE  # DATA header (4) + payload (512)

TIMEOUT_SEC = 10
MAX_RETRIES = 5
SOCK_RCVBUF = 1 << 20  # 1 MB receive buffer for large transfers

# Server default new_file_mode and the effective mode after this process's umask.
# The tftptest server inherits the same umask as this test process.
_SAVED_UMASK = os.umask(0)
os.umask(_SAVED_UMASK)
SERVER_NEW_FILE_MODE = 0o666           # tftptest default (tftp_parsecfg_defaults)
EFFECTIVE_NEW_FILE_MODE = SERVER_NEW_FILE_MODE & ~_SAVED_UMASK

# ---------------------------------------------------------------------------
# Minimal TFTP client (octet mode)
# ---------------------------------------------------------------------------

class TFTPError(Exception):
    """Raised when the server sends an ERROR packet."""
    def __init__(self, code: int, msg: str):
        super().__init__(f"TFTP error {code}: {msg}")
        self.code = code
        self.msg = msg


def _build_rrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_RRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _build_wrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_WRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _build_ack(block: int) -> bytes:
    return struct.pack("!HH", TFTP_OP_ACK, block & 0xFFFF)


def _parse_data(pkt: bytes) -> tuple[int, bytes]:
    """Parse a DATA packet. Returns (block_number, payload)."""
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
    """Parse an ACK packet. Returns block_number."""
    if len(pkt) < 4:
        raise ValueError(f"ACK packet too short ({len(pkt)} bytes)")
    opcode, block = struct.unpack("!HH", pkt[:4])
    if opcode == TFTP_OP_ERROR:
        msg = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
        raise TFTPError(block, msg)
    if opcode != TFTP_OP_ACK:
        raise ValueError(f"Expected ACK (opcode 4), got opcode {opcode}")
    return block


def tftp_get(host: str, port: int, filename: str) -> bytes:
    """Download a file via TFTP RRQ (octet mode). Returns file contents."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        # Send RRQ to the well-known port
        sock.sendto(_build_rrq(filename), (host, port))

        data = bytearray()
        tid = None  # server's ephemeral (address, port) -- learned from first DATA
        expected_block = 1

        while True:
            for attempt in range(MAX_RETRIES):
                try:
                    pkt, addr = sock.recvfrom(TFTP_MAX_PACKET + 4)
                    break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(
                            f"Timed out waiting for block {expected_block} "
                            f"(after {MAX_RETRIES} retries)"
                        )
                    # Re-send last ACK (or re-send RRQ for block 1)
                    if expected_block == 1:
                        sock.sendto(_build_rrq(filename), (host, port))
                    else:
                        sock.sendto(_build_ack(expected_block - 1), tid)

            # Lock onto the server's TID from the first response
            if tid is None:
                tid = addr
            elif addr != tid:
                # Wrong TID -- ignore (per RFC 1350, we could send ERROR 5)
                continue

            block, payload = _parse_data(pkt)

            if block != (expected_block & 0xFFFF):
                # Duplicate or out-of-order -- re-ACK and keep waiting
                sock.sendto(_build_ack(block), tid)
                continue

            data.extend(payload)
            sock.sendto(_build_ack(block), tid)
            expected_block += 1

            # A DATA with < 512 bytes signals end of transfer
            if len(payload) < TFTP_BLOCK_SIZE:
                break

        return bytes(data)
    finally:
        sock.close()


def tftp_put(host: str, port: int, filename: str, content: bytes) -> None:
    """Upload a file via TFTP WRQ (octet mode)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    try:
        # Send WRQ to the well-known port
        sock.sendto(_build_wrq(filename), (host, port))

        tid = None
        offset = 0
        block = 0  # We expect ACK 0 first, then send DATA 1, etc.

        # Wait for ACK 0
        for attempt in range(MAX_RETRIES):
            try:
                pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                break
            except socket.timeout:
                if attempt == MAX_RETRIES - 1:
                    raise TimeoutError("Timed out waiting for ACK 0")
                sock.sendto(_build_wrq(filename), (host, port))

        tid = addr
        ack_block = _parse_ack(pkt)
        assert ack_block == 0, f"Expected ACK 0, got ACK {ack_block}"

        block = 1
        while True:
            chunk = content[offset:offset + TFTP_BLOCK_SIZE]
            data_pkt = struct.pack("!HH", TFTP_OP_DATA, block & 0xFFFF) + chunk

            for attempt in range(MAX_RETRIES):
                sock.sendto(data_pkt, tid)
                try:
                    pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                    if addr != tid:
                        continue
                    ack_block = _parse_ack(pkt)
                    if ack_block == (block & 0xFFFF):
                        break
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(f"Timed out waiting for ACK {block}")

            offset += len(chunk)
            block += 1

            # Last block: payload < 512 bytes
            if len(chunk) < TFTP_BLOCK_SIZE:
                break
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Test file generation and helpers
# ---------------------------------------------------------------------------

def generate_test_file(path: Path, size: int) -> str:
    """Create a test file of exact size with deterministic content.
    Returns the hex MD5 digest."""
    md5 = hashlib.md5()
    remaining = size
    # Write in 64 KB chunks for efficiency
    chunk_size = 65536
    # Use a deterministic pattern: repeating 256-byte sequence
    pattern = bytes(range(256))
    with open(path, "wb") as f:
        while remaining > 0:
            n = min(remaining, chunk_size)
            # Build the chunk from the repeating pattern
            full_reps = n // 256
            tail = n % 256
            chunk = pattern * full_reps + pattern[:tail]
            f.write(chunk)
            md5.update(chunk)
            remaining -= n
    return md5.hexdigest()


def _make_content(size: int) -> bytes:
    """Generate deterministic in-memory content of the given size."""
    pattern = bytes(range(256))
    full_reps, tail = divmod(size, 256)
    return pattern * full_reps + pattern[:tail]


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def _check_new_file_mode(path: Path) -> None:
    """Assert that a newly created file has the server's effective permission bits."""
    actual = path.stat().st_mode & 0o777
    assert actual == EFFECTIVE_NEW_FILE_MODE, (
        f"File mode 0o{actual:04o} != expected 0o{EFFECTIVE_NEW_FILE_MODE:04o} "
        f"(new_file_mode=0o{SERVER_NEW_FILE_MODE:04o}, umask=0o{_SAVED_UMASK:04o})"
    )


# ---------------------------------------------------------------------------
# Server management
# ---------------------------------------------------------------------------

class TFTPTestServer:
    """Manages the tftptest server process lifecycle."""

    def __init__(self, binary: str, port: int, root_dir: str, verbosity: int = 0):
        self.binary = binary
        self.port = port
        self.root_dir = root_dir
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
        # Give the server a moment to bind
        time.sleep(0.3)
        if self.proc.poll() is not None:
            _, stderr = self.proc.communicate(timeout=2)
            raise RuntimeError(
                f"Server exited immediately (rc={self.proc.returncode}):\n"
                f"{stderr.decode(errors='replace')}"
            )

    def stop(self) -> tuple[str, str]:
        """Stop the server and return (stdout, stderr)."""
        if self.proc is None:
            return ("", "")
        self.proc.send_signal(signal.SIGINT)
        try:
            stdout, stderr = self.proc.communicate(timeout=3)
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
# Test cases
# ---------------------------------------------------------------------------

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"


def run_test(name: str, func, *args, **kwargs) -> bool:
    """Run a test function. Returns True on pass."""
    print(f"  {name} ... ", end="", flush=True)
    try:
        func(*args, **kwargs)
        print(PASS)
        return True
    except Exception as e:
        print(f"{FAIL}: {e}")
        return False


def test_rrq_small(host: str, port: int, root: Path):
    """RRQ a small file (~100 bytes) that fits in a single DATA packet."""
    fname = "small.bin"
    expected_md5 = generate_test_file(root / fname, 100)

    data = tftp_get(host, port, fname)
    assert len(data) == 100, f"Expected 100 bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"


def test_rrq_medium(host: str, port: int, root: Path):
    """RRQ a medium file (~50 KB) spanning many blocks."""
    fname = "medium.bin"
    size = 50 * 1024  # 50 KB = ~98 blocks
    expected_md5 = generate_test_file(root / fname, size)

    data = tftp_get(host, port, fname)
    assert len(data) == size, f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"


def test_rrq_large(host: str, port: int, root: Path):
    """RRQ a file of exactly 65535 x 512 bytes (block number edge case).

    This tests the maximum file size transferable without block number wrap.
    Block numbers go 1..65535, and the last block is exactly 512 bytes,
    so the server must send an additional empty DATA to signal EOF.
    """
    fname = "large.bin"
    size = 65535 * TFTP_BLOCK_SIZE  # 33,553,920 bytes
    expected_md5 = generate_test_file(root / fname, size)

    data = tftp_get(host, port, fname)
    assert len(data) == size, f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"


def test_rrq_extralarge(host: str, port: int, root: Path):
    """RRQ a file larger than 65535 x 512 (block number wraps past 0).

    This verifies the server handles uint16_t block number wrap-around.
    Size: 65536 x 512 + 100 = 33,554,532 bytes.
    """
    fname = "extralarge.bin"
    size = 65536 * TFTP_BLOCK_SIZE + 100  # wraps block counter
    expected_md5 = generate_test_file(root / fname, size)

    data = tftp_get(host, port, fname)
    assert len(data) == size, f"Expected {size} bytes, got {len(data)}"
    assert md5_bytes(data) == expected_md5, "MD5 mismatch"


def test_rrq_no_read_permission(host: str, port: int, root: Path):
    """RRQ a file without world-read permission (S_IROTH not set).

    The server enforces a world-readable policy regardless of owner.
    Expect ACCESS_VIOLATION (error code 2).
    """
    fname = "no_read.bin"
    (root / fname).write_bytes(b"secret content")
    os.chmod(root / fname, 0o600)

    try:
        tftp_get(host, port, fname)
        raise AssertionError("Expected TFTPError but get succeeded")
    except TFTPError as e:
        assert e.code == 2, f"Expected error code 2 (ACCESS_VIOLATION), got {e.code}"


def test_rrq_nonexistent(host: str, port: int, root: Path):
    """RRQ of a file that does not exist -- expect FILE_NOT_FOUND (error code 1)."""
    try:
        tftp_get(host, port, "does_not_exist.bin")
        raise AssertionError("Expected TFTPError but get succeeded")
    except TFTPError as e:
        assert e.code == 1, f"Expected error code 1 (FILE_NOT_FOUND), got {e.code}"


def test_wrq_small(host: str, port: int, root: Path):
    """WRQ a small file (~100 bytes); verify content and permission bits."""
    fname = "upload_small.bin"
    content = bytes(range(100))

    tftp_put(host, port, fname, content)

    written = (root / fname).read_bytes()
    assert len(written) == 100, f"Expected 100 bytes on disk, got {len(written)}"
    assert written == content, "Content mismatch"
    _check_new_file_mode(root / fname)


def test_wrq_medium(host: str, port: int, root: Path):
    """WRQ a medium file (~50 KB); verify content and permission bits."""
    fname = "upload_medium.bin"
    size = 50 * 1024
    pattern = bytes(range(256))
    content = (pattern * (size // 256 + 1))[:size]

    tftp_put(host, port, fname, content)

    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content, "Content mismatch"
    _check_new_file_mode(root / fname)


def test_wrq_large(host: str, port: int, root: Path):
    """WRQ a large file (65535 x 512 bytes -- block number edge case).

    Verifies content integrity and that created file has correct permission bits.
    """
    fname = "upload_large.bin"
    size = 65535 * TFTP_BLOCK_SIZE
    content = _make_content(size)

    tftp_put(host, port, fname, content)

    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content, "Content mismatch"
    _check_new_file_mode(root / fname)


def test_wrq_extralarge(host: str, port: int, root: Path):
    """WRQ a file larger than 65535 x 512 (block number wraps past 0).

    Size: 65536 x 512 + 100 = 33,554,532 bytes.
    Verifies content integrity and that created file has correct permission bits.
    """
    fname = "upload_extralarge.bin"
    size = 65536 * TFTP_BLOCK_SIZE + 100
    content = _make_content(size)

    tftp_put(host, port, fname, content)

    written = (root / fname).read_bytes()
    assert len(written) == size, f"Expected {size} bytes on disk, got {len(written)}"
    assert written == content, "Content mismatch"
    _check_new_file_mode(root / fname)


def test_wrq_existing_no_write_permission(host: str, port: int, root: Path):
    """WRQ to an existing file that lacks world-write permission (S_IWOTH not set).

    The server refuses to overwrite files that aren't world-writable.
    Expect ACCESS_VIOLATION (error code 2).
    """
    fname = "no_write.bin"
    (root / fname).write_bytes(b"original content")
    os.chmod(root / fname, 0o644)  # world-readable but not world-writable

    try:
        tftp_put(host, port, fname, b"new content")
        raise AssertionError("Expected TFTPError but put succeeded")
    except TFTPError as e:
        assert e.code == 2, f"Expected error code 2 (ACCESS_VIOLATION), got {e.code}"


def test_wrq_existing_world_writable(host: str, port: int, root: Path):
    """WRQ to an existing world-writable file -- expect successful overwrite."""
    fname = "world_writable.bin"
    original = b"original content"
    new_content = b"replaced by WRQ"
    (root / fname).write_bytes(original)
    os.chmod(root / fname, 0o666)

    tftp_put(host, port, fname, new_content)

    written = (root / fname).read_bytes()
    assert written == new_content, f"File was not overwritten correctly: {written!r}"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_server_binary() -> str:
    """Locate the tftptest binary relative to this script."""
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir.parent.parent / "build" / "debug" / "tftptest",
        script_dir.parent.parent / "build" / "release" / "tftptest",
    ]
    for p in candidates:
        if p.is_file() and os.access(p, os.X_OK):
            return str(p)
    sys.exit(
        "Could not find tftptest binary. Run `make debug` from the repo root first."
    )


def main():
    parser = argparse.ArgumentParser(description="Nominal TFTP integration tests")
    parser.add_argument("--port", type=int, default=23069, help="TFTP port (default 23069)")
    parser.add_argument("--server-bin", type=str, default=None, help="Path to tftptest binary")
    parser.add_argument("--skip-large", action="store_true",
                        help="Skip the large/extralarge tests (they create ~64 MB files)")
    args = parser.parse_args()

    binary = args.server_bin or find_server_binary()
    host = "127.0.0.1"
    port = args.port

    print(f"Server binary: {binary}")
    print(f"TFTP port:     {port}")
    print(f"Effective new_file_mode: 0o{EFFECTIVE_NEW_FILE_MODE:04o} "
          f"(0o{SERVER_NEW_FILE_MODE:04o} & ~umask 0o{_SAVED_UMASK:04o})")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_") as tmpdir:
        root = Path(tmpdir)
        print(f"Test root dir: {root}")
        print()

        with TFTPTestServer(binary, port, tmpdir) as server:
            results = []

            print("=== RRQ (Read) Tests ===")
            results.append(run_test("RRQ small (100 B)",  test_rrq_small,  host, port, root))
            results.append(run_test("RRQ medium (50 KB)", test_rrq_medium, host, port, root))

            if not args.skip_large:
                results.append(run_test("RRQ large (65535x512 B)",          test_rrq_large,      host, port, root))
                results.append(run_test("RRQ extralarge (65536x512+100 B)", test_rrq_extralarge, host, port, root))
            else:
                print("  RRQ large ... SKIPPED")
                print("  RRQ extralarge ... SKIPPED")

            print()
            print("=== WRQ (Write) Tests ===")
            results.append(run_test("WRQ small (100 B)",   test_wrq_small,  host, port, root))
            results.append(run_test("WRQ medium (50 KB)",  test_wrq_medium, host, port, root))

            if not args.skip_large:
                results.append(run_test("WRQ large (65535x512 B)",          test_wrq_large,      host, port, root))
                results.append(run_test("WRQ extralarge (65536x512+100 B)", test_wrq_extralarge, host, port, root))
            else:
                print("  WRQ large ... SKIPPED")
                print("  WRQ extralarge ... SKIPPED")

            print()
            print("=== Permission Tests ===")
            results.append(run_test("RRQ no read permission",       test_rrq_no_read_permission,          host, port, root))
            results.append(run_test("RRQ nonexistent file",         test_rrq_nonexistent,                 host, port, root))
            results.append(run_test("WRQ existing no write perm",   test_wrq_existing_no_write_permission, host, port, root))
            results.append(run_test("WRQ existing world-writable",  test_wrq_existing_world_writable,     host, port, root))

            print()
            _, stderr = server.stop()
            if stderr:
                print("--- Server stderr (last 20 lines) ---")
                for line in stderr.strip().splitlines()[-20:]:
                    print(f"  {line}")
                print()

        passed = sum(results)
        total = len(results)
        print(f"{'='*40}")
        print(f"Results: {passed}/{total} passed")

        if passed < total:
            sys.exit(1)


if __name__ == "__main__":
    main()
