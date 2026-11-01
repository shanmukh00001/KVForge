# KVForge — Design System

KVForge has no graphical interface. Its "user interface" consists of the command-line interface, log output, `INFO` output, error messages, benchmark reports and written documentation. This design system defines consistent conventions for all of them so the product feels coherent and professional.

## 1. Principles
1. **Plain and predictable.** Output is plain text, line-oriented, and easy to grep and parse.
2. **Machine-readable first, human-friendly second.** Logs and `INFO` use stable `key:value` or fixed-column formats.
3. **Honest.** Messages state what happened and what to do; no vague errors, no marketing language.
4. **Consistent vocabulary.** The same term always means the same thing (see §2).
5. **No color dependence.** Color is optional decoration and never carries meaning alone.

## 2. Vocabulary (use these terms exactly)
| Term | Meaning | Do not use |
|---|---|---|
| key | A name in the keyspace | "name", "id" |
| value | The string stored under a key | "data" (ambiguous) |
| TTL | Remaining time before a key expires | "lifetime" |
| expiry / deadline | Absolute time at which a key expires | "timeout" (reserved for network timeouts) |
| client / connection | A TCP peer / its server-side state | "user" |
| AOF | The append-only file | "log file" (reserved for diagnostic logs) |
| fsync policy | `always`, `everysec`, `no` | "sync mode" |
| rehash | Incremental hash table resize | "rebuild" |
| command | One RESP request | "query" |
| log | Diagnostic text on stderr | "output" (ambiguous) |

## 3. Naming conventions
### 3.1 Command-line flags
- Lowercase, hyphen-separated: `--max-clients`, `--appendfsync` (the persistence flags keep the Redis spellings `--appendonly`, `--appendfsync`, `--appendfilename` for familiarity; all KVForge-specific flags use hyphens).
- Boolean flags take explicit values: `--appendonly yes|no`.
- Every flag has a default, documented in `--help` and in the PRD config table.

### 3.2 Code
| Item | Convention | Example |
|---|---|---|
| Files | `snake_case.hpp/.cpp` | `hash_table.cpp` |
| Types | `PascalCase` | `HashTable`, `ServerContext` |
| Functions and variables | `snake_case` | `rehash_step`, `now_ms` |
| Member variables | trailing underscore | `rehash_idx_` |
| Constants | `kPascalCase` | `kMaxBulkLen` |
| Macros | Avoided; if needed `KVFORGE_UPPER_CASE` | `KVFORGE_LOG` |
| Namespaces | `kvforge::<module>` | `kvforge::store` |

### 3.3 Metrics and INFO fields
- Lowercase `snake_case`, Redis-compatible names where the meaning matches (`connected_clients`, `keyspace_hits`).
- Units in the name when not obvious: `uptime_in_seconds`, `used_memory` (bytes), `aof_current_size` (bytes).

## 4. Output formats
### 4.1 Diagnostic log line
```
<timestamp> <pid> <level> <message>
```
- Timestamp: UTC, ISO-8601 with milliseconds, e.g. `2026-10-07T09:15:42.123Z`.
- Level: `ERROR`, `WARN `, `INFO `, `DEBUG` (fixed width 5).
- Message: sentence case, no trailing period, key facts as `key=value` pairs at the end.
- Example: `2026-10-07T09:15:42.123Z 4242 INFO  Ready to accept connections addr=127.0.0.1 port=6380`

### 4.2 Error replies to clients
- Prefix `ERR` for general errors; use Redis-compatible text where a compatible error exists.
- Start with a capital letter, no trailing period, quote the offending token with single quotes: `unknown command 'FOO'`.
- Never include stack traces, file paths or internal addresses.

### 4.3 Benchmark reports
- Markdown tables, one row per scenario, fixed columns (see `UI_SPECIFICATION.md` §7).
- Always include: machine, kernel, compiler, flags, Redis version, persistence settings, command line, number of runs, and a link to raw output.

## 5. Documentation style
| Element | Rule |
|---|---|
| Voice | Direct, technical, active ("The server closes the connection") |
| Tense | Present tense for behavior; past tense only for history |
| Headings | Sentence case, no more than three heading levels per page |
| Code | Fenced blocks with language tags (`cpp`, `bash`, `text`, `mermaid`) |
| Wire examples | Show `\r\n` explicitly in prose; show line-per-token form in RESP blocks and state it |
| Numbers | Use binary units for sizes (KiB, MiB); state units for every number |
| Claims | Every performance or compatibility claim links to a test or raw benchmark file |
| Limitations | Stated plainly in the README; never hidden in deep docs |
| Diagrams | Mermaid in Markdown for flows and sequences; ASCII only for trees and layouts |

## 6. Diagram style
- Flowcharts: top-to-bottom, decisions as diamonds with explicit Yes/No labels.
- Sequence diagrams: participants named with the vocabulary above.
- One idea per diagram; no more than about 12 nodes.

## 7. Terminal output conventions
- Respect `NO_COLOR` and non-TTY output: no color codes unless stderr is a TTY **and** `NO_COLOR` is unset.
- When color is enabled: `ERROR` red, `WARN` yellow, others default. The level text is always present, so meaning never depends on color.
- Lines are not wrapped by the program; they are single lines (see `RESPONSIVE_DESIGN.md`).
- Exit codes are part of the interface:

| Code | Meaning |
|---|---|
| 0 | Clean shutdown or `--help`/`--version` |
| 1 | Configuration error, bind failure, corrupt AOF |
| 2 | Fatal runtime error (for example out of memory) |

## 8. Accessibility
- All documentation is plain Markdown readable by screen readers; diagrams have a text description in the surrounding paragraph.
- Tables are used for comparisons only and have header rows.
- No information is conveyed by color alone in docs or terminal output.

## 9. Brand
- Name: **KVForge** (one word, capital K, V, F).
- Tagline: "A Redis-compatible key-value server, built from scratch in C++."
- Always say "Redis-compatible subset" when describing compatibility. The project is not affiliated with or endorsed by Redis Ltd.; the README includes this sentence.