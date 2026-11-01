# KVForge — Security Checklist & Hardening Guide

This checklist provides actionable security requirements and audit verification steps for developers, code reviewers, and system operators deploying KVForge. Complete and verify every applicable section prior to production deployment or tagging releases.

---

## 1. Build-Time & Compiler Hardening Checklist

- [ ] **Zero Warning Policy Enforced:**
  Compiled with `-Wall -Wextra -Wpedantic -Werror`. No warnings generated under either GCC 11+ or Clang 14+.
- [ ] **Stack Smashing Protection:**
  Compiled with `-fstack-protector-strong` enabled in release builds.
- [ ] **Fortify Source Active:**
  Compiled with `-D_FORTIFY_SOURCE=2` with optimization (`-O2`).
- [ ] **Position Independent Executable (PIE):**
  Compiled with `-fPIE -pie` to enable full Address Space Layout Randomization (ASLR).
- [ ] **Read-Only Relocations & Immediate Binding:**
  Linked with `-Wl,-z,relro,-z,now` to mark Global Offset Table (GOT) as read-only after startup.
- [ ] **No Banned C Functions:**
  Verified that no banned or unsafe C functions exist in the codebase:
  ```bash
  grep -rE "strcpy|sprintf|gets|atoi|atol|strtok|system|popen|vfork" src/
  # Must produce ZERO matches
  ```
- [ ] **Static Analysis Passed:**
  `clang-tidy` and `cppcheck` execute without errors or security findings in CI.

---

## 2. Dynamic Testing & Sanitizer Verification Checklist

- [ ] **AddressSanitizer (ASan) Clean:**
  The full GoogleTest suite and integration test suite pass under `-fsanitize=address` with zero buffer overflows, use-after-free, or memory leaks detected.
- [ ] **UndefinedBehaviorSanitizer (UBSan) Clean:**
  The test suite passes under `-fsanitize=undefined` with zero signed integer overflows, misaligned pointers, or invalid enum values detected.
- [ ] **Parser Fuzzing Continuous Verification:**
  `fuzz_resp_parser` run under `libFuzzer` for at least 60 seconds in CI on every PR and $\ge 1\text{ hour}$ before major releases with zero crashes or hangs.
- [ ] **AOF Loader Fuzzing Verification:**
  `fuzz_aof_loader` run under `libFuzzer` for at least 30 minutes with zero crashes.
- [ ] **Malformed-Input Regression Corpus:**
  All negative-length, oversized-length, incomplete CRLF, and deep-pipeline regression test vectors pass as expected.

---

## 3. Host System & Filesystem Hardening Checklist

- [ ] **Dedicated Unprivileged System User:**
  KVForge executes under a non-login, unprivileged user (`kvforge` / UID 10001) rather than `root`:
  ```bash
  id kvforge
  # Output: uid=10001(kvforge) gid=10001(kvforge)
  ```
- [ ] **Restrictive Data Directory Permissions:**
  `/var/lib/kvforge` is owned by `kvforge:kvforge` with permissions `0700` (`drwx------`):
  ```bash
  chmod 700 /var/lib/kvforge
  ```
- [ ] **Restrictive AOF File Permissions:**
  `appendonly.aof` is owned by `kvforge:kvforge` with permissions `0600` (`-rw-------`):
  ```bash
  chmod 600 /var/lib/kvforge/appendonly.aof
  ```
- [ ] **Secure Configuration Permissions:**
  `/etc/kvforge/kvforge.conf` is owned by `root:kvforge` with permissions `0640` (`-rw-r-----`).
- [ ] **File Descriptor Cap Configured:**
  `/etc/security/limits.d/99-kvforge.conf` sets `nofile` to at least `65536` for the service user.

---

## 4. Network Perimeter & Protocol Hardening Checklist

- [ ] **Loopback Binding by Default:**
  Configuration explicitly sets `bind 127.0.0.1` unless placed behind a private VPC interface or local reverse proxy.
- [ ] **Warning Logged on Public Binding:**
  Verified that launching with `--bind 0.0.0.0` emits the mandatory warning:
  `[WARN] [net] Binding to a non-loopback address without authentication exposes all data to the network`.
- [ ] **Local Firewall (UFW / iptables) In Effect:**
  Firewall rules block external connections to port 6380:
  ```bash
  sudo ufw status | grep 6380
  # Ensure external interfaces are denied
  ```
- [ ] **TLS Reverse Proxy Configured (If Remote):**
  If clients communicate over a network, an external proxy (Envoy, HAProxy, Stunnel) terminates TLS before routing traffic locally to KVForge.
- [ ] **Connection Limits Active:**
  `--max-clients` is set to an appropriate threshold (default 10,000) and clamped against OS limits.

---

## 5. Runtime & Systemd Sandboxing Checklist

- [ ] **Systemd Process Hardening Active in Unit File:**
  Verify `/etc/systemd/system/kvforge.service` contains the following isolation directives:
  - [ ] `NoNewPrivileges=true`
  - [ ] `ProtectSystem=strict`
  - [ ] `ProtectHome=true`
  - [ ] `PrivateTmp=true`
  - [ ] `PrivateDevices=true`
  - [ ] `ProtectKernelTunables=true`
  - [ ] `ProtectControlGroups=true`
  - [ ] `CapabilityBoundingSet=` (drops all Linux capabilities)
- [ ] **Disk Full Protection Verified:**
  Verified that on AOF write failure, KVForge rejects write commands with `-MISCONF Errors writing to the append-only file` rather than silently dropping data or diverging RAM from disk.
- [ ] **Safe Signal Handling via `signalfd`:**
  Verified that signals (`SIGINT`, `SIGTERM`) are processed through `signalfd` in the main event loop, preventing race conditions or async-signal-unsafe executions.
- [ ] **SIGPIPE Ignored:**
  Process ignores `SIGPIPE` at startup to prevent crashing when writing to suddenly closed client sockets.

---

## 6. Privacy & Information Disclosure Checklist

- [ ] **Zero Logging of Payload Values:**
  Verified across all log statements that command argument values (cached data, user payloads) are **never written to logs** at any log level.
- [ ] **Key Masking at Standard Log Levels:**
  Key names are excluded from logs at `INFO`, `WARN`, and `ERROR` levels.
- [ ] **Error Replies Sanitized (CRLF Injection Prevention):**
  Verified that client error messages strip or escape `\r\n` characters before returning error strings, preventing RESP protocol injection.
- [ ] **No Internal Pointers or Paths Exposed:**
  Error replies never reveal memory addresses, stack traces, or internal server paths.

---

## 7. Security Audit Sign-Off Template

Copy this table into pull request descriptions or release manifests:

| Audit Item | Verified By | Date | Pass / Fail | Notes |
|---|---|---|---|---|
| Dual Compiler Build (GCC/Clang) | | | | |
| Banned C Functions Grep | | | | Zero matches |
| ASan + UBSan Full Test Suite | | | | Zero findings |
| libFuzzer 1-Hour Run | | | | Zero crashes |
| Systemd Sandboxing Flags | | | | Validated in VM |
| Loopback Default Binding | | | | Validated `127.0.0.1` |
| AOF Permissions (0600) | | | | Verified on disk |
| Privacy Log Scrubbing | | | | Values never logged |
