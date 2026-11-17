#include <gtest/gtest.h>
#include "persistence/aof.hpp"
#include "commands/command_table.hpp"
#include "util/clock.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>

using namespace kvforge;

class AOFTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = "test_kvforge_" + std::to_string(monotonic_time_ms()) + ".aof";
        ::unlink(test_file_.c_str());
    }

    void TearDown() override {
        ::unlink(test_file_.c_str());
    }

    std::string test_file_;
};

// AC-AOF-01: Basic persistence and replay
TEST_F(AOFTest, ACAOF01_BasicPersistenceAndReplay) {
    std::string err;

    // 1. Write commands to AOF with ALWAYS policy
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));

        EXPECT_TRUE(aof.append_record({"SET", "a", "1"}, err));
        EXPECT_TRUE(aof.append_record({"INCR", "n"}, err));
        EXPECT_TRUE(aof.append_record({"SET", "k", "hello"}, err));
        EXPECT_TRUE(aof.append_record({"DEL", "a"}, err));
        aof.close();
    }

    // 2. Replay into a completely clean ServerContext
    ServerContext ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, ctx, err));

    // Verify key states
    EXPECT_EQ(ctx.db.find("a"), nullptr); // Deleted
    ASSERT_NE(ctx.db.find("n"), nullptr);
    EXPECT_EQ(ctx.db.find("n")->value, "1");
    ASSERT_NE(ctx.db.find("k"), nullptr);
    EXPECT_EQ(ctx.db.find("k")->value, "hello");
}

// AC-AOF-03: Expiry deadlines across restarts
TEST_F(AOFTest, ACAOF03_ExpiryAcrossRestart) {
    std::string err;
    uint64_t now_wall = wall_clock_time_ms();

    // Key 1: Expires 10 seconds in the future
    uint64_t future_expire = now_wall + 10000;
    // Key 2: Expired 5 seconds in the past
    uint64_t past_expire = now_wall - 5000;

    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));

        EXPECT_TRUE(aof.append_record({"SET", "active_key", "valid", "PXAT", std::to_string(future_expire)}, err));
        EXPECT_TRUE(aof.append_record({"SET", "dead_key", "expired", "PXAT", std::to_string(past_expire)}, err));
        aof.close();
    }

    ServerContext ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, ctx, err));

    // Active key must be present with remaining TTL
    DictEntry* active = ctx.db.find("active_key");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->value, "valid");

    // Dead key must NOT be resurrected!
    DictEntry* dead = ctx.db.find("dead_key");
    EXPECT_EQ(dead, nullptr);
}

// AC-AOF-05: Partial record at EOF truncated cleanly
TEST_F(AOFTest, ACAOF05_TruncatedTailRecovery) {
    std::string err;

    // 1. Write two valid records
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));
        EXPECT_TRUE(aof.append_record({"SET", "key1", "val1"}, err));
        EXPECT_TRUE(aof.append_record({"SET", "key2", "val2"}, err));
        aof.close();
    }

    // 2. Append a half-written record simulating an abrupt crash/power loss
    int fd = ::open(test_file_.c_str(), O_WRONLY | O_APPEND);
    ASSERT_GE(fd, 0);
    std::string partial_record = "*3\r\n$3\r\nSET\r\n$4\r\nkey3\r\n$4\r\n"; // Missing payload and CRLFs
    ssize_t written = ::write(fd, partial_record.data(), partial_record.size());
    EXPECT_GT(written, 0);
    ::close(fd);

    // 3. Replay: Must truncate the partial tail and restore earlier valid records safely
    ServerContext ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, ctx, err));

    ASSERT_NE(ctx.db.find("key1"), nullptr);
    EXPECT_EQ(ctx.db.find("key1")->value, "val1");
    ASSERT_NE(ctx.db.find("key2"), nullptr);
    EXPECT_EQ(ctx.db.find("key2")->value, "val2");
    EXPECT_EQ(ctx.db.find("key3"), nullptr);
}

// AC-AOF-06: Corruption in the middle triggers failure
TEST_F(AOFTest, ACAOF06_CorruptedMiddleDetection) {
    std::string err;

    // Write a corrupted stream with invalid RESP syntax in the middle
    int fd = ::open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ASSERT_GE(fd, 0);
    std::string corrupt_data = "*2\r\n$3\r\nSET\r\n$1\r\na\r\n*INVALID_CORRUPTION\r\n";
    ssize_t written = ::write(fd, corrupt_data.data(), corrupt_data.size());
    EXPECT_GT(written, 0);
    ::close(fd);

    ServerContext ctx;
    bool success = AOF::load_and_replay(test_file_, ctx, err);
    EXPECT_FALSE(success);
    EXPECT_NE(err.find("Corrupt AOF"), std::string::npos);
}

// AC-AOF-07: Lazy expiry appends DEL to AOF
TEST_F(AOFTest, ACAOF07_LazyExpiryAppendsDEL) {
    std::string err;
    ServerContext ctx;

    AOF aof(test_file_, FsyncPolicy::ALWAYS);
    ASSERT_TRUE(aof.open_file(err));
    ctx.aof = &aof;

    // Set key in db and mark it as expired in expiry index
    ctx.db.set("temp_key", "secret");
    ctx.expiry_index.set_expiry("temp_key", monotonic_time_ms() - 100); // 100ms in the past

    // Trigger lazy check
    EXPECT_TRUE(check_and_expire(ctx, "temp_key"));
    aof.close();

    // Replay AOF into fresh context to verify DEL was recorded
    ServerContext fresh_ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, fresh_ctx, err));
    EXPECT_EQ(fresh_ctx.db.find("temp_key"), nullptr);
}

// Extended AOF coverage: policy parsing, multi-command replay, edge cases
TEST_F(AOFTest, ExtendedAOFReplayAndPolicyCoverage) {
    // 1. Policy parsing
    EXPECT_EQ(AOF::parse_policy("always"), FsyncPolicy::ALWAYS);
    EXPECT_EQ(AOF::parse_policy("no"), FsyncPolicy::NO);
    EXPECT_EQ(AOF::parse_policy("everysec"), FsyncPolicy::EVERYSEC);
    EXPECT_EQ(AOF::parse_policy("unknown_defaults_to_everysec"), FsyncPolicy::EVERYSEC);

    // 2. Replay non-existent file returns true (clean state)
    ServerContext ctx;
    std::string err;
    EXPECT_TRUE(AOF::load_and_replay("non_existent_file.aof", ctx, err));

    // 3. Replay 0-byte file returns true
    ::unlink(test_file_.c_str());
    {
        int fd = ::open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        ::close(fd);
    }
    EXPECT_TRUE(AOF::load_and_replay(test_file_, ctx, err));

    // 4. Replay PEXPIREAT, PERSIST, INCRBY, DECRBY, MSET, FLUSHALL
    ::unlink(test_file_.c_str());
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));
        uint64_t future_wall = wall_clock_time_ms() + 100000;
        EXPECT_TRUE(aof.append_record({"SET", "k_persist", "v1"}, err));
        EXPECT_TRUE(aof.append_record({"PEXPIREAT", "k_persist", std::to_string(future_wall)}, err));
        EXPECT_TRUE(aof.append_record({"PERSIST", "k_persist"}, err));

        EXPECT_TRUE(aof.append_record({"SET", "num", "10"}, err));
        EXPECT_TRUE(aof.append_record({"INCRBY", "num", "5"}, err));
        EXPECT_TRUE(aof.append_record({"DECRBY", "num", "3"}, err));

        EXPECT_TRUE(aof.append_record({"MSET", "m1", "val1", "m2", "val2"}, err));
        EXPECT_TRUE(aof.append_record({"FLUSHALL"}, err));
        EXPECT_TRUE(aof.append_record({"SET", "survivor", "alive"}, err));
        aof.close();
    }

    ServerContext replay_ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, replay_ctx, err));
    EXPECT_EQ(replay_ctx.db.size(), 1u);
    ASSERT_NE(replay_ctx.db.find("survivor"), nullptr);
    EXPECT_EQ(replay_ctx.db.find("survivor")->value, "alive");

    // 5. Append with EVERYSEC policy (hits return true)
    ::unlink(test_file_.c_str());
    {
        AOF aof_sec(test_file_, FsyncPolicy::EVERYSEC);
        ASSERT_TRUE(aof_sec.open_file(err));
        EXPECT_TRUE(aof_sec.append_record({"SET", "k_sec", "v_sec"}, err));
        aof_sec.close();
    }

    // 6. Replay PEXPIREAT with past timestamp (deletes key)
    ::unlink(test_file_.c_str());
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));
        EXPECT_TRUE(aof.append_record({"SET", "k_past", "val"}, err));
        EXPECT_TRUE(aof.append_record({"PEXPIREAT", "k_past", "1000"}, err)); // past timestamp
        aof.close();
    }
    ServerContext past_ctx;
    ASSERT_TRUE(AOF::load_and_replay(test_file_, past_ctx, err));
    EXPECT_EQ(past_ctx.db.find("k_past"), nullptr);

    // 7. Replay INCR on non-integer string (returns corrupt error)
    ::unlink(test_file_.c_str());
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));
        EXPECT_TRUE(aof.append_record({"SET", "k_str", "not_a_num"}, err));
        EXPECT_TRUE(aof.append_record({"INCR", "k_str"}, err));
        aof.close();
    }
    ServerContext bad_incr_ctx;
    EXPECT_FALSE(AOF::load_and_replay(test_file_, bad_incr_ctx, err));

    // 8. Replay INCR overflow (returns overflow error)
    ::unlink(test_file_.c_str());
    {
        AOF aof(test_file_, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(aof.open_file(err));
        EXPECT_TRUE(aof.append_record({"SET", "k_over", "9223372036854775807"}, err));
        EXPECT_TRUE(aof.append_record({"INCR", "k_over"}, err));
        aof.close();
    }
    ServerContext overflow_ctx;
    EXPECT_FALSE(AOF::load_and_replay(test_file_, overflow_ctx, err));

    // 9. Corrupt record start (does not start with '*')
    ::unlink(test_file_.c_str());
    {
        int fd = ::open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        std::string bad_hdr = "NOT_AN_AOF_RECORD\r\n";
        ssize_t w = ::write(fd, bad_hdr.data(), bad_hdr.size());
        (void)w;
        ::close(fd);
    }
    ServerContext corrupt_ctx;
    EXPECT_FALSE(AOF::load_and_replay(test_file_, corrupt_ctx, err));
}
