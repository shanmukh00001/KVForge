#include <gtest/gtest.h>
#include "util/int_parse.hpp"
#include <climits>

using namespace kvforge;

// Test 1: Standard integer parsing
TEST(IntParseTest, ValidNumbers) {
    int64_t val = 0;

    EXPECT_TRUE(parse_int64("0", val));
    EXPECT_EQ(val, 0);

    EXPECT_TRUE(parse_int64("42", val));
    EXPECT_EQ(val, 42);

    EXPECT_TRUE(parse_int64("-100", val));
    EXPECT_EQ(val, -100);

    EXPECT_TRUE(parse_int64("+500", val));
    EXPECT_EQ(val, 500);

    // Max and Min 64-bit boundaries
    EXPECT_TRUE(parse_int64("9223372036854775807", val));
    EXPECT_EQ(val, INT64_MAX);

    EXPECT_TRUE(parse_int64("-9223372036854775808", val));
    EXPECT_EQ(val, INT64_MIN);
}

// Test 2: Invalid inputs and overflow
TEST(IntParseTest, InvalidInputs) {
    int64_t val = 0;

    EXPECT_FALSE(parse_int64("", val));
    EXPECT_FALSE(parse_int64("-", val));
    EXPECT_FALSE(parse_int64("+", val));
    EXPECT_FALSE(parse_int64("123a", val));
    EXPECT_FALSE(parse_int64("hello", val));
    EXPECT_FALSE(parse_int64(" 123", val)); // Whitespace not allowed

    // Overflow: INT64_MAX + 1
    EXPECT_FALSE(parse_int64("9223372036854775808", val));
    // Underflow: INT64_MIN - 1
    EXPECT_FALSE(parse_int64("-9223372036854775809", val));
}

// Test 3: Safe addition and overflow detection
TEST(IntParseTest, SafeAdd) {
    int64_t res = 0;

    EXPECT_TRUE(safe_add(10, 20, res));
    EXPECT_EQ(res, 30);

    EXPECT_TRUE(safe_add(-10, -20, res));
    EXPECT_EQ(res, -30);

    // Overflow check
    EXPECT_FALSE(safe_add(INT64_MAX, 1, res));
    EXPECT_FALSE(safe_add(INT64_MAX, 100, res));

    // Underflow check
    EXPECT_FALSE(safe_add(INT64_MIN, -1, res));
    EXPECT_FALSE(safe_add(INT64_MIN, -100, res));
}