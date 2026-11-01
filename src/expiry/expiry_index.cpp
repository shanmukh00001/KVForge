#include "expiry/expiry_index.hpp"

namespace kvforge {

void ExpiryIndex::set_expiry(const std::string& key, uint64_t expire_at_ms) {
    key_to_time_[key] = expire_at_ms;
    heap_.push({expire_at_ms, key});
}

bool ExpiryIndex::remove_expiry(const std::string& key) {
    auto it = key_to_time_.find(key);
    if (it != key_to_time_.end()) {
        key_to_time_.erase(it);
        return true;
    }
    return false;
}

bool ExpiryIndex::get_expiry(const std::string& key, uint64_t& out_expire_at_ms) const {
    auto it = key_to_time_.find(key);
    if (it != key_to_time_.end()) {
        out_expire_at_ms = it->second;
        return true;
    }
    return false;
}

bool ExpiryIndex::is_expired(const std::string& key, uint64_t now_ms) const {
    auto it = key_to_time_.find(key);
    if (it != key_to_time_.end()) {
        return now_ms >= it->second;
    }
    return false;
}

void ExpiryIndex::clear() {
    key_to_time_.clear();
    while (!heap_.empty()) {
        heap_.pop();
    }
}

void ExpiryIndex::prune_stale_top() {
    while (!heap_.empty()) {
        const ExpiryEntry& top = heap_.top();
        auto it = key_to_time_.find(top.key);

        // If key was removed (PERSIST/DEL) or timestamp was updated, discard stale heap entry
        if (it == key_to_time_.end() || it->second != top.expire_at_ms) {
            heap_.pop();
        } else {
            break; // Top of the heap is valid and active!
        }
    }
}

bool ExpiryIndex::next_expiry_time(uint64_t& out_ms) {
    prune_stale_top();
    if (heap_.empty()) {
        return false;
    }
    out_ms = heap_.top().expire_at_ms;
    return true;
}

std::vector<std::string> ExpiryIndex::pop_expired(uint64_t now_ms, size_t max_count) {
    std::vector<std::string> expired_keys;
    expired_keys.reserve(max_count);

    while (!heap_.empty() && expired_keys.size() < max_count) {
        prune_stale_top();
        if (heap_.empty()) break;

        const ExpiryEntry& top = heap_.top();
        if (top.expire_at_ms > now_ms) {
            // The earliest key hasn't expired yet, so none of the others have either!
            break;
        }

        std::string key = top.key;
        heap_.pop();
        key_to_time_.erase(key);
        expired_keys.push_back(std::move(key));
    }

    return expired_keys;
}

} // namespace kvforge