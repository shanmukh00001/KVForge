# KVForge — Tech Stack

## 1. Runtime and language
| Item | Choice | Notes |
|---|---|---|
| Language | C++17 | `-std=c++17`, no extensions required |
| Platform | Linux (kernel 4.x or newer) | Needs `epoll`, `signalfd`, `getrandom` (glibc 2.25+) |
| Compilers | GCC 11+ and Clang 14+ | CI builds with both |
| Standard library | libstdc++ or libc++ | No Boost |
| Runtime dependencies | None beyond libc and libstdc++ | Statically linkable |
| Build system | CMake 3.16+ | Out-of-source builds in `build/` |

Developer on Windows: use WSL2 with Ubuntu 22.04. Benchmark numbers must be taken on native Linux or an Azure Linux VM and not from WSL2.

## 2. Linux system calls used
| Purpose | Calls |
|---|---|
| Sockets | `socket`, `setsockopt` (`SO_REUSEADDR`, `TCP_NODELAY`), `bind`, `listen`, `accept4` (with `SOCK_NONBLOCK`), `read`, `write`, `close` |
| Event loop | `epoll_create1`, `epoll_ctl`, `epoll_wait` |
| Signals | `sigprocmask`, `signalfd` |
| Files / AOF | `open` (`O_WRONLY|O_APPEND|O_CREAT`, mode `0600`), `write`, `fsync`, `ftruncate`, `fstat` |
| Time | `clock_gettime` (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`) |
| Randomness | `getrandom` (hash seed) |
| Tier 3 option C only | `fork`, `waitpid` |

## 3. Build configurations
| Config | Flags | Use |
|---|---|---|
| Release | `-O2 -DNDEBUG -fno-plt` plus `-Wall -Wextra -Wpedantic -Werror` | Benchmarks, releases |
| Debug | `-O0 -g` plus warnings | Development, gdb |
| ASan+UBSan | `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` | CI test job |
| Fuzz | Clang, `-O1 -g -fsanitize=fuzzer,address,undefined` | Parser fuzzing |
| Coverage | `--coverage` with `gcov`/`lcov` | Coverage report in CI |

CMake options: `KVFORGE_BUILD_TESTS` (default ON), `KVFORGE_BUILD_FUZZ` (default OFF, Clang only), `KVFORGE_SANITIZE` (OFF/ON).

## 4. Testing and quality tools
| Tool | Version | Purpose |
|---|---|---|
| GoogleTest | 1.14+ (fetched with CMake `FetchContent`) | Unit and integration tests |
| libFuzzer (Clang) | with Clang 14+ | Parser fuzzing |
| AddressSanitizer, UBSan | compiler built-in | Memory and UB checks |
| Valgrind | optional | Leak cross-check |
| Python | 3.10+ | Differential and crash test drivers |
| `redis` Python package | 5.x | Client for differential tests |
| Redis server | 7.x (apt or source) | Reference implementation, benchmark baseline |
| `redis-cli`, `redis-benchmark` | 7.x | Manual testing and load generation |
| `clang-format` | 14+ | Formatting (`.clang-format`, based on Google style, 100 columns) |
| `clang-tidy` | 14+ | Static analysis (`bugprone-*`, `performance-*`, `modernize-*` selected) |
| `cppcheck` | optional | Second static analyzer |
| `lcov` | any | Coverage HTML |

## 5. CI/CD tooling
| Item | Choice |
|---|---|
| CI | GitHub Actions on `ubuntu-22.04` |
| Jobs | build (GCC, Clang), unit tests, sanitizer tests, differential tests (installs `redis-server`), clang-tidy, short fuzz run, coverage |
| Release artifact | Tagged source tarball, plus a statically linked Linux x86-64 binary and SHA-256 checksum |

## 6. Benchmark environment
| Item | Specification |
|---|---|
| Machine | Native Linux or Azure Linux VM (for example 4 vCPU, 8 GiB), recorded in the benchmark report |
| Pinning | Server pinned to one core with `taskset`; `redis-benchmark` on other cores |
| Governor | Performance CPU governor where available |
| Redis baseline | Redis 7.x on the same machine, same port range, equal persistence settings |
| Tools | `redis-benchmark`, `/usr/bin/time -v` or `ps` for RSS, `perf` optional for profiling |

## 7. Why these choices
| Choice | Reason |
|---|---|
| C++17 | Strong data-structure and systems control; `std::string_view`, structured bindings, `std::optional` help without heavy dependencies |
| CMake | Industry standard; integrates with sanitizers, tests and IDEs |
| GoogleTest | Common and widely recognized in C++ projects |
| No networking library | The learning goal is raw sockets and `epoll` |
| No Boost | Keeps the build simple and dependencies visible |
| Python for test drivers | Fast to write; the `redis` client makes differential testing straightforward |
| GitHub Actions | Free for public repositories and visible to reviewers |

## 8. Version pinning
- Compiler and CMake minimum versions in `README.md` and `CMakeLists.txt`.
- GoogleTest pinned by tag in `FetchContent`.
- Python dependencies pinned in `tests/requirements.txt`.
- Redis version used for the baseline recorded in each benchmark report.

## 9. Coding environment recommendations
- VS Code with the C++ and CMake Tools extensions, or CLion.
- `compile_commands.json` exported (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`) for clang-tidy and editor tooling.
- `gdb` or `lldb` for debugging, `strace -f -tt` for syscall tracing.