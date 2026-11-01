# KVForge — API Specification (RESP2 Command Reference)

KVForge's API is its wire protocol and command set. This document is the contract that clients, tests and the differential test harness rely on.

## 1. Transport
- TCP, default `127.0.0.1:6380`.
- Requests: RESP2 arrays of bulk strings, or inline commands.
- Replies: RESP2.
- One request produces exactly one reply, in order (pipelining supported).

## 2. RESP2 reply types
| Type | Wire format | Example |
|---|---|---|
| Simple string | `+<text>\r\n` | `+OK\r\n` |
| Error | `-<message>\r\n` | `-ERR unknown command 'FOO'\r\n` |
| Integer | `:<int64>\r\n` | `:42\r\n` |
| Bulk string | `$<len>\r\n<bytes>\r\n` | `$5\r\nhello\r\n` |
| Null bulk | `$-1\r\n` | |
| Array | `*<n>\r\n<elements>` | `*2\r\n$1\r\na\r\n$-1\r\n` |
| Empty array | `*0\r\n` | |

## 3. Command naming and arguments
- Command names are case-insensitive. Option keywords (`EX`, `NX`) are case-insensitive.
- Keys and values are binary-safe.
- Integers in arguments are parsed as signed 64-bit decimal; anything else is `-ERR value is not an integer or out of range`.
- In the tables below, arity counts the command name (Redis convention): `GET key` has arity 2; `DEL key [key ...]` has arity -2 (at least 2).

## 4. Error messages
| Situation | Reply |
|---|---|
| Unknown command | `-ERR unknown command '<name>'` |
| Wrong argument count | `-ERR wrong number of arguments for '<name>' command` (name lowercase) |
| Syntax problem in options | `-ERR syntax error` |
| Non-integer where integer required | `-ERR value is not an integer or out of range` |
| Integer overflow in INCR family | `-ERR increment or decrement would overflow` |
| Invalid expire time | `-ERR invalid expire time in '<name>' command` (name lowercase) |
| Protocol violation | `-ERR Protocol error: <reason>` (connection then closed) |
| Too many clients | `-ERR max number of clients reached` (connection then closed) |
| Unsupported `CONFIG`/`CLIENT` subcommand | `-ERR unknown subcommand '<sub>'` |

Differential tests compare error replies by their prefix up to the first quote or colon where Redis includes extra context (for example Redis 7 appends `, with args beginning with: ...` to unknown-command errors; KVForge does not).

## 5. Commands

### 5.1 Connection and diagnostics
| Command | Arity | Reply | Complexity | AOF |
|---|---|---|---|---|
| `PING` | 1 | `+PONG` | O(1) | no |
| `PING message` | 2 | bulk `message` | O(1) | no |
| `ECHO message` | 2 | bulk `message` | O(1) | no |
| `COMMAND` | ≥1 | `*0` (empty array). `COMMAND COUNT` returns the integer count of supported commands. `COMMAND DOCS ...` returns `*0`. | O(1) | no |
| `CONFIG GET parameter` | 3 | Array `[name, value]` for supported parameters, otherwise `*0`. Supported: `port`, `bind`, `appendonly`, `appendfsync`, `maxclients`, `save` (always empty string). Glob patterns are not supported (exact names only). | O(1) | no |
| `CLIENT SETNAME name` / `CLIENT SETINFO attr value` | ≥3 | `+OK` (accepted and ignored for client-library compatibility) | O(1) | no |
| `INFO [section]` | 1–2 | bulk string, see §6 | O(1) | no |

### 5.2 Strings
| Command | Arity | Reply | Complexity | AOF record |
|---|---|---|---|---|
| `SET key value [EX s \| PX ms \| PXAT ms] [NX \| XX]` | ≥3 | `+OK`, or null bulk if NX/XX condition fails | O(1) | `SET key value [PXAT abs]` |
| `GET key` | 2 | bulk or null bulk | O(1) | no |
| `MSET key value [key value ...]` | ≥3, odd count | `+OK` | O(N) | `MSET ...` |
| `MGET key [key ...]` | ≥2 | array of bulk or null bulk | O(N) | no |
| `INCR key` / `DECR key` | 2 | integer (new value) | O(1) | as received |
| `INCRBY key n` / `DECRBY key n` | 3 | integer (new value) | O(1) | as received |

`SET` rules:
- Options may appear in any order after `value`. `EX`, `PX`, `PXAT` are mutually exclusive; `NX` and `XX` are mutually exclusive; violations return `-ERR syntax error`.
- `EX`/`PX` require a positive integer; zero or negative returns `-ERR invalid expire time in 'set' command`. `EX` seconds overflowing `int64` milliseconds also returns that error.
- `PXAT` accepts an absolute epoch millisecond value; if it is `<= now`, the key is not created (or is deleted if it existed) and the reply is `+OK`.
- Overwriting a key removes any previous TTL unless a new expiry option is given.
- `INCR` family: missing key is treated as `0`; result stored as a decimal string; existing TTL is preserved; overflow or underflow returns the overflow error and leaves the value unchanged. A value that is not a canonical int64 string returns the not-an-integer error.

### 5.3 Keys and expiry
| Command | Arity | Reply | Complexity | AOF record |
|---|---|---|---|---|
| `DEL key [key ...]` | ≥2 | integer: number of keys removed | O(N) | `DEL <existing keys>` |
| `EXISTS key [key ...]` | ≥2 | integer: count (repeated keys counted repeatedly) | O(N) | no |
| `EXPIRE key seconds` | 3 | `:1` if TTL set, `:0` if key missing | O(1) | `PEXPIREAT key abs` |
| `PEXPIRE key ms` | 3 | same, milliseconds | O(1) | `PEXPIREAT key abs` |
| `PEXPIREAT key ms-epoch` | 3 | same, absolute (used by AOF; also available to clients) | O(1) | as received |
| `TTL key` | 2 | integer seconds (rounded up), `-1` no TTL, `-2` missing | O(1) | no |
| `PTTL key` | 2 | integer milliseconds, `-1`, `-2` | O(1) | no |
| `PERSIST key` | 2 | `:1` if a TTL was removed, `:0` otherwise | O(1) | `PERSIST key` (only when `:1`) |
| `KEYS pattern` (Tier 2) | 2 | array of matching keys; order unspecified | O(N) blocking | no |
| `DBSIZE` | 1 | integer | O(1) | no |
| `FLUSHALL` | 1 | `+OK` | O(N) | `FLUSHALL` |

Expiry rules:
- `EXPIRE`/`PEXPIRE` with a zero or negative value deletes the key immediately and returns `:1` if the key existed (matches Redis). AOF writes `DEL key`.
- `EXPIRE` with a value whose absolute time overflows `int64` returns `-ERR invalid expire time in 'expire' command`.
- `KEYS` glob subset: `*`, `?`, `[abc]`, `[a-z]`, `[^a]`, and `\` escapes.

## 6. INFO output
Reply is a bulk string containing sections separated by blank lines; each line is `key:value` with `\r\n` endings. `INFO` with no argument returns all sections; `INFO server`, `INFO clients`, `INFO stats`, `INFO memory`, `INFO persistence`, `INFO keyspace` return one section; unknown sections return an empty bulk string.

```
# Server
kvforge_version:0.2.0
os:Linux 5.15.0 x86_64
process_id:12345
tcp_port:6380
uptime_in_seconds:3600
uptime_in_days:0

# Clients
connected_clients:3
maxclients:10000

# Memory
used_memory:1048576
used_memory_human:1.00M

# Persistence
aof_enabled:1
aof_current_size:204800
aof_fsync_policy:everysec

# Stats
total_connections_received:42
total_commands_processed:1000000
keyspace_hits:600000
keyspace_misses:400000
expired_keys:1500
rehashing:0

# Keyspace
db0:keys=100000,expires=2500
```
`redis_version:` is intentionally **not** emitted, to avoid misleading clients into assuming full Redis feature support; clients that require it (rare) can be addressed by a documented flag in a later version.

## 7. Inline command syntax
```
PING\r\n
SET greeting "hello world"\r\n
```
Rules: split on whitespace; double quotes allow spaces and the escapes `\n \r \t \\ \" \xHH`; single quotes allow spaces and `\'`. An unbalanced quote returns `-ERR Protocol error: unbalanced quotes in request` and closes the connection. Maximum line length 64 KiB.

## 8. Pipelining and ordering
Replies are written in the same order as requests per connection. Commands from different connections are executed in the order the event loop reads them; there is no cross-connection ordering guarantee beyond that.

## 9. Atomicity
Each command executes atomically with respect to all other commands (single-threaded execution). `MSET` applies all pairs before any other command runs. There is no multi-command transaction.

## 10. Command-to-requirement map
| Commands | Requirements |
|---|---|
| `PING`, `ECHO` | FR-CMD-01 |
| `SET`, `GET` | FR-CMD-02, FR-CMD-03 |
| `DEL`, `EXISTS` | FR-CMD-04 |
| `INCR` family | FR-CMD-05 |
| `MGET`, `MSET` | FR-CMD-06 |
| `EXPIRE`, `PEXPIRE`, `TTL`, `PTTL`, `PERSIST` | FR-CMD-07 |
| `DBSIZE`, `FLUSHALL` | FR-CMD-08 |
| `COMMAND`, `CONFIG GET`, `INFO`, `CLIENT SETNAME/SETINFO` | FR-CMD-09 |
| `KEYS` | FR-CMD-10 |
| `PEXPIREAT`, `SET ... PXAT` | Internal and AOF support (FR-PER-02) |

`CLIENT SETNAME/SETINFO` and `PEXPIREAT` are small additions beyond the PRD command list; they exist for client-library compatibility and AOF replay and need no extra scope approval beyond this note.

## 11. Unsupported commands behavior
Any command not listed returns `-ERR unknown command '<name>'`. This includes `HELLO` (RESP3 negotiation), `SELECT`, `AUTH`, `MULTI`, `SUBSCRIBE`, `SCAN` (unless Tier 3 option D is built), list/set/hash/sorted-set commands (unless Tier 3 option A is built).

## 12. Tier 3 command reference (only for the chosen option)
| Option | Commands | Notes |
|---|---|---|
| A. Sorted sets | `ZADD key score member [...]`, `ZREM`, `ZSCORE`, `ZRANK`, `ZRANGE key start stop [WITHSCORES]`, `ZCARD` | Skip list; type mismatch returns `-WRONGTYPE Operation against a key holding the wrong kind of value` |
| B. LRU | `CONFIG GET maxmemory`; flag `--maxmemory`, `--maxmemory-policy allkeys-lru` | Approximate LRU by sampling 5 keys; `-OOM command not allowed when used memory > 'maxmemory'` when policy is `noeviction` |
| C. AOF rewrite | `BGREWRITEAOF` | Replies `+Background append only file rewriting started` |
| D. SCAN | `SCAN cursor [MATCH pattern] [COUNT n]` | Reverse-binary cursor over the hash table; safe during rehash |