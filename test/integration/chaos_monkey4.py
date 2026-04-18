#!/usr/bin/env python3
"""
Chaos monkey integration test #4: endless DATA stream — max_wrq_file_size enforcement.

Starts the server with a config file that limits WRQ file size to FILE_LIMIT bytes.
Attempts a WRQ upload of a file much larger than the limit, sending DATA blocks
until the server sends an ERROR packet.

Asserts:
  1. The server responds with an ERROR packet with code 3 (DISK_FULL / storage exceeded).
  2. After the error, a small RRQ for a known file succeeds — server still responsive.

Usage:
  python3 scripts/chaos_monkey4.py [--port PORT] [--server-bin PATH]
                                   [--file-limit BYTES]
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

TFTP_ERRC_DISK_FULL = 3

TFTP_BLOCK_SIZE = 512
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE

TIMEOUT_SEC = 10
MAX_RETRIES = 5
SOCK_RCVBUF = 1 << 20

DEFAULT_FILE_LIMIT = 10 * 1024  # 10 KB

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


def _parse_error(pkt: bytes) -> tuple[int, str]:
    """Return (error_code, message) from an ERROR packet (opcode already known to be 5)."""
    if len(pkt) < 4:
        raise ValueError(f"ERROR packet too short ({len(pkt)} bytes)")
    opcode, code = struct.unpack("!HH", pkt[:4])
    if opcode != TFTP_OP_ERROR:
        raise ValueError(f"Expected ERROR (opcode 5), got opcode {opcode}")
    msg = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
    return code, msg


# ---------------------------------------------------------------------------
# Helpers
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


def write_config(path: Path, port: int, file_limit: int) -> None:
    path.write_text(
        f"tftp_port = {port}\n"
        f"max_wrq_file_size = {file_limit}\n"
    )


# ---------------------------------------------------------------------------
# Server management
# ---------------------------------------------------------------------------

class TFTPTestServer:
    def __init__(self, binary: str, port: int, root_dir: str,
                 config_path: str | None = None, verbosity: int = 0):
        self.binary      = binary
        self.port        = port
        self.root_dir    = root_dir
        self.config_path = config_path
        self.verbosity   = verbosity
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        cmd = [self.binary, "-p", str(self.port), *(["-v"] * self.verbosity)]
        if self.config_path:
            cmd += ["-c", self.config_path]
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

def test_cm4_wrq_hits_file_limit(host: str, port: int, root: Path, file_limit: int):
    """
    Send an endless stream of DATA blocks well past the file limit.
    Expect ERROR(3) (DISK_FULL) from the server.
    """
    oversize = file_limit * 20   # 20× the limit
    fname    = "big_upload.bin"
    content  = make_content(oversize)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_SEC)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)

    error_code = None
    blocks_sent = 0

    try:
        sock.sendto(_build_wrq(fname), (host, port))

        # Wait for ACK 0
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

        opcode = struct.unpack("!H", pkt[:2])[0]
        if opcode == TFTP_OP_ERROR:
            error_code, msg = _parse_error(pkt)
            # Server rejected the WRQ outright — that is also acceptable
            assert error_code == TFTP_ERRC_DISK_FULL, \
                f"Expected ERROR code 3 (DISK_FULL), got {error_code}: {msg}"
            return f"server rejected WRQ immediately with ERROR({error_code}), {file_limit} B limit"

        if _parse_ack(pkt) != 0:
            raise ValueError("Expected ACK 0")

        block  = 1
        offset = 0

        while offset < len(content):
            chunk    = content[offset : offset + TFTP_BLOCK_SIZE]
            data_pkt = struct.pack("!HH", TFTP_OP_DATA, block & 0xFFFF) + chunk

            for attempt in range(MAX_RETRIES):
                sock.sendto(data_pkt, tid)
                blocks_sent += 1
                try:
                    while True:
                        pkt, addr = sock.recvfrom(TFTP_MAX_PACKET)
                        if addr != tid:
                            continue
                        opcode = struct.unpack("!H", pkt[:2])[0]
                        if opcode == TFTP_OP_ERROR:
                            error_code, msg = _parse_error(pkt)
                            raise TFTPError(error_code, msg)
                        ack_block = _parse_ack(pkt)
                        if ack_block == (block & 0xFFFF):
                            break
                    break
                except TFTPError:
                    raise
                except socket.timeout:
                    if attempt == MAX_RETRIES - 1:
                        raise TimeoutError(f"Timed out waiting for ACK {block}")

            offset += len(chunk)
            block  += 1

        raise AssertionError(
            f"Server accepted {oversize} bytes without enforcing {file_limit}-byte limit"
        )

    except TFTPError as e:
        error_code = e.code
        assert error_code == TFTP_ERRC_DISK_FULL, \
            f"Expected ERROR code 3 (DISK_FULL), got {error_code}: {e.msg}"
        return f"ERROR({error_code}) after {blocks_sent} blocks ({blocks_sent * TFTP_BLOCK_SIZE} B sent)"
    finally:
        sock.close()


def test_cm4_server_still_responsive(host: str, port: int, root: Path):
    """
    After the file-limit error, verify the server still handles a legitimate
    small RRQ correctly.
    """
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
        return "server responsive after file-limit error"
    finally:
        sock.close()


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
        description="Chaos monkey #4: endless DATA stream — tests max_wrq_file_size enforcement"
    )
    parser.add_argument("--port",        type=int, default=23069)
    parser.add_argument("--server-bin",  type=str, default=None)
    parser.add_argument("--file-limit",  type=int, default=DEFAULT_FILE_LIMIT,
                        help=f"max_wrq_file_size in bytes (default {DEFAULT_FILE_LIMIT})")
    args = parser.parse_args()

    binary     = args.server_bin or find_server_binary()
    host       = "127.0.0.1"
    port       = args.port
    file_limit = args.file_limit

    print(f"Server binary:  {binary}")
    print(f"TFTP port:      {port}")
    print(f"File limit:     {file_limit} bytes")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_chaos4_") as tmpdir:
        root       = Path(tmpdir)
        cfg_path   = root / "tftptest.conf"
        write_config(cfg_path, port, file_limit)

        print(f"Test root dir:  {root}")
        print(f"Config file:    {cfg_path}")
        print()

        with TFTPTestServer(binary, port, tmpdir, config_path=str(cfg_path)) as _server:
            results = []

            print("=== WRQ File-Limit Tests ===")
            results.append(run_test(
                f"WRQ hits {file_limit}-byte limit → ERROR(3)",
                test_cm4_wrq_hits_file_limit, host, port, root, file_limit
            ))
            results.append(run_test(
                "Server still responsive after limit error (small RRQ)",
                test_cm4_server_still_responsive, host, port, root
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
