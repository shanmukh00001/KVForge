#include <gtest/gtest.h>
#include "util/glob.hpp"
#include "commands/command_table.hpp"
#include "protocol/resp_parser.hpp"

using namespace kvforge;

// Test Glob Pattern Matching
TEST(GlobTest, PatternMatchingRules) {
    // 1. Literal matching
    EXPECT_TRUE(glob_match("hello", "hello"));
    EXPECT_FALSE(glob_match("hello", "world"));

    // 2. Wildcard *
    EXPECT_TRUE(glob_match("h*o", "hello"));
    EXPECT_TRUE(glob_match("h*o", "ho"));
    EXPECT_TRUE(glob_match("h*o", "heeeeeeello"));
    EXPECT_TRUE(glob_match("user:*", "user:1001"));
    EXPECT_TRUE(glob_match("*", "anything_at_all"));
    EXPECT_TRUE(glob_match("*", ""));
    EXPECT_FALSE(glob_match("user:*", "session:1001"));

    // 3. Single character ?
    EXPECT_TRUE(glob_match("h?llo", "hello"));
    EXPECT_TRUE(glob_match("h?llo", "hallo"));
    EXPECT_FALSE(glob_match("h?llo", "hllo"));
    EXPECT_FALSE(glob_match("h?llo", "heello"));

    // 4. Character sets [abc]
    EXPECT_TRUE(glob_match("h[ae]llo", "hello"));
    EXPECT_TRUE(glob_match("h[ae]llo", "hallo"));
    EXPECT_FALSE(glob_match("h[ae]llo", "hullo"));

    // 5. Character ranges [a-z]
    EXPECT_TRUE(glob_match("item:[0-9]", "item:5"));
    EXPECT_FALSE(glob_match("item:[0-9]", "item:a"));

    // 6. Negated sets [^a]
    EXPECT_TRUE(glob_match("h[^e]llo", "hallo"));
    EXPECT_FALSE(glob_match("h[^e]llo", "hello"));

    // 7. Escapes \*
    EXPECT_TRUE(glob_match(R"(hello\*)", "hello*"));
    EXPECT_FALSE(glob_match(R"(hello\*)", "helloworld"));
}

// AC-SRV-04: KEYS pattern command with glob and skipping expired keys
TEST(KeysTest, ACSRV04_KeysCommand) {
    ServerContext ctx;
    register_string_commands(ctx.command_table);
    register_key_commands(ctx.command_table);

    Connection dummy_conn(-1);

    // Seed keys
    ctx.command_table.dispatch(ctx, {"SET", "user:1", "alice"}, dummy_conn);
    ctx.command_table.dispatch(ctx, {"SET", "user:2", "bob"}, dummy_conn);
    ctx.command_table.dispatch(ctx, {"SET", "session:token", "xyz"}, dummy_conn);
    ctx.command_table.dispatch(ctx, {"SET", "temp_key", "secret", "EX", "1"}, dummy_conn);

    // 1. Match user:*
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"KEYS", "user:*"}, conn);
        Command reply;
        ParseResult res = RespParser::parse(conn.write_buf().data(), conn.write_buf().size(), reply);
        EXPECT_EQ(res.status, ParseStatus::OK);
        EXPECT_EQ(reply.size(), 2);
    }

    // 2. Match * (all keys)
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"KEYS", "*"}, conn);
        Command reply;
        ParseResult res = RespParser::parse(conn.write_buf().data(), conn.write_buf().size(), reply);
        EXPECT_EQ(res.status, ParseStatus::OK);
        EXPECT_EQ(reply.size(), 4);
    }

    // 3. Expired key must be pruned and omitted!
    ctx.expiry_index.set_expiry("temp_key", monotonic_time_ms() - 100); // Backdate to expired
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"KEYS", "temp*"}, conn);
        Command reply;
        ParseResult res = RespParser::parse(conn.write_buf().data(), conn.write_buf().size(), reply);
        EXPECT_EQ(res.status, ParseStatus::OK);
        EXPECT_EQ(reply.size(), 0); // temp_key was expired and pruned!
    }
}

// AC-INFO-01 & AC-INFO-02: Complete INFO sections and section filtering
TEST(InfoTest, ACINFO01_02_CompleteSectionsAndFiltering) {
    ServerContext ctx;
    ctx.start_time_sec = 1000;
    ctx.total_commands_processed = 42;
    ctx.total_connections_received = 5;
    ctx.connected_clients = 2;
    ctx.max_clients = 10000;
    register_server_commands(ctx.command_table);

    // 1. INFO with no args returns all sections
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"INFO"}, conn);
        EXPECT_NE(conn.write_buf().find("# Server"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("# Clients"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("# Memory"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("# Persistence"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("# Stats"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("# Keyspace"), std::string::npos);
    }

    // 2. INFO server returns ONLY server section
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"INFO", "server"}, conn);
        EXPECT_NE(conn.write_buf().find("# Server"), std::string::npos);
        EXPECT_EQ(conn.write_buf().find("# Clients"), std::string::npos);
        EXPECT_EQ(conn.write_buf().find("# Stats"), std::string::npos);
    }

    // 3. INFO clients returns ONLY clients section
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"INFO", "clients"}, conn);
        EXPECT_NE(conn.write_buf().find("connected_clients:2"), std::string::npos);
        EXPECT_NE(conn.write_buf().find("maxclients:10000"), std::string::npos);
        EXPECT_EQ(conn.write_buf().find("# Server"), std::string::npos);
    }

    // 4. Unknown section returns empty bulk string
    {
        Connection conn(-1);
        ctx.command_table.dispatch(ctx, {"INFO", "nonexistent_section"}, conn);
        EXPECT_EQ(conn.write_buf(), "$0\r\n\r\n");
    }
}
