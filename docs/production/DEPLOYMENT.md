# KVForge — Deployment Guide

This guide describes how to deploy, configure, and operate KVForge in production environments. KVForge is a high-performance, single-threaded, Redis-compatible in-memory key-value server written in C++17 using raw Linux `epoll` sockets and RESP2 protocol.

---

## 1. Operational Principles & Architecture Context

KVForge follows specific operational constraints established in the architecture specifications:
- **Single-Node Service:** Operates as a single process per port; there is no native clustering or replication in Tier 1/2.
- **Security Posture:** Safe by default on a trusted host. There is **no native TLS** and **no AUTH command**. Remote access requires external isolation (private VPC, firewall, or reverse proxy/stunnel).
- **Default Network Binding:** Binds to `127.0.0.1:6380` by default. Binding to `0.0.0.0` or a public interface emits a prominent warning at startup.
- **Persistence Model:** Append-Only File (AOF) with configurable fsync policies (`always`, `everysec`, `no`). Files are stored with strict permissions (`0600`).

---

## 2. System Requirements & Prerequisites

### 2.1 Hardware Requirements
| Resource | Minimum | Recommended | Notes |
|---|---|---|---|
| **CPU** | 1 vCPU / Core | 2+ vCPU / Dedicated Core | KVForge core loop is single-threaded; isolate or pin to a dedicated core for lowest latency |
| **Memory** | 512 MiB | 4 GiB – 64 GiB | Sized according to keyspace dataset; memory tracking is maintained in `INFO` |
| **Disk** | 10 GB SSD | NVMe SSD with high IOPS | Required for AOF persistence; sequential write throughput directly dictates latency under `appendfsync always` |
| **Network** | 1 Gbps | 10 Gbps | Standard TCP stack |

### 2.2 Operating System & Kernel
- **OS:** Linux (Kernel 4.18 or newer recommended, e.g., Ubuntu 22.04 LTS, Debian 12, RHEL 9).
- **Architecture:** x86-64 or ARM64.
- **C Library:** glibc 2.25+ (required for `getrandom` syscall and `accept4`).
- **Required Linux Features:** `epoll` (`epoll_create1`), `signalfd`, `O_CLOEXEC`, `SO_REUSEADDR`, `TCP_NODELAY`.

---

## 3. Binary Build & Packaging

### 3.1 Production Compilation
Always build with optimizations enabled and assertions disabled:
```bash
# Clone the repository
git clone https://github.com/your-org/kvforge.git
cd kvforge

# Create out-of-source release build directory
cmake -B build-release -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DKVFORGE_BUILD_TESTS=OFF \
  -DCMAKE_CXX_FLAGS="-O2 -DNDEBUG -fno-plt -fstack-protector-strong -D_FORTIFY_SOURCE=2"

# Compile with all available cores
cmake --build build-release --parallel $(nproc)

# Strip binary to remove debug symbols and reduce footprint
strip build-release/kvforge

# Verify binary integrity
build-release/kvforge --version
```

### 3.2 Checksum Generation
```bash
sha256sum build-release/kvforge > kvforge-release.sha256
```

---

## 4. Host System Tuning

To achieve sub-millisecond latencies and sustain tens of thousands of concurrent connections, configure host kernel parameters before launching KVForge.

### 4.1 File Descriptor Limits
KVForge defaults to `--max-clients 10000`. By default, Linux systemd or shells enforce `ulimit -n 1024`, which restricts server capacity.

Add to `/etc/security/limits.d/99-kvforge.conf`:
```ini
kvforge    soft    nofile    65536
kvforge    hard    nofile    65536
```

### 4.2 Kernel Network & Memory Parameters
Create `/etc/sysctl.d/99-kvforge.conf`:
```ini
# Increase socket listen backlog for high connection bursts
net.core.somaxconn = 65535

# Increase maximum incoming socket queue
net.core.netdev_max_backlog = 10000

# Enable TCP SYN cookies to mitigate SYN floods
net.ipv4.tcp_syncookies = 1

# Increase ephemeral port range for test drivers and proxies
net.ipv4.ip_local_port_range = 10240 65535

# Disable swapping or reduce aggressiveness
vm.swappiness = 1

# Ensure memory overcommit allows memory allocations without premature OOM
vm.overcommit_memory = 1
```

Apply immediately:
```bash
sudo sysctl --system
```

### 4.3 Transparent Huge Pages (THP)
Transparent Huge Pages can cause latency spikes during memory re-allocation and rehashing. Disable or set to `madvise`:
```bash
echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
```
Persist this setting via a systemd one-shot service or `rc.local`.

---

## 5. User & Filesystem Setup

Run KVForge under a dedicated, unprivileged system user with minimal permissions:

```bash
# Create system group and user
sudo groupadd --system kvforge
sudo useradd --system --gid kvforge --no-create-home --shell /usr/sbin/nologin kvforge

# Create configuration directory
sudo mkdir -p /etc/kvforge
sudo chown -R root:kvforge /etc/kvforge
sudo chmod 750 /etc/kvforge

# Create data directory for AOF persistence
sudo mkdir -p /var/lib/kvforge
sudo chown -R kvforge:kvforge /var/lib/kvforge
sudo chmod 700 /var/lib/kvforge

# Create log directory (if not logging directly to systemd journal)
sudo mkdir -p /var/log/kvforge
sudo chown -R kvforge:kvforge /var/log/kvforge
sudo chmod 750 /var/log/kvforge
```

---

## 6. Configuration File (`/etc/kvforge/kvforge.conf`)

Install the production configuration file at `/etc/kvforge/kvforge.conf` (permissions `0640`, owner `root:kvforge`):

```conf
# ==============================================================================
# KVForge Production Configuration File
# ==============================================================================

# Network binding: keep 127.0.0.1 for local sidecars or internal interfaces only
bind 127.0.0.1
port 6380

# Maximum concurrent client connections
maxclients 10000

# Persistence settings
appendonly yes
appendfsync everysec
appendfilename /var/lib/kvforge/appendonly.aof

# Logging configuration
loglevel info
```

---

## 7. Systemd Service Deployment

Create `/etc/systemd/system/kvforge.service`:

```ini
[Unit]
Description=KVForge Redis-Compatible In-Memory Key-Value Server
Documentation=https://github.com/your-org/kvforge
After=network.target network-online.target
Wants=network-online.target

[Service]
Type=simple
User=kvforge
Group=kvforge
WorkingDirectory=/var/lib/kvforge
ExecStart=/usr/local/bin/kvforge --config /etc/kvforge/kvforge.conf
ExecStop=/bin/kill -s TERM $MAINPID

# Graceful shutdown wait time (allows flushing dirty buffers and fsyncing AOF)
TimeoutStopSec=30s
KillMode=mixed
KillSignal=SIGTERM

# Restart behavior
Restart=on-failure
RestartSec=5s

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096
LimitMEMLOCK=infinity

# Security sandboxing
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
ProtectKernelTunables=true
ProtectControlGroups=true
ReadWritePaths=/var/lib/kvforge /var/log/kvforge
ReadOnlyPaths=/etc/kvforge

# Standard output and error logging to systemd journal
StandardOutput=journal
StandardError=journal
SyslogIdentifier=kvforge

[Install]
WantedBy=multi-user.target
```

Enable and start the service:
```bash
sudo cp build-release/kvforge /usr/local/bin/kvforge
sudo chmod 755 /usr/local/bin/kvforge

sudo systemctl daemon-reload
sudo systemctl enable kvforge.service
sudo systemctl start kvforge.service
sudo systemctl status kvforge.service
```

---

## 8. Network Security & TLS Termination

KVForge intentionally does not implement SSL/TLS or authentication (`AUTH`). Direct exposure to the public internet is strictly forbidden.

### 8.1 Local Firewall (UFW)
Ensure external traffic cannot reach the KVForge port:
```bash
# Allow only local loopback traffic
sudo ufw allow in on lo to any port 6380 proto tcp
sudo ufw deny 6380/tcp
```

### 8.2 TLS Termination via Stunnel or HAProxy (Remote Clients)
When clients reside on separate machines across a network, terminate TLS using a dedicated reverse proxy (such as HAProxy, Envoy, or Stunnel) on the same host:

```
[Remote Client] ---> (TLS: Port 6379) ---> [HAProxy / TLS Term] ---> (Plain RESP2: 127.0.0.1:6380) ---> [KVForge]
```

Example HAProxy frontend/backend configuration:
```haproxy
frontend kvforge_tls_in
    bind *:6379 ssl crt /etc/ssl/certs/kvforge.pem
    mode tcp
    default_backend kvforge_local

backend kvforge_local
    mode tcp
    server local_kvforge 127.0.0.1:6380 check
```

---

## 9. Containerized Deployment (Docker / OCI)

For container-based environments (Kubernetes, Docker Compose), use a minimal multi-stage image running as non-root.

### 9.1 Multi-Stage Dockerfile
```dockerfile
# Stage 1: Build binary
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gcc-11 \
    g++-11 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++-11 \
    -DKVFORGE_BUILD_TESTS=OFF \
    -DCMAKE_CXX_FLAGS="-O2 -DNDEBUG -fno-plt" \
    && cmake --build build --parallel $(nproc) \
    && strip build/kvforge

# Stage 2: Minimal runtime image
FROM ubuntu:22.04
RUN groupadd -r -g 10001 kvforge && \
    useradd -r -u 10001 -g kvforge -d /data -s /usr/sbin/nologin kvforge && \
    mkdir -p /data /etc/kvforge && \
    chown -R kvforge:kvforge /data /etc/kvforge

COPY --from=builder /workspace/build/kvforge /usr/local/bin/kvforge

USER 10001:10001
WORKDIR /data
VOLUME ["/data"]

EXPOSE 6380
ENTRYPOINT ["/usr/local/bin/kvforge"]
CMD ["--bind", "0.0.0.0", "--port", "6380", "--appendfilename", "/data/appendonly.aof"]
```

---

## 10. Post-Deployment Verification

Execute verification checks immediately following deployment:

1. **Verify Process Status:**
   ```bash
   systemctl is-active kvforge
   ```

2. **Verify Port & Interface Binding:**
   ```bash
   ss -tlpn | grep 6380
   # Expected output must reflect 127.0.0.1:6380 or intended private IP
   ```

3. **Verify Responsiveness with `redis-cli`:**
   ```bash
   redis-cli -p 6380 PING
   # Expected: PONG
   ```

4. **Verify Storage Permissions:**
   ```bash
   ls -la /var/lib/kvforge/
   # Expected: appendonly.aof owned by kvforge:kvforge with 0600 permissions
   ```

5. **Verify Version & Telemetry:**
   ```bash
   redis-cli -p 6380 INFO server
   # Verify version, uptime, and process ID
   ```
