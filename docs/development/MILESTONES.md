# KVForge — Milestones

Planning assumptions:
- One developer, about 1.5–2 hours per working day (roughly 10–14 hours per week), alongside interview preparation.
- Target total: about 45–60 hours for Tier 1 + Tier 2, over 4 weeks. If more time is available per day the schedule compresses; do not add scope to fill it.
- Week numbers are relative to the start date. Dates are intentionally not fixed so the plan survives schedule shifts.

## 1. Milestone overview
| ID | Milestone | Phases | Est. hours | Target | Release |
|---|---|---|---|---|---|
| M0 | Project skeleton and CI | 0 | 3–4 | Week 1, days 1–2 | — |
| M1 | Server accepts clients | 1 | 5–7 | Week 1, days 3–5 | — |
| M2 | Protocol and `redis-cli` works | 2, 3 | 8–10 | Week 1 end / Week 2 start | — |
| M3 | Core data commands | 4, 5 | 7–9 | Week 2 | — |
| M4 | Expiry and robustness; **Tier 1 complete** | 6, 7 | 8–10 | Week 2 end | **v0.1.0** |
| M5 | Incremental rehash and AOF | 8, 9 | 10–12 | Week 3 | — |
| M6 | Verification suites | 10, 11 | 8–10 | Week 4 | — |
| M7 | Benchmarks and documentation; **Tier 2 complete** | 12 | 6–8 | Week 4 end | **v0.2.0** |
| M8 | Tier 3 (optional, at most one feature) | — | 8–15 | After v0.2, if time permits | v0.3.0 |

## 2. Milestone details

### M0 — Project skeleton and CI
**Deliverables:** repository with CMake, `.clang-format`, `.clang-tidy`, logging, config parsing, `--help`/`--version`, GitHub Actions workflow (GCC and Clang builds, unit tests).
**Exit criteria:** CI green; `--help` golden test passes; README stub with project name and disclaimer.
**Evidence to keep:** link to the green CI run.

### M1 — Server accepts clients
**Deliverables:** listener, epoll loop, connection buffers, signal handling, echo behavior, client cap.
**Exit criteria:** AC-NET-01, AC-NET-02; SIGTERM exits 0; ASan clean; idle CPU < 1%.
**Evidence:** integration test output; screenshot or log of 1,000 connections.

### M2 — Protocol and `redis-cli` works
**Deliverables:** RESP parser and writer, dispatcher, `PING`, `ECHO`, `COMMAND`, `CONFIG GET`, `CLIENT`, minimal `INFO`; fuzz harness started.
**Exit criteria:** AC-PING-*, AC-PRO-01..06, AC-SRV-03; `redis-benchmark -t ping` runs; 10-minute fuzz run clean.
**Evidence:** recorded `redis-cli` session (paste into README).

### M3 — Core data commands
**Deliverables:** hash table (full resize acceptable), `SET`/`GET`/`DEL`/`EXISTS`/`MSET`/`MGET`/`DBSIZE`/`FLUSHALL`, `INCR` family with overflow handling.
**Exit criteria:** AC-SET-01..04/08/09, AC-GET-01, AC-DEL-01, AC-EXISTS-01, AC-MULTI-*, AC-SRV-01..02, AC-INCR-*, AC-HT-01, AC-PRO-08.
**Evidence:** passing test report; first throughput smoke number from `redis-benchmark` (informal).

### M4 — Expiry and robustness (Tier 1 complete)
**Deliverables:** lazy and active expiry, TTL commands, `SET` options, limits, backpressure, fairness, graceful shutdown, README quick start.
**Exit criteria:** every `[T1]` item in `ACCEPTANCE_CRITERIA.md` passes; AC-DOC-02 verified in a fresh Ubuntu 22.04 container; CI green with sanitizer job.
**Release:** tag `v0.1.0`, write the changelog entry.
**Go/no-go checkpoint:** if M4 is not complete by the end of Week 3, stop and reassess scope (see §4 cut lines).

### M5 — Incremental rehash and AOF
**Deliverables:** two-table incremental rehash, `rehashing` in `INFO`, AOF writer with three fsync policies, loader with tail recovery and corruption detection, normalized records.
**Exit criteria:** AC-HT-02..04, AC-AOF-01..07.
**Evidence:** latency measurement of the resize at 1,000,000 keys; AOF replay equivalence test output.

### M6 — Verification suites
**Deliverables:** `KEYS`, complete `INFO`, differential test generator and runner, crash-recovery runner (`kill -9`), AOF-loader fuzz harness, coverage report, sanitizer CI job.
**Exit criteria:** AC-SRV-04, AC-INFO-*, AC-DIFF-01 (10,000 sequences, 0 mismatches or documented differences), AC-QUA-01..03, AC-OBS-01; 1-hour fuzz run on the parser recorded.
**Evidence:** differential summary line with seed; coverage summary; fuzz log excerpt.

### M7 — Benchmarks and documentation (Tier 2 complete)
**Deliverables:** `bench/` scripts and raw results, benchmark report, README complete per `UI_SPECIFICATION.md` §8, design docs and ADRs finalized, known limitations section, resume bullets filled with real numbers.
**Exit criteria:** AC-BEN-01..03, AC-DOC-01..04; QA checklist (`testing/QA_CHECKLIST.md`) fully ticked.
**Release:** tag `v0.2.0` with source tarball, static binary and checksums.

### M8 — Tier 3 (optional)
**Entry criteria:** M7 complete; one option chosen from `SCOPE.md` §4 with a design addendum.
**Exit criteria:** the option's acceptance criteria and tests pass; no regression in Tier 1–2 benchmark results (within noise); docs updated.
**Release:** `v0.3.0`.

## 3. Weekly plan
| Week | Focus | Output at end of week |
|---|---|---|
| 1 | M0, M1, start M2 | Building project; server accepts and echoes; parser in progress |
| 2 | Finish M2, M3, M4 | `redis-cli` fully usable; Tier 1 commands and expiry; **v0.1.0** |
| 3 | M5 | Incremental rehash and working AOF with recovery |
| 4 | M6, M7 | Verified, benchmarked, documented; **v0.2.0** |

Weekly review (15 minutes): compare completed exit criteria against the plan, update the risk list, decide whether a cut line applies.

## 4. Cut lines (what to drop first if time runs short)
Apply in this order; each cut must be documented as a known limitation.
1. Tier 3 entirely (already optional).
2. `KEYS` command (keep `INFO` and tests).
3. AOF fsync policy `always` (keep `everysec` and `no`); document the gap.
4. Coverage report (keep sanitizer CI).
5. Parser fuzzing duration: shorten from 1 hour to 15 minutes per release, state it in the README.
6. Differential test volume: reduce from 10,000 to 2,000 sequences, state it.

**Never cut:** the incremental parser tests, limits enforcement, AOF tail recovery, benchmark methodology honesty, README known limitations.

**Minimum shippable project (if only about 2.5 weeks are available):** Phases 0–7 (Tier 1) plus Phase 9 (AOF with `everysec` and `always`), a basic differential test of the string commands, and one honest benchmark table. This is still a credible flagship.

## 5. Risk register
| ID | Risk | Likelihood | Impact | Mitigation | Trigger |
|---|---|---|---|---|---|
| R1 | Parser edge cases consume more time than planned | Medium | Medium | Start fuzzing in Phase 2; test every split position | M2 slips by more than 3 days |
| R2 | `epoll` write-path bugs (stuck replies) | Medium | High | Slow-reader integration test early | Any hang in `redis-benchmark` |
| R3 | Differential tests reveal many divergences | Medium | Medium | Classify quickly; document intentional differences instead of chasing parity | More than 20 open divergences |
| R4 | Benchmark numbers disappoint | Medium | Low | Present honestly with analysis; profile with `perf` for one optimization round | KVForge below 30% of Redis |
| R5 | Placement preparation reduces available time | High | High | Cut lines; minimum shippable scope; fixed daily time box | Two consecutive weeks under 8 hours |
| R6 | No native Linux environment for benchmarks | Medium | Medium | Use an Azure Linux VM for the benchmark runs only | Before M7 |
| R7 | Scope creep from "just one more command" | High | Medium | Change control in `SCOPE.md` §8 | Any new command proposed |

## 6. Progress tracking
Maintain a table in `docs/PROGRESS.md` (created at M0) with one row per milestone: planned date, actual date, hours spent, notes. Hours spent feed the retrospective and make honest estimates for future projects.

## 7. Definition of done (every milestone)
- All exit criteria met with automated tests where possible.
- CI green, including sanitizer job after M4.
- Docs updated for any behavior change.
- Evidence artifact saved (CI link, report file, recorded session).
- `main` is releasable.

## 8. Release checklist (v0.1.0 and v0.2.0)
1. All `[T1]` (or `[T1]` + `[T2]`) acceptance criteria pass.
2. `QA_CHECKLIST.md` sections for that tier are complete.
3. Changelog entry written; version string updated in code (`--version`, `INFO`).
4. Tag created; GitHub release with tarball, binary, SHA-256 checksums.
5. README updated with real numbers and links to raw results (v0.2.0).
6. Resume bullets updated with measured numbers only.