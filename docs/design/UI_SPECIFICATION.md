# KVForge — UI Specification (CLI, Logs, Reports, README)

This document specifies every user-visible text surface exactly, so implementation and tests can match it character for character where noted.

## 1. Surfaces
| Surface | Medium | Audience |
|---|---|---|
| S1 Command line (`--help`, `--version`) | stdout | Operator |
| S2 Startup banner and logs | stderr | Operator |
| S3 Protocol replies and `INFO` | TCP | Client / Operator |
| S4 Errors on startup | stderr + exit code | Operator |
| S5 Test and benchmark reports | Markdown files | Reviewer |
| S6 README | GitHub-rendered Markdown | Everyone |

## 2. S1 Command line

### 2.1 `kvforge --help` (exact text, printed to stdout, exit code 0)
```text
KVForge 0.2.0 - a Redis-compatible key-value server

Usage:
  kvforge [options]
  kvforge --config <file> [options]

Network:
  --bind <addr>            Listen address (default: 127.0.0.1)
  --port <port>            Listen port (default: 6380)
  --max-clients <n>        Maximum simultaneous clients (default: 10000)

Persistence:
  --appendonly <yes|no>    Enable the append-only file (default: no)
  --appendfsync <policy>   always | everysec | no (default: everysec)
  --appendfilename <path>  AOF path (default: kvforge.aof)

Operation:
  --expire-budget-us <n>   Active-expiry time budget per loop iteration (default: 1000)
  --loglevel <level>       error | warn | info | debug (default: info)
  --config <file>          Read options from a file (command-line flags win)

Other:
  --help                   Show this help and exit
  --version                Show the version and exit

Notes:
  KVForge has no authentication. Binding to a non-loopback address exposes all data.
```

### 2.2 `kvforge --version` (stdout, exit 0)
```text
KVForge 0.2.0 (C++17, <compiler> <version>, <build type>)
```

### 2.3 Config file format
Lines of `name value`, `#` starts a comment, names are the flag names without `--`.
```text
# kvforge.conf
bind 127.0.0.1
port 6380
appendonly yes
appendfsync everysec
loglevel info
```

### 2.4 Invalid usage (stderr, exit code 1)
| Situation | Message |
|---|---|
| Unknown flag | `kvforge: unknown option '--foo'. Try 'kvforge --help'.` |
| Missing value | `kvforge: option '--port' requires a value.` |
| Bad value | `kvforge: invalid value 'abc' for '--port' (expected an integer from 1 to 65535).` |
| Bad enum | `kvforge: invalid value 'sometimes' for '--appendfsync' (expected always, everysec or no).` |
| Config file unreadable | `kvforge: cannot read config file '<path>': <system error>.` |

## 3. S2 Startup banner and logs

### 3.1 Normal startup sequence (level `info`)
```text
2026-10-07T09:15:42.001Z 4242 INFO  KVForge 0.2.0 starting pid=4242
2026-10-07T09:15:42.002Z 4242 INFO  Configuration port=6380 bind=127.0.0.1 appendonly=yes appendfsync=everysec max_clients=10000
2026-10-07T09:15:42.010Z 4242 INFO  Loading AOF path=kvforge.aof size_bytes=204800
2026-10-07T09:15:42.120Z 4242 INFO  AOF loaded records=10231 keys=8950 skipped_expired=12 elapsed_ms=110
2026-10-07T09:15:42.121Z 4242 INFO  Ready to accept connections addr=127.0.0.1 port=6380
```
No ASCII-art banner. The words `Ready to accept connections` are fixed; integration tests wait for this line.

### 3.2 Event messages
| Level | Message template |
|---|---|
| DEBUG | `Client connected id=<n> peer=<ip:port>` / `Client disconnected id=<n> reason=<eof|error|protocol|limit>` |
| WARN | `Binding to a non-loopback address without authentication exposes all data to the network addr=<ip>` |
| WARN | `Client closed for exceeding output buffer limit id=<n> limit_bytes=<n>` |
| WARN | `Client closed for protocol error id=<n> reason=<text>` |
| WARN | `Truncated AOF tail path=<p> offset=<n> removed_bytes=<n>` |
| WARN | `max_clients lowered to <n> because of the open file limit` |
| ERROR | `Cannot bind addr=<ip> port=<n>: <system error>` |
| ERROR | `Corrupt AOF path=<p> offset=<n>` |
| ERROR | `Cannot open AOF path=<p>: <system error>` |
| INFO | `Received signal=<SIGTERM|SIGINT>, shutting down` |
| INFO | `AOF flushed bytes=<n>` and `Shutdown complete` |

## 4. S3 Protocol replies and INFO
See `architecture/API_SPECIFICATION.md` for the complete reply contract, error messages and the `INFO` layout. Conventions:
- `INFO` field order is stable across versions; new fields are appended to the end of their section.
- `used_memory_human` uses binary units with two decimals: `1.00K`, `2.50M`, `1.20G`.
- `uptime_in_days` is `uptime_in_seconds / 86400` rounded down.

## 5. S4 Startup failure presentation
Rule: print one `ERROR` log line stating the cause with key facts, then exit with the right code. No stack traces. Examples:
```text
2026-10-07T09:15:42.003Z 4242 ERROR Cannot bind addr=127.0.0.1 port=6380: Address already in use
2026-10-07T09:15:42.050Z 4242 ERROR Corrupt AOF path=kvforge.aof offset=18234
```
Exit codes: see `DESIGN_SYSTEM.md` §7.

## 6. Interactive use with `redis-cli`
Expected session (replies shown as `redis-cli` prints them):
```text
$ redis-cli -p 6380
127.0.0.1:6380> PING
PONG
127.0.0.1:6380> SET session abc EX 30
OK
127.0.0.1:6380> TTL session
(integer) 30
127.0.0.1:6380> GET missing
(nil)
127.0.0.1:6380> INCR hits
(integer) 1
127.0.0.1:6380> FOO
(error) ERR unknown command 'FOO'
```
The prompt appears without errors or warnings (AC-SRV-03).

## 7. S5 Benchmark report template (`bench/results/<date>-report.md`)
```markdown
# KVForge benchmark report <YYYY-MM-DD>

## Environment
| Item | Value |
|---|---|
| Machine | <model, vCPU, RAM> |
| OS / kernel | <Ubuntu 22.04, 5.15.x> |
| Compiler / flags | <gcc 11.4, -O2 -DNDEBUG> |
| KVForge commit | <hash> |
| Redis version | <7.x.y> |
| Persistence | <both off | both AOF everysec | both AOF always> |
| CPU pinning | server on core 2; benchmark on cores 0-1 |

## Method
Command: `redis-benchmark -p <port> -t set,get -n 1000000 -c 50 -P <1|16> -d 64 --csv`
Runs per scenario: 5 (median reported). Raw files: `bench/results/raw/...`

## Results
| Scenario | Server | Throughput (req/s) | p50 (ms) | p99 (ms) | RSS at 1M keys (MiB) |
|---|---|---|---|---|---|
| SET, P=1 | KVForge | ... | ... | ... | ... |
| SET, P=1 | Redis | ... | ... | ... | ... |
| GET, P=1 | KVForge | ... | ... | ... | ... |
| GET, P=1 | Redis | ... | ... | ... | ... |
| SET, P=16 | KVForge | ... | ... | ... | ... |
| SET, P=16 | Redis | ... | ... | ... | ... |
| GET, P=16 | KVForge | ... | ... | ... | ... |
| GET, P=16 | Redis | ... | ... | ... | ... |

## Analysis
<where KVForge is slower or faster and the likely reasons>

## Limitations of this measurement
<noise sources, single machine, synthetic workload>
```
(The `...` cells are filled by the benchmark script; they are results, not unresolved specification.)

### 7.1 Test report conventions
- Differential test summary line: `Differential: sequences=10000 commands=2500000 mismatches=0 seed=<n>`. The seed is always printed so failures are reproducible.
- Crash-recovery test summary: `Crash recovery: policy=<p> writes_acked=<n> recovered=<n> lost=<n>`.

## 8. S6 README structure (required order)
1. Title, tagline, badges (CI status, license)
2. One-paragraph description and the compatibility disclaimer
3. Highlights (5 bullets: epoll event loop, RESP2, custom hash table with incremental rehash, AOF with crash recovery, differential-tested)
4. Quick start (build, run, `redis-cli` session)
5. Supported commands (table)
6. Configuration (flags table)
7. Architecture (one diagram + 5-line explanation, link to docs)
8. Correctness (tests, differential testing, fuzzing, sanitizers)
9. Performance (summary table + link to full report, methodology)
10. Design decisions (links to ADRs)
11. Known limitations
12. Roadmap (Tier 3 options)
13. Project layout
14. Contributing and license
15. Disclaimer: "KVForge is not affiliated with or endorsed by Redis Ltd."

### 8.1 Quick start (exact commands in the README)
```bash
git clone https://github.com/<user>/kvforge.git
cd kvforge
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kvforge --port 6380 &
redis-cli -p 6380 PING        # PONG
```

## 9. Copy and tone
- Operator-facing text: neutral, imperative ("Check the AOF file permissions").
- No emoji in program output. Emoji are acceptable in README badges only.
- Error text names the problem first, then values: `invalid value 'abc' for '--port'`.

## 10. UI acceptance checks
| Check | Verified by |
|---|---|
| `--help` output equals §2.1 exactly | Integration test (golden file) |
| `Ready to accept connections` appears once after startup | Integration test |
| Invalid flag messages match §2.4 | Integration tests |
| Log line format matches the regex `^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \d+ (ERROR\|WARN \|INFO \|DEBUG) .+$` | Integration test |
| README quick start works on a fresh Ubuntu 22.04 container | CI job |