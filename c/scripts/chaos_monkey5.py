#!/usr/bin/env python3
"""
Chaos monkey integration test #5: request flood without completion.

Sends N RRQ (or WRQ) packets without ever responding (no ACKs / no DATA).
Each session times out on the server side.  After N timeouts the server
should lock out ALL subsequent requests — even legitimate ones — and reply
with ERROR(2) ACCESS_VIOLATION.

Server is started with:
  timeout_sec = 1
  max_retransmits = 2
  max_abandoned_sessions = <flood_count>

Tests:
  test_cm5_rrq_flood_triggers_lockout  — N silent RRQs → lockout → RRQ rejected
  test_cm5_wrq_flood_triggers_lockout  — N silent WRQs → lockout → WRQ rejected

Usage:
  python3 scripts/chaos_monkey5.py [--port PORT] [--server-bin PATH]
                                   [--flood-count N]
"""

from __future__ import annotations

import argparse
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

TFTP_ERRC_ACCESS_VIOLATION = 2

TFTP_BLOCK_SIZE = 512
TFTP_MAX_PACKET = 4 + TFTP_BLOCK_SIZE

SOCK_RCVBUF = 1 << 20

DEFAULT_FLOOD_COUNT = 3

# Server config values used for this test
_SERVER_TIMEOUT_SEC     = 1
_SERVER_MAX_RETRANSMITS = 2

# ---------------------------------------------------------------------------
# TFTP packet helpers
# ---------------------------------------------------------------------------

def _build_rrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_RRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _build_wrq(filename: str, mode: str = "octet") -> bytes:
    return struct.pack("!H", TFTP_OP_WRQ) + filename.encode() + b"\x00" + mode.encode() + b"\x00"


def _parse_error(pkt: bytes) -> tuple[int, str]:
    if len(pkt) < 4:
        raise ValueError(f"Packet too short for ERROR ({len(pkt)} bytes)")
    opcode, code = struct.unpack("!HH", pkt[:4])
    if opcode != TFTP_OP_ERROR:
        raise ValueError(f"Expected ERROR (opcode 5), got opcode {opcode}")
    msg = pkt[4:].split(b"\x00", 1)[0].decode(errors="replace")
    return code, msg


def _write_config(path: Path, port: int, flood_count: int) -> None:
    path.write_text(
        f"tftp_port              = {port}\n"
        f"timeout_sec            = {_SERVER_TIMEOUT_SEC}\n"
        f"max_retransmits        = {_SERVER_MAX_RETRANSMITS}\n"
        f"max_abandoned_sessions = {flood_count}\n"
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
            stdout, stderr = self.proc.communicate(timeout=10)
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
# Helpers
# ---------------------------------------------------------------------------

def _session_timeout_sec() -> float:
    """Wall-clock time for one server session to expire."""
    # server waits timeout_sec for each retransmit, up to max_retransmits times
    return (_SERVER_TIMEOUT_SEC * (_SERVER_MAX_RETRANSMITS + 1)) + 1.0


def _abandon_sessions(host: str, port: int, opcode_builder, count: int) -> None:
    """
    Send `count` request packets and do nothing else — let each session time out.
    We must wait for each session to expire before sending the next one, because
    the server handles only one session at a time.
    """
    wait_sec = _session_timeout_sec()
    for i in range(count):
        print(f"    Abandoning session {i + 1}/{count} "
              f"(waiting ~{wait_sec:.0f}s for timeout)...", flush=True)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)
        try:
            sock.sendto(opcode_builder(), (host, port))
            # We intentionally do NOT respond — let the server time out
        finally:
            sock.close()
        # Wait long enough for the server FSM to exhaust its retransmits
        time.sleep(wait_sec)


def _expect_lockout_error(host: str, port: int, request_pkt: bytes,
                          recv_timeout: float = 5.0) -> tuple[int, str]:
    """
    Send a request and expect an immediate ERROR(ACCESS_VIOLATION) reply.
    Returns (error_code, error_message).
    Raises AssertionError if a non-error or no reply is received.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(recv_timeout)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCK_RCVBUF)
    try:
        sock.sendto(request_pkt, (host, port))
        try:
            pkt, _addr = sock.recvfrom(TFTP_MAX_PACKET + 4)
        except socket.timeout:
            raise AssertionError(
                "Server did not reply — expected ERROR(ACCESS_VIOLATION) but got silence"
            )
        opcode = struct.unpack("!H", pkt[:2])[0]
        if opcode != TFTP_OP_ERROR:
            raise AssertionError(
                f"Expected ERROR packet (opcode 5), got opcode {opcode}"
            )
        code, msg = _parse_error(pkt)
        return code, msg
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

def test_cm5_rrq_flood_triggers_lockout(host: str, port: int, flood_count: int):
    """
    Abandon flood_count RRQ sessions → verify next RRQ is rejected with ERROR(2).
    """
    _abandon_sessions(
        host, port,
        lambda: _build_rrq("nosuchfile.bin"),
        flood_count
    )

    code, msg = _expect_lockout_error(
        host, port,
        _build_rrq("nosuchfile.bin")
    )
    assert code == TFTP_ERRC_ACCESS_VIOLATION, \
        f"Expected ERROR code 2 (ACCESS_VIOLATION), got {code}: {msg}"
    return f"lockout confirmed after {flood_count} abandoned RRQs — ERROR({code}): {msg}"


def test_cm5_wrq_flood_triggers_lockout(host: str, port: int, flood_count: int):
    """
    Abandon flood_count WRQ sessions → verify next WRQ is rejected with ERROR(2).
    """
    _abandon_sessions(
        host, port,
        lambda: _build_wrq("upload.bin"),
        flood_count
    )

    code, msg = _expect_lockout_error(
        host, port,
        _build_wrq("upload.bin")
    )
    assert code == TFTP_ERRC_ACCESS_VIOLATION, \
        f"Expected ERROR code 2 (ACCESS_VIOLATION), got {code}: {msg}"
    return f"lockout confirmed after {flood_count} abandoned WRQs — ERROR({code}): {msg}"


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
        description="Chaos monkey #5: request flood without completion — tests max_abandoned_sessions lockout"
    )
    parser.add_argument("--port",        type=int, default=23069)
    parser.add_argument("--server-bin",  type=str, default=None)
    parser.add_argument("--flood-count", type=int, default=DEFAULT_FLOOD_COUNT,
                        help=f"Number of sessions to abandon before expecting lockout (default {DEFAULT_FLOOD_COUNT})")
    args = parser.parse_args()

    if args.flood_count < 1:
        sys.exit("--flood-count must be >= 1")

    binary      = args.server_bin or find_server_binary()
    host        = "127.0.0.1"
    port        = args.port
    flood_count = args.flood_count

    # Each session: 1s timeout × (2 retransmits + 1) = 3s per session + 1s buffer
    # Total wait: flood_count × ~4s  (both test variants run their own flood)
    est_wait = flood_count * _session_timeout_sec() * 2
    print(f"Server binary:    {binary}")
    print(f"TFTP port:        {port}")
    print(f"Flood count:      {flood_count} abandoned sessions before lockout")
    print(f"Server timeout:   {_SERVER_TIMEOUT_SEC}s × {_SERVER_MAX_RETRANSMITS} retransmits")
    print(f"Est. total wait:  ~{est_wait:.0f}s (2 test variants × flood_count sessions)")
    print()

    with tempfile.TemporaryDirectory(prefix="tftptest_chaos5_") as tmpdir:
        root     = Path(tmpdir)
        cfg_path = root / "tftptest.conf"

        print(f"Test root dir:  {root}")

        results = []

        # --- RRQ flood test ---
        _write_config(cfg_path, port, flood_count)
        print(f"\n=== RRQ Flood Test (max_abandoned_sessions={flood_count}) ===")
        with TFTPTestServer(binary, port, tmpdir, config_path=str(cfg_path)) as _server:
            results.append(run_test(
                f"RRQ flood ({flood_count} abandoned) → lockout → ERROR(2)",
                test_cm5_rrq_flood_triggers_lockout, host, port, flood_count
            ))
            print()
            _, stderr = _server.stop()
            if stderr:
                print("  --- Server stderr (last 10 lines) ---")
                for line in stderr.strip().splitlines()[-10:]:
                    print(f"    {line}")
                print()

        # --- WRQ flood test (fresh server instance) ---
        _write_config(cfg_path, port, flood_count)
        print(f"=== WRQ Flood Test (max_abandoned_sessions={flood_count}) ===")
        with TFTPTestServer(binary, port, tmpdir, config_path=str(cfg_path)) as _server:
            results.append(run_test(
                f"WRQ flood ({flood_count} abandoned) → lockout → ERROR(2)",
                test_cm5_wrq_flood_triggers_lockout, host, port, flood_count
            ))
            print()
            _, stderr = _server.stop()
            if stderr:
                print("  --- Server stderr (last 10 lines) ---")
                for line in stderr.strip().splitlines()[-10:]:
                    print(f"    {line}")
                print()

        passed = sum(results)
        total  = len(results)
        print(f"{'=' * 40}")
        print(f"Results: {passed}/{total} passed")

        if passed < total:
            sys.exit(1)


if __name__ == "__main__":
    main()
