# KVForge — Threat Model (STRIDE)

This document provides a formal STRIDE threat model for KVForge. It evaluates system trust boundaries, attack surfaces, threat vectors, mitigations, and residual risks in accordance with the security architecture defined in `architecture/SECURITY_ARCHITECTURE.md`.

---

## 1. System Decomposition & Data Flow Diagram (DFD)

```mermaid
flowchart TD
    subgraph Untrusted Network
        Attacker[External Attacker]
        Client[Legitimate Client]
    end

    subgraph Host Trust Boundary
        subgraph Operating System & Kernel
            EP[epoll / Sockets / signalfd]
            FS[(Linux Filesystem / AOF)]
        end

        subgraph KVForge Process (Unprivileged UID 10001)
            Loop[Event Loop]
            Parser[RESP2 State Machine Parser]
            Dispatcher[Command Dispatcher]
            Store[HashTable / ExpiryIndex]
            AOF[AOF Writer / Loader]
        end
    end

    Client -->|TCP: Plain RESP2 bytes| EP
    Attacker -.->|Abusive TCP bytes / Flood| EP
    EP -->|Read buffer| Loop
    Loop --> Parser
    Parser --> Dispatcher
    Dispatcher --> Store
    Dispatcher -->|Normalized writes| AOF
    AOF -->|fsync / append| FS
    FS -.->|Replay at boot| AOF
```

### 1.1 Trust Boundaries
- **TB-1 (Network Boundary):** All network input arriving over TCP sockets is completely untrusted.
- **TB-2 (Host Process Boundary):** The host OS kernel and filesystem. Operator flags and configuration files are trusted.
- **TB-3 (Persistence Boundary):** The AOF file on disk is semi-trusted. It may be truncated due to crashes, or altered by malicious local processes.

---

## 2. STRIDE Threat Analysis

### 2.1 Spoofing (S)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-S-01** | **Unauthenticated Client Access:** An unauthorized entity connects to the listening socket and issues commands. | Unauthorized read/write/flush access to stored data. | Default bind address is strictly loopback (`127.0.0.1`). Binding to a non-loopback address emits a prominent warning. Production deployments must enforce external network perimeter isolation (firewall, VPC, VPN) or an authenticating TLS reverse proxy. | Mitigated by Architecture |
| **TH-S-02** | **Source IP Spoofing:** An attacker attempts TCP connection spoofing. | Unauthorized access. | Standard TCP 3-way handshake prevents IP spoofing across routable networks. | Mitigated by TCP |

---

### 2.2 Tampering (T)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-T-01** | **RESP Parser Framing & Smuggling:** Malformed multibulk headers, mismatched bulk lengths, or chunked inputs attempting parser desynchronization. | Command injection, parser crash, or memory corruption. | Strict streaming state-machine parser. Exact digit count validation (max 19 digits) with explicit integer overflow checks. Rejects invalid CRLF sequences immediately. Fuzzed continuously with libFuzzer. | Verified by Fuzzing |
| **TH-T-02** | **Error-Message Injection (CRLF Injection):** Client supplies malicious characters (including `\r\n`) within arguments that trigger an error reply. If echoed verbatim, the error reply could inject unintended RESP tokens into the downstream stream. | Protocol desynchronization or client-side cache poisoning. | When generating error replies (e.g. `-ERR unknown command '...'`), KVForge strips or escapes all `\r\n` characters from the reflected string. Length is strictly bounded, preventing protocol desynchronization. | Mitigated |
| **TH-T-03** | **AOF File Modification on Disk:** Local attacker modifies or injects commands directly into the AOF file. | Keyspace corruption, unintended commands executed at next reboot. | AOF file is created with restrictive permissions (`0600`), readable and writable only by the `kvforge` system user. Replay parser uses the same strict validation as the network parser. Corrupt middle records cause startup abort (AC-AOF-06). | Mitigated by OS & Parser |

---

### 2.3 Repudiation (R)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-R-01** | **Destructive Administrative Action:** An authorized or unauthorized actor issues `FLUSHALL`, wiping all cached keys. | Data loss without forensic trail. | Write commands and `FLUSHALL` are durably recorded in the AOF stream. Structured `WARN` log entries are written to stderr with timestamp and client file descriptor. | Mitigated |

---

### 2.4 Information Disclosure (I)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-I-01** | **Memory Leakage / Uninitialized Buffers:** Heap memory disclosure via returned string payloads. | Disclosure of previously freed keys, values, or sensitive process memory. | Safe C++ modern abstractions (`std::string`, `std::vector`). Zero raw manual `memcpy` into uninitialized network buffers. Validated continuously in CI under AddressSanitizer and MemorySanitizer. | Verified by ASan |
| **TH-I-02** | **Detailed System Leakage in Error Replies:** Error messages revealing host paths, stack traces, or internal addresses. | Host reconnaissance. | Error replies follow standardized Redis error strings (e.g. `-ERR unknown command`, `-ERR syntax error`). Internal file paths, pointer values, and stack traces are never sent over the wire. | Mitigated |
| **TH-I-03** | **Sensitive Data Leakage in Logs:** Application keys or passwords written to server logs. | Exposure of PII or credentials to log aggregation pipelines. | Strict zero-logging policy: payload values are **never logged at any log level**. Keys are omitted at `INFO` and above, and only logged at `DEBUG` during localized testing. | Mitigated |

---

### 2.5 Denial of Service (DoS)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-DOS-01** | **Hash Flooding / Collision Attack:** Attacker crafts keys designed to collide into the same hash bucket, degrading lookup from $O(1)$ to $O(N)$. | CPU exhaustion; event loop stall. | Hash function uses a randomized 64-bit seed generated via the `getrandom()` system call at process startup. Attacker cannot predict bucket distributions across restarts. Progressive incremental rehashing prevents single-cycle latency spikes. | Mitigated |
| **TH-DOS-02** | **Memory Pinning via Fake Bulk Length:** Attacker sends `*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$67108864\r\n` (declaring 64 MiB) but sends no bytes, attempting to exhaust RAM. | Out-of-memory crash. | Parser does **not** allocate the declared bulk length upfront. Memory grows dynamically only as actual bytes arrive over the socket. Connections are bounded by query buffer limit (128 MiB) and client timeout. | Mitigated |
| **TH-DOS-03** | **Slow-Reader Output Buffer Bloat:** Attacker issues `KEYS *` or huge `MGET`, then intentionally stops reading from socket. | Unbounded buffer growth leading to OOM. | Client output buffer is bounded to 64 MiB (AC-LIM-04). When the threshold is exceeded, KVForge closes the connection and frees the pending buffer immediately. | Mitigated |
| **TH-DOS-04** | **Connection Exhaustion (Slowloris):** Attacker opens thousands of TCP sockets and holds them open. | Socket / FD exhaustion. | Server enforces `--max-clients` (default 10,000) and clamps against `RLIMIT_NOFILE - 32`. Excess connections are rejected with `-ERR max number of clients reached` and immediately closed. Sockets enable `SO_KEEPALIVE`. | Mitigated |
| **TH-DOS-05** | **Disk Full / AOF Persistence Failure:** Disk partition fills up, preventing AOF write/fsync. | Potential data loss or silent data divergence. | When `write()` or `fsync()` fails, KVForge locks out subsequent write commands with `-MISCONF Errors writing to the append-only file (disk full or write error)` while serving reads. Resumes once disk space is restored. | Mitigated |
| **TH-DOS-06** | **Blocking Algorithmic Commands:** Attacker or operator issues `KEYS *` over millions of keys. | Single-threaded event loop blocked, starving other clients. | Documented $O(N)$ operational limitation. Operators must monitor and restrict `KEYS` in production environments. | Documented Limitation |

---

### 2.6 Elevation of Privilege (EoP)

| Threat ID | Threat Vector | Impact | Mitigation in KVForge | Status |
|---|---|---|---|---|
| **TH-EOP-01** | **Arbitrary Code Execution / Shell Breakout:** Attacker exploits memory flaw to execute arbitrary system commands. | Complete host compromise. | 1. Zero shell execution functions in the codebase (`system()`, `popen()`, `execve()` are strictly banned).<br>2. Compiles with `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`.<br>3. Runs under unprivileged user `kvforge` (UID 10001).<br>4. Systemd unit enforces `NoNewPrivileges=true`, `ProtectSystem=strict`, `ProtectHome=true`, `PrivateTmp=true`. | Fully Hardened |

---

## 3. Residual Risk & Accepted Trade-Offs

The following limitations are explicitly accepted by design for KVForge Tier 1/2:
1. **Unauthenticated Protocol:** KVForge has no native user authentication or RBAC. It must not be deployed on untrusted or multi-tenant networks without perimeter firewalls or an authenticating reverse proxy.
2. **Cleartext Wire Protocol:** Network traffic is unencrypted plain RESP2. Confidential data must be secured in transit via TLS proxies, SSH tunnels, or VPN.
3. **Absence of At-Rest Encryption:** The AOF file is stored in cleartext RESP2 format on disk. Data at rest must be protected using Linux filesystem-level encryption (LUKS / dm-crypt) or encrypted cloud block volumes.
