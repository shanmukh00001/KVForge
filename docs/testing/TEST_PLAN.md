# KVForge — Test Plan

Concrete suites, cases and procedures. Strategy and gating rules are in `TEST_STRATEGY.md`. Acceptance criteria IDs (AC-*) are defined in `ACCEPTANCE_CRITERIA.md`.

## 1. Repository layout for tests
```
tests/
├── CMakeLists.txt
├── unit/            test_config.cpp  test_resp_parser.cpp  test_resp_writer.cpp
│                    test_hash_table.cpp  test_store.cpp  test_expiry.cpp  test_int_parse.cpp
│                    test_glob.cpp  test_commands_*.cpp  test_aof_writer.cpp  test_aof_loader.cpp
├── integration/     harness.hpp (spawn kvforge, ephemeral port, temp dir, raw client)
│                    test_server_basic.cpp  test_protocol.cpp  test_limits.cpp
│                    test_expiry_timing.cpp  test_persistence.cpp  test_cli_output.cpp
├── differential/    run_diff.py  gen_commands.py  compat_exclusions.json  requirements.txt
├── crash/           run_crash.py  aof_fixtures.py
├── fuzz/            fuzz_resp_parser.cpp  fuzz_aof_loader.cpp  corpus/  dictionaries/resp.dict
└── requirements.txt
bench/               run.sh  collect.py  results/
```

## 2. How to run
| Goal | Command |
|---|---|
| Build + all unit and integration tests | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure` |
| Sanitizer run | `cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DKVFORGE_SANITIZE=ON && cmake --build build-san -j && ctest --test-dir build-san --output-on-failure` |
| Differential (quick) | `python3 tests/differential/run_diff.py --sequences 1000 --seed $RANDOM` |
| Differential (full) | `python3 tests/differential/run_diff.py --sequences 10000 --seed 12345` |
| Crash tests | `python3 tests/crash/run_crash.py --policy always --iterations 200` |
| Fuzz smoke | `./build-fuzz/fuzz_resp_parser -max_total_time=60 tests/fuzz/corpus/resp` |
| Coverage | `cmake -DKVFORGE_COVERAGE=ON ...; ctest; lcov --capture ... ; genhtml ...` |

Seeds: all random suites print `seed=<n>`; replay with `KVFORGE_TEST_SEED=<n>` (C++) or `--seed <n>` (Python).

## 3. Unit test suites
| Suite ID | Module | Cases (non-exhaustive) | ACs |
|---|---|---|---|
| UT-CFG | config | defaults; every flag; config file; CLI overrides file; invalid values with exact messages; `--help` golden text and ≤ 80 columns | AC-CFG-01, UI §10 |
| UT-PAR | RESP parser | valid arrays of bulk; empty array; null bulk; inline with quotes; every split position of each sample; one byte at a time; 10,000 pipelined commands; invalid length digits; negative lengths other than allowed; length over limits; missing CR or LF; bulk longer than declared; nested array rejected; unbalanced inline quotes; line over 64 KiB; allocation not proportional to declared length | AC-PRO-01..03, 07, 08, AC-LIM-01, 02 |
| UT-WRT | RESP writer | each reply type; large bulk; array of nulls; integer extremes | AC-PRO-* |
| UT-INT | int parser | `0`, `-1`, `9223372036854775807`, `-9223372036854775808`; reject `+1`, `01`, `-0`, `-05`, ` 1`, `1 `, `1.0`, empty, 20+ digits, overflow by one | AC-INCR-03, 04, 06 |
| UT-HT | hash table | insert/find/erase; 2,000,000 keys; collisions with a forced constant hash; grow and shrink thresholds; **mid-rehash**: pause after k steps and verify find/insert/erase for keys in both tables; invariants (each entry in exactly one table; counts) | AC-HT-01, 02 |
| UT-HTM | hash table model test | 1,000,000 random ops (insert, update, erase, find) against `std::unordered_map`, comparing results; includes forced rehash steps interleaved | AC-HT-01, 02 |
| UT-STO | store | upsert, erase, lazy expiry on lookup, `now > deadline` boundary (equal is still alive), TTL set/clear, dbsize excludes expired | AC-EXP-01, 02 |
| UT-EXP | expiry index | push/pop ordering; stale item discarded when TTL changed (AC-EXP-06); stale item discarded after `PERSIST`/`DEL`/overwrite; budget respected; rebuild when stale > `2*live+1024`; `next_deadline` | AC-EXP-05, 06 |
| UT-CMD-STR | string commands | `SET` all option combinations including invalid combos, `GET`, `MSET` odd args, `MGET` mixed, arity errors, case-insensitivity | AC-SET-01..09, AC-GET-01, AC-MULTI-01..03 |
| UT-CMD-KEY | key commands | `DEL`, `EXISTS` with duplicates, `EXPIRE` zero/negative/overflow, `PEXPIREAT` in the past, `TTL` rounding table (400 ms→0, 500→1, 1,499→1, 1,500→2), `PERSIST`, `DBSIZE`, `FLUSHALL`, `KEYS` globs | AC-DEL-01, AC-EXISTS-01, AC-EXP-02..04, AC-SRV-01, 02, 04 |
| UT-CMD-INC | counters | missing key; boundaries at int64 limits; TTL preserved; non-integer value; `INCRBY` with non-integer argument; `DECRBY` of `INT64_MIN` handled (overflow error) | AC-INCR-01..06 |
| UT-CMD-SRV | server commands | `PING`, `ECHO`, `COMMAND`, `COMMAND COUNT`, `CONFIG GET` supported/unknown, `CLIENT SETNAME/SETINFO`, `INFO` fields present and counters (hits/misses) | AC-PING-*, AC-INFO-01, 02 |
| UT-AOF-W | AOF writer | exact bytes for each normalized record in `DATABASE_DESIGN.md` §6.2; read-only and failed commands not written; fsync policy invocation counts via an injected `Syncer` | AC-AOF-02, 04 |
| UT-AOF-L | AOF loader | load valid file; truncation at every byte offset of the final record (tail removed, earlier records kept); corruption in the middle (error with offset); expired absolute deadlines skipped; replay equals original state | AC-AOF-01, 03, 05, 06 |
| UT-GLOB | glob | `*`, `?`, `[abc]`, `[a-z]`, `[^a]`, escapes, pathological pattern performance (no exponential backtracking: `a*a*a*a*b` on a long `aaaa…` string finishes in under 100 ms) | AC-SRV-04 |

## 4. Integration tests (real binary, real sockets)
Harness: spawns `kvforge --port 0` (prints chosen port in the "Ready" line via `addr`/`port` fields), waits for `Ready to accept connections`, provides `send_raw(bytes)`, `read_reply()`, and `cli(...)` helpers. Kills the process on teardown.

| ID | Scenario | ACs |
|---|---|---|
| IT-01 | Start, `PING`, `SET`/`GET`, shutdown with SIGTERM exits 0 | AC-PING-01, AC-NET-03 |
| IT-02 | Bind to a used port: exits 1 with the ERROR line | AC-NET-01 |
| IT-03 | 1,000 simultaneous idle connections, then all closed; `connected_clients` returns to baseline | AC-NET-02, AC-PRO-09 |
| IT-04 | `max_clients=5`: sixth connection gets the error and is closed; others unaffected | AC-LIM-03 |
| IT-05 | Command sent one byte at a time with 5 ms gaps; reply appears only after the last byte | AC-PRO-01 |
| IT-06 | 10,000 commands in a single `send`; 10,000 replies in order | AC-PRO-03 |
| IT-07 | `nc`-style inline commands including quoted strings | AC-PRO-04 |
| IT-08 | Garbage input (`*abc\r\n`) closes only that connection | AC-PRO-07 |
| IT-09 | Binary-safe value round trip (`\0`, `\r\n`, 0x80–0xFF) | AC-PRO-08 |
| IT-10 | Bulk length 64 MiB + 1 and array length 1,048,577 rejected | AC-LIM-01, 02 |
| IT-11 | Slow reader: client requests 200 MB of replies (via many `GET` of a big value) without reading; server disconnects it at the output limit; RSS returns near baseline; other client keeps working | AC-LIM-04 |
| IT-12 | `SET k v PX 100`; after 150 ms `GET` is null; `EXISTS` is 0 | AC-EXP-01 |
| IT-13 | 100,000 keys with `PX 200`; after 2 s `DBSIZE` is 0 and `expired_keys` ≥ 100000 | AC-EXP-05 |
| IT-14 | Idle server CPU below 1% over 5 s (measure via `/proc/<pid>/stat`) | AC-EXP-07 |
| IT-15 | AOF enabled: write, SIGTERM, restart, state restored; TTL keeps its deadline (not reset) | AC-AOF-01, 03, AC-NET-03 |
| IT-16 | `--help` equals golden file; invalid flags produce exact messages and exit code 1 | UI §2 |
| IT-17 | Log lines match the regex in `UI_SPECIFICATION.md` §10; no ANSI codes when stderr is piped | UI §10, RESPONSIVE §9 |
| IT-18 | `redis-cli` one-shot commands and interactive start (skipped if `redis-cli` missing, required in CI) | AC-SRV-03 |
| IT-19 | `redis-benchmark -t ping,set,get -n 100000 -c 50 -P 16` finishes with no errors | AC-BEN-01 |
| IT-20 | `INFO` counters: after 3 hits and 2 misses, deltas are 3 and 2 | AC-INFO-02 |
| IT-21 | `INFO persistence` shows `aof_enabled:1` and a growing `aof_current_size` | AC-INFO-01 |
| IT-22 | `rehashing` is `1` during a forced growth (insert quickly 1,000,000 keys and sample `INFO`) and `0` afterwards | AC-HT-04 |

## 5. Latency test for resizing (AC-HT-03)
Procedure: start KVForge with persistence off. Pipe 1,000,000 `SET key:<i> v` using `redis-benchmark -t set -n 1000000 -c 1 -P 1 -r 1000000 --csv` and also with a custom Python/C++ client that records each request's round-trip time. Report the maximum and the 99.99th percentile. Pass if no single `SET` takes longer than 10 ms on the reference machine (excluding the first 100 operations). Record machine specs in the result file. If the allocation of the next table exceeds 10 ms on the reference machine, document the measured value and treat it as a finding to discuss in `DESIGN.md`.

## 6. Differential testing

### 6.1 Setup
- Start real Redis 7.x: `redis-server --port 6390 --save "" --appendonly no --daemonize no`.
- Start KVForge: `kvforge --port 6391`.
- The runner connects to both with raw sockets (to compare exact reply bytes after normalization), not through a high-level client that hides differences.

### 6.2 Generator (`gen_commands.py`)
Seeded RNG; each sequence has 50–500 commands. Command weights:

| Command | Weight | Argument generation |
|---|---|---|
| `SET` | 20 | key from pool; value from value pool; options: none 60%, `EX`/`PX` with large TTL 15%, `NX` 10%, `XX` 10%, invalid combos 5% |
| `GET` | 15 | key from pool |
| `DEL` | 8 | 1–4 keys |
| `EXISTS` | 6 | 1–4 keys (duplicates allowed) |
| `INCR`, `DECR` | 8 | key from pool |
| `INCRBY`, `DECRBY` | 6 | integer from pool including near-limit values and invalid strings |
| `MSET` | 4 | 1–4 pairs; 10% odd arg count |
| `MGET` | 4 | 1–4 keys |
| `EXPIRE`, `PEXPIRE` | 6 | large positive values; 5% zero, negative or overflow-sized |
| `PEXPIREAT` | 2 | far-future absolute ms; 5% past |
| `TTL`, `PTTL` | 6 | key from pool |
| `PERSIST` | 3 | key from pool |
| `DBSIZE` | 2 | none |
| `FLUSHALL` | 1 | none |
| Wrong arity / unknown | 3 | random known command with wrong argument counts; random unknown names |
Each sequence starts with `FLUSHALL` on both servers.

### 6.3 Comparison rules
| Reply kind | Rule |
|---|---|
| Simple string, integer, bulk, null, array | Exact byte equality after parsing into typed values |
| `TTL` | Equal, or differ by at most 1 |
| `PTTL` | Differ by at most 2,000 ms (time elapses between the two servers' commands) |
| Errors | Equal after normalizing: strip Redis's `, with args beginning with:` suffix; compare text for the errors listed in `API_SPECIFICATION.md` §4 |
| Excluded commands | `INFO`, `CONFIG`, `COMMAND`, `CLIENT`, `KEYS` (compared as a set in a separate test) |
| Documented differences | Listed in `compat_exclusions.json`; each entry has a command pattern and a reason |

### 6.4 Output
```
Differential: sequences=10000 commands=2500000 mismatches=0 seed=12345 redis=7.x.y
```
On a mismatch the runner prints the seed, sequence index, the shrunk reproduction (delta-debugging: remove commands while the mismatch persists) and both replies, then exits non-zero.

### 6.5 Additional differential scenarios
- `KEYS` compared as a set for patterns `*`, `a*`, `?b`, `[ab]*`, `[^a]*` on a seeded dataset (no TTLs).
- Pipelined sequences: send 50 commands per write and compare the reply stream.
- Split-write delivery: the same commands sent in random chunk sizes.

## 7. Crash-recovery and AOF tests (`run_crash.py`)

### 7.1 `kill -9` test
For each iteration (seeded):
1. Start KVForge with `--appendonly yes --appendfsync <policy>` in a fresh temp directory.
2. A writer sends a numbered series of deterministic write commands (mix of `SET`, `INCR`, `DEL`, `SET ... EX`) and records each acknowledged operation number.
3. After a random delay (0.05–1.5 s), `kill -9` the process.
4. Restart KVForge on the same directory; read the full keyspace with `KEYS *` plus `GET`/`TTL` (or a debug dump command in test builds).
5. Compute the model state for every prefix of the issued operations. **Pass** if the recovered state equals the model state of some prefix whose length is at least the number of acknowledged operations minus the policy allowance (`always`: 0; `everysec` and `no`: unbounded for power loss, but for `kill -9` the OS page cache survives, so the expected prefix is normally all acknowledged operations).
6. Verify TTLs keep their absolute deadlines.

### 7.2 Honest limits of this test
`kill -9` does not discard data already passed to `write()`, so this test validates ordering, tail-record handling and replay, **not** that `fsync` reaches the disk. To verify the policies, add:
- **fsync accounting test:** run under `strace -f -e trace=fsync,fdatasync -c` (or an `LD_PRELOAD` shim counting `fsync`). Assert: `always` → at least one `fsync` per command batch; `everysec` → at most about one per second over a 5-second run; `no` → zero until shutdown.
- Document that true power-loss durability is out of what a user-space test can prove.

### 7.3 Tail-truncation and corruption tests
- Generate an AOF with 1,000 records; for every byte offset inside the last record, truncate and start KVForge: it must start, log the truncation warning, and contain all earlier records.
- Corrupt a byte in the middle record header: KVForge exits with code 1 and logs the offset.
- Empty file, file with only a partial first record, and file larger than 100 MB (load time recorded) are handled.
- AOF permissions are `0600` after creation (AC-AOF + SECURITY §3.5).

## 8. Fuzzing

### 8.1 Parser harness (`fuzz_resp_parser.cpp`)
```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return 0;
    // First byte selects a chunking strategy; rest is the byte stream.
    // Run A: feed all bytes at once. Run B: feed in chunks derived from data[0].
    std::vector<Command> a = parse_all(data + 1, size - 1, /*chunk=*/size);
    std::vector<Command> b = parse_all(data + 1, size - 1, /*chunk=*/1 + data[0] % 16);
    if (a != b) __builtin_trap();            // chunking must not change the result
    return 0;
}
```
Invariants checked: no crash; no allocation larger than the configured query-buffer limit; consumed bytes never exceed input; the same commands result regardless of chunking.
Seed corpus: all valid samples and all malformed samples from §3. Dictionary: `*`, `$`, `\r\n`, `-1`, `0`, `1048576`, `67108864`, `SET`, `GET`, quotes.

### 8.2 AOF loader harness (`fuzz_aof_loader.cpp`)
Write the input to an in-memory file, run the loader against a fresh store with small limits, require: returns `Ok`, `TruncatedTail` or `Corrupt(offset)`, never crashes, never loops forever (iteration cap), and the store invariants hold afterward.

### 8.3 Run schedule
PR: 60 s per harness. Release: ≥ 1 hour for the parser and ≥ 30 minutes for the AOF loader, recorded as total executions and new coverage. Any crash input is minimized and added to `corpus/regression/` as a permanent unit test.

## 9. Limits and abuse tests
| ID | Scenario | Pass condition |
|---|---|---|
| AB-01 | 100 connections each declare a 60 MiB bulk string and send 1 byte | Server memory stays under 100 MiB (allocation proportional to received bytes) |
| AB-02 | One client sends 100 MiB of random bytes with no protocol structure | Connection closed with a protocol error; memory returns to baseline |
| AB-03 | Slow reader (IT-11) at scale: 50 slow readers | Each disconnected at limit; server responsive to a fresh client throughout |
| AB-04 | Pipeline of 1,000,000 `PING` in one write | All replies in order; other clients' latency under 50 ms during the burst (fairness cap) |
| AB-05 | `KEYS *` on 1,000,000 keys | Completes; other clients see a pause equal to the command duration; documented |
| AB-06 | Connection storm: 20,000 connection attempts with `max_clients=10000` | Excess connections rejected; no crash; file descriptors released |
| AB-07 | Rapid connect/disconnect 100,000 times | `connected_clients` returns to baseline; no leak under ASan |
| AB-08 | Hash flooding attempt: keys crafted for FNV-1a without seed knowledge | Throughput does not degrade by more than 2× versus random keys |

## 10. Benchmark procedure (`bench/run.sh`)
Run on native Linux or an Azure Linux VM (not WSL2). Steps:
1. Record environment (CPU model, cores, RAM, kernel, compiler, flags, Redis version, git commit).
2. Build KVForge Release. Start Redis (`--save "" --appendonly no`) and KVForge with equal persistence settings. Repeat the whole matrix for AOF `everysec`.
3. Pin server processes: `taskset -c 2 <server>`; run load generator on other cores: `taskset -c 0,1 redis-benchmark ...`.
4. Warm-up: `redis-benchmark -t set,get -n 200000 -c 50 -q` (discarded).
5. Matrix: `-t set,get`, `-P 1` and `-P 16`, `-c 50`, `-n 1000000`, `-d 64`, `-r 1000000` (random keys), `--csv`. Repeat 5 times per cell; store raw CSVs in `bench/results/raw/`.
6. Latency percentiles: use `redis-benchmark`'s latency summary output (p50, p99) captured from the non-CSV run.
7. Memory: flush, load 1,000,000 keys with 64-byte values (`redis-benchmark -t set -n 1000000 -r 1000000 -d 64`), read RSS from `/proc/<pid>/status` (`VmRSS`) for both servers.
8. `collect.py` takes the median of the 5 runs, fills the report template (`UI_SPECIFICATION.md` §7), and links raw files.
9. Publish results even when Redis is faster; fill the "Analysis" section.

Pass condition (AC-BEN-01..03): all cells present, methodology stated, raw files linked, differences explained.

## 11. Soak test (release only)
Run for 1 hour with AOF `everysec`: a mixed workload (70% `GET`, 25% `SET` with random TTLs 1–60 s, 5% `DEL`/`INCR`) from 50 connections plus a monitor that samples `INFO` every 10 seconds. Pass: no errors, RSS stabilizes (growth under 10% across the last 30 minutes after warm-up), `expired_keys` increases steadily, `rehashing` returns to 0, the AOF replays successfully after a final clean shutdown.

## 12. Traceability matrix (acceptance criteria → tests)
| AC group | Tests |
|---|---|
| AC-PING-* | UT-CMD-SRV, IT-01 |
| AC-SET-*, AC-GET-01 | UT-CMD-STR, differential |
| AC-DEL-01, AC-EXISTS-01 | UT-CMD-KEY, differential |
| AC-INCR-* | UT-INT, UT-CMD-INC, differential |
| AC-MULTI-* | UT-CMD-STR, differential |
| AC-EXP-01..07 | UT-STO, UT-EXP, UT-CMD-KEY, IT-12, IT-13, IT-14 |
| AC-SRV-01..04 | UT-CMD-KEY, UT-CMD-SRV, UT-GLOB, IT-18, differential (`KEYS` set test) |
| AC-PRO-01..09 | UT-PAR, IT-03, IT-05..IT-09 |
| AC-LIM-01..04 | UT-PAR, IT-04, IT-10, IT-11, AB-01..03, AB-06 |
| AC-HT-01..04 | UT-HT, UT-HTM, IT-22, §5 latency test |
| AC-AOF-01..07 | UT-AOF-W, UT-AOF-L, IT-15, crash suite §7, fsync accounting test |
| AC-INFO-01..02, AC-OBS-01, AC-CFG-01 | UT-CMD-SRV, UT-CFG, IT-17, IT-20, IT-21 |
| AC-NET-01..03 | IT-01..IT-03, IT-15 |
| AC-DIFF-01 | §6 |
| AC-QUA-01..03 | Sanitizer CI job, §8 fuzzing, coverage report |
| AC-BEN-01..03 | §10, IT-19 |
| AC-DOC-01..04 | README quick-start CI job, QA checklist |

Maintain this table when tests are added or renamed; a release is blocked if any AC has no mapped passing test.