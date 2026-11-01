# KVForge — Product Requirements Document (PRD)

Version: 1.0 | Status: Approved for implementation

## 1. Overview
KVForge is a Redis-compatible in-memory key-value server written in C++17 for Linux. It listens on a TCP port, parses RESP2 requests, executes commands against an in-memory store, and replies in RESP2. It supports key expiry and optional durability through an append-only file (AOF).

## 2. Definitions
- **RESP2:** Redis Serialization Protocol version 2. Types: simple string `+`, error `-`, integer `:`, bulk string `$`, array `*`. Null bulk string is `$-1\r\n`.
- **Inline command:** A plain text command terminated by `\r\n` (for `telnet` / `nc` use).
- **Pipelining:** A client sends multiple commands without waiting for replies; replies are returned in order.
- **AOF:** Append-only file recording every write command.
- **Lazy expiry:** Deleting an expired key when it is accessed.
- **Active expiry:** Deleting expired keys proactively in the event loop within a time budget.

## 3. Functional requirements

### 3.1 Networking
| ID | Requirement | Priority |
|---|---|---|
| FR-NET-01 | The server shall listen on a configurable TCP address and port (default `127.0.0.1:6380`). | Must |
| FR-NET-02 | The server shall use non-blocking sockets and `epoll` in level-triggered mode in a single-threaded event loop. | Must |
| FR-NET-03 | The server shall accept multiple concurrent clients up to `max_clients` (default 10,000). Excess connections are closed after sending `-ERR max number of clients reached`. | Must |
| FR-NET-04 | Each connection shall have an independent read buffer and write buffer. | Must |
| FR-NET-05 | When a write cannot complete, the server shall register `EPOLLOUT` and resume later; it shall never block. | Must |
| FR-NET-06 | The server shall shut down gracefully on `SIGINT`/`SIGTERM`: stop accepting, flush AOF, close connections, exit with code 0. | Must |
| FR-NET-07 | The server shall close a connection that exceeds any configured limit (see 5.3), after sending an error where possible. | Must |

### 3.2 Protocol
| ID | Requirement | Priority |
|---|---|---|
| FR-PRO-01 | The server shall parse RESP2 arrays of bulk strings as commands. | Must |
| FR-PRO-02 | The server shall parse inline commands (space separated, `\r\n` terminated). | Must |
| FR-PRO-03 | The parser shall handle commands split across any number of reads, including one byte at a time. | Must |
| FR-PRO-04 | The parser shall handle multiple commands in a single read (pipelining) and return replies in order. | Must |
| FR-PRO-05 | Malformed protocol input shall produce `-ERR Protocol error: <reason>` and close the connection. | Must |
| FR-PRO-06 | Command names shall be case-insensitive; keys and values are binary-safe byte strings. | Must |
| FR-PRO-07 | Unknown commands shall return `-ERR unknown command '<name>'`. Wrong argument count shall return `-ERR wrong number of arguments for '<name>' command`. | Must |

### 3.3 Commands (Tier 1 unless noted)
| ID | Commands | Notes |
|---|---|---|
| FR-CMD-01 | `PING [message]`, `ECHO message` | `PING` returns `+PONG`; with message returns it as bulk string |
| FR-CMD-02 | `SET key value [EX s] [PX ms] [NX] [XX]` | Returns `+OK`, or null bulk when NX/XX condition fails |
| FR-CMD-03 | `GET key` | Bulk string or null bulk |
| FR-CMD-04 | `DEL key [key ...]`, `EXISTS key [key ...]` | Return integer counts |
| FR-CMD-05 | `INCR`, `DECR`, `INCRBY key n`, `DECRBY key n` | 64-bit signed; overflow and non-integer detected |
| FR-CMD-06 | `MGET key [key ...]`, `MSET key value [key value ...]` | `MGET` returns array with nulls for missing keys |
| FR-CMD-07 | `EXPIRE`, `PEXPIRE`, `TTL`, `PTTL`, `PERSIST` | TTL returns `-2` missing, `-1` no expiry |
| FR-CMD-08 | `DBSIZE`, `FLUSHALL` | Integer count; `+OK` |
| FR-CMD-09 | `COMMAND`, `CONFIG GET <param>`, `INFO [section]` | Minimal compatible stubs so `redis-cli` and `redis-benchmark` start cleanly |
| FR-CMD-10 | `KEYS pattern` (glob subset) | Tier 2, for debugging; documented as O(N) |
| FR-CMD-11 | One Tier 3 feature chosen from: sorted sets (`ZADD`, `ZRANGE`, `ZRANK`, `ZSCORE`), LRU eviction with `maxmemory`, AOF rewrite, `SCAN` | Optional, at most one |

### 3.4 Storage
| ID | Requirement | Priority |
|---|---|---|
| FR-STO-01 | Keys and values shall be stored in a custom hash table (chained buckets, power-of-two size, seeded hash). `std::unordered_map` shall not be used for the main keyspace. | Must |
| FR-STO-02 | The hash table shall resize by incremental rehashing: two tables coexist and a bounded number of buckets migrate on each operation and on idle ticks. | Must (Tier 2) |
| FR-STO-03 | Lookups, inserts and deletes shall be correct while a rehash is in progress. | Must |

### 3.5 Expiry
| ID | Requirement | Priority |
|---|---|---|
| FR-EXP-01 | Expired keys shall never be returned by any command (lazy expiry on access). | Must |
| FR-EXP-02 | The server shall actively delete expired keys using a min-heap index of `(expire_at_ms, key)` within a per-iteration time budget (default 1 ms). | Must |
| FR-EXP-03 | Heap entries made stale by TTL changes shall be validated against the key's current expiry before deletion. | Must |
| FR-EXP-04 | The `epoll_wait` timeout shall be the time to the next expiry, capped at 100 ms. | Must |
| FR-EXP-05 | Runtime expiry logic shall use a monotonic clock for scheduling and a wall clock for absolute timestamps. | Must |

### 3.6 Persistence (Tier 2)
| ID | Requirement | Priority |
|---|---|---|
| FR-PER-01 | Every successful write command shall be appended to the AOF in RESP format. | Must |
| FR-PER-02 | Relative expirations (`EXPIRE`, `SET EX`) shall be written to the AOF as absolute `PEXPIREAT` times so restarts do not reset TTLs. | Must |
| FR-PER-03 | The fsync policy shall be configurable: `always`, `everysec` (default), `no`. | Must |
| FR-PER-04 | On startup the server shall replay the AOF to rebuild state, skipping keys whose absolute expiry has passed. | Must |
| FR-PER-05 | If the final AOF record is truncated (partial write at crash), the server shall truncate the file to the last complete record and start, logging a warning. | Must |
| FR-PER-06 | If the AOF is corrupt in the middle, the server shall refuse to start and report the byte offset. | Must |

### 3.7 Observability
| ID | Requirement | Priority |
|---|---|---|
| FR-OBS-01 | `INFO` shall report: `uptime_in_seconds`, `connected_clients`, `total_connections_received`, `total_commands_processed`, `keyspace_hits`, `keyspace_misses`, `expired_keys`, `used_memory` (approximate), `aof_enabled`, `aof_current_size`, `rehashing`. | Must |
| FR-OBS-02 | The server shall log to stderr with levels `error`, `warn`, `info`, `debug`, selectable at startup. | Must |

## 4. Configuration
Command-line flags and an optional config file. Precedence: command-line over file over defaults.

| Parameter | Default | Description |
|---|---|---|
| `--bind` | `127.0.0.1` | Listen address (loopback by default because there is no authentication) |
| `--port` | `6380` | Listen port |
| `--max-clients` | `10000` | Maximum simultaneous connections |
| `--appendonly` | `no` | Enable AOF (`yes`/`no`) |
| `--appendfsync` | `everysec` | `always`, `everysec`, `no` |
| `--appendfilename` | `kvforge.aof` | AOF path |
| `--loglevel` | `info` | Log verbosity |
| `--expire-budget-us` | `1000` | Active-expiry time budget per loop iteration |

## 5. Non-functional requirements

### 5.1 Performance (targets, measured not promised)
- Single instance, 50 clients, `redis-benchmark -t set,get -n 1000000 -P 16`: report throughput and p50/p99 latency alongside real Redis on the same machine with equal persistence settings.
- No single operation shall cause a pause longer than 10 ms at 1,000,000 keys during a resize (incremental rehashing).

### 5.2 Reliability
- No crash on any input from the fuzzing corpus.
- Zero ASan/UBSan findings across the test suite.
- Acknowledged writes under `appendfsync always` survive `kill -9`.

### 5.3 Resource limits
| Limit | Default |
|---|---|
| Maximum bulk string length | 64 MiB |
| Maximum array elements per command | 1,048,576 |
| Maximum inline command length | 64 KiB |
| Maximum query buffer per client | 128 MiB |
| Maximum output buffer per client | 64 MiB |
| Maximum clients | 10,000 |

### 5.4 Portability and build
- Linux only (kernel 4.x or newer). Developed on Ubuntu 22.04 or WSL2.
- C++17, CMake 3.16 or newer, GCC 11+ or Clang 14+.

### 5.5 Maintainability
- One module per responsibility: `net`, `protocol`, `store`, `expiry`, `persistence`, `commands`.
- Public behavior documented in README; design decisions in `docs/`.

## 6. Constraints and assumptions
- Command execution is single-threaded by design. One slow command blocks all clients; `KEYS` is documented as such.
- Memory accounting is approximate (sum of key, value and entry overhead), not allocator-exact.
- Tests that compare with real Redis require a Redis 7.x binary installed on the test machine.

## 7. Dependencies
| Dependency | Use | Required |
|---|---|---|
| Linux `epoll` | Event loop | Yes |
| CMake | Build | Yes |
| GoogleTest | Unit tests | Dev only |
| libFuzzer (Clang) | Parser fuzzing | Dev only |
| Redis 7.x | Differential testing, benchmarking baseline | Dev only |
| Python 3 + `redis` package | Differential test driver | Dev only |
| GitHub Actions | CI | Dev only |

## 8. Out of scope
Replication, clustering, TLS, AUTH/ACL, scripting, pub/sub, streams, transactions, RDB snapshots, multi-database `SELECT`, Windows/macOS native builds.

## 9. Release criteria
A release is complete when all Must requirements for its tier pass their acceptance criteria (see `ACCEPTANCE_CRITERIA.md`), CI is green with sanitizers, and the README lists known limitations.