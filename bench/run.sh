#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RAW_DIR="$SCRIPT_DIR/results/raw"

mkdir -p "$RAW_DIR"

REQUESTS=${1:-1000000}
RUNS=${2:-5}
REDIS_PORT=6390
KVF_PORT=6391

echo "=================================================="
echo "KVForge Performance Benchmark Runner"
echo "Requests per test: $REQUESTS | Runs per cell: $RUNS"
echo "=================================================="

# 1. Environment Capture
echo "Capturing environment metadata..."
CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -n 1 | cut -d ':' -f 2 | sed 's/^[ \t]*//')
CORES=$(nproc)
RAM_TOTAL=$(grep "MemTotal" /proc/meminfo | awk '{print int($2/1024) " MB"}')
KERNEL_VER=$(uname -srmo)
COMPILER_VER=$(g++ --version | head -n 1)
GIT_COMMIT=$(git -C "$REPO_DIR" rev-parse --short HEAD 2>/dev/null || echo "release")
REDIS_BIN="/home/shannu/deps/redis-7.2.4/src/redis-server"
REDIS_BENCH="/home/shannu/deps/redis-7.2.4/src/redis-benchmark"
REDIS_CLI="/home/shannu/deps/redis-7.2.4/src/redis-cli"
REDIS_VER=$($REDIS_BIN --version | awk '{print $3}' | cut -d '=' -f 2)

cat << EOF > "$RAW_DIR/environment.json"
{
  "cpu": "$CPU_MODEL",
  "cores": $CORES,
  "ram": "$RAM_TOTAL",
  "kernel": "$KERNEL_VER",
  "compiler": "$COMPILER_VER",
  "cxxflags": "-O3 -DNDEBUG",
  "redis_version": "$REDIS_VER",
  "kvforge_version": "0.2.0",
  "git_commit": "$GIT_COMMIT"
}
EOF

# 2. Build Release KVForge
echo "Building Release binary of KVForge..."
mkdir -p "$REPO_DIR/build_release"
cd "$REPO_DIR/build_release"
cmake -DCMAKE_BUILD_TYPE=Release .. > /dev/null
make -j$(nproc) kvforge > /dev/null
KVF_BIN="$REPO_DIR/build_release/kvforge"

cleanup() {
    fuser -k $REDIS_PORT/tcp 2>/dev/null || true
    fuser -k $KVF_PORT/tcp 2>/dev/null || true
}
trap cleanup EXIT
cleanup

# Helper: Read RSS in MiB from pid
get_rss_mib() {
    local pid=$1
    local kb=$(grep "VmRSS" /proc/$pid/status 2>/dev/null | awk '{print $2}')
    if [ -n "$kb" ]; then
        echo "scale=2; $kb / 1024" | bc
    else
        echo "0"
    fi
}

echo ""
echo "--- Benchmarking Redis 7.2.4 ---"
$REDIS_BIN --port $REDIS_PORT --save "" --appendonly no --daemonize yes
sleep 1
REDIS_PID=$(fuser $REDIS_PORT/tcp 2>/dev/null | tr -d ' ' || true)

echo "Warming up Redis..."
$REDIS_BENCH -p $REDIS_PORT -t set,get -n 20000 -c 50 -q > /dev/null

for pipe in 1 16; do
    for op in set get; do
        echo "  Running Redis: OP=$op, P=$pipe ($RUNS runs)..."
        for r in $(seq 1 $RUNS); do
            $REDIS_BENCH -p $REDIS_PORT -t $op -P $pipe -n $REQUESTS -c 50 -r 1000000 -d 64 --csv > "$RAW_DIR/redis_${op}_P${pipe}_run${r}.csv"
        done
    done
done

# Memory test on Redis
echo "Measuring Redis memory footprint at 1M keys..."
$REDIS_CLI -p $REDIS_PORT FLUSHALL > /dev/null
$REDIS_BENCH -p $REDIS_PORT -t set -n 1000000 -r 1000000 -d 64 -q > /dev/null
REDIS_RSS_MIB=$(get_rss_mib $REDIS_PID)
echo "Redis RSS at 1M keys: ${REDIS_RSS_MIB} MiB"

$REDIS_CLI -p $REDIS_PORT shutdown > /dev/null 2>&1 || true
sleep 1


echo ""
echo "--- Benchmarking KVForge v0.2.0 ---"
$KVF_BIN --port $KVF_PORT > /dev/null 2>&1 &
KVF_PID=$!
sleep 1

echo "Warming up KVForge..."
$REDIS_BENCH -p $KVF_PORT -t set,get -n 20000 -c 50 -q > /dev/null

for pipe in 1 16; do
    for op in set get; do
        echo "  Running KVForge: OP=$op, P=$pipe ($RUNS runs)..."
        for r in $(seq 1 $RUNS); do
            $REDIS_BENCH -p $KVF_PORT -t $op -P $pipe -n $REQUESTS -c 50 -r 1000000 -d 64 --csv > "$RAW_DIR/kvforge_${op}_P${pipe}_run${r}.csv"
        done
    done
done

# Memory test on KVForge
echo "Measuring KVForge memory footprint at 1M keys..."
$REDIS_CLI -p $KVF_PORT FLUSHALL > /dev/null
$REDIS_BENCH -p $KVF_PORT -t set -n 1000000 -r 1000000 -d 64 -q > /dev/null
KVF_RSS_MIB=$(get_rss_mib $KVF_PID)
echo "KVForge RSS at 1M keys: ${KVF_RSS_MIB} MiB"

kill -9 $KVF_PID 2>/dev/null || true
wait $KVF_PID 2>/dev/null || true

cat << EOF > "$RAW_DIR/rss_memory.json"
{
  "redis_mib": $REDIS_RSS_MIB,
  "kvforge_mib": $KVF_RSS_MIB
}
EOF

echo ""
echo "Aggregating benchmark results..."
python3 "$SCRIPT_DIR/collect.py"
