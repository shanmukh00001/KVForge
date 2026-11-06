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

    // Disable copy
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Look up a key. Returns pointer to DictEntry, or nullptr if missing.
    DictEntry* find(std::string_view key);

    // Insert or update a key.
    // Returns true if new key was inserted, false if existing key was updated.
    bool set(std::string key, std::string value);

    // Delete a key. Returns true if key was deleted, false if key was not found.
    bool del(std::string_view key);

    // Number of active keys across both tables
    size_t size() const { return t0_.used + t1_.used; }

    // Total buckets allocated across both tables
    size_t bucket_count() const { 
        return (t0_.mask + 1) + (is_rehashing() ? (t1_.mask + 1) : 0); 
    }

    // Is the table currently undergoing incremental rehashing?
    bool is_rehashing() const { return rehash_idx_ != -1; }

    // Perform up to max_buckets steps of incremental rehashing.
    // Returns true if there is still more rehashing left to do, false if rehash finished or idle.
    bool rehash_step(size_t max_buckets = 1);

    // Clear all entries from both tables
    void clear();

    // 64-bit FNV-1a Hash
    static uint64_t hash_key(std::string_view key);

private:
    struct Table {
        std::vector<DictEntry*> buckets;
        size_t used = 0;
        size_t mask = 0;
    };

    void start_rehash(size_t new_size);
    void check_grow();

    Table t0_;                  // Primary table
    Table t1_;                  // Rehashing target table
    ptrdiff_t rehash_idx_ = -1; // -1 = inactive; >= 0 is current bucket in t0 being migrated
};

} // namespace kvforge