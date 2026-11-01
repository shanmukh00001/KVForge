# KVForge — QA Checklist

Use this checklist before tagging a release. Copy it into the release pull request and tick every item. Items marked **[T1]** are required for v0.1.0; **[T2]** items are additionally required for v0.2.0. Automated evidence (CI links, report files) is preferred over manual ticks; a manual tick needs a one-line note of what was observed.

Release: `v____`  Date: `________`  Commit: `________`  Tester: `________`

## 1. Build and environment
- [ ] **[T1]** Clean clone builds in Release with GCC 11+ and zero warnings
- [ ] **[T1]** Clean clone builds in Release with Clang 14+ and zero warnings
- [ ] **[T1]** CI is green on the release commit (all blocking jobs in `TEST_STRATEGY.md` §9)
- [ ] **[T1]** `kvforge --version` prints the release version; `INFO` shows the same `kvforge_version`
- [ ] **[T1]** `kvforge --help` matches the golden file; every line ≤ 80 columns
- [ ] **[T1]** README quick start works verbatim in a fresh Ubuntu 22.04 container (AC-DOC-02)

## 2. Functional: commands
- [ ] **[T1]** `PING`, `ECHO` behave per AC-PING-01..04
- [ ] **[T1]** `SET` with every option combination, including invalid ones (AC-SET-01..09)
- [ ] **[T1]** `GET`, `DEL`, `EXISTS`, `MSET`, `MGET` (AC-GET-01, AC-DEL-01, AC-EXISTS-01, AC-MULTI-01..03)
- [ ] **[T1]** `INCR`/`DECR`/`INCRBY`/`DECRBY` including overflow and non-integer values (AC-INCR-01..06)
- [ ] **[T1]** `EXPIRE`, `PEXPIRE`, `PEXPIREAT`, `TTL`, `PTTL`, `PERSIST` (AC-EXP-01..04); TTL rounding matches the table in `UT-CMD-KEY`
- [ ] **[T1]** `DBSIZE`, `FLUSHALL` (AC-SRV-01, 02)
- [ ] **[T1]** `COMMAND`, `CONFIG GET`, `CLIENT SETNAME/SETINFO`, `INFO` respond; unknown commands return the documented error
- [ ] **[T2]** `KEYS` with each glob form; expired keys not returned (AC-SRV-04)
- [ ] **[T2]** `INFO` contains every required field, counters move as expected (AC-INFO-01, 02)

## 3. Protocol and networking
- [ ] **[T1]** `redis-cli` interactive session starts without errors and runs the sample session from `UI_SPECIFICATION.md` §6
- [ ] **[T1]** `redis-benchmark -t ping,set,get -n 100000 -c 50 -P 16` completes with no errors (IT-19)
- [ ] **[T1]** Commands split at every byte boundary and one byte at a time produce correct replies (AC-PRO-01, 02)
- [ ] **[T1]** Pipelined 10,000 commands return 10,000 ordered replies (AC-PRO-03)
- [ ] **[T1]** `nc` with inline commands works, including quoted strings (AC-PRO-04)
- [ ] **[T1]** Malformed input closes only the offending connection (AC-PRO-07)
- [ ] **[T1]** Binary-safe values round-trip (AC-PRO-08)
- [ ] **[T1]** Client disconnect mid-command leaks nothing (ASan run) (AC-PRO-09)
- [ ] **[T1]** SIGINT and SIGTERM shut the server down cleanly with exit code 0

## 4. Limits and robustness
- [ ] **[T1]** Oversized bulk and array lengths rejected (AC-LIM-01, 02)
- [ ] **[T1]** `max_clients` enforced, existing clients unaffected (AC-LIM-03)
- [ ] **[T1]** Slow-reader client disconnected at the output limit, memory returns to baseline (AC-LIM-04)
- [ ] **[T1]** Idle CPU < 1% with no clients and with 1,000 idle clients (AC-EXP-07, AC-NET-02)
- [ ] **[T1]** Starting on a busy port exits with code 1 and a clear message (AC-NET-01)
- [ ] **[T2]** Abuse tests AB-01..AB-08 pass (`TEST_PLAN.md` §9)
- [ ] **[T2]** 1-hour soak test passes: no errors, RSS stable, `rehashing` returns to 0 (`TEST_PLAN.md` §11)

## 5. Expiry
- [ ] **[T1]** Lazy expiry: expired keys never returned (AC-EXP-01)
- [ ] **[T1]** Active expiry reclaims untouched keys (AC-EXP-05)
- [ ] **[T1]** Changing a TTL does not leave a stale deadline in effect (AC-EXP-06)
- [ ] **[T1]** A key at exactly its deadline is still readable and expires one millisecond later (unit test)

## 6. Hash table and rehashing
- [ ] **[T1]** 2,000,000 keys insert and read back correctly (AC-HT-01)
- [ ] **[T2]** Operations correct mid-rehash (AC-HT-02)
- [ ] **[T2]** Resize latency measured at 1,000,000 keys; maximum single-operation latency recorded and ≤ 10 ms, or the exception documented in `DESIGN.md` (AC-HT-03)
- [ ] **[T2]** `INFO` shows `rehashing:1` during growth and `0` afterwards (AC-HT-04)

## 7. Persistence
- [ ] **[T2]** Restart restores writes, deletes and counters (AC-AOF-01)
- [ ] **[T2]** Read-only and failed commands are not in the AOF (AC-AOF-02)
- [ ] **[T2]** TTLs keep absolute deadlines across restart; keys expired during downtime are absent (AC-AOF-03)
- [ ] **[T2]** `always` recovers every acknowledged write in the `kill -9` suite; fsync accounting matches each policy (AC-AOF-04, `TEST_PLAN.md` §7)
- [ ] **[T2]** Truncated tail at every byte offset recovers with a warning (AC-AOF-05)
- [ ] **[T2]** Mid-file corruption refuses to start, reports the offset, exits non-zero (AC-AOF-06)
- [ ] **[T2]** Active and lazy expiry append `DEL` so replay does not resurrect keys (AC-AOF-07)
- [ ] **[T2]** AOF file mode is `0600`

## 8. Correctness evidence
- [ ] **[T2]** Full differential run: 10,000 sequences, 0 unexplained mismatches; summary line saved with seed and Redis version (AC-DIFF-01)
- [ ] **[T2]** `docs/COMPATIBILITY.md` lists every intentional difference; `compat_exclusions.json` matches it exactly
- [ ] **[T2]** ASan + UBSan full test run: zero findings (AC-QUA-01)
- [ ] **[T2]** Parser fuzzing ≥ 1 hour and AOF-loader fuzzing ≥ 30 minutes: no crash, hang or finding; executions recorded (AC-QUA-02)
- [ ] **[T2]** Coverage for parser, hash table, expiry, TTL and AOF modules ≥ 85% (AC-QUA-03)
- [ ] **[T2]** Every fixed defect since the last release has a regression test

## 9. Performance
- [ ] **[T2]** Benchmarks run on native Linux or an Azure Linux VM, not WSL2
- [ ] **[T2]** KVForge and Redis ran on the same machine with equal persistence settings, server pinned to one core (AC-BEN-01)
- [ ] **[T2]** Median of 5 runs; throughput, p50, p99 and RSS at 1,000,000 keys reported for both servers; machine specification stated (AC-BEN-02)
- [ ] **[T2]** Cases where Redis is faster are shown and explained (AC-BEN-03)
- [ ] **[T2]** Every number in the README links to a raw output file (AC-DOC-04)
- [ ] **[T2]** No tracked benchmark regressed by more than 20% versus the previous release without explanation

## 10. Security
- [ ] **[T1]** Default bind is `127.0.0.1`; binding elsewhere logs the warning
- [ ] **[T1]** No keys or values appear in logs at any level (checked at `debug` with a sample workload)
- [ ] **[T1]** No banned functions in the code (`grep` for `strcpy|sprintf|gets|atoi|atol|strtok`)
- [ ] **[T2]** Malformed-input corpus passes under ASan; `fuzz/corpus/regression/` all pass
- [ ] **[T2]** `SECURITY_ARCHITECTURE.md` §5 limitations are all listed in the README

## 11. Output and UI
- [ ] **[T1]** Log lines match the format regex; no ANSI codes when stderr is piped
- [ ] **[T1]** `NO_COLOR=1` disables color on a TTY (manual: note the terminal used)
- [ ] **[T1]** Invalid flags and values produce the exact messages and exit code 1 (`UI_SPECIFICATION.md` §2.4)
- [ ] **[T1]** `Ready to accept connections` is printed once at startup, after AOF loading completes

## 12. Documentation
- [ ] **[T1]** README has the required sections in order (`UI_SPECIFICATION.md` §8) for the tier being released
- [ ] **[T2]** README "Known limitations" lists at least: command subset, no replication, no auth/TLS, blocking `KEYS`, approximate memory accounting, Linux only, AOF without rewrite (AC-DOC-03)
- [ ] **[T2]** `DESIGN.md` and ADRs cover epoll mode, single thread, rehashing, expiry, absolute AOF timestamps, fsync policies and limits (AC-DOC-01)
- [ ] **[T2]** README renders correctly on GitHub desktop and mobile; Mermaid diagrams have text equivalents (manual: note devices)
- [ ] **[T2]** README includes the disclaimer that KVForge is not affiliated with or endorsed by Redis Ltd.
- [ ] **[T1]** `CHANGELOG.md` has an entry for this release

## 13. Repository hygiene and release
- [ ] **[T1]** `main` is green and contains exactly the release commit
- [ ] **[T1]** No secrets, large binaries or generated files committed; `.gitignore` covers `build*/`
- [ ] **[T1]** Version string updated in `CMakeLists.txt` and code
- [ ] **[T1]** Tag `vX.Y.Z` created; GitHub release published
- [ ] **[T2]** Release assets attached: source tarball, static Linux x86-64 binary, SHA-256 checksums
- [ ] **[T2]** Resume and portfolio bullets updated with measured numbers only (no number you cannot reproduce from a raw file)

## 14. Sign-off
| Item | Value |
|---|---|
| Open S1 defects | must be 0 |
| Open S2 defects | must be 0 |
| Open S3 defects | listed in release notes |
| Checklist completed by | |
| Date | |
| Release approved | yes / no |

## 15. If an item fails
1. Record the failure with the item ID, observed behavior and evidence.
2. Classify severity per `TEST_STRATEGY.md` §10.
3. Fix with a regression test (S1/S2 block the release).
4. Re-run the failed item and any item it could affect; re-run the full automated suite.
5. Update this checklist if the failure revealed a missing check.