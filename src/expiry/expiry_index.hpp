#pragma once

#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace kvforge {

// Represents an item in the Min-Heap
struct ExpiryEntry {
    uint64_t expire_at_ms; // Monotonic timestamp when key expires
    std::string key;

    // Comparator for Min-Heap: smallest expire_at_ms sits at the top
    bool operator>(const ExpiryEntry& other) const {
        return expire_at_ms > other.expire_at_ms;
    }
};

class ExpiryIndex {
public:
    ExpiryIndex() = default;

    // Set or update a key's expiration time
    void set_expiry(const std::string& key, uint64_t expire_at_ms);

    // Remove expiration for a key (PERSIST or DEL). Returns true if key was tracked.
    bool remove_expiry(const std::string& key);

    // Get expiration time for a key. Returns true if key has expiry, false otherwise.
    bool get_expiry(const std::string& key, uint64_t& out_expire_at_ms) const;

    // Check if a key is expired at the given timestamp
    bool is_expired(const std::string& key, uint64_t now_ms) const;

    // Number of keys with active expiration timers
    size_t size() const { return key_to_time_.size(); }

    // Clears all expirations (for FLUSHALL)
    void clear();

    // Peeks at the earliest upcoming expiration timestamp.
    // Returns true and sets out_ms if at least one key is scheduled.
    bool next_expiry_time(uint64_t& out_ms);

    // Pops expired keys from the heap up to max_count or until now_ms
    // Returns a list of keys that must be deleted from the hash table
    std::vector<std::string> pop_expired(uint64_t now_ms, size_t max_count = 100);

private:
    // Helper to clean stale heap entries from the top
    void prune_stale_top();

    // Key -> expire_at_ms (for O(1) existence and TTL lookups)
    std::unordered_map<std::string, uint64_t> key_to_time_;

    // Min-heap priority queue: ordered by earliest expire_at_ms
    std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, std::greater<ExpiryEntry>> heap_;
};

} // namespace kvforge