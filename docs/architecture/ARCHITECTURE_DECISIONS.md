# KVForge — Architecture Decision Records (ADRs)

Format: Context → Decision → Alternatives considered → Consequences. Status values: Accepted, Superseded, Proposed. New decisions are appended with the next number; accepted decisions are never edited except to mark them superseded.

| ADR | Title | Status |
|---|---|---|
| 001 | Single-threaded command execution | Accepted |
| 002 | Level-triggered `epoll` | Accepted |
| 003 | Custom hash table with separate chaining | Accepted |
| 004 | Incremental rehashing | Accepted |
| 005 | Seeded FNV-1a hash with finalizer | Accepted |
| 006 | Hybrid expiry: lazy plus active via min-heap | Accepted |
| 007 | Append-only file only (no RDB) | Accepted |
| 008 | Absolute timestamps in the AOF | Accepted |
| 009 | Three fsync policies | Accepted |
| 010 | RESP2 only | Accepted |
| 011 | Loopback bind by default | Accepted |
| 012 | Differential testing against real Redis | Accepted |
| 013 | Approximate memory accounting | Accepted |
| 014 | Incremental, resumable RESP parser with hard limits | Accepted |
| 015 | No external runtime dependencies | Accepted |

---

## ADR-001 Single-threaded command execution
**Context.** The workload is small in-memory operations. Multi-threaded stores need locks or sharding, add complexity, and make correctness harder to test.
**Decision.** Execute all commands on one thread (the event loop thread). Network I/O is also handled on that thread.
**Alternatives.** (a) One thread per connection: simple but heavy at thousands of clients and needs locks. (b) Multi-threaded I/O with single-threaded execution (as newer Redis): more throughput, more complexity. (c) Sharded multi-threaded execution: scales across cores but cross-key commands (`MSET`, `MGET`) become hard.
**Consequences.** Atomic commands without locks; deterministic behavior; easy to test. One slow command (`KEYS`, `FLUSHALL` on huge data) blocks everyone. Throughput is bounded by one core, which is acceptable because most time goes to syscalls and memory access. Multi-threaded I/O is listed as possible future work.

## ADR-002 Level-triggered `epoll`
**Context.** `epoll` supports level-triggered (LT) and edge-triggered (ET) modes. ET demands reading and writing until `EAGAIN` every time and is a common source of lost-wakeup bugs.
**Decision.** Use LT. Register `EPOLLIN` always; register `EPOLLOUT` only while a connection has unsent data. Cap work per connection per iteration for fairness.
**Alternatives.** `select`/`poll` (O(n) scans, `FD_SETSIZE` cap for `select`); ET `epoll` (slightly fewer wakeups, harder correctness); `io_uring` (modern but larger kernel requirement and complexity).
**Consequences.** Simpler, safer code. Slightly more `epoll_wait` returns under load, mitigated by reading in chunks. Linux-only.

## ADR-003 Custom hash table with separate chaining
**Context.** The keyspace is the core data structure and a main learning goal. `std::unordered_map` does not allow incremental rehashing and hides its memory behavior.
**Decision.** Implement a hash table with separate chaining and power-of-two sizing.
**Alternatives.** `std::unordered_map` (simple, but full-rehash pauses and no control); open addressing with robin-hood (better cache behavior, harder deletion and incremental resize); swiss-table style (high performance, large implementation effort).
**Consequences.** Full control of memory layout and resize behavior; more code to test. Chaining costs a pointer per entry and cache misses on long chains; acceptable at load factor ≤ 1.

## ADR-004 Incremental rehashing
**Context.** Resizing a table with millions of entries in one step creates a latency spike that stalls all clients.
**Decision.** Keep two tables during a resize and migrate a few buckets per operation plus bounded work on idle loop iterations. Lookups check both tables; inserts go to the new table.
**Alternatives.** Stop-the-world rehash (simple, spikes); background thread (needs locking, violates ADR-001); pre-sizing (not generally possible).
**Consequences.** Bounded per-operation latency (except the new array allocation); more complex invariants, covered by unit tests that pause mid-rehash (AC-HT-02). Memory is temporarily higher (both tables).

## ADR-005 Seeded FNV-1a hash with finalizer
**Context.** Predictable hash functions allow hash-flooding denial-of-service.
**Decision.** Use 64-bit FNV-1a XORed with a random per-process seed, then a finalizer mix to improve low-bit distribution (because indexing uses the low bits).
**Alternatives.** SipHash-1-3 (cryptographically keyed, slightly slower, ~50 more lines); MurmurHash3 or xxHash (fast, but unkeyed, so vulnerable to precomputed collisions if seeds are guessable); `std::hash` (implementation-defined).
**Consequences.** Good speed and simple code; resists casual attacks, not determined ones. If the threat model changes (untrusted public exposure), switch to SipHash-1-3 behind the same `hash(key)` interface.

## ADR-006 Hybrid expiry: lazy plus active via min-heap
**Context.** Expired keys must never be visible, and memory must be reclaimed even for keys never read again.
**Decision.** Lazy deletion on access guarantees correctness. A min-heap ordered by deadline drives active deletion within a time budget each loop iteration. Heap items are validated against the key's current deadline; the heap is rebuilt when stale items exceed `2 × live_ttl_keys + 1024`.
**Alternatives.** Redis-style random sampling of keys with TTL (no extra memory, probabilistic reclamation); timing wheel (O(1) insert, coarse granularity); per-key timers (heavy).
**Consequences.** Deterministic reclamation order and precise deadlines; extra memory for the heap and duplicated key strings; cleanup logic for stale entries. Interview note: heap-with-validation trades memory for determinism; sampling trades determinism for memory.

## ADR-007 Append-only file only (no RDB)
**Context.** Two persistence models exist: periodic snapshots (RDB) and command logging (AOF).
**Decision.** Implement AOF only in Tier 2. Tier 3 option C may add AOF rewrite for compaction.
**Alternatives.** RDB snapshots (compact, fast restart, but lose data since the last snapshot and need `fork` for non-blocking saves); both (more scope).
**Consequences.** Simple, human-readable, fine-grained durability; file grows without rewrite; slower startup on large logs. Documented limitation.

## ADR-008 Absolute timestamps in the AOF
**Context.** Logging `EXPIRE k 60` and replaying it a day later would give the key a fresh 60 seconds.
**Decision.** Normalize every expiry to absolute epoch milliseconds (`PXAT` / `PEXPIREAT`) before writing. Skip already-expired keys on replay.
**Alternatives.** Log relative TTLs with a timestamp header (extra format complexity); periodic TTL snapshots (loses precision).
**Consequences.** Replay is deterministic regardless of restart time. Wall-clock jumps can shift deadlines (documented).

## ADR-009 Three fsync policies
**Context.** `fsync` on every write is safe but slow; never calling it is fast but risky.
**Decision.** Provide `always`, `everysec` (default), `no`, matching the well-understood Redis model. For `always`, one `fsync` covers a client's pipelined batch and replies are released after it returns.
**Alternatives.** Group commit across clients with a short delay (higher throughput under load, more complexity); `O_DSYNC` (simpler but usually slower).
**Consequences.** Operators choose durability versus speed. Benchmark reports must state the policy for both KVForge and Redis.

## ADR-010 RESP2 only
**Context.** RESP3 adds new types and requires `HELLO` negotiation.
**Decision.** Support RESP2 only; `HELLO` returns an unknown-command error, causing clients to fall back to RESP2.
**Alternatives.** Add RESP3 (more types, more tests, more scope).
**Consequences.** Compatible with `redis-cli`, `redis-benchmark` and common client libraries in default mode. Clients forcing RESP3 will fail clearly.

## ADR-011 Loopback bind by default
**Context.** The server has no authentication or TLS.
**Decision.** Default `--bind 127.0.0.1`; warn when binding elsewhere.
**Alternatives.** Default to `0.0.0.0` (convenient, historically led to widespread exposed Redis instances); require explicit bind (equivalent to loopback default with extra friction).
**Consequences.** Safe by default; users who want remote access must opt in knowingly.

## ADR-012 Differential testing against real Redis
**Context.** Hand-written unit tests encode the author's assumptions about Redis behavior and can be wrong in the same way as the implementation.
**Decision.** Generate random deterministic command sequences, run them against Redis 7.x and KVForge, and compare replies. Time-dependent replies (`TTL`) are compared with tolerance; unordered replies (`KEYS`) are compared as sets.
**Alternatives.** Hand-picked examples only (cheaper, weaker evidence); property-based tests without an oracle (cannot detect semantic mismatches).
**Consequences.** Strong correctness evidence; CI needs a Redis binary; divergences must be classified as bugs or documented differences.

## ADR-013 Approximate memory accounting
**Context.** Exact accounting requires allocator hooks and costs performance.
**Decision.** Maintain an incremental estimate based on string capacities and structure sizes. Use RSS for comparisons.
**Alternatives.** Custom allocator wrapper or `jemalloc` stats (accurate, more dependency and code).
**Consequences.** `used_memory` is indicative only; benchmark reports include RSS. Required for Tier 3 option B (LRU) only as an approximation.

## ADR-014 Incremental, resumable RESP parser with hard limits
**Context.** TCP is a byte stream; packet boundaries are arbitrary and clients are untrusted.
**Decision.** Parse directly from the connection's buffer with explicit `NeedMore`, `Ok`, `Error` results; no recursion; limits enforced before allocation; allocation proportional to bytes actually received.
**Alternatives.** Blocking read until a whole command arrives (breaks the event loop); a parser generator (extra dependency, harder to fuzz and control).
**Consequences.** Robust under split and pipelined input; requires thorough fuzzing and byte-at-a-time tests.

## ADR-015 No external runtime dependencies
**Context.** Dependencies add build friction and obscure what the code actually does.
**Decision.** Use only the C++ standard library and Linux/POSIX APIs at runtime. Test tooling is allowed to use GoogleTest and Python.
**Alternatives.** libevent/libuv (portable event loops; hides the `epoll` learning goal); Boost.Asio (large dependency).
**Consequences.** Self-contained binary, transparent code. More code to own, and portability limited to Linux.

---

## ADR template
```
## ADR-NNN Title
**Context.** What problem and constraints?
**Decision.** What we chose, in one or two sentences.
**Alternatives.** What else was considered and why not.
**Consequences.** Benefits, costs, follow-up work, how we will know if it was wrong.
```