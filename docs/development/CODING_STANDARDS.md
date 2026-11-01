
# KVForge — Coding Standards

Goal: code that is safe, readable and easy to review. These rules are enforced by tooling where possible (`clang-format`, `clang-tidy`, compiler warnings, sanitizers) and by review otherwise. Naming conventions are defined in `design/DESIGN_SYSTEM.md` §3.2 and are not repeated in full here.

## 1. Language and build rules
- C++17 only. No compiler extensions, no `-fno-exceptions`/`-fno-rtti` games (keep defaults).
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Werror` on GCC and Clang. A warning is a bug.
- Every file compiles on both GCC 11+ and Clang 14+.
- No new dependency without an ADR.

## 2. Formatting
- `clang-format` (Google-based style, 100 columns, 4-space indentation, `PointerAlignment: Left`). CI fails on unformatted code.
- One class per header where practical; header guards via `#pragma once`.
- Include order: matching header, C++ standard library, POSIX/Linux headers, project headers; each group alphabetized and separated by a blank line. Use `<>` for system headers and `"module/file.hpp"` for project headers.
- No `using namespace` in headers. In `.cpp` files only for narrow scopes.

## 3. Naming (summary)
| Item | Convention |
|---|---|
| Files | `snake_case.hpp/.cpp` |
| Types | `PascalCase` |
| Functions, variables | `snake_case` |
| Private members | trailing `_` |
| Constants and `constexpr` | `kPascalCase` |
| Namespaces | `kvforge::<module>` |
| Test names | `Suite.Behavior_Condition`, e.g. `Incr.Overflow_ReturnsError` |

## 4. Memory and ownership
1. Prefer value types and RAII. No naked owning `new`/`delete` outside `HashTable` (which owns `Entry` nodes) and, if added, a small arena.
2. Ownership is explicit in types: `std::unique_ptr` for owning pointers, raw pointers and references for non-owning use, never both meanings in one type.
3. No `malloc`/`free`. No `strcpy`, `strcat`, `sprintf`, `gets`, `atoi`, `atol`, `strtok`. Use `std::string`, `std::string_view`, `std::snprintf` with sizes, `std::to_chars`, `std::from_chars`.
4. Never hold a `string_view` or pointer into a connection buffer across any operation that can modify that buffer.
5. Every size computation that involves untrusted input is checked for overflow before use (`__builtin_add_overflow`, `__builtin_mul_overflow` or manual checks).
6. Reserve capacity when the size is known (`reserve`).

## 5. Error handling
| Situation | Mechanism |
|---|---|
| Invalid client input | Reply with an error; never throw, never crash |
| Protocol violation | Return `ParseResult::Error`; the connection layer replies and closes |
| System call failure | Check every return value; handle `EINTR` (retry) and `EAGAIN`/`EWOULDBLOCK` (flow control); other errors close the affected connection and log with `errno` text |
| Fatal startup errors | Log at ERROR and exit with the documented code (1) |
| Out of memory (`std::bad_alloc`) | Caught at the top of the loop, logged, exit code 2 |
| Programming errors (violated invariants) | `assert` in Debug and sanitizer builds; in Release, `KVFORGE_CHECK(cond)` logs and aborts for invariants that would corrupt data |

Rules:
- No exceptions for control flow. Exceptions are not thrown across module boundaries except `std::bad_alloc` and `std::length_error` from the standard library.
- Functions that can fail return `bool`, `std::optional<T>` or a small `Result` type; do not use out-of-band globals like `errno` for internal APIs.
- Never ignore a return value of `read`, `write`, `epoll_ctl`, `fsync`, `ftruncate`, `open`, `close` (use `[[nodiscard]]` on wrappers).

## 6. Concurrency
- The server is single-threaded. Do not introduce threads without a new ADR and an update to `SYSTEM_ARCHITECTURE.md`.
- Exception: test harnesses may use threads.
- Signal handling only via `signalfd`; no signal handlers that call non-async-signal-safe functions.

## 7. Time and clocks
- Obtain wall-clock time only in the event loop to fill `ctx.now_ms`; handlers use `ctx.now_ms`.
- Obtain monotonic time through `util/clock.hpp` for timers.
- Tests inject a fake clock through the `Clock` interface.

## 8. Logging
- Use macros/functions from `util/log.hpp`: `log_error`, `log_warn`, `log_info`, `log_debug` with `key=value` fields.
- Never log key or value contents. At debug level log only command names and sizes.
- No logging in hot paths at info level or above (per-command logging is debug only and compiled in always but guarded by a cheap level check).
- Error logs include `errno` text where relevant: `Cannot bind addr=... port=...: Address already in use`.

## 9. Functions, classes and structure
- Functions do one thing, preferably under 50 lines. Cyclomatic complexity under 10; split otherwise.
- Prefer free functions in anonymous namespaces for file-local helpers.
- Classes have a clear invariant documented in a short comment; constructors establish it.
- Rule of zero where possible; otherwise rule of five (`HashTable` explicitly deletes copy and defines move).
- Avoid inheritance except for small interfaces (`Clock`, `ReplyWriter` for tests). No deep hierarchies, no virtual calls in the per-byte parsing path.
- `const` and `constexpr` by default; `explicit` on single-argument constructors; `[[nodiscard]]` on functions whose result must be checked.
- Command handlers are small functions with the uniform signature and no shared mutable state other than `ServerContext`.

## 10. Comments and documentation
- Code explains **what**; comments explain **why** and invariants.
- Every header has a top-of-file comment describing the module's responsibility in 1–3 sentences.
- Public functions and classes get a brief doc comment when behavior is not obvious from the signature (preconditions, units, ownership, complexity).
- Reference spec sections in comments where logic follows a spec: `// DATABASE_DESIGN.md 3.4`.
- No commented-out code in commits. TODO comments must include an issue number: `// TODO(#12): ...`.

## 11. Performance rules
- Measure before optimizing; record benchmark results for any optimization pull request.
- No allocation on the dispatch fast path for common commands where avoidable (use `string_view` for arguments when safe).
- Avoid `std::map` or `std::list` in the data path; prefer contiguous containers.
- No premature SIMD or assembly.

## 12. Testing standards
- Every bug fix includes a regression test.
- Unit tests are deterministic, fast (whole unit suite under 10 seconds) and do not depend on wall-clock sleeps (use the fake clock).
- Integration tests use ephemeral ports (bind to port 0) and temporary directories; they clean up after themselves.
- Tests do not call `sleep` longer than 200 ms; prefer polling with a timeout.
- Random tests print their seed and accept a seed from an environment variable for reproduction.
- Test code follows the same formatting rules; it may be less strict about `-Wconversion` only through targeted pragmas.

## 13. Static analysis (`.clang-tidy`)
Enabled checks: `bugprone-*`, `performance-*`, `readability-*` (selected), `modernize-*` (selected), `cppcoreguidelines-init-variables`, `cppcoreguidelines-pro-type-member-init`, `misc-*` (selected). Disabled: `modernize-use-trailing-return-type`, `readability-magic-numbers` (constants are named per policy instead), `bugprone-easily-swappable-parameters`. New suppressions require a comment with the reason.

## 14. Git and review workflow
- Branches: `main` (always green and releasable), feature branches `phase-N-short-name`, fixes `fix/short-name`.
- Commit messages: Conventional Commits (`feat:`, `fix:`, `test:`, `docs:`, `refactor:`, `perf:`, `build:`, `ci:`), imperative mood, subject ≤ 72 characters, body explains why.
- Pull requests: one concern each, under about 400 changed lines where possible, description lists the acceptance criteria covered.
- Even as a solo project, open pull requests against `main` so CI runs and history is clean; squash-merge.
- Tags: `v0.1.0`, `v0.2.0` (semantic versioning). Changelog in `CHANGELOG.md` using "Keep a Changelog" headings.

## 15. Pull request checklist
- [ ] Builds with GCC and Clang, zero warnings
- [ ] `clang-format` and `clang-tidy` clean
- [ ] New or changed behavior has tests; relevant acceptance criteria IDs listed
- [ ] ASan/UBSan test run clean locally
- [ ] No new global mutable state, no new dependencies
- [ ] Specs and docs updated if behavior changed
- [ ] Benchmarks re-run if the change touches a hot path (net, parser, store, expiry, AOF)
- [ ] No secrets, no large binary files, no generated files committed

## 16. Banned and restricted constructs
| Construct | Rule |
|---|---|
| `goto` | Banned |
| Raw arrays and pointer arithmetic on untrusted data | Banned; use `std::string_view`, `std::vector`, spans with bounds checks |
| `reinterpret_cast` | Only in `socket_util` for `sockaddr` and with a comment |
| C-style casts | Banned; use `static_cast` etc. |
| Global mutable variables | Banned (a single `g_shutdown` flag is not needed because of `signalfd`) |
| Macros | Only for logging and `KVFORGE_CHECK` |
| `std::endl` | Banned (use `'\n'`) |
| `#pragma pack`, type punning | Banned |
| `volatile` for synchronization | Banned |

## 17. Example: preferred patterns
```cpp
// Parse a canonical int64 safely.
[[nodiscard]] bool parse_int64(std::string_view s, int64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    if (*first == '+') return false;                       // canonical form only
    if (s.size() > 1 && s[0] == '0') return false;         // no leading zeros
    if (s.size() >= 2 && s[0] == '-' && s[1] == '0') return false;   // "-0", "-05" are not canonical
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}
```
```cpp
// Handle a system call result explicitly.
ssize_t n = ::read(fd, buf, sizeof(buf));
if (n > 0) { /* append n bytes */ }
else if (n == 0) { close_connection(conn, CloseReason::Eof); }
else if (errno == EAGAIN || errno == EWOULDBLOCK) { /* wait for next event */ }
else if (errno != EINTR) { log_warn("read failed", {{"id", conn.id}, {"errno", std::strerror(errno)}}); close_connection(conn, CloseReason::Error); }
```