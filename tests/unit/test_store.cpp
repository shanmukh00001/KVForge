#include <gtest/gtest.h>
#include "store/hash_table.hpp"

using namespace kvforge;

// Test 1: Basic insert, update, and find
TEST(HashTableTest, BasicOperations) {
    HashTable ht(16);

    EXPECT_EQ(ht.size(), 0u);
    EXPECT_EQ(ht.find("key1"), nullptr);

    // Insert new key
    EXPECT_TRUE(ht.set("key1", "val1"));
    EXPECT_EQ(ht.size(), 1u);

    // Find key
    DictEntry* entry = ht.find("key1");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, "key1");
    EXPECT_EQ(entry->value, "val1");

    // Update existing key (returns false because it's an update, not a new key)
    EXPECT_FALSE(ht.set("key1", "val1_updated"));
    EXPECT_EQ(ht.size(), 1u);

    entry = ht.find("key1");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->value, "val1_updated");
}

// Test 2: Deletion
TEST(HashTableTest, Deletion) {
    HashTable ht(16);

    ht.set("k1", "v1");
    ht.set("k2", "v2");
    EXPECT_EQ(ht.size(), 2u);

    // Delete existing key
    EXPECT_TRUE(ht.del("k1"));
    EXPECT_EQ(ht.size(), 1u);
    EXPECT_EQ(ht.find("k1"), nullptr);
    EXPECT_NE(ht.find("k2"), nullptr);

    // Delete non-existent key
    EXPECT_FALSE(ht.del("k1"));
    EXPECT_FALSE(ht.del("non_existent"));
    EXPECT_EQ(ht.size(), 1u);
}

// Test 3: Table resizing (Load Factor > 1.0 triggers growth)
TEST(HashTableTest, AutoResize) {
    HashTable ht(16); // Initial buckets = 16
    EXPECT_EQ(ht.bucket_count(), 16u);

    // Insert 100 items to force multiple resizes (16 -> 32 -> 64 -> 128)
    for (int i = 0; i < 100; ++i) {
        std::string k = "key_" + std::to_string(i);
        std::string v = "val_" + std::to_string(i);
        ht.set(k, v);
    }

    EXPECT_EQ(ht.size(), 100u);
    EXPECT_GE(ht.bucket_count(), 128u); // Verify table grew!

    // Verify all 100 items are still intact and correct after resizing!
    for (int i = 0; i < 100; ++i) {
        std::string k = "key_" + std::to_string(i);
        std::string v = "val_" + std::to_string(i);
        DictEntry* e = ht.find(k);
        ASSERT_NE(e, nullptr) << "Failed to find " << k;
        EXPECT_EQ(e->value, v);
    }
}

// Test 4: Clear
TEST(HashTableTest, Clear) {
    HashTable ht(16);
    ht.set("a", "1");
    ht.set("b", "2");
    EXPECT_EQ(ht.size(), 2u);

    ht.clear();
    EXPECT_EQ(ht.size(), 0u);
    EXPECT_EQ(ht.find("a"), nullptr);
    EXPECT_EQ(ht.find("b"), nullptr);
}