# KVForge — User Stories

## 1. Personas
| ID | Persona | Description |
|---|---|---|
| P1 | Application developer | Connects to KVForge with `redis-cli` or a Redis client library to store counters, cache values and use TTL keys |
| P2 | Operator | Runs the server, configures persistence, watches health, restarts after crashes |
| P3 | Contributor / learner | Reads and modifies the code to understand how a key-value store works |
| P4 | Technical reviewer | Evaluates correctness, performance claims and engineering quality |

## 2. Story format
`US-<area>-<nn>`: As a `<persona>`, I want `<capability>` so that `<benefit>`. Each story lists the related requirements (PRD) and acceptance criteria (AC).

## 3. Connectivity and protocol
| ID | Story | Related |
|---|---|---|
| US-NET-01 | As P1, I want to connect with `redis-cli -p 6380` and run `PING` so that I can confirm the server is alive. | FR-NET-01, FR-CMD-01 / AC-PING-01 |
| US-NET-02 | As P1, I want to use `telnet` or `nc` and type plain commands so that I can explore without a client library. | FR-PRO-02 / AC-PRO-04 |
| US-NET-03 | As P1, I want to send many commands at once (pipelining) and get replies in order so that I can achieve high throughput. | FR-PRO-04 / AC-PRO-03 |
| US-NET-04 | As P1, I want a clear error when I send an unknown command or the wrong number of arguments so that I can fix my client code. | FR-PRO-07 / AC-PRO-05, AC-PRO-06 |
| US-NET-05 | As P2, I want the server to stay up when a client sends garbage or disconnects mid-command so that one bad client cannot hurt the others. | FR-PRO-05, FR-NET-07 / AC-PRO-07, AC-LIM-01 |
| US-NET-06 | As P2, I want the server to shut down cleanly on `SIGTERM` so that no acknowledged data is lost. | FR-NET-06 / AC-NET-03 |

## 4. Data commands
| ID | Story | Related |
|---|---|---|
| US-DAT-01 | As P1, I want `SET` and `GET` so that I can store and fetch values by key. | FR-CMD-02, FR-CMD-03 / AC-SET-01, AC-GET-01 |
| US-DAT-02 | As P1, I want `SET key value NX` so that I can create a key only if it does not exist (simple locks and idempotency). | FR-CMD-02 / AC-SET-03 |
| US-DAT-03 | As P1, I want `SET key value EX 60` so that I can store a value that disappears after 60 seconds. | FR-CMD-02 / AC-SET-05, AC-EXP-01 |
| US-DAT-04 | As P1, I want `INCR` so that I can maintain atomic counters. | FR-CMD-05 / AC-INCR-01 |
| US-DAT-05 | As P1, I want a clear error when `INCR` is used on a non-numeric value or would overflow so that counters never silently wrap. | FR-CMD-05 / AC-INCR-03, AC-INCR-04 |
| US-DAT-06 | As P1, I want `MSET` and `MGET` so that I can read and write many keys in one round trip. | FR-CMD-06 / AC-MULTI-01, AC-MULTI-02 |
| US-DAT-07 | As P1, I want `DEL` and `EXISTS` to accept multiple keys and return counts so that I can clean up and check in bulk. | FR-CMD-04 / AC-DEL-01, AC-EXISTS-01 |
| US-DAT-08 | As P1, I want binary-safe keys and values so that I can store any bytes. | FR-PRO-06 / AC-PRO-08 |

## 5. Expiry
| ID | Story | Related |
|---|---|---|
| US-EXP-01 | As P1, I want `EXPIRE`, `PEXPIRE` and `TTL` so that I can control and inspect key lifetimes. | FR-CMD-07 / AC-EXP-02, AC-EXP-03 |
| US-EXP-02 | As P1, I want expired keys to never be returned so that I never read stale data. | FR-EXP-01 / AC-EXP-01 |
| US-EXP-03 | As P2, I want expired keys to be reclaimed even if nobody reads them so that memory does not grow without bound. | FR-EXP-02 / AC-EXP-05 |
| US-EXP-04 | As P1, I want `PERSIST` to remove a TTL so that a key becomes permanent. | FR-CMD-07 / AC-EXP-04 |

## 6. Persistence and recovery
| ID | Story | Related |
|---|---|---|
| US-PER-01 | As P2, I want to enable an append-only file so that data survives restarts. | FR-PER-01 / AC-AOF-01 |
| US-PER-02 | As P2, I want to choose `always`, `everysec` or `no` for fsync so that I can trade durability against speed. | FR-PER-03 / AC-AOF-04 |
| US-PER-03 | As P2, I want TTLs to keep their original deadlines across a restart so that keys do not live longer than intended. | FR-PER-02 / AC-AOF-03 |
| US-PER-04 | As P2, I want the server to recover automatically when the last AOF record was cut off by a crash so that a power loss does not prevent startup. | FR-PER-05 / AC-AOF-05 |
| US-PER-05 | As P2, I want the server to refuse to start with a clear message when the AOF is corrupted in the middle so that I do not silently lose data. | FR-PER-06 / AC-AOF-06 |

## 7. Observability and operations
| ID | Story | Related |
|---|---|---|
| US-OBS-01 | As P2, I want `INFO` to show uptime, clients, commands processed, hits/misses, expired keys and memory so that I can check health. | FR-OBS-01 / AC-INFO-01 |
| US-OBS-02 | As P2, I want leveled logs on stderr so that I can debug problems. | FR-OBS-02 / AC-OBS-01 |
| US-OBS-03 | As P2, I want configuration through flags and a file so that I can run the server in different environments. | PRD §4 / AC-CFG-01 |
| US-OBS-04 | As P2, I want the server to cap clients, buffers and argument sizes so that a misbehaving client cannot exhaust memory. | PRD §5.3 / AC-LIM-01..04 |

## 8. Quality and evidence
| ID | Story | Related |
|---|---|---|
| US-QUA-01 | As P4, I want differential tests against real Redis so that I can trust the replies are correct. | AC-DIFF-01 |
| US-QUA-02 | As P4, I want reproducible benchmarks with exact commands and hardware so that I can verify performance claims. | AC-BEN-01..03 |
| US-QUA-03 | As P4, I want a fuzzed parser and sanitizer-clean CI so that I can trust robustness. | AC-QUA-01, AC-QUA-02 |
| US-QUA-04 | As P3, I want a DESIGN document explaining each major decision and trade-off so that I can learn from it. | AC-DOC-01 |
| US-QUA-05 | As P3, I want a README quick start that works from a fresh clone so that I can build and run in minutes. | AC-DOC-02 |
| US-QUA-06 | As P4, I want a clear list of known limitations so that I can judge the scope honestly. | AC-DOC-03 |

## 9. Story priority
| Priority | Stories |
|---|---|
| Must (Tier 1) | US-NET-01..05, US-DAT-01..08, US-EXP-01..02, US-EXP-04, US-OBS-01..04, US-QUA-05 |
| Must (Tier 2) | US-NET-06, US-EXP-03, US-PER-01..05, US-QUA-01..04, US-QUA-06 |
| Optional (Tier 3) | Defined with the chosen option when selected |