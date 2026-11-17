# Phase 12: Benchmarks, Documentation & v0.2 Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Phase 12 for KVForge: automated benchmark harness (`bench/run.sh`, `bench/collect.py`, `AC-BEN-01..03`), complete architecture decision records (`AC-DOC-01`), comprehensive README following the 15-section UI spec (`AC-DOC-02..04`), and prepare the v0.2 Tier 2 release.

**Architecture:**
- Bash and Python benchmarking framework executing `redis-benchmark` matrices across KVForge and Redis 7.2.4 with CPU pinning (`taskset`), RSS memory capture from `/proc/<pid>/status`, 5-run median aggregation, and raw CSV logging in `bench/results/raw/`.
- ADR documentation in `docs/architecture/` documenting core architectural choices and trade-offs.
- A production-grade `README.md` following `UI_SPECIFICATION.md` §8 with exact quickstart commands, command tables, benchmark results, known limitations, and disclaimers.

**Tech Stack:** C++17, Bash, Python 3, `redis-benchmark`, Linux `/proc` filesystem, CMake.

**Spec:** `docs/testing/TEST_PLAN.md` §10, `docs/design/UI_SPECIFICATION.md` §7–§8, `docs/ACCEPTANCE_CRITERIA.md` AC-BEN-01..03, AC-DOC-01..04.

## Global Constraints
- Linux / WSL2 Ubuntu 22.04 environment.
- Benchmarking must compare identical persistence settings on the same machine.
- Benchmark matrix: `SET` and `GET`, `-P 1` and `-P 16`, `-c 50`, `-n 1000000`, 5 runs per cell to compute median (`AC-BEN-01`).
- Performance results in README must link to raw output files (`AC-DOC-04`).
- README must follow the strict 15-section order specified in `UI_SPECIFICATION.md` §8 (`AC-DOC-02`, `AC-DOC-03`).

---

### Task 1: Benchmark Harness and Aggregator (`bench/`)

**Files:**
- Create: `bench/run.sh`
- Create: `bench/collect.py`
- Directory: `bench/results/raw/`

- [ ] **Step 1: Implement `bench/run.sh`**
  - Record system hardware, kernel, compiler, flags, Redis version, and git commit.
  - Build KVForge in Release mode (`-DCMAKE_BUILD_TYPE=Release`).
  - Warm up with 200,000 requests.
  - Execute matrix: `SET`/`GET` at `-P 1` and `-P 16`, `-c 50`, random keys `-r 1000000`, 64-byte values `-d 64`.
  - Repeat 5 times per cell for both Redis 7.2.4 and KVForge, saving raw outputs to `bench/results/raw/`.
  - Capture `/proc/<pid>/status` `VmRSS` after loading 1,000,000 keys.

- [ ] **Step 2: Implement `bench/collect.py`**
  - Parse raw outputs, compute 5-run median throughput, p50, and p99 latency.
  - Fill Markdown benchmark report per `UI_SPECIFICATION.md` §7.

- [ ] **Step 3: Run benchmark suite**
  - Execute benchmark run and generate `bench/BENCHMARK_REPORT.md`.

---

### Task 2: Architecture Decision Records (ADRs)

**Files:**
- Create/Update: `docs/architecture/ADR-001-epoll.md`
- Create/Update: `docs/architecture/ADR-002-single-threaded.md`
- Create/Update: `docs/architecture/ADR-003-incremental-rehash.md`
- Create/Update: `docs/architecture/ADR-004-expiry-engine.md`
- Create/Update: `docs/architecture/ADR-005-aof-durability.md`
- Create/Update: `docs/architecture/ADR-006-resource-limits.md`

- [ ] **Step 1: Document design decisions and trade-offs**
  - Verify all 7 key architectural decisions named in `AC-DOC-01` are thoroughly documented with context, decision, trade-offs, and consequences.

---

### Task 3: Comprehensive Production README.md

**Files:**
- Create: `README.md`

- [ ] **Step 1: Draft README matching UI_SPECIFICATION §8 order**
  - Title, tagline, badges
  - Description and compatibility disclaimer
  - Highlights (5 bullets)
  - Quickstart (tested on clean environment)
  - Supported commands table
  - Configuration flags table
  - Architecture diagram and notes
  - Correctness and verification (Differential, Crash, Fuzzing, Sanitizers)
  - Performance summary table linking raw bench files
  - Design decisions and ADR links
  - Known limitations (`AC-DOC-03`)
  - Roadmap (Tier 3 options)
  - Project layout
  - Contributing and license
  - Legal disclaimer

- [ ] **Step 2: Verify Quickstart and Acceptance Criteria**
  - Test quickstart commands (`AC-DOC-02`).
  - Update `MILESTONES.md` for v0.2 Tier 2 completion.
