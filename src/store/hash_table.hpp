#pragma once

#include "store/entry.hpp"
#include <vector>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace kvforge {

class HashTable {
public:
    explicit HashTable(size_t initial_size = 16);
    ~HashTable();

    // Disable copy (we manage raw pointers in buckets!)
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Look up a key. Returns pointer to DictEntry, or nullptr if missing.
    DictEntry* find(std::string_view key) const;

    // Insert or update a key.
    // Returns true if new key was inserted, false if existing key was updated.
    bool set(std::string key, std::string value);

    // Delete a key. Returns true if key was deleted, false if key was not found.
    bool del(std::string_view key);

    // Number of active keys in the table
    size_t size() const { return count_; }

    // Number of buckets currently allocated
    size_t bucket_count() const { return mask_ + 1; }

    // Clear all entries from the table
    void clear();

    // Seeded 64-bit MurmurHash2 function
    static uint64_t hash_key(std::string_view key);

private:
    void resize_if_needed();

    std::vector<DictEntry*> buckets_; // Array of bucket head pointers
    size_t count_ = 0;                // Number of stored keys
    size_t mask_ = 0;                 // bucket_count - 1 (for fast bitwise AND indexing)
};

} // namespace kvforge