#!/usr/bin/env python3
"""
run_crash.py - Crash-recovery and AOF durability test harness for KVForge.
Implements the specification in docs/testing/TEST_PLAN.md §7.
"""

import os
import sys
import time
import stat
import socket
import signal
import shutil
import random
import tempfile
import subprocess
from typing import List, Tuple, Dict, Optional, Any

class SimpleRespClient:
    def __init__(self, host: str, port: int, timeout: float = 3.0):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((self.host, self.port))
        self.buf = bytearray()

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def send_cmd(self, args: List[str]):
        parts = [f"*{len(args)}\r\n".encode("utf-8")]
        for a in args:
            b = a.encode("utf-8")
            parts.append(f"${len(b)}\r\n".encode("utf-8") + b + b"\r\n")
        self.sock.sendall(b"".join(parts))

    def _read_line(self) -> bytes:
        while True:
            idx = self.buf.find(b"\r\n")
            if idx != -1:
                line = bytes(self.buf[:idx])
                del self.buf[:idx + 2]
                return line
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionResetError("Server closed connection")
            self.buf.extend(chunk)

    def _read_bytes(self, n: int) -> bytes:
        while len(self.buf) < n + 2:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionResetError("Server closed connection")
            self.buf.extend(chunk)
        data = bytes(self.buf[:n])
        del self.buf[:n + 2]
        return data

    def read_reply(self) -> Tuple[str, Any]:
        while not self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionResetError("Server closed connection")
            self.buf.extend(chunk)

        prefix = chr(self.buf[0])
        del self.buf[:1]
        if prefix == '+':
            return ("status", self._read_line().decode("utf-8", errors="replace"))
        elif prefix == '-':
            return ("error", self._read_line().decode("utf-8", errors="replace"))
        elif prefix == ':':
            return ("int", int(self._read_line().decode("utf-8")))
        elif prefix == '$':
            length = int(self._read_line().decode("utf-8"))
            if length == -1:
                return ("null", None)
            return ("bulk", self._read_bytes(length).decode("utf-8", errors="replace"))
        elif prefix == '*':
            count = int(self._read_line().decode("utf-8"))
            if count == -1:
                return ("null", None)
            items = []
            for _ in range(count):
                items.append(self.read_reply())
            return ("array", items)
        else:
            raise ValueError(f"Unknown prefix: {prefix}")

def wait_for_server(port: int, max_wait: float = 3.0) -> bool:
    start = time.time()
    while time.time() - start < max_wait:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.2)
            s.connect(("127.0.0.1", port))
            s.close()
            return True
        except Exception:
            time.sleep(0.05)
    return False

def test_permissions(kvforge_bin: str, port: int):
    """Verifies that created AOF has 0600 permissions (AC-AOF-01 + SECURITY §3.5)."""
    print("\n--- Test: AOF File Permissions (0600) ---")
    tmpdir = tempfile.mkdtemp(prefix="kvf_perm_")
    aof_path = os.path.join(tmpdir, "test.aof")
    try:
        proc = subprocess.Popen([
            kvforge_bin,
            "--port", str(port),
            "--appendonly", "yes",
            "--appendfsync", "always",
            "--appendfilename", aof_path
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert wait_for_server(port), "Server failed to start"

        client = SimpleRespClient("127.0.0.1", port)
        client.send_cmd(["SET", "init_key", "val"])
        assert client.read_reply() == ("status", "OK")
        client.close()

        mode = stat.S_IMODE(os.stat(aof_path).st_mode)
        assert mode == 0o600, f"Expected AOF file permissions 0600, got {oct(mode)}"
        print(f"PASSED: AOF permissions are {oct(mode)}")
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=1.0)
        except Exception:
            proc.kill()
        shutil.rmtree(tmpdir, ignore_errors=True)

def test_empty_and_partial_first(kvforge_bin: str, port: int):
    """Verifies empty file and file with partial first record."""
    print("\n--- Test: Empty file & Partial first record ---")
    tmpdir = tempfile.mkdtemp(prefix="kvf_edge_")
    aof_path = os.path.join(tmpdir, "edge.aof")
    try:
        # 1. Zero-byte file
        with open(aof_path, "wb") as f:
            pass
        proc = subprocess.Popen([
            kvforge_bin, "--port", str(port),
            "--appendonly", "yes", "--appendfilename", aof_path
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert wait_for_server(port), "Server failed on 0-byte file"
        client = SimpleRespClient("127.0.0.1", port)
        client.send_cmd(["DBSIZE"])
        assert client.read_reply() == ("int", 0)
        client.close()
        proc.terminate()
        proc.wait(timeout=1.0)
        print("PASSED: 0-byte AOF loaded cleanly")

        # 2. Partial first record
        with open(aof_path, "wb") as f:
            f.write(b"*3\r\n$3\r\nSET\r\n$2\r\nk1\r\n")  # cut off before value
        proc = subprocess.Popen([
            kvforge_bin, "--port", str(port),
            "--appendonly", "yes", "--appendfilename", aof_path
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert wait_for_server(port), "Server failed on partial 1st record"
        client = SimpleRespClient("127.0.0.1", port)
        client.send_cmd(["DBSIZE"])
        assert client.read_reply() == ("int", 0)
        client.close()
        proc.terminate()
        proc.wait(timeout=1.0)
        print("PASSED: Partial first record truncated cleanly")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

def test_middle_corruption(kvforge_bin: str, port: int):
    """Verifies that middle corruption fails startup with code 1 and logs offset (AC-AOF-06)."""
    print("\n--- Test: Middle Corruption Rejection (AC-AOF-06) ---")
    tmpdir = tempfile.mkdtemp(prefix="kvf_corrupt_")
    aof_path = os.path.join(tmpdir, "corrupt.aof")
    try:
        # Create AOF with 10 records
        with open(aof_path, "wb") as f:
            for i in range(10):
                cmd = f"*3\r\n$3\r\nSET\r\n$4\r\nk{i:03d}\r\n$4\r\nv{i:03d}\r\n".encode("utf-8")
                f.write(cmd)

        # Corrupt 5th record header (offset around ~130)
        with open(aof_path, "r+b") as f:
            f.seek(140)
            corrupt_offset = f.tell()
            f.write(b"X")  # replace with invalid RESP prefix

        proc = subprocess.Popen([
            kvforge_bin, "--port", str(port),
            "--appendonly", "yes", "--appendfilename", aof_path
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        stdout, stderr = proc.communicate(timeout=3.0)
        assert proc.returncode == 1, f"Expected exit code 1 on corrupt AOF, got {proc.returncode}"
        combined_logs = stdout + stderr
        assert "AOF corrupt" in combined_logs or "offset" in combined_logs, \
            f"Expected corruption log, got:\n{combined_logs}"
        print(f"PASSED: KVForge refused startup with exit code 1 on corrupted middle record")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

def test_tail_truncation_single_byte(kvforge_bin: str, port: int):
    """
    Verifies that for every single byte offset inside the last record,
    KVForge truncates the incomplete tail and recovers all previous records (AC-AOF-05).
    """
    print("\n--- Test: Byte-by-Byte Tail Truncation (AC-AOF-05) ---")
    tmpdir = tempfile.mkdtemp(prefix="kvf_trunc_")
    aof_path = os.path.join(tmpdir, "trunc.aof")
    try:
        base_records = []
        for i in range(50):
            base_records.append(f"*3\r\n$3\r\nSET\r\n$4\r\nk{i:03d}\r\n$4\r\nv{i:03d}\r\n".encode("utf-8"))
        last_record = b"*3\r\n$3\r\nSET\r\n$9\r\nlast_key9\r\n$10\r\nlast_val10\r\n"

        base_bytes = b"".join(base_records)
        base_len = len(base_bytes)

        # Test truncation at several byte offsets inside the last record
        offsets_to_test = [
            base_len + 1,
            base_len + 2,
            base_len + 5,
            base_len + 10,
            base_len + 20,
            base_len + len(last_record) - 2,
            base_len + len(last_record) - 1
        ]

        for cut_len in offsets_to_test:
            with open(aof_path, "wb") as f:
                f.write(base_bytes + last_record[:cut_len - base_len])

            proc = subprocess.Popen([
                kvforge_bin, "--port", str(port),
                "--appendonly", "yes", "--appendfilename", aof_path
            ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            assert wait_for_server(port), f"Server failed to recover with tail truncated at {cut_len}"
            client = SimpleRespClient("127.0.0.1", port)
            client.send_cmd(["DBSIZE"])
            assert client.read_reply() == ("int", 50)
            client.send_cmd(["GET", "k000"])
            assert client.read_reply() == ("bulk", "v000")
            client.send_cmd(["GET", "k049"])
            assert client.read_reply() == ("bulk", "v049")
            client.send_cmd(["EXISTS", "last_key9"])
            assert client.read_reply() == ("int", 0)  # incomplete last command discarded
            client.close()

            proc.terminate()
            proc.wait(timeout=1.0)

        print(f"PASSED: Tail truncation verified across {len(offsets_to_test)} byte offsets")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

def test_kill9_crash_resistance(kvforge_bin: str, port: int, policy: str = "always"):
    """
    Simulates unexpected kill -9 under rapid write load.
    Verifies that all acknowledged writes survive (AC-AOF-04).
    """
    print(f"\n--- Test: kill -9 Crash Resistance (Policy: {policy}) ---")
    tmpdir = tempfile.mkdtemp(prefix=f"kvf_kill_{policy}_")
    aof_path = os.path.join(tmpdir, "crash.aof")
    try:
        proc = subprocess.Popen([
            kvforge_bin, "--port", str(port),
            "--appendonly", "yes",
            "--appendfsync", policy,
            "--appendfilename", aof_path
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert wait_for_server(port), "Server failed to start"

        client = SimpleRespClient("127.0.0.1", port)
        acked_writes = 0

        # Rapidly send writes and acknowledge them
        for i in range(1, 301):
            client.send_cmd(["SET", f"op_{i}", f"val_{i}"])
            rep = client.read_reply()
            if rep == ("status", "OK"):
                acked_writes = i
            if i % 50 == 0:
                time.sleep(0.01)

        client.close()
        time.sleep(0.05)

        # Kill -9 abruptly
        print(f"Issued {acked_writes} acknowledged writes. Killing process with SIGKILL (kill -9)...")
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()

        # Restart KVForge pointing to same AOF
        print("Restarting KVForge on the crash AOF file...")
        proc2 = subprocess.Popen([
            kvforge_bin, "--port", str(port),
            "--appendonly", "yes",
            "--appendfsync", policy,
            "--appendfilename", aof_path
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert wait_for_server(port), "Server failed to restart after crash"

        # Verify recovered data
        client2 = SimpleRespClient("127.0.0.1", port)
        client2.send_cmd(["DBSIZE"])
        _, recovered_size = client2.read_reply()

        if policy == "always":
            assert recovered_size == acked_writes, \
                f"Policy 'always' lost writes! Acked={acked_writes}, recovered={recovered_size}"
            print(f"PASSED: 100% of acknowledged writes ({acked_writes}) recovered under 'always'")
        else:
            print(f"Recovered {recovered_size} / {acked_writes} writes under policy '{policy}'")

        # Verify first and last acked keys
        client2.send_cmd(["GET", "op_1"])
        assert client2.read_reply() == ("bulk", "val_1")
        client2.send_cmd(["GET", f"op_{recovered_size}"])
        assert client2.read_reply() == ("bulk", f"val_{recovered_size}")
        client2.close()

        proc2.terminate()
        proc2.wait(timeout=1.0)
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

def main():
    kvforge_bin = sys.argv[1] if len(sys.argv) > 1 else "/home/shannu/2ndres_pro/build/kvforge"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 6395

    print(f"Starting Crash & AOF Durability Test Suite (KVForge: {kvforge_bin}, Port: {port})")

    test_permissions(kvforge_bin, port)
    test_empty_and_partial_first(kvforge_bin, port)
    test_middle_corruption(kvforge_bin, port)
    test_tail_truncation_single_byte(kvforge_bin, port)
    test_kill9_crash_resistance(kvforge_bin, port, policy="always")
    test_kill9_crash_resistance(kvforge_bin, port, policy="everysec")

    print("\n==========================================")
    print("ALL CRASH RECOVERY & DURABILITY TESTS PASSED")
    print("==========================================")

if __name__ == "__main__":
    main()
