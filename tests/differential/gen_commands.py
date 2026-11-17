#!/usr/bin/env python3
"""
gen_commands.py - Deterministic, weighted RESP command generator for differential testing.
Implements the generator specification in docs/testing/TEST_PLAN.md §6.2.
"""

import random
import time
from typing import List, Union

KEY_POOL = [f"k{i}" for i in range(1, 21)] + ["foo", "bar", "baz", "counter", "temp_token", "alpha", "beta"]
VAL_POOL = [
    "hello", "world", "short", "longer_value_string_1234567890",
    "special_!@#$%^&*()_+", "0", "1", "-1", "999999", "random_payload"
]
INT_POOL = [
    0, 1, -1, 5, -5, 10, -10, 100, -100,
    9223372036854775807,   # LLONG_MAX
    -9223372036854775808,  # LLONG_MIN
    9223372036854775806,
    -9223372036854775807,
    "not_a_number", "123.456", ""
]

def format_resp(args: List[Union[str, bytes]]) -> bytes:
    """Encodes a command arguments list into RESP multibulk wire format."""
    parts = [f"*{len(args)}\r\n".encode("utf-8")]
    for arg in args:
        if isinstance(arg, str):
            arg_bytes = arg.encode("utf-8", errors="surrogateescape")
        else:
            arg_bytes = bytes(arg)
        parts.append(f"${len(arg_bytes)}\r\n".encode("utf-8") + arg_bytes + b"\r\n")
    return b"".join(parts)

def gen_set_command(rng: random.Random) -> List[str]:
    key = rng.choice(KEY_POOL)
    val = rng.choice(VAL_POOL)
    cmd = ["SET", key, str(val)]
    
    # Options: none 60%, EX/PX with large TTL 15%, NX 10%, XX 10%, invalid combos 5%
    roll = rng.random()
    if roll < 0.60:
        pass
    elif roll < 0.75:
        # Large TTL so differential test isn't racy on expiration
        ttl = rng.randint(300, 3600)
        unit = rng.choice(["EX", "PX"])
        if unit == "PX":
            ttl *= 1000
        cmd.extend([unit, str(ttl)])
    elif roll < 0.85:
        cmd.append("NX")
    elif roll < 0.95:
        cmd.append("XX")
    else:
        # Invalid combos
        sub_roll = rng.choice(["both_nx_xx", "negative_ttl", "both_ex_px", "missing_ttl_arg"])
        if sub_roll == "both_nx_xx":
            cmd.extend(["NX", "XX"])
        elif sub_roll == "negative_ttl":
            cmd.extend(["EX", "-10"])
        elif sub_roll == "both_ex_px":
            cmd.extend(["EX", "100", "PX", "100000"])
        elif sub_roll == "missing_ttl_arg":
            cmd.append("EX")
    return cmd

def gen_del_command(rng: random.Random) -> List[str]:
    count = rng.randint(1, 4)
    keys = [rng.choice(KEY_POOL) for _ in range(count)]
    return ["DEL"] + keys

def gen_exists_command(rng: random.Random) -> List[str]:
    count = rng.randint(1, 4)
    keys = [rng.choice(KEY_POOL) for _ in range(count)]
    return ["EXISTS"] + keys

def gen_incr_command(rng: random.Random, name: str) -> List[str]:
    key = rng.choice(KEY_POOL)
    return [name, key]

def gen_incrby_command(rng: random.Random, name: str) -> List[str]:
    key = rng.choice(KEY_POOL)
    delta = rng.choice(INT_POOL)
    return [name, key, str(delta)]

def gen_mset_command(rng: random.Random) -> List[str]:
    count = rng.randint(1, 4)
    args = ["MSET"]
    for _ in range(count):
        args.append(rng.choice(KEY_POOL))
        args.append(str(rng.choice(VAL_POOL)))
    # 10% odd argument count error test
    if rng.random() < 0.10:
        args.append("dangling_key")
    return args

def gen_mget_command(rng: random.Random) -> List[str]:
    count = rng.randint(1, 4)
    return ["MGET"] + [rng.choice(KEY_POOL) for _ in range(count)]

def gen_expire_command(rng: random.Random, name: str) -> List[str]:
    key = rng.choice(KEY_POOL)
    roll = rng.random()
    if roll < 0.95:
        ttl = rng.randint(300, 3600)
        if name == "PEXPIRE":
            ttl *= 1000
    else:
        # 5% zero, negative, or overflow
        ttl = rng.choice([0, -5, 9223372036854775807, "invalid_ttl"])
    return [name, key, str(ttl)]

def gen_pexpireat_command(rng: random.Random) -> List[str]:
    key = rng.choice(KEY_POOL)
    now_ms = int(time.time() * 1000)
    if rng.random() < 0.95:
        # Far-future
        ts = now_ms + rng.randint(300000, 3600000)
    else:
        # Past
        ts = now_ms - rng.randint(10000, 50000)
    return ["PEXPIREAT", key, str(ts)]

def gen_ttl_command(rng: random.Random, name: str) -> List[str]:
    key = rng.choice(KEY_POOL)
    return [name, key]

def gen_wrong_arity_or_unknown(rng: random.Random) -> List[str]:
    roll = rng.random()
    if roll < 0.50:
        known = rng.choice(["GET", "DEL", "EXISTS", "INCR", "DECR", "TTL", "PTTL", "PERSIST", "DBSIZE", "FLUSHALL"])
        # Wrong arity
        if known in ["DBSIZE", "FLUSHALL"]:
            return [known, "extra_arg"]
        elif known in ["GET", "INCR", "DECR", "TTL", "PTTL", "PERSIST"]:
            return [known] if rng.random() < 0.5 else [known, "k1", "k2"]
        else:
            return [known] # DEL / EXISTS with 0 args
    else:
        unknown = rng.choice(["UNKNOWN_CMD", "FOOBAR", "BLAH", "XYZ", "NOT_A_COMMAND", "RANDOMCMD"])
        return [unknown, "arg1", "arg2"]

def generate_command_sequence(rng: random.Random, length: int) -> List[List[str]]:
    """
    Generates a deterministic sequence of Redis commands according to weights in TEST_PLAN §6.2.
    Starts with FLUSHALL.
    """
    commands = [["FLUSHALL"]]
    
    weights = [
        ("SET", 20),
        ("GET", 15),
        ("DEL", 8),
        ("EXISTS", 6),
        ("INCR_DECR", 8),
        ("INCRBY_DECRBY", 6),
        ("MSET", 4),
        ("MGET", 4),
        ("EXPIRE_PEXPIRE", 6),
        ("PEXPIREAT", 2),
        ("TTL_PTTL", 6),
        ("PERSIST", 3),
        ("DBSIZE", 2),
        ("WRONG_UNKNOWN", 3)
    ]
    
    categories = [cat for cat, weight in weights]
    cum_weights = []
    total = 0
    for _, w in weights:
        total += w
        cum_weights.append(total)
        
    for _ in range(length):
        cat = rng.choices(categories, cum_weights=cum_weights, k=1)[0]
        if cat == "SET":
            cmd = gen_set_command(rng)
        elif cat == "GET":
            cmd = ["GET", rng.choice(KEY_POOL)]
        elif cat == "DEL":
            cmd = gen_del_command(rng)
        elif cat == "EXISTS":
            cmd = gen_exists_command(rng)
        elif cat == "INCR_DECR":
            name = rng.choice(["INCR", "DECR"])
            cmd = gen_incr_command(rng, name)
        elif cat == "INCRBY_DECRBY":
            name = rng.choice(["INCRBY", "DECRBY"])
            cmd = gen_incrby_command(rng, name)
        elif cat == "MSET":
            cmd = gen_mset_command(rng)
        elif cat == "MGET":
            cmd = gen_mget_command(rng)
        elif cat == "EXPIRE_PEXPIRE":
            name = rng.choice(["EXPIRE", "PEXPIRE"])
            cmd = gen_expire_command(rng, name)
        elif cat == "PEXPIREAT":
            cmd = gen_pexpireat_command(rng)
        elif cat == "TTL_PTTL":
            name = rng.choice(["TTL", "PTTL"])
            cmd = gen_ttl_command(rng, name)
        elif cat == "PERSIST":
            cmd = ["PERSIST", rng.choice(KEY_POOL)]
        elif cat == "DBSIZE":
            cmd = ["DBSIZE"]
        elif cat == "WRONG_UNKNOWN":
            cmd = gen_wrong_arity_or_unknown(rng)
        else:
            cmd = ["PING"]
        commands.append(cmd)
        
    return commands

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Deterministic RESP command generator")
    parser.add_argument("--seed", type=int, default=12345, help="Random seed")
    parser.add_argument("--length", type=int, default=50, help="Number of commands to generate")
    args = parser.parse_args()
    
    rng = random.Random(args.seed)
    seq = generate_command_sequence(rng, args.length)
    for c in seq:
        print(" ".join(c))
