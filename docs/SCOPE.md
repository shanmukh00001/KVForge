# KVForge — Scope

This document defines exactly what is and is not being built, by tier. If something is not listed as in scope, it is out of scope.

## 1. Scope summary
| Tier | Release | Theme | Status |
|---|---|---|---|
| 1 | v0.1 | Working server and core commands | In scope |
| 2 | v0.2 | Correctness, durability, measurement | In scope |
| 3 | v0.3 | One optional advanced feature | In scope, pick at most one |
| — | — | Everything else | Out of scope |

## 2. Tier 1 — Working core (v0.1)

### 2.1 Networking and protocol
- TCP server, non-blocking sockets, `epoll` level-triggered, single-threaded event loop.
- Per-connection read and write buffers; `EPOLLOUT` on partial writes.
- RESP2 request parsing and reply serialization (all five types).
- Inline command support.
- Pipelining.
- Resource limits (bulk size, query buffer, output buffer, max clients).
- Graceful shutdown on `SIGINT` and `SIGTERM`.

### 2.2 Commands
| Command | Notes |
|---|---|
| `PING [msg]`, `ECHO msg` | |
| `SET key value [EX s] [PX ms] [NX] [XX]` | |
| `GET key` | |
| `DEL key...`, `EXISTS key...` | |
| `INCR`, `DECR`, `INCRBY`, `DECRBY` | int64 with overflow detection |
| `MGET`, `MSET` | |
| `EXPIRE`, `PEXPIRE`, `TTL`, `PTTL`, `PERSIST` | |
| `DBSIZE`, `FLUSHALL` | |
| `COMMAND`, `CONFIG GET`, `INFO` | minimal compatible output |

### 2.3 Storage and expiry
- Hash table with chained buckets and a seeded hash function (basic resize allowed in Tier 1).
- Lazy expiry and active expiry via min-heap.

### 2.4 Project infrastructure
- CMake build, README quick start, unit tests for parser and store, GitHub Actions build.

## 3. Tier 2 — Correctness, durability, measurement (v0.2)
- Incremental rehashing with correct behavior mid-rehash.
- Append-only file: write, fsync policies (`always`, `everysec`, `no`), replay on startup, truncated-tail recovery, absolute-time expiry records.
- `KEYS pattern` with a glob subset (`*`, `?`, `[abc]`), documented as O(N).
- `INFO` with real metrics (see PRD FR-OBS-01).
- Differential tests against real Redis 7.x.
- libFuzzer harness for the RESP parser.
- ASan and UBSan builds in CI.
- Crash-recovery test using `kill -9`.
- Benchmark scripts, raw results, and a methodology document.
- DESIGN documentation with trade-offs and known limitations.

## 4. Tier 3 — Optional (v0.3), choose exactly one at most
| Option | Adds | Interview value |
|---|---|---|
| A. Sorted sets | Skip list; `ZADD`, `ZRANGE`, `ZRANK`, `ZSCORE`, `ZREM` | Data structure depth |
| B. LRU eviction | `maxmemory` limit and approximate LRU sampling | Memory management |
| C. AOF rewrite | Background compaction using `fork()` | OS depth, copy-on-write |
| D. `SCAN` | Cursor-based iteration safe during rehashing | Explains why `KEYS` is dangerous |

Rule: Tier 3 work starts only after Tier 2 acceptance criteria all pass and documentation is complete.

## 5. Explicitly out of scope
| Item | Reason |
|---|---|
| Replication, clustering, sharding, Sentinel | Large distributed-systems effort; not needed to show core skills |
| AUTH, ACL, TLS | Security hardening of a learning-grade server; documented as a limitation |
| Lua scripting, modules | Large surface area |
| Pub/sub, streams | Different subsystem |
| `MULTI`/`EXEC`, `WATCH` | Not required for the core story |
| RDB snapshots | AOF covers durability |
| Lists, sets, hashes (other than Tier 3 A) | Hash-table and string story is sufficient |
| Multiple databases (`SELECT`) | Single keyspace only |
| Multi-threaded command execution | Violates the single-threaded design principle |
| Windows and macOS native support | `epoll` is Linux-specific |
| Full Redis compatibility claims | KVForge is a documented subset |

## 6. Compatibility statement
KVForge is verified against **Redis 7.x** for the commands listed in Tiers 1 and 2. Differences in error text, ordering of `KEYS` output and `INFO` fields are documented in the README. KVForge is not a drop-in replacement for Redis.

## 7. Definition of done by tier
| Tier | Done when |
|---|---|
| 1 | All Tier 1 commands pass acceptance criteria; `redis-cli` and `redis-benchmark` run cleanly; unit tests pass; README quick start works from a fresh clone |
| 2 | All Tier 2 acceptance criteria pass; differential tests show 100% agreement; sanitizers clean; fuzzing run recorded; benchmark results and methodology published; DESIGN and known limitations written |
| 3 | The chosen option meets its acceptance criteria, is covered by tests, and is documented without regressing Tier 1–2 results |

## 8. Change control
Any addition not in this document requires: (1) a written one-paragraph justification, (2) a statement of what is removed or deferred to keep the schedule, (3) an update to this file and `PRD.md`. Default answer to new ideas: add to the "Future work" section of the README, not to the build.