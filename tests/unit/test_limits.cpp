#include <gtest/gtest.h>
#include "protocol/resp_parser.hpp"
#include "net/connection.hpp"

using namespace kvforge;

// AC-LIM-01: Bulk string declared with length above 64 MiB gets rejected
TEST(LimitsTest, ACLIM01_BulkStringExceedsLimit) {
    Command cmd;
    // 64 MiB is 67,108,864 bytes. We test 67,108,865 bytes (64 MiB + 1 byte)
    std::string payload = "*1\r\n$67108865\r\n";
    ParseResult res = RespParser::parse(payload.data(), payload.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::ERROR);
    EXPECT_EQ(res.error_msg, "Protocol error: invalid bulk length");
}

// AC-LIM-02: Multibulk array count exceeding 1,048,576 gets rejected
TEST(LimitsTest, ACLIM02_MultibulkExceedsLimit) {
    Command cmd;
    // Limit is 1,048,576 elements. We declare 1,048,577
    std::string payload = "*1048577\r\n";
    ParseResult res = RespParser::parse(payload.data(), payload.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::ERROR);
    EXPECT_EQ(res.error_msg, "Protocol error: invalid multibulk length");
}

// AC-LIM-04: Output buffer exceeding 64 MiB is detected
TEST(LimitsTest, ACLIM04_OutputBufferExceeded) {
    Connection conn(-1); // dummy socket fd for unit test
    EXPECT_FALSE(conn.is_output_buf_exceeded());

    // 64 MiB is 64 * 1024 * 1024 bytes = 67,108,864 bytes
    // We append 64 MiB + 1 byte to verify detection
    std::string large_chunk(64 * 1024 * 1024 + 1, 'X');
    conn.append_write(large_chunk);

    EXPECT_TRUE(conn.is_output_buf_exceeded());
}

// Memory Reclamation: Buffer capacity shrinks back down after large bursts
TEST(LimitsTest, BufferCompactionAndShrinking) {
    Connection conn(-1);

    // Expand write buffer to 128 KiB (> 64 KiB threshold)
    std::string burst(128 * 1024, 'A');
    conn.append_write(burst);
    EXPECT_GE(conn.write_buf().capacity(), 128 * 1024);

    // Consume all bytes
    conn.consume_write_buf(conn.write_buf().size());
    EXPECT_TRUE(conn.write_buf().empty());

    // Capacity must have shrunk back down (<= 64 KiB or 0)
    EXPECT_LE(conn.write_buf().capacity(), Connection::BUFFER_SHRINK_THRESHOLD);
}

// AC-PRO-07: Malformed array syntax (*abc\r\n) returns protocol error
TEST(LimitsTest, ACPRO07_MalformedArrayLength) {
    Command cmd;
    std::string payload = "*abc\r\n$4\r\nPING\r\n";
    ParseResult res = RespParser::parse(payload.data(), payload.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::ERROR);
    EXPECT_EQ(res.error_msg, "Protocol error: invalid multibulk length");
}

// Inline command exceeding 64 KiB without newline is rejected
TEST(LimitsTest, InlineCommandTooLong) {
    Command cmd;
    std::string long_inline(65 * 1024, 'P'); // 65 KB without newline
    ParseResult res = RespParser::parse(long_inline.data(), long_inline.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::ERROR);
    EXPECT_EQ(res.error_msg, "Protocol error: inline command too long");
}

// AC-PRO-09: Connection destroyed mid-command cleans up buffers safely
TEST(LimitsTest, ACPRO09_ClientDisconnectMidCommand) {
    {
        Connection conn(-1);
        std::string partial = "*2\r\n$3\r\nGET\r\n$5\r\nhe"; // Incomplete
        conn.read_buf().append(partial);
        Command cmd;
        ParseResult res = RespParser::parse(conn.read_buf().data(), conn.read_buf().size(), cmd);
        EXPECT_EQ(res.status, ParseStatus::NEED_MORE);
        // conn goes out of scope here; memory must cleanly deallocate without leaks
    }
    SUCCEED();
}