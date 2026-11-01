# KVForge — Data Design

KVForge has no SQL schema. Its "database" is an in-memory keyspace plus an append-only file. This document defines both exactly: data structures, algorithms, parameters and the on-disk format.

## 1. Data model
- One keyspace (database 0). No `SELECT`.
- Keys: arbitrary byte strings, length 0 up to 64 MiB (limited by the bulk string limit).
- Values: arbitrary byte strings (type: string only in Tier 1 and Tier 2).
- Each key has an optional absolute expiry time in epoch milliseconds.
- Integer values: a string that parses as a signed 64-bit decimal integer (no leading `+`, no leading zeros except `0`, optional leading `-`, no whitespace) is treated as an integer by `INCR` family commands. The result is stored back as its decimal string.

## 2. Entry layout
```cpp
struct Entry {
    std::string key;
    std::string value;
    int64_t     expire_at_ms;   // -1 when no expiry
    Entry*      next;           // singly-linked bucket chain
};
```
Approximate per-entry overhead: `sizeof(Entry)` plus allocator overhead, plus heap allocations for strings longer than the small-string capacity.

## 3. Hash table

### 3.1 Parameters
| Parameter | Value |
|---|---|
| Collision strategy | Separate chaining (singly linked list per bucket) |
| Bucket count | Always a power of two; index = `hash & (size - 1)` |
| Initial size | 16 buckets |
| Grow trigger | `used >= size` (load factor 1.0) and not already rehashing |
| Grow target | Smallest power of two `>= used * 2` |
| Shrink trigger | `size > 16` and `used * 10 < size` (load factor below 0.1), checked after deletions, not already rehashing |
| Shrink target | Smallest power of two `>= used`, minimum 16 |
| Hash function | 64-bit FNV-1a over the key bytes, XORed with a per-process random 64-bit seed obtained from `getrandom`, followed by a 64-bit finalizer mix (`x ^= x >> 33; x *= 0xff51afd7ed558ccd; x ^= x >> 33`) |
| Seed | Generated once at startup; never persisted |

Known limitation: FNV-1a with a secret seed resists casual collision attacks but is not a cryptographic keyed hash. SipHash-1-3 is the documented upgrade path (see `ARCHITECTURE_DECISIONS.md`, ADR-005).

### 3.2 Two-table structure for incremental rehashing
```cpp
class HashTable {
    struct Table { std::vector<Entry*> buckets; size_t used = 0; };
    Table t0_;                 // primary
    Table t1_;                 // target during rehash (empty otherwise)
    ptrdiff_t rehash_idx_ = -1;  // -1 = not rehashing; else next t0 bucket to migrate
};
```

### 3.3 Operations while rehashing
| Operation | Behavior |
|---|---|
| `find(key)` | Search `t0` bucket for the key's hash; if not found and rehashing, search `t1` bucket |
| `insert(key)` | If rehashing, insert into `t1`; otherwise into `t0` |
| `erase(key)` | Search `t0`, then `t1`; unlink from whichever holds it |
| `upsert(key)` | `find`; update if found; otherwise `insert` |
| Any mutating or lookup call | First perform `rehash_step(1)` if rehashing |
| Event loop idle | `rehash_step` up to 100 buckets within a 1 ms budget per loop iteration |

### 3.4 Rehash algorithm
```
start_rehash(new_size):
    t1.buckets = vector(new_size, nullptr); t1.used = 0; rehash_idx = 0

rehash_step(max_buckets):
    visited_empty_limit = max_buckets * 10        # avoid long scans over empty buckets
    while max_buckets > 0 and rehash_idx < t0.size and visited_empty_limit > 0:
        if t0.buckets[rehash_idx] is empty: rehash_idx++, visited_empty_limit--, continue
        move every entry in this bucket to t1 (recompute index with t1 mask), update used counts
        t0.buckets[rehash_idx] = nullptr; rehash_idx++; max_buckets--
    if rehash_idx == t0.size:
        t0 = move(t1); t1 = empty; rehash_idx = -1       # finished
```
Invariants: every entry exists in exactly one of `t0` or `t1`; buckets of `t0` with index `< rehash_idx` are empty; `used(t0) + used(t1)` equals `size()`.

Maximum pause per operation: one bucket chain migration plus the table allocation at `start_rehash` (allocating the new bucket array is the dominant cost; at 1,000,000 keys it is a 16 MiB zeroed vector and is expected to take well under 10 ms; this is measured in AC-HT-03).

### 3.5 Complexity
Average `O(1)` for find, insert and erase; worst case `O(n)` per chain with adversarial keys (mitigated by the seed).

## 4. Expiry index

### 4.1 Structure
Min-heap ordered by `expire_at_ms`:
```cpp
struct ExpireItem { int64_t at_ms; std::string key; };
std::priority_queue<ExpireItem, std::vector<ExpireItem>, std::greater<>> heap;
```
Plus a counter `live_with_ttl` maintained by the store (number of entries with a TTL).

### 4.2 Rules
1. Whenever a key gets or changes a deadline (`SET ... EX/PX/PXAT`, `EXPIRE`, `PEXPIRE`, `PEXPIREAT`), push `(at_ms, key)`.
2. Removing a TTL (`PERSIST`, plain `SET`, `DEL`) does not touch the heap. The old item becomes stale.
3. **Validation on pop:** an item is acted on only if the key exists and `entry.expire_at_ms == item.at_ms`. Otherwise discard it.
4. **Active cycle:** pop items while `top.at_ms <= now_ms` and the time budget (`--expire-budget-us`, default 1000 µs) is not exhausted. Delete valid ones, increment `expired_keys`, append `DEL key` to the AOF.
5. **Heap hygiene:** when `heap.size() > 2 * live_with_ttl + 1024`, rebuild the heap from the keyspace (iterate entries with a TTL). Amortized cost is low and bounds memory growth from repeated TTL updates.
6. **Next deadline** for the loop timeout is `heap.top().at_ms` (possibly stale; stale items only cause an early wake-up, never a missed expiry).

### 4.3 Lazy expiry
`Store::lookup(key, now_ms)` deletes an entry whose `expire_at_ms != -1 && expire_at_ms <= now_ms` and returns "missing". The deletion appends `DEL key` to the AOF and increments `expired_keys`.

### 4.4 Boundary semantics
A key with `expire_at_ms == now_ms` is expired. `TTL` returns remaining whole seconds rounded **up** from milliseconds (for example 1,500 ms remaining returns 2; 900 ms returns 1; matches Redis behavior for non-expired keys), and never returns 0 for an existing non-expired key.

## 5. Statistics counters (for `INFO`)
| Counter | Updated when |
|---|---|
| `total_connections_received` | Each accepted connection |
| `connected_clients` | Gauge: accept increments, close decrements |
| `total_commands_processed` | After each command that reaches a handler (including handler-level errors such as `INCR` on a non-integer). Unknown-command and wrong-arity errors are not counted |
| `keyspace_hits` / `keyspace_misses` | `GET` and `MGET` per key, `EXISTS`/`TTL`/`PTTL` per key; a hit means the key existed and was not expired |
| `expired_keys` | Each key deleted by expiry (lazy or active) |
| `used_memory` | Maintained incrementally (see SYSTEM_ARCHITECTURE §11) |
| `aof_current_size` | Bytes appended to the AOF since start plus loaded size |
| `rehashing` | `1` while `rehash_idx != -1` |

## 6. AOF file format

### 6.1 Rules
- The file is a concatenation of RESP2 arrays of bulk strings, one array per command. No header, no footer, no checksum in version 1.
- It is readable by `redis-cli --pipe` for compatible commands and by a human with `cat`.
- File is opened with `O_APPEND`, mode `0600`.
- One record is appended per successful state change, in the order the changes happened.

### 6.2 Record normalization
Commands are written in a deterministic, replay-safe form. Anything that depends on the current time or on a precondition is rewritten.

| Executed command | Record written |
|---|---|
| `SET k v` | `SET k v` |
| `SET k v EX 10` / `PX 10000` at time T | `SET k v PXAT <T+deadline_ms>` |
| `SET k v NX` / `XX` that succeeded | `SET k v` (plus `PXAT` when it had an expiry); failed conditionals write nothing |
| `DEL a b c` | `DEL <only the keys that existed>` (nothing if none) |
| `INCR k`, `DECR k`, `INCRBY k n`, `DECRBY k n` | the same command as received (deterministic) |
| `MSET a 1 b 2` | `MSET a 1 b 2` |
| `EXPIRE k s` / `PEXPIRE k ms` that returned 1 | `PEXPIREAT k <absolute ms>` |
| `PERSIST k` that returned 1 | `PERSIST k` |
| `FLUSHALL` | `FLUSHALL` |
| Lazy or active expiry deleting `k` | `DEL k` |
| A command with a non-positive resulting expiry (key deleted immediately) | `DEL k` |

`SET ... PXAT <ms>` is also accepted from clients as a compatible extension (it exists in Redis 6.2+).

Read-only commands and failed commands are never written.

### 6.3 Example
Commands: `SET session abc EX 100` at epoch ms `1760000000000`, then `INCR hits`, then `DEL session`.
File content (RESP, shown with `\r\n` as line ends):
```
*5
$3
SET
$7
session
$3
abc
$4
PXAT
$13
1760000100000
*2
$4
INCR
$4
hits
*2
$3
DEL
$7
session
```

### 6.4 Fsync policies
| Policy | Behavior | Durability |
|---|---|---|
| `always` | `write` then `fsync` after each command batch **before** replies are flushed to clients | Every acknowledged write survives a crash (subject to disk honesty) |
| `everysec` (default) | `write` immediately to the OS; `fsync` at most once per second from the event loop | Up to about 1 second of acknowledged writes can be lost |
| `no` | `write` immediately; never explicitly `fsync` | Depends on OS flush (often up to 30 seconds) |

For `always`, a pipeline of N commands from one client is covered by a single `fsync` after the batch, and replies for that batch are held until the `fsync` returns.

### 6.5 Recovery algorithm
```
load(path):
    if not exists: return
    pos = 0; last_good = 0
    while pos < file_size:
        result = parse_one_record(file, pos)          # same RESP parser
        if result == Incomplete (EOF inside a record):
            truncate file to last_good; log warn "Truncated AOF tail at offset X"; break
        if result == Invalid:
            log error "Corrupt AOF at offset pos"; exit 1
        if record is a SET/PEXPIREAT with absolute time <= now: apply as deletion (skip)
        execute record through command table in replay mode (no AOF append, no reply)
        last_good = new_pos; pos = new_pos
```
Notes:
- A record that parses but fails execution (for example an `INCR` on a non-integer) is a corruption signal: log error with offset, exit 1.
- Replay uses wall-clock `now` at load time, so keys whose deadline passed during downtime are not resurrected.
- If the file ends with a partial record, only that last record is dropped.

### 6.6 Growth and limits (Tier 2)
The AOF grows without bound in Tier 2 (no rewrite). This is a documented limitation. Tier 3 option C (AOF rewrite) addresses it by writing a minimal `SET`/`PXAT` snapshot of current state.

## 7. Memory accounting
`used_memory` is an estimate: `sizeof(HashTable buckets) + Σ(sizeof(Entry) + key.capacity() + value.capacity()) + heap_items * (sizeof(ExpireItem) + key.capacity()) + Σ connection buffer capacities`. It is intentionally approximate and is reported alongside process RSS in benchmark results (RSS is the authoritative figure for comparison with Redis).

## 8. Invariants checked by tests
1. `store.dbsize()` equals the number of reachable, non-expired entries.
2. Every entry exists in exactly one table during rehash.
3. After any sequence of operations and a restart from AOF with `always`, the keyspace equals the pre-restart keyspace (ignoring expired keys).
4. No heap item can cause deletion of a key whose current deadline differs from the item's deadline.