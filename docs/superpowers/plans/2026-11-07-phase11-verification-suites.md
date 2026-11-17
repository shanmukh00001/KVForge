# Phase 11: Verification Suites Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the full Phase 11 verification suite for KVForge: differential testing against Redis 7.x (`AC-DIFF-01`), crash-recovery and AOF durability testing (`AC-AOF-04..07`), libFuzzer harnesses for parser and persistence (`AC-QUA-02`), and sanitizer verification (ASan + UBSan, `AC-QUA-01`) with coverage verification (`AC-QUA-03`).

**Architecture:** 
- A Python socket-level differential runner (`run_diff.py`) and reproducible generator (`gen_commands.py`) comparing KVForge against real Redis 7.x with delta-debugging minimizer.
- A Python crash-testing harness (`run_crash.py`) validating `kill -9` crash resistance under load, single-byte tail truncation handling, middle corruption exit codes, and fsync batch accounting.
- Standalone LLVM/Clang libFuzzer C++ targets for `RESPParser` and `AOF` replay verifying differential chunking invariants and memory boundaries.
- Clang ASan/UBSan build configuration with 0 leaks/UBs across the entire test suite and $\ge 85\%$ core line coverage.

**Tech Stack:** C++17, Python 3 (raw sockets, subprocess, random, struct), Clang 14 (`libFuzzer`, AddressSanitizer, UndefinedBehaviorSanitizer), Redis 7.x (local build), CMake 3.22.

**Spec:** `docs/testing/TEST_PLAN.md` §6–§9, `docs/ACCEPTANCE_CRITERIA.md` AC-DIFF-01, AC-QUA-01..03, AC-AOF-04..07, `docs/development/IMPLEMENTATION_PLAN.md` §13.

## Global Constraints
- Linux / WSL2 Ubuntu 22.04 environment.
- Strict C++17 conformance; zero compiler warnings (`-Wall -Wextra -Wpedantic -Werror`).
- Zero memory leaks and zero undefined behavior under ASan and UBSan (`AC-QUA-01`).
- Differential testing must cover at least 10,000 random command sequences with identical normalized replies (`AC-DIFF-01`).
- Parser and AOF loader libFuzzer harnesses must run with no crashes, infinite loops, or memory safety findings (`AC-QUA-02`).
- Line coverage for core modules (parser, store, TTL, int_parse, AOF) must be $\ge 85\%$ (`AC-QUA-03`).

## Review Focus
1. Raw socket connection handling and RESP normalization in Python without relying on external dependencies.
2. Differential tolerance rules: `TTL` tolerance $\le 1$ second, `PTTL` tolerance $\le 2000$ ms, error prefix matching (`ERR`, `WRONGTYPE`).
3. `kill -9` timing and OS file sync verification: unacknowledged writes vs acknowledged writes under `always`.
4. Tail truncation: verifying that dropping a partial write preserves all preceding records cleanly.
5. LibFuzzer chunking invariant: feeding arbitrary bytes all-at-once vs in 1..16 byte fragments yields identical command streams.

---

### Task 1: Command Generator for Differential Testing

**Files:**
- Create: `tests/differential/gen_commands.py`
- Test: `tests/differential/test_generator.py`

**Interfaces:**
- Produces: `generate_command_sequence(rng: random.Random, length: int) -> List[List[str]]`
- Produces: `format_resp_command(cmd_args: List[str]) -> bytes`

- [ ] **Step 1: Write tests for command generator**
  - Verify deterministic command generation with fixed seeds.
  - Verify RESP multibulk wire serialization for generated commands.
  - Verify distribution covers strings, integers, keys, expiry, and negative/wrong-arity tests.

- [ ] **Step 2: Implement generator module**
  - Implement command weights and argument generators according to `TEST_PLAN.md` §6.2.
  - Include option permutations for `SET` (`EX`, `PX`, `NX`, `XX`, and conflicts).
  - Include integer bounds, large TTLs, and unknown commands.

- [ ] **Step 3: Run generator unit tests**
  - Verify 100 sequences generate valid RESP arrays without errors.

---

### Task 2: Differential Testing Runner and Comparator

**Files:**
- Create: `tests/differential/compat_exclusions.json`
- Create: `tests/differential/run_diff.py`
- Modify: `CMakeLists.txt` (optional test target)

**Interfaces:**
- Consumes: `tests/differential/gen_commands.py`
- Produces: `run_differential_test(num_sequences, seed, redis_port, kvforge_port)`
- Exit code 0 if all sequences match; non-zero on mismatch with delta-debugged reproduction.

- [ ] **Step 1: Implement RESP raw socket client and reply parser**
  - Lightweight parser parsing Simple Strings, Errors, Integers, Bulk Strings, Nulls, and Arrays from a raw socket buffer.
  - Reply normalization (error text prefix, TTL $\pm 1$s, PTTL $\pm 2000$ms).

- [ ] **Step 2: Implement Delta-Debugging Minimizer**
  - When a mismatch is found between Redis and KVForge, shrink the command list by bisecting to find the minimal failing sequence.

- [ ] **Step 3: Implement Additional Differential Scenarios**
  - `KEYS` pattern comparison as unordered sets for patterns `*`, `a*`, `?b`, `[ab]*`, `[^a]*`.
  - Pipelined execution (50 commands per socket send).
  - Chunked split-write delivery (feeding byte-by-byte or in random fragments).

- [ ] **Step 4: Execute differential tests against Redis 7.x and KVForge**
  - Run 10,000 sequences verifying zero unhandled mismatches (`AC-DIFF-01`).

---

### Task 3: Crash Recovery & AOF Durability Test Suite

**Files:**
- Create: `tests/crash/run_crash.py`

**Interfaces:**
- Consumes: `build/kvforge` binary
- Produces: CLI script testing `kill -9` under write load, tail truncation, middle corruption, edge cases.

- [ ] **Step 1: Implement `kill -9` test**
  - Start KVForge with `--appendonly yes --appendfsync always`.
  - Send rapid numbered writes (`SET k<i> v<i>`, `INCR ctr`).
  - Send `kill -9` at random time intervals (0.05–0.5s).
  - Restart KVForge with same AOF; read full keyspace; verify all acknowledged writes exist and no corruption occurred (`AC-AOF-04`).

- [ ] **Step 2: Implement single-byte tail truncation test**
  - Generate 1,000 commands in an AOF file.
  - For every byte offset in the 1,000th command, truncate the file.
  - Start KVForge; assert it logs truncation warning, strips partial command, and loads 999 records intact (`AC-AOF-05`).

- [ ] **Step 3: Implement middle-corruption test**
  - Corrupt a byte in the header of record 500.
  - Start KVForge; assert it exits with code 1 and logs corruption offset (`AC-AOF-06`).

- [ ] **Step 4: Implement edge-cases & file permissions tests**
  - Empty file: starts cleanly as empty store.
  - File with only partial 1st command: truncates to 0, starts as empty store.
  - File permissions: verify generated AOF file has `0600` permissions (`AC-AOF-01`).

---

### Task 4: LibFuzzer Harnesses for RESP Parser and AOF Loader

**Files:**
- Create: `tests/fuzz/fuzz_resp_parser.cpp`
- Create: `tests/fuzz/fuzz_aof_loader.cpp`
- Create: `tests/fuzz/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)`
- Option: `-DENABLE_FUZZING=ON`

- [ ] **Step 1: Implement `fuzz_resp_parser.cpp`**
  - Differential chunking test: parse all-at-once vs chunked by derived sizes.
  - Invariants: no crash, commands match, memory caps respected.

- [ ] **Step 2: Implement `fuzz_aof_loader.cpp`**
  - Write fuzzer payload to temporary file or memfd.
  - Invoke `AOF::load_and_replay`.
  - Invariants: returns `Ok`, `TruncatedTail`, or `Corrupt`; no crash or hang.

- [ ] **Step 3: Integrate into CMake with Clang 14**
  - Add `ENABLE_FUZZING` option linking `fuzzer,address`.
  - Compile fuzzers and run for 60 seconds each verifying clean execution (`AC-QUA-02`).

---

### Task 5: Sanitizer Verification & Line Coverage

**Files:**
- Modify: `CMakeLists.txt` (build flags)

- [ ] **Step 1: Build entire project under ASan + UBSan**
  - Build with `-fsanitize=address,undefined -fno-omit-frame-pointer`.
  - Run all 42+ unit tests with `ctest` / `./kvforge_tests`.
  - Assert zero memory leaks and zero sanitizer findings (`AC-QUA-01`).

- [ ] **Step 2: Code Coverage Analysis**
  - Build with `--coverage` flags.
  - Run tests and calculate line coverage using `gcov` / `lcov`.
  - Verify core modules (parser, store, TTL, int_parse, AOF) exceed 85% line coverage (`AC-QUA-03`).
