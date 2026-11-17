#!/usr/bin/env python3
"""
run_diff.py - Differential testing runner comparing KVForge with Redis 7.x.
Implements the specification in docs/testing/TEST_PLAN.md §6.
"""

import socket
import sys
import os
import time
import json
import random
import subprocess
import argparse
from typing import List, Tuple, Any, Optional, Dict
from gen_commands import generate_command_sequence, format_resp

class RespParser:
    """Lightweight incremental RESP parser for raw socket streams."""
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.buf = bytearray()

    def _read_more(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise ConnectionResetError("Connection closed by server")
        self.buf.extend(chunk)

    def _read_line(self) -> bytes:
        while True:
            idx = self.buf.find(b"\r\n")
            if idx != -1:
                line = bytes(self.buf[:idx])
                del self.buf[:idx + 2]
                return line
            self._read_more()

    def _read_bytes(self, n: int) -> bytes:
        while len(self.buf) < n + 2:  # includes trailing \r\n
            self._read_more()
        data = bytes(self.buf[:n])
        del self.buf[:n + 2]
        return data

    def parse_reply(self) -> Tuple[str, Any]:
        """
        Returns (reply_type, value) where reply_type is one of:
        'status', 'error', 'int', 'bulk', 'array', 'null'
        """
        while not self.buf:
            self._read_more()

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
            return ("bulk", self._read_bytes(length))
        elif prefix == '*':
            count = int(self._read_line().decode("utf-8"))
            if count == -1:
                return ("null", None)
            items = []
            for _ in range(count):
                items.append(self.parse_reply())
            return ("array", items)
        else:
            raise ValueError(f"Unknown RESP prefix: {repr(prefix)}")

class DiffClient:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.parser: Optional[RespParser] = None

    def connect(self, timeout=5.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((self.host, self.port))
        self.parser = RespParser(self.sock)

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
            self.parser = None

    def send_raw(self, data: bytes):
        self.sock.sendall(data)

    def send_command(self, cmd_args: List[str]):
        data = format_resp(cmd_args)
        self.send_raw(data)

    def read_reply(self) -> Tuple[str, Any]:
        return self.parser.parse_reply()

def normalize_error(err: str) -> str:
    # Strip Redis 7 details like ", with args beginning with: ..."
    if err.startswith("ERR unknown command"):
        return "ERR unknown command"
    if err.startswith("ERR syntax error"):
        return "ERR syntax error"
    if err.startswith("ERR wrong number of arguments"):
        return "ERR wrong number of arguments"
    if err.startswith("ERR value is not an integer"):
        return "ERR value is not an integer or out of range"
    if "would overflow" in err:
        return "ERR increment or decrement would overflow"
    if err.startswith("ERR invalid expire time"):
        return "ERR invalid expire time"
    return err

def compare_replies(cmd: List[str], reply_redis: Tuple[str, Any], reply_kvf: Tuple[str, Any]) -> Tuple[bool, str]:
    type_r, val_r = reply_redis
    type_k, val_k = reply_kvf

    cmd_name = cmd[0].upper()

    # Special handling for TTL/PTTL tolerances
    if cmd_name == "TTL" and type_r == "int" and type_k == "int":
        # Can differ by at most 1 second due to clock tick timing
        if val_r < 0 or val_k < 0:
            if val_r == val_k:
                return (True, "")
            return (False, f"TTL special code mismatch: redis={val_r}, kvforge={val_k}")
        diff = abs(val_r - val_k)
        if diff <= 1:
            return (True, "")
        return (False, f"TTL delta {diff} > 1s (redis={val_r}, kvforge={val_k})")

    if cmd_name == "PTTL" and type_r == "int" and type_k == "int":
        if val_r < 0 or val_k < 0:
            if val_r == val_k:
                return (True, "")
            return (False, f"PTTL special code mismatch: redis={val_r}, kvforge={val_k}")
        diff = abs(val_r - val_k)
        if diff <= 2000:
            return (True, "")
        return (False, f"PTTL delta {diff} > 2000ms (redis={val_r}, kvforge={val_k})")

    # Error normalization
    if type_r == "error" and type_k == "error":
        norm_r = normalize_error(val_r)
        norm_k = normalize_error(val_k)
        if norm_r == norm_k or val_r == val_k:
            return (True, "")
        return (False, f"Error mismatch: redis={repr(val_r)} kvforge={repr(val_k)}")

    # Standard types
    if type_r != type_k:
        return (False, f"Type mismatch: redis={type_r} kvforge={type_k} (values: {val_r} vs {val_k})")

    if type_r in ("status", "int", "bulk", "null"):
        if val_r != val_k:
            return (False, f"Value mismatch ({type_r}): redis={val_r} kvforge={val_k}")
        return (True, "")

    if type_r == "array":
        if len(val_r) != len(val_k):
            return (False, f"Array length mismatch: redis={len(val_r)} kvforge={len(val_k)}")
        for i, (item_r, item_k) in enumerate(zip(val_r, val_k)):
            ok, reason = compare_replies(cmd, item_r, item_k)
            if not ok:
                return (False, f"Array element {i} mismatch: {reason}")
        return (True, "")

    return (False, f"Unhandled reply comparison: {type_r}")

def run_sequence(client_r: DiffClient, client_k: DiffClient, commands: List[List[str]]) -> Optional[Tuple[int, List[str], Any, Any, str]]:
    """Runs a sequence of commands on both clients. Returns mismatch details if any."""
    for idx, cmd in enumerate(commands):
        client_r.send_command(cmd)
        client_k.send_command(cmd)
        rep_r = client_r.read_reply()
        rep_k = client_k.read_reply()
        ok, reason = compare_replies(cmd, rep_r, rep_k)
        if not ok:
            return (idx, cmd, rep_r, rep_k, reason)
    return None

def run_keys_set_differential(client_r: DiffClient, client_k: DiffClient, seed: int):
    """Verifies KEYS patterns compared as sets on a seeded dataset (TEST_PLAN §6.5)."""
    rng = random.Random(seed)
    client_r.send_command(["FLUSHALL"])
    client_k.send_command(["FLUSHALL"])
    client_r.read_reply()
    client_k.read_reply()

    # Seed dataset
    keys = ["apple", "apricot", "avocado", "banana", "blueberry", "blackberry",
            "cherry", "cranberry", "date", "fig", "grape", "k1", "k2", "key_10", "k_special"]
    for k in keys:
        cmd = ["SET", k, "v"]
        client_r.send_command(cmd)
        client_k.send_command(cmd)
        client_r.read_reply()
        client_k.read_reply()

    patterns = ["*", "a*", "?b*", "*berry", "[ab]*", "[^a]*", "c*", "nonexistent*"]
    for pat in patterns:
        client_r.send_command(["KEYS", pat])
        client_k.send_command(["KEYS", pat])
        _, rep_r = client_r.read_reply()
        _, rep_k = client_k.read_reply()
        
        set_r = set(item[1] for item in rep_r)
        set_k = set(item[1] for item in rep_k)
        if set_r != set_k:
            raise AssertionError(f"KEYS pattern '{pat}' set mismatch: redis={set_r} kvforge={set_k}")

def run_pipelined_differential(client_r: DiffClient, client_k: DiffClient, seed: int, pipe_size: int = 50):
    """Pipelined sequences: send pipe_size commands per write and compare the reply stream."""
    rng = random.Random(seed)
    seq = generate_command_sequence(rng, pipe_size)
    
    # Send all commands in single payload
    payload = b"".join(format_resp(c) for c in seq)
    client_r.send_raw(payload)
    client_k.send_raw(payload)

    for idx, cmd in enumerate(seq):
        rep_r = client_r.read_reply()
        rep_k = client_k.read_reply()
        ok, reason = compare_replies(cmd, rep_r, rep_k)
        if not ok:
            raise AssertionError(f"Pipeline mismatch at cmd {idx} ({cmd}): {reason}")

def run_chunked_differential(client_r: DiffClient, client_k: DiffClient, seed: int):
    """Split-write delivery: send commands in small random chunk sizes (TEST_PLAN §6.5)."""
    rng = random.Random(seed)
    seq = generate_command_sequence(rng, 20)
    payload = b"".join(format_resp(c) for c in seq)

    # Send payload in small random chunks (1 to 16 bytes)
    offset = 0
    while offset < len(payload):
        chunk_len = rng.randint(1, 16)
        chunk = payload[offset:offset + chunk_len]
        offset += len(chunk)
        client_r.send_raw(chunk)
        client_k.send_raw(chunk)

    for idx, cmd in enumerate(seq):
        rep_r = client_r.read_reply()
        rep_k = client_k.read_reply()
        ok, reason = compare_replies(cmd, rep_r, rep_k)
        if not ok:
            raise AssertionError(f"Chunked delivery mismatch at cmd {idx} ({cmd}): {reason}")

def shrink_reproduction(client_r: DiffClient, client_k: DiffClient, commands: List[List[str]]) -> List[List[str]]:
    """Delta-debugging shrinking: isolates the minimal failing sub-sequence."""
    print("Shrinking failing sequence with delta-debugging...")
    current = list(commands)
    
    # Simple binary reduction
    changed = True
    while changed and len(current) > 2:
        changed = False
        # Try removing chunks from current (keeping the initial FLUSHALL)
        for i in range(1, len(current)):
            candidate = [current[0]] + current[1:i] + current[i+1:]
            res = run_sequence(client_r, client_k, candidate)
            if res is not None:
                current = candidate
                changed = True
                break
    return current

def main():
    parser = argparse.ArgumentParser(description="Differential test runner: KVForge vs Redis 7.x")
    parser.add_argument("--redis-port", type=int, default=6390, help="Port of running Redis server")
    parser.add_argument("--kvforge-port", type=int, default=6391, help="Port of running KVForge server")
    parser.add_argument("--sequences", type=int, default=10000, help="Number of sequences to run (default 10,000 for AC-DIFF-01)")
    parser.add_argument("--cmds-per-seq", type=int, default=50, help="Commands per sequence")
    parser.add_argument("--seed", type=int, default=12345, help="RNG seed")
    parser.add_argument("--skip-full", action="store_true", help="Run smoke test (100 sequences) instead of full 10,000")
    args = parser.parse_args()

    num_seq = 100 if args.skip_full else args.sequences

    client_r = DiffClient("127.0.0.1", args.redis_port)
    client_k = DiffClient("127.0.0.1", args.kvforge_port)

    print(f"Connecting to Redis on port {args.redis_port} and KVForge on port {args.kvforge_port}...")
    client_r.connect()
    client_k.connect()
    print("Connected.")

    # 1. Run KEYS set differential
    print("Running KEYS set differential test...")
    run_keys_set_differential(client_r, client_k, args.seed)
    print("KEYS set differential: PASSED")

    # 2. Run Pipelined differential
    print("Running Pipelined differential test...")
    run_pipelined_differential(client_r, client_k, args.seed + 1, pipe_size=50)
    print("Pipelined differential: PASSED")

    # 3. Run Chunked/split-write differential
    print("Running Split-write chunked differential test...")
    run_chunked_differential(client_r, client_k, args.seed + 2)
    print("Split-write chunked differential: PASSED")

    # 4. Main Sequence Differential Test
    print(f"Running Main Differential Suite: {num_seq} sequences (commands={num_seq * args.cmds_per_seq})...")
    rng = random.Random(args.seed)
    total_commands = 0

    start_time = time.time()
    for s_idx in range(num_seq):
        seq = generate_command_sequence(rng, args.cmds_per_seq)
        total_commands += len(seq)
        mismatch = run_sequence(client_r, client_k, seq)
        if mismatch is not None:
            cmd_idx, cmd, rep_r, rep_k, reason = mismatch
            print(f"\n[FAIL] Mismatch in sequence {s_idx+1} at cmd index {cmd_idx}: {' '.join(cmd)}")
            print(f"  Redis reply:   {rep_r}")
            print(f"  KVForge reply: {rep_k}")
            print(f"  Reason:        {reason}")
            shrunk = shrink_reproduction(client_r, client_k, seq)
            print("\nMinimal reproducing sequence:")
            for sc in shrunk:
                print("  " + " ".join(sc))
            sys.exit(1)

        if (s_idx + 1) % 500 == 0 or s_idx == num_seq - 1:
            elapsed = time.time() - start_time
            print(f"  Progress: {s_idx + 1}/{num_seq} sequences completed ({total_commands} cmds, {elapsed:.1f}s)")

    elapsed = time.time() - start_time
    print(f"\nDifferential: sequences={num_seq} commands={total_commands} mismatches=0 seed={args.seed} elapsed={elapsed:.2f}s")
    print("ALL DIFFERENTIAL TESTS PASSED.")

if __name__ == "__main__":
    main()
