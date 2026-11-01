# KVForge — User Flows

All diagrams use Mermaid. Flow IDs are referenced by acceptance criteria and tests.

## 1. Flow index
| ID | Flow | Actor |
|---|---|---|
| UF-01 | Server startup and AOF recovery | Operator |
| UF-02 | Client connects and runs a command | Application developer |
| UF-03 | Partial and pipelined requests | Application developer |
| UF-04 | SET with expiry, then access | Application developer |
| UF-05 | Active expiry in the event loop | System |
| UF-06 | Write path with AOF and fsync policy | System |
| UF-07 | Slow client and backpressure | System |
| UF-08 | Protocol error or limit violation | Application developer |
| UF-09 | Graceful shutdown | Operator |
| UF-10 | Crash and recovery | Operator |
| UF-11 | Verification workflow (tests and benchmarks) | Reviewer / Contributor |

## 2. UF-01 Startup and AOF recovery
```mermaid
flowchart TD
    A[Start kvforge with flags/config] --> B{Config valid?}
    B -- No --> B1[Print error, exit 1]
    B -- Yes --> C[Create listening socket, bind, listen]
    C --> D{Bind OK?}
    D -- No --> D1[Log error, exit 1]
    D -- Yes --> E{appendonly = yes?}
    E -- No --> H[Create epoll instance, register listener]
    E -- Yes --> F{AOF file exists?}
    F -- No --> F1[Create empty AOF] --> H
    F -- Yes --> G[Replay AOF record by record]
    G --> G1{Record result}
    G1 -- Complete and valid --> G2[Apply to store; skip if absolute expiry passed] --> G
    G1 -- Truncated final record --> G3[Truncate file to last good offset, log warn] --> H
    G1 -- Corrupt in the middle --> G4[Log byte offset, exit 1]
    G1 -- End of file --> H
    H --> I[Enter event loop; log ready on port]
```

## 3. UF-02 Connect and run a command
```mermaid
sequenceDiagram
    participant C as Client
    participant L as Listener
    participant E as Event loop
    participant P as Parser
    participant D as Dispatcher
    participant S as Store
    C->>L: TCP connect
    L->>E: accept, set non-blocking, register EPOLLIN
    C->>E: *3 SET key value (RESP bytes)
    E->>P: bytes appended to connection read buffer
    P->>D: complete command [SET, key, value]
    D->>S: set(key, value)
    S-->>D: ok
    D-->>E: +OK appended to write buffer
    E-->>C: +OK
```

## 4. UF-03 Partial and pipelined requests
```mermaid
flowchart TD
    A[EPOLLIN on connection] --> B[read until EAGAIN or buffer limit]
    B --> C[Append to read buffer]
    C --> D[Parse loop]
    D --> E{Complete command in buffer?}
    E -- No --> F[Keep leftover bytes, wait for next EPOLLIN]
    E -- Yes --> G[Execute command, append reply to write buffer]
    G --> D
    F --> H{Write buffer not empty?}
    H -- Yes --> I[Try write now; if incomplete, register EPOLLOUT]
    H -- No --> J[Done]
```
Rules: replies are always in command order; a half-received command never produces a reply; leftover bytes are kept per connection.

## 5. UF-04 SET with expiry then access
```mermaid
sequenceDiagram
    participant C as Client
    participant S as Store
    participant X as Expiry heap
    C->>S: SET session abc EX 10
    S->>S: store value, expire_at = now + 10000 ms
    S->>X: push (expire_at, session)
    S-->>C: +OK
    Note over C,S: 5 seconds later
    C->>S: TTL session
    S-->>C: :5
    Note over C,S: 11 seconds later
    C->>S: GET session
    S->>S: expire_at passed, delete key (lazy expiry)
    S-->>C: $-1 (null bulk)
```

## 6. UF-05 Active expiry
```mermaid
flowchart TD
    A[Event loop iteration] --> B[epoll_wait with timeout = min(next expiry, 100 ms)]
    B --> C[Handle ready events]
    C --> D[Start expiry cycle with time budget]
    D --> E{Heap top expire_at <= now?}
    E -- No --> J[End cycle]
    E -- Yes --> F[Pop entry]
    F --> G{Key exists and its current expiry equals entry?}
    G -- No --> H[Stale entry, discard] --> K
    G -- Yes --> I[Delete key, expired_keys++, append DEL to AOF] --> K
    K{Budget exhausted?}
    K -- No --> E
    K -- Yes --> J
    J --> A
```

## 7. UF-06 Write path with AOF
```mermaid
flowchart TD
    A[Write command executed successfully] --> B{AOF enabled?}
    B -- No --> Z[Reply to client]
    B -- Yes --> C[Rewrite relative TTL to absolute PEXPIREAT]
    C --> D[Append RESP record to AOF buffer]
    D --> E{appendfsync}
    E -- always --> F[write + fsync before reply] --> Z
    E -- everysec --> G[write now, fsync at most once per second] --> Z
    E -- no --> H[write now, leave flushing to OS] --> Z
```

## 8. UF-07 Slow client and backpressure
```mermaid
flowchart TD
    A[Reply appended to write buffer] --> B[write to socket]
    B --> C{All bytes sent?}
    C -- Yes --> D[Disable EPOLLOUT if set]
    C -- No, EAGAIN --> E[Enable EPOLLOUT, keep remaining bytes]
    E --> F{Write buffer > max output limit?}
    F -- Yes --> G[Log warn, close connection, free buffers]
    F -- No --> H[Continue serving other clients]
    H --> I[EPOLLOUT fires] --> B
```

## 9. UF-08 Protocol error or limit violation
```mermaid
flowchart TD
    A[Parser reads bytes] --> B{Valid RESP or inline?}
    B -- Yes --> C{Within limits?}
    C -- Yes --> D[Continue normally]
    C -- No --> E[Reply -ERR with limit reason, close connection]
    B -- No --> F[Reply -ERR Protocol error: reason, close connection]
```
Examples of violations: bulk length above 64 MiB, array length above 1,048,576, query buffer above 128 MiB, inline line above 64 KiB, invalid length digits, missing `\r\n`.

## 10. UF-09 Graceful shutdown
```mermaid
flowchart TD
    A[SIGINT or SIGTERM received] --> B[Set shutdown flag via signal-safe mechanism]
    B --> C[Event loop notices flag]
    C --> D[Stop accepting new connections]
    D --> E[Flush pending write buffers where possible]
    E --> F{AOF enabled?}
    F -- Yes --> G[Write and fsync AOF buffer]
    F -- No --> H[Skip]
    G --> I[Close all connections and epoll fd]
    H --> I
    I --> J[Exit code 0]
```

## 11. UF-10 Crash and recovery
```mermaid
flowchart TD
    A[kill -9 or power loss] --> B[AOF may end with partial record]
    B --> C[Operator restarts kvforge]
    C --> D[UF-01 replay]
    D --> E{Last record complete?}
    E -- Yes --> F[All acknowledged writes recovered per fsync policy]
    E -- No --> G[Truncate partial tail, warn, start]
```
Durability expectations: `always` recovers every acknowledged write; `everysec` can lose up to about one second; `no` depends on the OS.

## 12. UF-11 Verification workflow
```mermaid
flowchart LR
    A[Commit pushed] --> B[CI: build Release]
    B --> C[CI: unit tests]
    C --> D[CI: ASan + UBSan build and tests]
    D --> E[CI: differential test vs Redis 7.x]
    E --> F[CI: short fuzz run]
    F --> G{All green?}
    G -- Yes --> H[Merge allowed]
    G -- No --> I[Fix and re-run]
    H --> J[Release: run benchmark script, publish raw results]
```