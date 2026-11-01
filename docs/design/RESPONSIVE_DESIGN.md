# KVForge — Responsive Design

For KVForge, "responsive" means: output and documentation stay usable across terminal widths, interactive versus piped use, different client types, and different rendering environments (GitHub desktop and mobile, editors, plain text). There are no screen breakpoints.

## 1. Environments covered
| Environment | Examples | Key concern |
|---|---|---|
| Narrow terminal | 80 columns, split panes, SSH on a phone | Line length and wrapping |
| Wide terminal | 200+ columns | Avoid useless padding |
| Non-interactive output | `kvforge > log.txt`, journald, CI logs | No color, no cursor control |
| Different clients | `redis-cli`, `nc`/`telnet`, client libraries, benchmark tools | Protocol tolerance |
| Rendered docs | GitHub web, GitHub mobile, VS Code preview, plain `cat` | Tables, diagrams, code blocks |

## 2. Terminal width rules
| Rule | Detail |
|---|---|
| Program output is never hard-wrapped | Logs are single lines regardless of width, so `grep`, `awk` and log shippers work. The terminal or pager wraps them. |
| `--help` text | Every line is at most 80 columns (checked by a test). |
| Error messages | Single line; key facts as `key=value` at the end so truncated terminal views still show the cause first. |
| Reports in Markdown | Tables are used; the first column is the shortest, most identifying label. |
| No box-drawing or alignment tricks in program output | They break when pasted into issues and chats. |

## 3. Interactive versus non-interactive
| Condition | Behavior |
|---|---|
| stderr is a TTY and `NO_COLOR` unset | Level names may be colored (ERROR red, WARN yellow) |
| stderr is not a TTY, or `NO_COLOR` set | Plain text, no escape codes |
| `TERM=dumb` | Plain text |
| Output redirected to a file | Same plain text format; timestamps always included |
| Under systemd/journald | Timestamps remain in the line (journald adds its own; duplicates are acceptable and not suppressed) |
| Reading from stdin | Not used; the program never prompts interactively |

KVForge never emits cursor movement, progress bars or spinners.

## 4. Client types and protocol tolerance
| Client | Mode | What KVForge guarantees |
|---|---|---|
| `redis-cli` (interactive) | RESP2 arrays | Starts cleanly (`COMMAND`, `COMMAND DOCS` answered with `*0`); prompt works |
| `redis-cli` with command arguments | RESP2 | One-shot commands print the expected result |
| `redis-benchmark` | RESP2 pipelined | `CONFIG GET` probes answered; pipelining (`-P`) correct |
| `nc` / `telnet` | Inline commands | Plain text commands terminated by `\r\n` or `\n` accepted; quoted strings supported |
| Client libraries (redis-py, node-redis, jedis, go-redis) | RESP2 | Default RESP2 mode works; `CLIENT SETNAME/SETINFO` accepted; RESP3/`HELLO` falls back with an unknown-command error |
| Scripts piping raw RESP | RESP2 | Same as above; partial writes and split packets handled |

Line endings: inline commands accept both `\r\n` and `\n`; RESP framing requires `\r\n`.

## 5. Output size behavior
| Surface | Large-scale behavior |
|---|---|
| `KEYS *` on a large keyspace | Documented as blocking; reply built in the connection's output buffer with the output-limit check. A reply that would exceed the per-client output limit closes that client with a warning instead of exhausting memory. |
| `INFO` | Constant size (no per-key listing) |
| Logs | Rate-limited for repeated identical warnings: after 10 identical WARN messages within 10 seconds, further duplicates are suppressed and a summary line `Suppressed <n> similar messages` is emitted every 10 seconds |
| Benchmark reports | Fixed number of rows; raw data lives in separate files |

## 6. Documentation rendering
### 6.1 Markdown compatibility
- Use GitHub-flavored Markdown only (tables, fenced code, task lists).
- Mermaid diagrams must also be understandable from the surrounding text because some viewers do not render Mermaid.
- Tables: no more than 6 columns in documents expected to be read on mobile; wide tables (benchmarks) are allowed but the key columns come first.
- Code blocks: lines limited to 100 characters so GitHub mobile and diffs do not require horizontal scrolling for most content.
- Avoid HTML in Markdown except `<details>` for long logs and raw outputs.

### 6.2 Images and badges
- No essential information is only inside an image. Any screenshot or diagram image has alt text and a text equivalent.
- README badges (CI, license) are decorative; the README reads correctly without them.

### 6.3 Accessibility in documents
- Heading levels never skip (H1 → H2 → H3).
- Link text describes the destination ("see the architecture decisions"), not "click here".
- Tables have header rows and are not used for layout.

## 7. Internationalization considerations
- Program messages are English only.
- Keys and values are bytes and may contain any encoding (UTF-8 or binary). `redis-cli` displays non-printable bytes as escapes; KVForge never transforms them.
- Case-insensitive command and option matching is ASCII-only.
- Log timestamps are UTC to avoid locale and timezone ambiguity.

## 8. Resource-adaptive behavior (the server "responding" to its environment)
| Condition | Behavior |
|---|---|
| Low file-descriptor limit | `max_clients` lowered automatically with a WARN (see SECURITY_ARCHITECTURE §3.1) |
| Many idle clients | `epoll_wait` timeout stays at most 100 ms; idle CPU stays below 1% (AC-EXP-07) |
| Burst of pipelined commands from one client | Per-iteration command cap (1,000) keeps other clients responsive |
| Large buffers after a burst | Buffers shrink after emptying (above 64 KiB) to return memory |
| Heavy expiry backlog | Active expiry limited by the time budget so command latency stays bounded |

## 9. Acceptance checks
| Check | Verified by |
|---|---|
| `--help` lines ≤ 80 columns | Unit/golden test |
| No ANSI escape codes when stderr is not a TTY | Integration test with piped stderr |
| `NO_COLOR=1` disables color on a TTY | Manual check recorded in the QA checklist |
| `nc localhost 6380` accepts `PING` and quoted `SET` | Integration test (AC-PRO-04) |
| `redis-cli` and `redis-benchmark` run cleanly | AC-SRV-03, AC-BEN-01 |
| README renders correctly on GitHub desktop and mobile; Mermaid blocks have text equivalents | Manual check in the QA checklist |