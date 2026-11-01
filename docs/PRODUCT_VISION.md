# KVForge — Product Vision

## 1. One-line pitch
KVForge is a single-threaded, event-driven, in-memory key-value store written in C++17 that speaks the RESP2 wire protocol, so the real `redis-cli` and `redis-benchmark` work against it unchanged.

KVForge implements a **documented subset** of Redis commands. It does not claim full Redis compatibility.

## 2. Problem statement
Most developers use infrastructure (Redis, Postgres, Nginx) without understanding how it works internally. Learning material explains concepts (sockets, event loops, hash tables, expiry, durability) in isolation, but rarely shows them working together in one verifiable system.

KVForge exists to be that system: small enough to read in an afternoon, correct enough to be tested against the real thing, and fast enough to be measured against it.

## 3. Vision
A clean, well-tested, well-documented reference implementation of a Redis-compatible store that:
1. Demonstrates correct non-blocking network programming (partial reads, pipelining, backpressure).
2. Demonstrates core data-structure engineering (a custom hash table with incremental rehashing, a min-heap expiry index).
3. Demonstrates durability engineering (append-only file, fsync policies, crash recovery).
4. Proves its correctness with differential testing against real Redis and proves its performance with reproducible benchmarks.

## 4. Target audiences
| Audience | What they want from KVForge |
|---|---|
| Application developer (client) | A server they can connect to with any Redis client library and use for basic caching, counters and TTL keys |
| Systems learner / contributor | Readable code, a DESIGN document explaining every decision, and tests they can run |
| Technical reviewer (interviewer, recruiter, senior engineer) | Evidence of depth: correctness tests, benchmark methodology, honest limitations |

## 5. Goals
- G1. Compatible with `redis-cli` and `redis-benchmark` for the supported command subset.
- G2. Correct under partial reads, split packets, pipelined commands and slow clients.
- G3. Durable through an append-only file with selectable fsync policy and safe crash recovery.
- G4. Verified: unit tests, differential tests against real Redis, parser fuzzing, sanitizer builds, crash-recovery tests.
- G5. Measured: published benchmark results with exact commands, hardware and methodology, including cases where real Redis is faster.
- G6. Documented: every major design decision recorded with its trade-offs.

## 6. Non-goals
- Replication, clustering, sharding or Sentinel-style failover.
- Authentication, ACLs and TLS.
- Lua scripting, modules, pub/sub, streams, transactions (`MULTI`/`EXEC`).
- RDB snapshots.
- Windows or macOS native support (Linux only; WSL2 is acceptable for development).
- Beating Redis on performance. The goal is to be within a defensible range and to explain the gap.

## 7. Product principles
1. **Correctness before speed.** A fast wrong answer is a bug.
2. **Honesty in claims.** README statements must be backed by tests or benchmarks that a reader can reproduce.
3. **Single-threaded command execution.** Commands are atomic without locks. Simplicity is a feature.
4. **Bounded resources.** Every buffer, connection count and argument size has an explicit limit. No unbounded growth from untrusted input.
5. **Observable.** `INFO` exposes real runtime metrics so behavior can be inspected.
6. **Readable.** Prefer clear code over clever code. Each module has one responsibility.

## 8. Success metrics
| Metric | Target |
|---|---|
| Differential test agreement with real Redis on deterministic command sequences | 100% of replies match across at least 10,000 generated sequences |
| `redis-cli` and `redis-benchmark` run without protocol errors | Yes, for all supported commands |
| Throughput vs real Redis (same machine, same persistence settings, `-c 50`) | Reported as a ratio; target is at least 50% of Redis on `SET`/`GET` with `-P 16`, otherwise explained in the design doc |
| Crash recovery | After `kill -9`, restart recovers all writes acknowledged under `fsync always`; under `everysec` loses at most about 1 second |
| Sanitizers (ASan, UBSan) | Zero findings in CI over the full test suite |
| Parser fuzzing | At least 1 hour of libFuzzer with no crashes before release |
| Documentation | README, DESIGN, ARCHITECTURE, benchmark methodology, and known limitations all present |

## 9. Release tiers
- **Tier 1 (v0.1):** working server, RESP2 parsing, core commands, pipelining, TTL, `INFO`.
- **Tier 2 (v0.2):** incremental rehashing, AOF persistence with recovery, differential tests, fuzzing, sanitizers, benchmarks.
- **Tier 3 (v0.3, optional, at most one):** sorted sets (skip list), or LRU eviction with `maxmemory`, or AOF rewrite, or `SCAN`.

## 10. Risks
| Risk | Impact | Mitigation |
|---|---|---|
| Partial-read parser bugs | Corrupted or stuck connections | Parser fuzzing; tests that feed commands one byte at a time |
| Scope creep | Project never finishes | Tier boundaries; Tier 3 only after Tier 2 is documented |
| Benchmark results misleading | Loss of credibility | Same machine, equal persistence settings, median of several runs, published raw output |
| Unbounded memory from malicious input | Server crash | Hard limits on bulk size, query buffer, output buffer, connections |
| Time pressure | Incomplete polish | Ship Tier 1 + AOF + benchmarks first, document gaps honestly |