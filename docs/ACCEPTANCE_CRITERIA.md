# KVForge — Acceptance Criteria

Format: Given / When / Then. Wire replies are shown in RESP2 notation (`\r\n` omitted for readability). Where Redis 7.x behavior is the reference, differential tests must agree on the reply type and value. Error message text must match for the listed errors.

Tier markers: **[T1]** Tier 1, **[T2]** Tier 2.

## 1. Basic commands
| ID | Criterion |
|---|---|
| AC-PING-01 [T1] | Given a connected client, when `PING` is sent, then the reply is `+PONG`. |
| AC-PING-02 [T1] | When `PING hello` is sent, then the reply is bulk string `$5 hello`. |
| AC-PING-03 [T1] | When `ECHO hi` is sent, then the reply is bulk string `$2 hi`. |
| AC-PING-04 [T1] | When `ping` (lowercase) is sent, then the reply is `+PONG`. |

## 2. SET and GET
| ID | Criterion |
|---|---|
| AC-SET-01 [T1] | When `SET k v` is sent, then the reply is `+OK` and `GET k` returns bulk `v`. |
| AC-GET-01 [T1] | When `GET missing` is sent for a key that does not exist, then the reply is null bulk `$-1`. |
| AC-SET-02 [T1] | When `SET k v2` is sent for an existing key, then the value is overwritten and any previous TTL is removed. |
| AC-SET-03 [T1] | Given `k` exists, when `SET k v NX` is sent, then the reply is null bulk and the value is unchanged. Given `k` does not exist, the reply is `+OK` and the key is created. |
| AC-SET-04 [T1] | Given `k` does not exist, when `SET k v XX` is sent, then the reply is null bulk and no key is created. Given `k` exists, the reply is `+OK`. |
| AC-SET-05 [T1] | When `SET k v EX 10` is sent, then `TTL k` returns 10 or 9 immediately after, and `PTTL k` returns a value between 9000 and 10000. |
| AC-SET-06 [T1] | When `SET k v PX 1500` is sent, then `PTTL k` returns a value between 1 and 1500. |
| AC-SET-07 [T1] | When `SET k v EX 0` or `SET k v EX -5` is sent, then the reply is `-ERR invalid expire time in 'set' command` and the key is not modified. |
| AC-SET-08 [T1] | When `SET k v NX XX` is sent, then the reply is `-ERR syntax error`. |
| AC-SET-09 [T1] | When `SET k` is sent, then the reply is `-ERR wrong number of arguments for 'set' command`. |

## 3. DEL and EXISTS
| ID | Criterion |
|---|---|
| AC-DEL-01 [T1] | Given `a` and `b` exist and `c` does not, when `DEL a b c` is sent, then the reply is `:2` and `a`, `b` no longer exist. |
| AC-EXISTS-01 [T1] | Given `a` exists, when `EXISTS a a missing` is sent, then the reply is `:2` (a key named twice counts twice). |

## 4. Counters
| ID | Criterion |
|---|---|
| AC-INCR-01 [T1] | Given `n` does not exist, when `INCR n` is sent, then the reply is `:1` and `GET n` returns `1`. |
| AC-INCR-02 [T1] | Given `n` is `10`, when `INCRBY n 5` is sent, then the reply is `:15`; `DECR n` then returns `:14`; `DECRBY n 4` returns `:10`. |
| AC-INCR-03 [T1] | Given `s` is `abc`, when `INCR s` is sent, then the reply is `-ERR value is not an integer or out of range` and the value is unchanged. |
| AC-INCR-04 [T1] | Given `n` is `9223372036854775807`, when `INCR n` is sent, then the reply is `-ERR increment or decrement would overflow` and the value is unchanged. |
| AC-INCR-05 [T1] | Given `n` has a TTL, when `INCR n` is sent, then the TTL is preserved. |
| AC-INCR-06 [T1] | When `INCRBY n abc` is sent, then the reply is `-ERR value is not an integer or out of range`. |

## 5. MGET and MSET
| ID | Criterion |
|---|---|
| AC-MULTI-01 [T1] | When `MSET a 1 b 2` is sent, then the reply is `+OK` and both keys are set. |
| AC-MULTI-02 [T1] | Given `a` is `1` and `c` does not exist, when `MGET a c` is sent, then the reply is an array of 2 elements: bulk `1` and null bulk. |
| AC-MULTI-03 [T1] | When `MSET a 1 b` (odd argument count) is sent, then the reply is `-ERR wrong number of arguments for 'mset' command`. |

## 6. Expiry
| ID | Criterion |
|---|---|
| AC-EXP-01 [T1] | Given `k` was set with `PX 100`, when 150 ms have elapsed and `GET k` is sent, then the reply is null bulk and `EXISTS k` returns `:0`. |
| AC-EXP-02 [T1] | `TTL` returns `-2` for a missing key, `-1` for a key without expiry, and the remaining whole seconds otherwise. `PTTL` follows the same rule in milliseconds. |
| AC-EXP-03 [T1] | `EXPIRE k 30` returns `:1` when `k` exists and `:0` when it does not. `PEXPIRE` behaves the same in milliseconds. |
| AC-EXP-04 [T1] | `PERSIST k` returns `:1` when a TTL was removed and `:0` when the key has no TTL or does not exist. |
| AC-EXP-05 [T1] | Given 100,000 keys each with `PX 200` and no further access, when 2 seconds have elapsed, then `DBSIZE` returns `:0` (active expiry reclaimed them) and `INFO` shows `expired_keys` of at least 100000. |
| AC-EXP-06 [T1] | Given `k` was set with `EX 100`, when `EXPIRE k 1` changes its TTL and 2 seconds elapse, then `GET k` returns null bulk (stale heap entry did not keep the old deadline). |
| AC-EXP-07 [T1] | An idle server (no clients) shall use less than 1% CPU, showing the `epoll_wait` timeout is working. |

## 7. Server commands
| ID | Criterion |
|---|---|
| AC-SRV-01 [T1] | `DBSIZE` returns the number of non-expired keys as an integer. |
| AC-SRV-02 [T1] | `FLUSHALL` returns `+OK` and `DBSIZE` then returns `:0`. |
| AC-SRV-03 [T1] | `redis-cli -p 6380` starts and shows a prompt without errors (`COMMAND` and `CONFIG GET` respond). |
| AC-SRV-04 [T2] | `KEYS user:*` returns an array of all matching keys; `KEYS *` returns all keys; keys that have expired are not returned. |

## 8. Protocol
| ID | Criterion |
|---|---|
| AC-PRO-01 [T1] | A command sent one byte at a time (with delays) produces exactly one correct reply after the final byte and none before. |
| AC-PRO-02 [T1] | A command split across two TCP segments at every possible byte boundary produces the same reply as when sent whole. |
| AC-PRO-03 [T1] | 10,000 commands sent in a single write produce 10,000 replies in the same order. |
| AC-PRO-04 [T1] | Via `nc`, the text `PING\r\n` returns `+PONG`. |
| AC-PRO-05 [T1] | An unknown command `FOO` returns an error beginning `-ERR unknown command 'FOO'`. |
| AC-PRO-06 [T1] | `GET` with no key returns `-ERR wrong number of arguments for 'get' command`. |
| AC-PRO-07 [T1] | Sending `*abc\r\n` returns `-ERR Protocol error: ...` and the server closes that connection while other connections continue to work. |
| AC-PRO-08 [T1] | A value containing `\0`, `\r\n` and bytes above 127 is stored and returned byte-for-byte identical. |
| AC-PRO-09 [T1] | A client that disconnects in the middle of a command does not leak its buffers and does not affect other clients (checked by ASan/leak sanitizer and `connected_clients`). |

## 9. Limits
| ID | Criterion |
|---|---|
| AC-LIM-01 [T1] | A bulk string declared with length above 64 MiB gets `-ERR Protocol error: invalid bulk length` and the connection closes. |
| AC-LIM-02 [T1] | A command array with more than 1,048,576 elements gets `-ERR Protocol error: invalid multibulk length` and the connection closes. |
| AC-LIM-03 [T1] | When `max_clients` connections are open, the next connection receives `-ERR max number of clients reached` and is closed; existing clients are unaffected. |
| AC-LIM-04 [T1] | A client that never reads its replies and causes its output buffer to exceed 64 MiB is disconnected; server memory returns to baseline and other clients continue normally. |

## 10. Hash table and rehashing
| ID | Criterion |
|---|---|
| AC-HT-01 [T2] | Inserting 2,000,000 distinct keys then reading all of them returns all values correctly. |
| AC-HT-02 [T2] | During an in-progress rehash, `GET`, `SET`, `DEL` and `EXISTS` for keys in both tables return correct results (unit test forces and pauses rehash). |
| AC-HT-03 [T2] | At 1,000,000 keys, the maximum latency of any single `SET` that triggers a resize is at most 10 ms on the reference benchmark machine. |
| AC-HT-04 [T2] | `INFO` reports `rehashing:1` while a rehash is active and `rehashing:0` after it completes. |

## 11. Persistence (AOF)
| ID | Criterion |
|---|---|
| AC-AOF-01 [T2] | With `--appendonly yes`, after `SET a 1`, `INCR n`, `DEL a` and a clean restart, `GET a` returns null bulk and `GET n` returns `1`. |
| AC-AOF-02 [T2] | Read-only commands (`GET`, `TTL`, `PING`) are not written to the AOF. Failed writes (for example `INCR` on a non-integer) are not written. |
| AC-AOF-03 [T2] | Given `SET k v EX 100` and a restart 30 seconds later, `TTL k` returns between 69 and 70, not 100. A key whose deadline passed while the server was down is absent after restart. |
| AC-AOF-04 [T2] | With `appendfsync always`, a test that sends 10,000 `SET` commands and `kill -9`s the server after the last reply recovers all 10,000 keys. With `everysec`, recovery loses at most the writes from the last second. |
| AC-AOF-05 [T2] | If the AOF final record is cut at any byte offset, the server starts, truncates to the last complete record, logs a warning, and all earlier records are present. |
| AC-AOF-06 [T2] | If bytes in the middle of the AOF are corrupted, the server exits with a non-zero code and logs the byte offset. |
| AC-AOF-07 [T2] | Active expiry of a key appends a `DEL` record so that replay does not resurrect it. |

## 12. Observability and configuration
| ID | Criterion |
|---|---|
| AC-INFO-01 [T1/T2] | `INFO` returns a bulk string of `key:value` lines containing at least: `uptime_in_seconds`, `connected_clients`, `total_connections_received`, `total_commands_processed`, `keyspace_hits`, `keyspace_misses`, `expired_keys`, `used_memory`, `aof_enabled`, `aof_current_size`, `rehashing`. |
| AC-INFO-02 [T1] | After 3 successful `GET`s and 2 `GET`s of missing keys, `keyspace_hits` increases by 3 and `keyspace_misses` by 2. |
| AC-OBS-01 [T1] | `--loglevel debug` prints per-connection events; `--loglevel error` prints only errors. |
| AC-CFG-01 [T1] | `--port 7000` binds port 7000; a config file value is overridden by a command-line flag; an invalid value (for example `--port abc`) prints an error and exits with code 1. |
| AC-NET-01 [T1] | Starting on a port already in use logs an error and exits with code 1. |
| AC-NET-02 [T1] | The server serves at least 1,000 simultaneous idle connections without error. |
| AC-NET-03 [T2] | `SIGTERM` causes exit code 0 after flushing the AOF; no acknowledged write is lost under `always`. |

## 13. Quality and evidence
| ID | Criterion |
|---|---|
| AC-DIFF-01 [T2] | A generator produces at least 10,000 random sequences of deterministic commands (SET/GET/DEL/EXISTS/INCR family/MSET/MGET/EXPIRE-with-large-TTL/TTL-range-compared); running the same sequence against Redis 7.x and KVForge gives identical reply types and values for every command (TTL values compared with a tolerance of 1 second). |
| AC-QUA-01 [T2] | The CI sanitizer job (ASan + UBSan) runs all unit and integration tests with zero findings. |
| AC-QUA-02 [T2] | A libFuzzer harness for the RESP parser runs for at least 1 hour without crash, hang or sanitizer finding before each tagged release. |
| AC-QUA-03 [T2] | Unit tests cover the parser, hash table (including mid-rehash), TTL logic, integer overflow and AOF replay; line coverage for these modules is at least 85%. |
| AC-BEN-01 [T2] | `bench/` contains scripts that run `redis-benchmark` against KVForge and real Redis on the same machine with equal persistence settings, for `SET` and `GET`, at `-P 1` and `-P 16`, with `-c 50` and at least 1,000,000 requests. |
| AC-BEN-02 [T2] | The README reports the median of at least 5 runs, with throughput, p50 and p99 latency, and RSS memory at 1,000,000 keys for both servers, and states the machine specification. |
| AC-BEN-03 [T2] | If Redis is faster in any scenario, the results show it and the design document explains the likely reasons. |

## 14. Documentation
| ID | Criterion |
|---|---|
| AC-DOC-01 [T2] | `docs/` contains design decisions covering: level-triggered `epoll`, single-threaded execution, incremental rehashing, expiry design, absolute timestamps in the AOF, fsync policies, and resource limits, each with trade-offs. |
| AC-DOC-02 [T1] | From a fresh clone on Ubuntu 22.04, following only the README commands builds the project and runs `redis-cli -p 6380 PING` returning `PONG`. |
| AC-DOC-03 [T2] | The README has a "Known limitations" section naming at least: subset of commands, no replication, no auth/TLS, single-threaded blocking commands such as `KEYS`, approximate memory accounting, Linux only. |
| AC-DOC-04 [T2] | Every performance number in the README links to the raw output file that produced it. |

## 15. Traceability
| Area | PRD requirement | Acceptance criteria |
|---|---|---|
| Networking | FR-NET-01..07 | AC-NET-01..03, AC-LIM-03, AC-LIM-04 |
| Protocol | FR-PRO-01..07 | AC-PRO-01..09 |
| Commands | FR-CMD-01..11 | AC-PING, AC-SET, AC-GET, AC-DEL, AC-EXISTS, AC-INCR, AC-MULTI, AC-SRV |
| Storage | FR-STO-01..03 | AC-HT-01..04 |
| Expiry | FR-EXP-01..05 | AC-EXP-01..07 |
| Persistence | FR-PER-01..06 | AC-AOF-01..07 |
| Observability | FR-OBS-01..02 | AC-INFO-01..02, AC-OBS-01 |
| Quality | PRD §5 | AC-DIFF, AC-QUA, AC-BEN, AC-DOC |