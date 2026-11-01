#include "store/hash_table.hpp"

namespace kvforge {

// Seeded 64-bit FNV-1a Hash Algorithm:
// Extremely fast, produces excellent uniform distribution across buckets
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
    // Ensure initial_size is at least 4, and is a power of 2
    size_t size = 16;
    while (size < initial_size) {
        size *= 2;
    }

    buckets_.resize(size, nullptr);
    mask_ = size - 1;
    count_ = 0;
}

HashTable::~HashTable() {
    clear();
}

void HashTable::clear() {
    // Traverse every bucket and delete all nodes in each linked list
    for (size_t i = 0; i < buckets_.size(); ++i) {
        DictEntry* current = buckets_[i];
        while (current != nullptr) {
            DictEntry* next = current->next;
            delete current; // Free heap memory
            current = next;
        }
        buckets_[i] = nullptr;
    }
    count_ = 0;
}

DictEntry* HashTable::find(std::string_view key) const {
    if (count_ == 0) return nullptr;

    // 1. Calculate bucket index using fast bitwise AND
    uint64_t h = hash_key(key);
    size_t idx = h & mask_;

    // 2. Walk the linked list in this bucket
    DictEntry* entry = buckets_[idx];
    while (entry != nullptr) {
        if (entry->key == key) {
            return entry; // Found!
        }
        entry = entry->next;
    }

    return nullptr; // Not found
}

bool HashTable::set(std::string key, std::string value) {
    // Check if table needs to grow before inserting
    resize_if_needed();

    uint64_t h = hash_key(key);
    size_t idx = h & mask_;

    // 1. Check if the key already exists in this bucket
    DictEntry* entry = buckets_[idx];
    while (entry != nullptr) {
        if (entry->key == key) {
            entry->value = std::move(value); // Update existing value
            return false; // Existing key updated
        }
        entry = entry->next;
    }

    // 2. Key does not exist: create a new entry
    DictEntry* new_entry = new DictEntry(std::move(key), std::move(value));

    // Insert at the HEAD of the bucket linked list (fastest: O(1))
    new_entry->next = buckets_[idx];
    buckets_[idx] = new_entry;
    count_++;

    return true; // New key inserted
}

bool HashTable::del(std::string_view key) {
    if (count_ == 0) return false;

    uint64_t h = hash_key(key);
    size_t idx = h & mask_;

    DictEntry* entry = buckets_[idx];
    DictEntry* prev = nullptr;

    while (entry != nullptr) {
        if (entry->key == key) {
            // Unlink entry from the chain
            if (prev == nullptr) {
                // Entry was the head of the list
                buckets_[idx] = entry->next;
            } else {
                // Entry was in the middle or end
                prev->next = entry->next;
            }

            delete entry; // Free memory!
            count_--;
            return true;
        }
        prev = entry;
        entry = entry->next;
    }

    return false; // Key not found
}

void HashTable::resize_if_needed() {
    // Resize when load factor reaches 1.0 (count_ >= bucket_count)
    if (count_ < buckets_.size()) {
        return;
    }

    size_t new_size = buckets_.size() * 2;
    std::vector<DictEntry*> new_buckets(new_size, nullptr);
    size_t new_mask = new_size - 1;

    // Migrate all entries to the new bucket array
    for (size_t i = 0; i < buckets_.size(); ++i) {
        DictEntry* entry = buckets_[i];
        while (entry != nullptr) {
            DictEntry* next = entry->next;

            // Recalculate index in the new larger bucket array
            size_t new_idx = hash_key(entry->key) & new_mask;

            // Insert at the head of the new bucket
            entry->next = new_buckets[new_idx];
            new_buckets[new_idx] = entry;

            entry = next;
        }
    }

    buckets_ = std::move(new_buckets);
    mask_ = new_mask;
}

} // namespace kvforge