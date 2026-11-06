#include <gtest/gtest.h>
#include "store/hash_table.hpp"
#include <unordered_map>
#include <random>
#include <string>

using namespace kvforge;

// AC-HT-02: Mid-rehash test. Operations must succeed regardless of whether
// an entry is in table 0 (unmigrated) or table 1 (already migrated).
TEST(IncrementalRehashTest, ACHT02_OperationsMidRehash) {
    HashTable table(16); // 16 initial buckets

    // Fill table with 16 keys (threshold reached: load factor = 1.0)
    for (int i = 0; i < 16; ++i) {
        table.set("key_" + std::to_string(i), "val_" + std::to_string(i));
    }
    EXPECT_EQ(table.size(), 16);
    EXPECT_FALSE(table.is_rehashing());

    // Inserting the 17th key triggers start_rehash (target size: 32 buckets)
    table.set("key_16", "val_16");
    EXPECT_TRUE(table.is_rehashing());
    EXPECT_EQ(table.size(), 17);

    // 1. Verify all 17 keys are findable mid-rehash
    for (int i = 0; i <= 16; ++i) {
        DictEntry* entry = table.find("key_" + std::to_string(i));
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value, "val_" + std::to_string(i));
    }

    // 2. Update an existing key mid-rehash
    table.set("key_5", "updated_val");
    DictEntry* updated = table.find("key_5");
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->value, "updated_val");

    // 3. Insert a brand new key mid-rehash (must go directly to target table)
    table.set("new_mid_key", "shiny_val");
    EXPECT_EQ(table.size(), 18);
    DictEntry* new_entry = table.find("new_mid_key");
    ASSERT_NE(new_entry, nullptr);
    EXPECT_EQ(new_entry->value, "shiny_val");

    // 4. Delete a key mid-rehash
    EXPECT_TRUE(table.del("key_0"));
    EXPECT_EQ(table.size(), 17);
    EXPECT_EQ(table.find("key_0"), nullptr);

    // 5. Complete all remaining rehash steps until finished
    while (table.is_rehashing()) {
        table.rehash_step(1);
    }
    EXPECT_FALSE(table.is_rehashing());

    // 6. Verify all remaining keys are still completely valid in the final table
    EXPECT_EQ(table.find("key_5")->value, "updated_val");
    EXPECT_EQ(table.find("new_mid_key")->value, "shiny_val");
    EXPECT_EQ(table.find("key_0"), nullptr);
}

// AC-HT-04: Randomized differential model test comparing HashTable against std::unordered_map
TEST(IncrementalRehashTest, ACHT04_RandomizedModelTest) {
    HashTable custom_table(16);
    std::unordered_map<std::string, std::string> reference_map;

    std::mt19937 rng(42); // Deterministic seed for reproducible testing
    std::uniform_int_distribution<int> op_dist(0, 2); // 0=SET, 1=GET, 2=DEL
    std::uniform_int_distribution<int> key_dist(0, 2000); // 2000 unique keys to force many rehashes

    // Run 50,000 mixed operations across growth and multiple rehashes
    for (int i = 0; i < 50000; ++i) {
        int op = op_dist(rng);
        std::string key = "key_" + std::to_string(key_dist(rng));

        if (op == 0) { // SET
            std::string val = "val_" + std::to_string(i);
            custom_table.set(key, val);
            reference_map[key] = val;
        } else if (op == 1) { // GET
            DictEntry* entry = custom_table.find(key);
            auto it = reference_map.find(key);

            if (it == reference_map.end()) {
                EXPECT_EQ(entry, nullptr);
            } else {
                ASSERT_NE(entry, nullptr);
                EXPECT_EQ(entry->value, it->second);
            }
        } else { // DEL
            bool custom_del = custom_table.del(key);
            bool ref_del = (reference_map.erase(key) > 0);
            EXPECT_EQ(custom_del, ref_del);
        }

        // Periodically do an idle rehash step just like the event loop does
        if (i % 10 == 0 && custom_table.is_rehashing()) {
            custom_table.rehash_step(5);
        }
    }

    // Finish any in-flight rehash
    while (custom_table.is_rehashing()) {
        custom_table.rehash_step(10);
    }

    // Final total size and content verification
    EXPECT_EQ(custom_table.size(), reference_map.size());
    for (const auto& [k, v] : reference_map) {
        DictEntry* entry = custom_table.find(k);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value, v);
    }
}