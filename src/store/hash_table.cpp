#include "store/hash_table.hpp"
#include <algorithm>

namespace kvforge {

// 64-bit FNV-1a Hash Algorithm
uint64_t HashTable::hash_key(std::string_view key) {
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET_BASIS;
    for (char c : key) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= FNV_PRIME;
    }
    return hash;
}

HashTable::HashTable(size_t initial_size) {
    size_t size = 16;
    while (size < initial_size) {
        size *= 2;
    }

    t0_.buckets.assign(size, nullptr);
    t0_.mask = size - 1;
    t0_.used = 0;
    rehash_idx_ = -1; // Not rehashing initially
}

HashTable::~HashTable() {
    clear();
}

void HashTable::clear() {
    // Delete all nodes in t0
    for (size_t i = 0; i < t0_.buckets.size(); ++i) {
        DictEntry* current = t0_.buckets[i];
        while (current != nullptr) {
            DictEntry* next = current->next;
            delete current;
            current = next;
        }
        t0_.buckets[i] = nullptr;
    }
    t0_.used = 0;

    // Delete all nodes in t1 (if rehash was in progress)
    for (size_t i = 0; i < t1_.buckets.size(); ++i) {
        DictEntry* current = t1_.buckets[i];
        while (current != nullptr) {
            DictEntry* next = current->next;
            delete current;
            current = next;
        }
        t1_.buckets[i] = nullptr;
    }
    t1_.buckets.clear();
    t1_.used = 0;
    t1_.mask = 0;

    rehash_idx_ = -1;
}

void HashTable::start_rehash(size_t new_size) {
    size_t size = 16;
    while (size < new_size) {
        size *= 2;
    }

    t1_.buckets.assign(size, nullptr);
    t1_.mask = size - 1;
    t1_.used = 0;
    rehash_idx_ = 0; // Begin migrating from bucket 0 of t0
}

void HashTable::check_grow() {
    if (is_rehashing()) return;

    // Trigger growth when load factor reaches 1.0
    if (t0_.used >= t0_.buckets.size()) {
        start_rehash(t0_.buckets.size() * 2);
    }
}

bool HashTable::rehash_step(size_t max_buckets) {
    if (!is_rehashing()) return false;

    // Guard against scanning millions of consecutive empty buckets in one go
    size_t empty_visits = max_buckets * 10;

    while (max_buckets > 0 && static_cast<size_t>(rehash_idx_) < t0_.buckets.size() && empty_visits > 0) {
        DictEntry* entry = t0_.buckets[rehash_idx_];

        if (entry == nullptr) {
            rehash_idx_++;
            empty_visits--;
            continue;
        }

        // Migrate every entry in this bucket chain over to t1
        while (entry != nullptr) {
            DictEntry* next = entry->next;

            // Calculate index in new table t1 using its bitmask
            size_t new_idx = hash_key(entry->key) & t1_.mask;

            // Link at the head of t1's bucket
            entry->next = t1_.buckets[new_idx];
            t1_.buckets[new_idx] = entry;

            t0_.used--;
            t1_.used++;
            entry = next;
        }

        t0_.buckets[rehash_idx_] = nullptr;
        rehash_idx_++;
        max_buckets--;
    }

    // Check if t0 is completely empty and migration is finished
    if (static_cast<size_t>(rehash_idx_) >= t0_.buckets.size()) {
        t0_ = std::move(t1_);
        t1_ = Table{};
        rehash_idx_ = -1; // Finished!
        return false;
    }

    return true; // Still more rehashing to do
}

DictEntry* HashTable::find(std::string_view key) {
    if (size() == 0) return nullptr;

    // Step 1 bucket forward if rehashing
    if (is_rehashing()) {
        rehash_step(1);
    }

    uint64_t h = hash_key(key);

    // 1. Search primary table t0
    if (!t0_.buckets.empty()) {
        size_t idx0 = h & t0_.mask;
        DictEntry* entry = t0_.buckets[idx0];
        while (entry != nullptr) {
            if (entry->key == key) {
                return entry;
            }
            entry = entry->next;
        }
    }

    // 2. If rehashing, search target table t1
    if (is_rehashing() && !t1_.buckets.empty()) {
        size_t idx1 = h & t1_.mask;
        DictEntry* entry = t1_.buckets[idx1];
        while (entry != nullptr) {
            if (entry->key == key) {
                return entry;
            }
            entry = entry->next;
        }
    }

    return nullptr;
}

bool HashTable::set(std::string key, std::string value) {
    if (is_rehashing()) {
        rehash_step(1);
    }

    uint64_t h = hash_key(key);

    // 1. Check if the key already exists in t0
    if (!t0_.buckets.empty()) {
        size_t idx0 = h & t0_.mask;
        DictEntry* entry = t0_.buckets[idx0];
        while (entry != nullptr) {
            if (entry->key == key) {
                entry->value = std::move(value);
                return false; // Updated existing key
            }
            entry = entry->next;
        }
    }

    // 2. Check if the key already exists in t1 (if rehashing)
    if (is_rehashing() && !t1_.buckets.empty()) {
        size_t idx1 = h & t1_.mask;
        DictEntry* entry = t1_.buckets[idx1];
        while (entry != nullptr) {
            if (entry->key == key) {
                entry->value = std::move(value);
                return false; // Updated existing key
            }
            entry = entry->next;
        }
    }

    // 3. Brand new key: check if we need to start rehashing
    check_grow();

    // If rehashing, always insert into t1; otherwise insert into t0
    Table& target = is_rehashing() ? t1_ : t0_;
    size_t idx = h & target.mask;

    DictEntry* new_entry = new DictEntry(std::move(key), std::move(value));
    new_entry->next = target.buckets[idx];
    target.buckets[idx] = new_entry;
    target.used++;

    return true; // Inserted new key
}

bool HashTable::del(std::string_view key) {
    if (size() == 0) return false;

    if (is_rehashing()) {
        rehash_step(1);
    }

    uint64_t h = hash_key(key);

    // 1. Search and delete from t0
    if (!t0_.buckets.empty()) {
        size_t idx0 = h & t0_.mask;
        DictEntry* entry = t0_.buckets[idx0];
        DictEntry* prev = nullptr;

        while (entry != nullptr) {
            if (entry->key == key) {
                if (prev == nullptr) {
                    t0_.buckets[idx0] = entry->next;
                } else {
                    prev->next = entry->next;
                }
                delete entry;
                t0_.used--;
                return true;
            }
            prev = entry;
            entry = entry->next;
        }
    }

    // 2. Search and delete from t1 (if rehashing)
    if (is_rehashing() && !t1_.buckets.empty()) {
        size_t idx1 = h & t1_.mask;
        DictEntry* entry = t1_.buckets[idx1];
        DictEntry* prev = nullptr;

        while (entry != nullptr) {
            if (entry->key == key) {
                if (prev == nullptr) {
                    t1_.buckets[idx1] = entry->next;
                } else {
                    prev->next = entry->next;
                }
                delete entry;
                t1_.used--;
                return true;
            }
            prev = entry;
            entry = entry->next;
        }
    }

    return false;
}

} // namespace kvforge


