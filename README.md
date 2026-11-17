# KVForge

> A high-performance, Redis-compatible in-memory key-value store built from scratch in C++17 on Linux.

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen)](#8-correctness)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-lightgrey)](#7-architecture)
[![Coverage](https://img.shields.io/badge/coverage-88%25%2B-brightgreen)](#8-correctness)


---

## 2. Description

KVForge is an educational, production-minded implementation of a Redis-compatible in-memory key-value store engineered in modern C++17 without external runtime dependencies. Designed around a single-threaded level-triggered `epoll` event loop, KVForge implements core RESP2 wire protocol serialization and parsing, power-of-two separate-chaining hash tables with non-blocking incremental rehashing, dual lazy and active min-heap key expiration, and crash-resilient Append-Only File (AOF) persistence with configurable fsync policies (`always`, `everysec`, `no`). KVForge speaks standard Redis wire protocol and is fully operable using official Redis tools including `redis-cli` and `redis-benchmark`.

**Compatibility Note:** KVForge implements a curated subset of Redis commands over the RESP2 protocol. It is intended for systems programming study, architectural exploration, and embeddable key-value workloads. It does not implement Redis clustering, replication, Lua scripting, transactions (`MULTI`/`EXEC`), or ACLs.

---

## 3. Highlights

- **Level-Triggered `epoll` Event Loop:** Single-threaded non-blocking asynchronous network engine with unfairness caps (16 commands per socket cycle) avoiding client starvation and handling up to 10,000 concurrent client connections.
- **RESP2 Protocol Engine:** Zero-copy, resumable streaming parser capable of transparently handling arbitrary TCP packet fragmentations, chunked pipelines, and inline commands with strict resource bounding.
- **Incremental Rehashing Storage:** Custom power-of-two separate-chaining hash table with seeded FNV-1a hashing that amortizes dictionary resizes across read/write operations with zero stop-the-world latency spikes.
- **Crash-Resilient AOF Persistence:** Append-only persistence with `always`, `everysec`, and `no` fsync policies, automatic partial tail truncation recovery via `ftruncate()`, and absolute millisecond timestamp normalization.
- **Differential-Tested Correctness:** Verified across 10,000 deterministic random command sequences and 510,000 operations against real Redis 7.2.4 with zero mismatches, backed by LibFuzzer harnesses, ASan/UBSan sanitizers, and $\ge 88\%$ line coverage across all core modules.

---

## 4. Quick Start

### Build and Run

```bash
git clone https://github.com/shanmukh00001/KVForge.git
cd KVForge
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kvforge --port 6380 &
redis-cli -p 6380 PING        # PONG
```

### Interactive `redis-cli` Session

```bash
$ redis-cli -p 6380
127.0.0.1:6380> SET username "shanmukh"
OK
127.0.0.1:6380> GET username
"shanmukh"
127.0.0.1:6380> INCR page_views
(integer) 1
127.0.0.1:6380> INCRBY page_views 5
(integer) 6
127.0.0.1:6380> EXPIRE page_views 60
(integer) 1
127.0.0.1:6380> TTL page_views
(integer) 58
127.0.0.1:6380> KEYS *
1) "username"
2) "page_views"
127.0.0.1:6380> DBSIZE
(integer) 2
127.0.0.1:6380> INFO keyspace
# Keyspace
db0:keys=2,expires=1,avg_ttl=58000
```

---

## 5. Supported Commands

| Command | Syntax | Arity | Mutating? | Description |
|---|---|---|---|---|
| **PING** | `PING [message]` | -1 | No | Ping the server; returns `PONG` or echoed message. |
| **ECHO** | `ECHO message` | 2 | No | Echo the given string. |
| **COMMAND** | `COMMAND` | -1 | No | Returns empty array for client library capability discovery. |
| **CONFIG** | `CONFIG GET <parameter>` | -2 | No | Stub returning empty array for benchmark compatibility. |
| **INFO** | `INFO [section]` | -1 | No | Introspection info (`all`, `server`, `clients`, `memory`, `persistence`, `stats`, `keyspace`). |
| **GET** | `GET key` | 2 | No | Get the string value of a key. |
| **SET** | `SET key value [EX s] [PX ms] [EXAT ts] [PXAT ts] [NX\|XX]` | -3 | Yes | Set string value with optional expiration and existence guards. |
| **DEL** | `DEL key [key ...]` | -2 | Yes | Delete one or more keys; returns count deleted. |
| **EXISTS** | `EXISTS key [key ...]` | -2 | No | Determine how many keys exist. |
| **MSET** | `MSET key value [key value ...]` | -3 | Yes | Set multiple keys to multiple values atomically. |
| **MGET** | `MGET key [key ...]` | -2 | No | Get the values of all specified keys. |
| **INCR** | `INCR key` | 2 | Yes | Increment integer value of key by 1. |
| **DECR** | `DECR key` | 2 | Yes | Decrement integer value of key by 1. |
| **INCRBY** | `INCRBY key increment` | 3 | Yes | Increment integer value by specified offset. |
| **DECRBY** | `DECRBY key decrement` | 3 | Yes | Decrement integer value by specified offset. |
| **DBSIZE** | `DBSIZE` | 1 | No | Return the total number of keys in the database. |
| **FLUSHALL** | `FLUSHALL [ASYNC\|SYNC]` | -1 | Yes | Remove all keys from the current database. |
| **EXPIRE** | `EXPIRE key seconds` | 3 | Yes | Set a key's time to live in seconds. |
| **PEXPIRE** | `PEXPIRE key milliseconds` | 3 | Yes | Set a key's time to live in milliseconds. |
| **PEXPIREAT** | `PEXPIREAT key timestamp_ms` | 3 | Yes | Set key deadline using absolute Unix timestamp in milliseconds. |
| **TTL** | `TTL key` | 2 | No | Get remaining time to live in seconds (-1 no TTL, -2 not found). |
| **PTTL** | `PTTL key` | 2 | No | Get remaining time to live in milliseconds (-1 no TTL, -2 not found). |
| **PERSIST** | `PERSIST key` | 2 | Yes | Remove existing timeout on key. |
| **KEYS** | `KEYS pattern` | 2 | No | Find all keys matching glob pattern (`*`, `?`, `[abc]`, `[a-z]`, `\x`). |

---

## 6. Configuration

| Flag | Type | Default | Allowed Values | Description |
|---|---|---|---|---|
| `--bind` | String | `127.0.0.1` | Valid IPv4 address | Network interface address to bind server listener. |
| `--port` | Integer | `6380` | `1..65535` | TCP listening port. |
| `--max-clients` | Integer | `10000` | `1..100000` | Maximum simultaneous client connections before rejecting new ones. |
| `--appendonly` | String | `no` | `yes`, `no` | Enable Append-Only File (AOF) durability. |
| `--appendfsync` | String | `everysec` | `always`, `everysec`, `no` | File synchronization policy for disk writes. |
| `--appendfilename` | String | `kvforge.aof` | Non-empty string | Path to the AOF log file on disk. |
| `--log-level` | String | `INFO` | `DEBUG`, `INFO`, `WARN`, `ERROR` | Diagnostic logging verbosity. |
| `--help` | Flag | - | - | Print command-line usage manual and exit. |
| `--version` | Flag | - | - | Print binary version information and exit. |

---

## 7. Architecture

```
               +---------------------------+
               | redis-cli / Client Apps   |
               +-------------+-------------+
                             |  TCP (RESP2)
                             v
               +---------------------------+
               |  Non-Blocking Sockets     |
               +-------------+-------------+
                             |
                             v
+---------------------------------------------------------+
| Level-Triggered epoll EventLoop (Single Threaded)       |
| - Fairness scheduling (16 cmds/sock/iteration)          |
| - Resumable streaming RESP parser                       |
| - Command dispatcher & arity validation                 |
+----------------------------+----------------------------+
                             |
        +--------------------+--------------------+
        |                                         |
        v                                         v
+-------------------------+             +-------------------------+
| Incremental Hash Table  |             | Hybrid Expiry Engine    |
| - Power-of-two sizing   |             | - Lazy check on lookup  |
| - Separate chaining     |             | - Active min-heap sweep |
| - Two-table migration   |             | - Deadline validation   |
|   (ht[0] -> ht[1])      |             +-------------------------+
+-------------------------+
        |
        v
+---------------------------------------------------------+
| Append-Only File (AOF) Persistence                      |
| - Absolute epoch timestamp normalization (PEXPIREAT)    |
| - Selective fsync (always / everysec / no)              |
| - Crash recovery with ftruncate() of partial tails      |
+---------------------------------------------------------+
```

KVForge executes all networking, command dispatch, storage manipulation, and background maintenance within a single-threaded level-triggered `epoll` event loop ([ADR-001](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-001-single-threaded-command-execution), [ADR-002](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-002-level-triggered-epoll)). Client sockets are monitored for readiness; incoming byte streams are parsed without heap copying using a state-machine RESP parser ([ADR-014](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-014-incremental-resumable-resp-parser-with-hard-resource-limits)). Commands interact with a dual-table dictionary that migrates entries incrementally during load spikes ([ADR-004](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-004-incremental-rehashing)), and mutating operations are serialized into the append-only log with absolute epoch timestamps to guarantee deterministic restart replay ([ADR-008](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-008-absolute-timestamps-in-the-aof)).

Detailed architectural records and design trade-offs are documented in [ARCHITECTURE_DECISIONS.md](docs/architecture/ARCHITECTURE_DECISIONS.md).

---

## 8. Correctness

KVForge implements a multi-layered verification strategy ensuring conformance to the Redis specification, durability across crash events, and memory safety:

- **Unit & Component Testing:** 45 automated unit tests executed via GoogleTest covering configuration parsing, socket non-blocking flags, buffer compaction, RESP serialization/parsing, hash table rehash pauses, safe integer arithmetic, min-heap TTL ordering, AOF recovery, and glob pattern matching.
- **Differential Testing (`tests/differential/`):** Automated differential fuzzing against official Redis 7.2.4 over raw TCP sockets. The runner generated **10,000 deterministic sequences (510,000 commands)** covering single operations, multi-command pipelines, and split-packet TCP writes with zero mismatches (`AC-DIFF-01`).
- **Crash Recovery & Durability Testing (`tests/crash/`):**
  - **`kill -9` Recovery:** Verified 100% durability of acknowledged writes (300/300) under `appendfsync always` (`AC-AOF-04`).
  - **Byte-by-Byte Tail Truncation:** Tested truncated writes across 7 byte offsets in incomplete RESP arrays; successfully verified clean recovery of all 50 preceding records after truncating the invalid tail via `ftruncate()` (`AC-AOF-05`).
  - **Middle Corruption Detection:** Verified that byte corruptions inside historical records are rejected with non-zero exit code and corruption offset logs (`AC-AOF-06`).
- **Fuzz Testing (`tests/fuzz/`):**
  - `fuzz_resp_parser.cpp`: LibFuzzer harness verifying stream chunking invariance (single-byte vs arbitrary packet splits yield identical parsed commands). 67,000+ runs with zero findings.
  - `fuzz_aof_loader.cpp`: LibFuzzer harness verifying AOF parser never crashes, memory-leaks, or hangs on corrupted log input.
- **Sanitizers:** Clean execution under Clang 14 AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) with zero leaks or corruptions (`AC-QUA-01`).
- **Line Coverage:** GCC `--coverage` line coverage exceeds 88% across all core modules (`AC-QUA-03`):
  - `src/expiry/expiry_index.cpp`: **100.0%**
  - `src/util/int_parse.cpp`: **100.0%**
  - `src/protocol/resp_parser.cpp`: **93.3%**
  - `src/store/hash_table.cpp`: **90.1%**
  - `src/persistence/aof.cpp`: **88.0%**

---

## 9. Performance

Benchmarks were conducted on an Intel Core i7-1255U (12 cores, 7.8 GB RAM) running Linux WSL2, comparing KVForge v0.2.0 against Redis 7.2.4 using official `redis-benchmark`. Both servers were evaluated on local loopback under identical configurations (in-memory, standalone, 50 concurrent client connections, 64-byte payloads, 1,000,000 randomized keys).

Each scenario was warmed up with 200,000 operations, followed by 5 independent runs of 1,000,000 requests to compute the median:

| Scenario | Server | Throughput (req/s) | p50 (ms) | p99 (ms) | RSS at 1M keys (MiB) | Raw Data |
|---|---|---|---|---|---|---|
| **SET, P=1** | KVForge | 139,567 | 0.183 | 0.663 | 195.1 | [kvforge_set_P1_run1.csv](bench/results/raw/kvforge_set_P1_run1.csv) |
| **SET, P=1** | Redis 7.2.4 | 160,205 | 0.159 | 0.639 | 109.7 | [redis_set_P1_run1.csv](bench/results/raw/redis_set_P1_run1.csv) |
| **GET, P=1** | KVForge | 140,272 | 0.183 | 0.647 | 195.1 | [kvforge_get_P1_run1.csv](bench/results/raw/kvforge_get_P1_run1.csv) |
| **GET, P=1** | Redis 7.2.4 | 159,719 | 0.159 | 0.639 | 109.7 | [redis_get_P1_run1.csv](bench/results/raw/redis_get_P1_run1.csv) |
| **SET, P=16** | KVForge | 509,944 | 1.455 | 3.319 | 195.1 | [kvforge_set_P16_run1.csv](bench/results/raw/kvforge_set_P16_run1.csv) |
| **SET, P=16** | Redis 7.2.4 | 665,779 | 1.103 | 1.943 | 109.7 | [redis_set_P16_run1.csv](bench/results/raw/redis_set_P16_run1.csv) |
| **GET, P=16** | KVForge | 593,824 | 1.335 | 2.863 | 195.1 | [kvforge_get_P16_run1.csv](bench/results/raw/kvforge_get_P16_run1.csv) |
| **GET, P=16** | Redis 7.2.4 | 751,315 | 0.983 | 1.631 | 109.7 | [redis_get_P16_run1.csv](bench/results/raw/redis_get_P16_run1.csv) |

*Full methodology, analysis, and environment metadata are documented in the [Benchmark Report](bench/BENCHMARK_REPORT.md).*

---

## 10. Design Decisions

The development of KVForge was guided by explicit architectural trade-offs documented in the project's Architecture Decision Records:

- [ADR-001: Single-Threaded Command Execution](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-001-single-threaded-command-execution) — Locks avoided; deterministic state transitions; bounded latency.
- [ADR-002: Level-Triggered epoll](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-002-level-triggered-epoll) — Prevents lost-wakeup bugs; simplicity and safety over edge-triggered complexity.
- [ADR-003: Custom Hash Table with Separate Chaining](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-003-custom-hash-table-with-separate-chaining) — Direct control of memory layout and power-of-two mask indexing.
- [ADR-004: Incremental Rehashing](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-004-incremental-rehashing) — Dual-table migration amortizing resize latency to zero spikes.
- [ADR-005: Seeded FNV-1a Hash with Finalizer](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-005-seeded-fnv-1a-hash-with-finalizer) — Protection against algorithmic hash-flooding DoS.
- [ADR-006: Hybrid Expiry: Lazy + Active Min-Heap](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-006-hybrid-expiry-lazy-plus-active-via-min-heap) — Guarantees deterministic deadline eviction.
- [ADR-007: Append-Only File Only (No RDB)](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-007-append-only-file-only-no-rdb) — Transparent human-readable audit trail and predictable replay.
- [ADR-008: Absolute Timestamps in AOF](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-008-absolute-timestamps-in-the-aof) — Converts relative TTLs to absolute epoch ms (`PEXPIREAT`).
- [ADR-009: Three Fsync Policies](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-009-three-fsync-policies) — Operator flexibility between strict durability and raw throughput.
- [ADR-010: RESP2 Protocol Only](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-010-resp2-only) — Universal compatibility with all standard Redis client libraries.
- [ADR-011: Loopback Bind by Default](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-011-loopback-bind-by-default) — Safe-by-default network security stance.
- [ADR-012: Differential Testing Against Real Redis](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-012-differential-testing-against-real-redis) — Automatic specification compliance validation.
- [ADR-013: Approximate Memory Accounting](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-013-approximate-memory-accounting) — Low-overhead runtime memory tracking.
- [ADR-014: Resumable RESP Parser with Hard Resource Limits](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-014-incremental-resumable-resp-parser-with-hard-resource-limits) — Bounded inline commands, bulk strings, buffer allocations, and fairness scheduling.
- [ADR-015: No External Runtime Dependencies](docs/architecture/ARCHITECTURE_DECISIONS.md#adr-015-no-external-runtime-dependencies) — Pure C++17 and Linux POSIX APIs for transparency and portability.

---

## 11. Known Limitations

In accordance with the project specification (`AC-DOC-03`), the current Tier 2 release of KVForge has several deliberate design limitations:

1. **Subset of Commands:** KVForge implements 24 core Redis commands focusing on strings, keys, expiration, and server introspection. Advanced collections (Hashes, Sets, Sorted Sets, Lists, Streams, Bitmaps, HyperLogLog) and transactions (`MULTI`/`EXEC`) are not supported.
2. **No Replication or Clustering:** KVForge operates solely in standalone mode. Replication trees, cluster gossip protocol, Redis Sentinel, and automatic failover are omitted.
3. **No Authentication or TLS:** All network communication is unencrypted plaintext without password authentication (`AUTH`). The server binds to `127.0.0.1` by default; when exposed to untrusted networks, it must reside behind a secure reverse proxy or TLS termination tunnel.
4. **Single-Threaded Blocking Commands:** Commands such as `KEYS <pattern>` and `FLUSHALL` execute synchronously on the main thread and scan the entire keyspace. Invoking `KEYS` on datasets with millions of items will block all other clients during traversal.
5. **Approximate Memory Accounting:** Memory values reported in `INFO memory` are incremental estimations based on string capacities and structure overhead, rather than exact heap-allocator telemetry. Process RSS from `/proc/<pid>/status` should be monitored for OS-level allocation boundaries.
6. **Linux Platform Specificity:** Network multiplexing relies directly on Linux-specific `epoll` syscalls and POSIX process controls (`getrlimit`, `ftruncate`, `fsync`). Running on macOS or Windows requires WSL2 or containerized Linux environments.

---

## 12. Roadmap

Future development directions (Tier 3 options):

- **Option A: Replication and Distributed Sync:** Master-replica asynchronous replication stream, replica heartbeat, and read-replica offloading.
- **Option B: Memory Eviction Policies:** Implementation of `maxmemory` enforcement with approximated LRU / LFU eviction algorithms and volatile-TTL purging.
- **Option C: Background AOF Rewrite (`BGREWRITEAOF`):** Non-blocking copy-on-write log compaction using Linux `fork()` to snapshot current memory and replace historical append logs with minimal delta commands.

---

## 13. Project Layout

```
kvforge/
├── CMakeLists.txt              # Build configuration (strict warnings, C++17)
├── bench/                      # Automated benchmarking suite
│   ├── run.sh                  # Matrix runner (SET/GET, P=1/16, RSS measurement)
│   ├── collect.py              # CSV aggregator and markdown table generator
│   ├── BENCHMARK_REPORT.md     # Official benchmark report
│   └── results/raw/            # Raw redis-benchmark CSVs and memory telemetry
├── docs/                       # Architecture, testing, and UI specifications
│   ├── ACCEPTANCE_CRITERIA.md  # Acceptance criteria trace matrix
│   ├── architecture/           # Architecture Decision Records (ADR-001..015)
│   ├── design/                 # UI and CLI specifications
│   └── testing/                # Test plan and verification suites
├── src/                        # Production source code
│   ├── commands/               # Command implementations and registration table
│   ├── config/                 # Command-line arguments and configuration parsing
│   ├── expiry/                 # Active expiry engine and min-heap TTL index
│   ├── net/                    # epoll event loop, listener, connection buffers
│   ├── persistence/            # AOF writer, fsync policies, crash recovery engine
│   ├── protocol/               # RESP2 wire protocol parser and serializer
│   ├── store/                  # Custom hash table with incremental rehashing
│   └── util/                   # Logging, safe integer parsing, glob matching
└── tests/                      # Verification test suites
    ├── crash/                  # kill -9 durability and byte-level truncation tests
    ├── differential/           # Differential test harness vs Redis 7.2.4
    ├── fuzz/                   # LibFuzzer harnesses (RESP parser, AOF loader)
    └── unit/                   # 45 GoogleTest unit tests
```

---

## 14. Contributing and License

Contributions, bug reports, and suggestions are welcome. Please ensure that all pull requests maintain clean compilation (`-Wall -Wextra -Wpedantic -Werror`), zero memory leaks under AddressSanitizer, and passing unit test suites.

Distributed under the [MIT License](LICENSE).

---

## 15. Disclaimer

**KVForge is not affiliated with or endorsed by Redis Ltd.** Redis is a registered trademark of Redis Ltd. All trademarks and registered trademarks are the property of their respective owners.
