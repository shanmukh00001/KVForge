# KVForge — Test Strategy

## 1. Purpose
KVForge's value rests on correctness evidence. This strategy defines what is tested, at which level, with which tools, how results are trusted, and what gates a merge or a release. Concrete test cases live in `TEST_PLAN.md`; pre-release manual checks live in `QA_CHECKLIST.md`; requirements and acceptance criteria are in `PRD.md` and `ACCEPTANCE_CRITERIA.md`.

## 2. Quality goals
| Goal | Measure |
|---|---|
| Correct replies | 100% agreement with Redis 7.x on the supported deterministic command set across ≥ 10,000 generated sequences (AC-DIFF-01) |
| Robust against bad input | No crash, hang or sanitizer finding in fuzzing (≥ 1 hour per release) and in the malformed-input corpus |
| Memory safe | ASan and UBSan clean across the whole suite |
| Durable | Crash-recovery tests match the fsync policy guarantees |
| Bounded resources | Limit tests prove memory and connection bounds |
| Measured performance | Reproducible benchmark reports with methodology |
| Maintainable | Line coverage ≥ 85% for parser, hash table, expiry, TTL and AOF modules (AC-QUA-03) |

## 3. Test pyramid
```
                 ┌───────────────────────────┐
                 │ Benchmarks, abuse, manual │   few, slow, release-time
                 ├───────────────────────────┤
                 │ Differential, crash,      │   randomized, oracle-based
                 │ fuzz                      │
                 ├───────────────────────────┤
                 │ Integration (real binary, │   dozens, seconds each
                 │ real sockets, real files) │
                 ├───────────────────────────┤
                 │ Unit (parser, table,      │   hundreds, milliseconds each
                 │ expiry, commands, AOF)    │
                 └───────────────────────────┘
```

## 4. Test types
| Type | What it proves | Tooling | Runs |
|---|---|---|---|
| Unit | Individual modules behave to spec; edge cases and error paths | GoogleTest, fake clock, in-memory `ReplyWriter` | Every commit |
| Command-level | Handlers produce exact RESP replies and AOF records without sockets | GoogleTest | Every commit |
| Integration | The real binary behaves correctly over TCP, files and signals | GoogleTest launching `kvforge` as a child process, plus raw socket helpers | Every commit |
| Protocol robustness | Split packets, one-byte delivery, pipelining, malformed input, limit violations | GoogleTest with raw sockets | Every commit |
| Property / model-based | Hash table and store match a trusted model (`std::unordered_map`) under random operations | GoogleTest with seeded RNG | Every commit (bounded size), nightly (larger) |
| Differential | KVForge's replies equal real Redis 7.x for the same sequence | Python driver with `redis` package | Every PR (reduced count), release (full count) |
| Fuzzing | Parser and AOF loader never crash or hang on arbitrary bytes | libFuzzer with ASan/UBSan | PR smoke (60 s), release (≥ 1 h) |
| Crash recovery | After `kill -9`, data matches durability guarantee; truncated and corrupt AOFs are handled | Python/shell runner | PR (short), release (full) |
| Sanitizer builds | Memory errors and undefined behavior | ASan + UBSan | Every commit |
| Static analysis | Bug patterns, style | clang-tidy, compiler warnings | Every commit |
| Performance | Throughput and latency vs Redis | `redis-benchmark`, scripts in `bench/` | Release, plus before/after on hot-path changes |
| Resource-limit and abuse | Bounded memory, client cap, slow reader behavior | Integration harness | PR (selected), release (full) |
| Documentation | README quick start works from a clean checkout | CI job in a clean Ubuntu container | Every PR touching docs/build, release |
| Manual QA | Items not worth automating (visual README check, `NO_COLOR` on a TTY) | `QA_CHECKLIST.md` | Release |

## 5. Test oracles
| Behavior | Oracle |
|---|---|
| Command replies | Real Redis 7.x (differential), plus explicit expected values in unit tests |
| Hash table / store contents | `std::unordered_map` model |
| Expiry timing | Fake clock in unit tests; coarse real-time checks in integration tests (tolerance 50 ms on lower bound, 500 ms on upper bound) |
| AOF recovery | Model state captured before the crash, filtered by the fsync policy's guarantee |
| Parser | Reference corpus of valid and invalid inputs; round-trip tests (`encode(decode(x)) == x`) |
| Performance | Real Redis on the same machine and settings |

Where Redis behavior is version dependent, the test pins Redis 7.x and documents the version in the test header.

## 6. Determinism and reproducibility
1. Every random test prints its seed on start and accepts `KVFORGE_TEST_SEED` to replay a failure.
2. Time-dependent tests use the injected fake clock; tests that need real time use generous tolerances and avoid fixed sleeps longer than 200 ms.
3. Integration tests bind port 0 (ephemeral) and discover the actual port; they run in a per-test temporary directory.
4. Differential generators avoid non-deterministic outputs (no `KEYS` order dependence; `KEYS` compared as sets; `INFO` excluded).
5. CI runs on fixed OS images; Redis version is recorded in logs.
6. Flaky tests are quarantined within 24 hours and fixed within one week; they are never ignored.

## 7. Test data
| Dataset | Description |
|---|---|
| Key pool | 64 short keys plus 8 binary-ish keys (including `\0`, `\r\n`, high bytes) so collisions between operations are frequent |
| Value pool | Empty string, short strings, integers (positive, negative, zero, near int64 limits), non-canonical numerics (`01`, `+1`, ` 1`, `1.5`), 1 KiB and 1 MiB blobs |
| TTL pool | Large TTLs (≥ 100,000 seconds) in differential tests so keys never expire during the run; small TTLs only in dedicated timing tests |
| Malformed corpus | Files in `tests/fuzz/corpus/`: negative lengths, huge lengths, missing CRLF, truncated arrays, nested arrays, random binary, unbalanced quotes in inline mode |
| AOF fixtures | Valid, truncated at every byte of the final record, corrupted in the middle, huge, with expired absolute deadlines |

## 8. Entry and exit criteria
| Stage | Entry | Exit |
|---|---|---|
| Phase done | Code complete for the phase | The phase's acceptance criteria tests pass locally and in CI |
| Merge to `main` | Pull request open | CI green: build (GCC+Clang), unit, integration, ASan/UBSan, clang-tidy, format, fuzz smoke, differential (reduced), README quick-start job (if relevant) |
| Release v0.1.0 | All Tier 1 phases merged | Every `[T1]` AC passes; QA checklist Tier 1 section ticked |
| Release v0.2.0 | All Tier 2 phases merged | Every `[T1]` and `[T2]` AC passes; full differential (10,000), 1-hour fuzz run, full crash suite, benchmark report, QA checklist complete |

## 9. CI gating
| Job | Trigger | Blocking |
|---|---|---|
| build-gcc, build-clang | every push | yes |
| unit + integration | every push | yes |
| sanitizers (ASan + UBSan) | every push | yes |
| format + clang-tidy | every push | yes |
| differential-quick (1,000 sequences) | every PR | yes |
| fuzz-smoke (60 s per harness) | every PR | yes |
| crash-quick | every PR | yes |
| coverage report | every PR | no (reported; threshold check at release) |
| differential-full, fuzz-long, crash-full, abuse tests | manual dispatch / release tag | yes for release |
| benchmark | manual only (needs a quiet machine) | no |

## 10. Defect management
| Severity | Definition | Response |
|---|---|---|
| S1 | Crash, data corruption, memory-safety bug, wrong reply for a supported command | Fix before any merge; add regression test; add fuzz/differential case if applicable |
| S2 | Incorrect error text, limit not enforced, performance regression > 20% on a tracked benchmark | Fix before the next release |
| S3 | Documentation error, log wording | Fix in normal flow |
Every fixed defect gets a regression test referencing its issue number.

## 11. Differences from Redis
Every intentional difference is recorded in `docs/COMPATIBILITY.md` (created in Phase 11) with: command, Redis behavior, KVForge behavior, reason. Differential tests exclude only differences listed there, and the exclusion list is checked into the repository so it cannot grow silently.

Known intentional differences at v0.2: unknown-command error text lacks Redis 7's `with args beginning with` suffix; `INFO` has no `redis_version`; `COMMAND` returns an empty array; `KEYS` order is unspecified; `CONFIG GET` supports exact names only.

## 12. Non-goals of testing
- No testing of features outside `SCOPE.md` (replication, TLS, scripting).
- No claims of full Redis compatibility.
- No testing on non-Linux platforms.
- No long-duration soak beyond the 1-hour release soak described in `TEST_PLAN.md` §11.

## 13. Roles
The project has one developer; the roles are hats worn in order: author (writes tests with code), reviewer (re-reads the pull request against the checklist before merging), release manager (runs the release suites and QA checklist).

## 14. Metrics reported at release
Test counts by type, coverage by module, differential sequences and mismatches, fuzz hours and executions, crash-test iterations, sanitizer findings (target: 0), open defects by severity (target: no open S1/S2).