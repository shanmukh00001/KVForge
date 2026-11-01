#include <gtest/gtest.h>
#include "expiry/expiry_index.hpp"
#include "util/clock.hpp"

using namespace kvforge;

// Test 1: Basic setting and checking expiration
TEST(ExpiryIndexTest, BasicExpiry) {
    ExpiryIndex idx;
    uint64_t now = 1000;

    idx.set_expiry("k1", 1050); // expires at 1050
    idx.set_expiry("k2", 1100); // expires at 1100

    EXPECT_EQ(idx.size(), 2u);

    // At t = 1000, neither is expired
    EXPECT_FALSE(idx.is_expired("k1", now));
    EXPECT_FALSE(idx.is_expired("k2", now));

    // At t = 1060, k1 is expired, k2 is not
    EXPECT_TRUE(idx.is_expired("k1", 1060));
    EXPECT_FALSE(idx.is_expired("k2", 1060));

    // At t = 1150, both are expired
    EXPECT_TRUE(idx.is_expired("k1", 1150));
    EXPECT_TRUE(idx.is_expired("k2", 1150));
}

// Test 2: Min-Heap ordering and popping expired keys
TEST(ExpiryIndexTest, MinHeapOrdering) {
    ExpiryIndex idx;

    // Insert keys out of order
    idx.set_expiry("late", 5000);
    idx.set_expiry("early", 2000);
    idx.set_expiry("middle", 3000);

    uint64_t next_ms = 0;
    ASSERT_TRUE(idx.next_expiry_time(next_ms));
    EXPECT_EQ(next_ms, 2000u); // "early" must be at the top!

    // Pop at t = 2500 (only "early" should pop)
    auto popped = idx.pop_expired(2500, 10);
    ASSERT_EQ(popped.size(), 1u);
    EXPECT_EQ(popped[0], "early");

    // Next at the top must now be "middle" (3000)
    ASSERT_TRUE(idx.next_expiry_time(next_ms));
    EXPECT_EQ(next_ms, 3000u);
}

// Test 3: Lazy heap pruning when TTL is updated or removed (PERSIST)
TEST(ExpiryIndexTest, StaleEntryPruning) {
    ExpiryIndex idx;

    idx.set_expiry("k1", 1000);
    // Overwrite k1 with a later expiry
    idx.set_expiry("k1", 5000);

    // At t = 2000, the old 1000 entry should be pruned as stale!
    auto popped = idx.pop_expired(2000, 10);
    EXPECT_TRUE(popped.empty()); // Nothing should pop!

    uint64_t next_ms = 0;
    ASSERT_TRUE(idx.next_expiry_time(next_ms));
    EXPECT_EQ(next_ms, 5000u); // k1's active time is 5000

    // Remove k1 (simulate PERSIST)
    EXPECT_TRUE(idx.remove_expiry("k1"));
    EXPECT_FALSE(idx.next_expiry_time(next_ms)); // Heap is empty of valid keys
}