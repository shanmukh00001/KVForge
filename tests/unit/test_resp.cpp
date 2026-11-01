#include <gtest/gtest.h>
#include "protocol/resp_parser.hpp"
#include "protocol/resp_writer.hpp"

using namespace kvforge;

// Test 1: Verify RespWriter output formats
TEST(RespWriterTest, Serialization) {
    EXPECT_EQ(RespWriter::simple_string("OK"), "+OK\r\n");
    EXPECT_EQ(RespWriter::simple_string("PONG"), "+PONG\r\n");
    EXPECT_EQ(RespWriter::error("ERR unknown command"), "-ERR unknown command\r\n");
    EXPECT_EQ(RespWriter::integer(42), ":42\r\n");
    EXPECT_EQ(RespWriter::integer(-1), ":-1\r\n");
    EXPECT_EQ(RespWriter::bulk_string("hello"), "$5\r\nhello\r\n");
    EXPECT_EQ(RespWriter::bulk_string(""), "$0\r\n\r\n");
    EXPECT_EQ(RespWriter::null_bulk_string(), "$-1\r\n");
    EXPECT_EQ(RespWriter::array_header(3), "*3\r\n");
    EXPECT_EQ(RespWriter::null_array(), "*-1\r\n");
}

// Test 2: Verify CRLF sanitization in RespWriter to prevent injection attacks
TEST(RespWriterTest, SanitizeCRLF) {
    std::string dirty = "ERR line1\r\nline2\nline3";
    EXPECT_EQ(RespWriter::error(dirty), "-ERR line1  line2 line3\r\n");
}

// Test 3: Parse standard RESP array command: *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
TEST(RespParserTest, ParseStandardArray) {
    std::string wire = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    Command cmd;
    ParseResult res = RespParser::parse(wire.data(), wire.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::OK);
    EXPECT_EQ(res.consumed, wire.size());
    ASSERT_EQ(cmd.size(), 3u);
    EXPECT_EQ(cmd[0], "SET");
    EXPECT_EQ(cmd[1], "foo");
    EXPECT_EQ(cmd[2], "bar");
}

// Test 4: Parse inline command (plain text from telnet or nc)
TEST(RespParserTest, ParseInline) {
    std::string wire = "PING\r\n";
    Command cmd;
    ParseResult res = RespParser::parse(wire.data(), wire.size(), cmd);

    EXPECT_EQ(res.status, ParseStatus::OK);
    EXPECT_EQ(res.consumed, 6u);
    ASSERT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0], "PING");

    // Inline with multiple arguments and extra spaces
    std::string wire2 = "  SET   key123   value456  \r\n";
    ParseResult res2 = RespParser::parse(wire2.data(), wire2.size(), cmd);

    EXPECT_EQ(res2.status, ParseStatus::OK);
    ASSERT_EQ(cmd.size(), 3u);
    EXPECT_EQ(cmd[0], "SET");
    EXPECT_EQ(cmd[1], "key123");
    EXPECT_EQ(cmd[2], "value456");
}

// Test 5: Partial / chunked reads (NEED_MORE)
TEST(RespParserTest, IncompleteData) {
    Command cmd;

    // Incomplete inline line (no \r\n yet)
    std::string partial_inline = "PIN";
    ParseResult r1 = RespParser::parse(partial_inline.data(), partial_inline.size(), cmd);
    EXPECT_EQ(r1.status, ParseStatus::NEED_MORE);
    EXPECT_EQ(r1.consumed, 0u);

    // Incomplete RESP array header
    std::string partial_array = "*3\r\n$3\r\nSET\r\n$3\r\n";
    ParseResult r2 = RespParser::parse(partial_array.data(), partial_array.size(), cmd);
    EXPECT_EQ(r2.status, ParseStatus::NEED_MORE);

    // Bulk string payload incomplete (says length is 5, but only gave 3 bytes)
    std::string partial_payload = "*1\r\n$5\r\nhel";
    ParseResult r3 = RespParser::parse(partial_payload.data(), partial_payload.size(), cmd);
    EXPECT_EQ(r3.status, ParseStatus::NEED_MORE);
}

// Test 6: Pipelining (multiple commands in one network buffer)
TEST(RespParserTest, Pipelining) {
    std::string wire = "PING\r\nPING\r\n";
    Command cmd;

    // Parse 1st command
    ParseResult r1 = RespParser::parse(wire.data(), wire.size(), cmd);
    EXPECT_EQ(r1.status, ParseStatus::OK);
    EXPECT_EQ(r1.consumed, 6u);
    ASSERT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0], "PING");

    // Advance buffer by r1.consumed
    const char* remaining_data = wire.data() + r1.consumed;
    size_t remaining_len = wire.size() - r1.consumed;

    // Parse 2nd command
    ParseResult r2 = RespParser::parse(remaining_data, remaining_len, cmd);
    EXPECT_EQ(r2.status, ParseStatus::OK);
    EXPECT_EQ(r2.consumed, 6u);
    ASSERT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0], "PING");
}

// Test 7: Malformed syntax error detection
TEST(RespParserTest, ProtocolErrors) {
    Command cmd;

    // Invalid array count (not a number)
    std::string bad_count = "*abc\r\n";
    ParseResult r1 = RespParser::parse(bad_count.data(), bad_count.size(), cmd);
    EXPECT_EQ(r1.status, ParseStatus::ERROR);

    // Expected '$' but got something else
    std::string bad_dollar = "*1\r\n!3\r\nfoo\r\n";
    ParseResult r2 = RespParser::parse(bad_dollar.data(), bad_dollar.size(), cmd);
    EXPECT_EQ(r2.status, ParseStatus::ERROR);
}