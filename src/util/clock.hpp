#pragma once

#include <cstdint>
#include <chrono>

namespace kvforge {

// Returns monotonic time in milliseconds (immune to system clock adjustments)
// Ideal for TTL timers and epoll timeouts
inline uint64_t monotonic_time_ms() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
    );
}

// Returns real-world wall clock time in milliseconds (since Unix Epoch)
// Ideal for absolute timestamps and AOF persistence logs
inline uint64_t wall_clock_time_ms() {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
    );
}

} // namespace kvforge