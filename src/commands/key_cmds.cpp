#include "util/glob.hpp"
#include "commands/command_table.hpp"
#include "persistence/aof.hpp"
#include "util/int_parse.hpp"
#include "util/clock.hpp"

namespace kvforge {

static void do_expire(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    int64_t sec = 0;

    if (!parse_int64(cmd[2], sec)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }

    if (sec > 0) {
        if (sec > INT64_MAX / 1000) {
            conn.append_write(RespWriter::error("ERR invalid expire time in 'expire' command"));
            return;
        }
        int64_t ms = sec * 1000;
        int64_t dummy = 0;
        if (!safe_add(static_cast<int64_t>(wall_clock_time_ms()), ms, dummy)) {
            conn.append_write(RespWriter::error("ERR invalid expire time in 'expire' command"));
            return;
        }
    }

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0));
        return;
    }

    if (sec <= 0) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        if (ctx.aof) {
            std::string aof_err;
            ctx.aof->append_record({"DEL", key}, aof_err);
        }
        conn.append_write(RespWriter::integer(1));
        return;
    }

    uint64_t expire_at = monotonic_time_ms() + (static_cast<uint64_t>(sec) * 1000);
    ctx.expiry_index.set_expiry(key, expire_at);

    if (ctx.aof) {
        std::string aof_err;
        uint64_t abs_wall = wall_clock_time_ms() + (static_cast<uint64_t>(sec) * 1000);
        ctx.aof->append_record({"PEXPIREAT", key, std::to_string(abs_wall)}, aof_err);
    }

    conn.append_write(RespWriter::integer(1));
}

static void do_pexpire(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    int64_t ms = 0;

    if (!parse_int64(cmd[2], ms)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }

    if (ms > 0) {
        int64_t dummy = 0;
        if (!safe_add(static_cast<int64_t>(wall_clock_time_ms()), ms, dummy)) {
            conn.append_write(RespWriter::error("ERR invalid expire time in 'pexpire' command"));
            return;
        }
    }

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0));
        return;
    }

    if (ms <= 0) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        if (ctx.aof) {
            std::string aof_err;
            ctx.aof->append_record({"DEL", key}, aof_err);
        }
        conn.append_write(RespWriter::integer(1));
        return;
    }

    uint64_t expire_at = monotonic_time_ms() + static_cast<uint64_t>(ms);
    ctx.expiry_index.set_expiry(key, expire_at);

    if (ctx.aof) {
        std::string aof_err;
        uint64_t abs_wall = wall_clock_time_ms() + static_cast<uint64_t>(ms);
        ctx.aof->append_record({"PEXPIREAT", key, std::to_string(abs_wall)}, aof_err);
    }

    conn.append_write(RespWriter::integer(1));
}

static void do_pexpireat(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    int64_t target_wall_ms = 0;

    if (!parse_int64(cmd[2], target_wall_ms)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0));
        return;
    }

    uint64_t now_wall = wall_clock_time_ms();
    if (target_wall_ms <= static_cast<int64_t>(now_wall)) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        if (ctx.aof) {
            std::string aof_err;
            ctx.aof->append_record({"DEL", key}, aof_err);
        }
        conn.append_write(RespWriter::integer(1));
        return;
    }

    uint64_t diff_ms = static_cast<uint64_t>(target_wall_ms - static_cast<int64_t>(now_wall));
    uint64_t expire_at = monotonic_time_ms() + diff_ms;
    ctx.expiry_index.set_expiry(key, expire_at);

    if (ctx.aof) {
        std::string aof_err;
        ctx.aof->append_record({"PEXPIREAT", key, std::to_string(target_wall_ms)}, aof_err);
    }

    conn.append_write(RespWriter::integer(1));
}

static void do_ttl(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    uint64_t expire_at = 0;
    if (!ctx.expiry_index.get_expiry(key, expire_at)) {
        conn.append_write(RespWriter::integer(-1));
        return;
    }

    uint64_t now_ms = monotonic_time_ms();
    if (now_ms >= expire_at) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    int64_t remaining_sec = static_cast<int64_t>((expire_at - now_ms + 999) / 1000);
    conn.append_write(RespWriter::integer(remaining_sec));
}

static void do_pttl(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    uint64_t expire_at = 0;
    if (!ctx.expiry_index.get_expiry(key, expire_at)) {
        conn.append_write(RespWriter::integer(-1));
        return;
    }

    uint64_t now_ms = monotonic_time_ms();
    if (now_ms >= expire_at) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    int64_t remaining_ms = static_cast<int64_t>(expire_at - now_ms);
    conn.append_write(RespWriter::integer(remaining_ms));
}

static void do_persist(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0));
        return;
    }

    if (ctx.expiry_index.remove_expiry(key)) {
        if (ctx.aof) {
            std::string aof_err;
            ctx.aof->append_record({"PERSIST", key}, aof_err);
        }
        conn.append_write(RespWriter::integer(1));
    } else {
        conn.append_write(RespWriter::integer(0));
    }
}

// Handler: KEYS pattern (O(N) keyspace scan with glob matching, skipping expired keys)
static void do_keys(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& pattern = cmd[1];
    auto all = ctx.db.all_keys();

    std::vector<std::string> matched;
    for (const auto& key : all) {
        if (check_and_expire(ctx, key)) {
            continue; // Skip expired key!
        }
        if (glob_match(pattern, key)) {
            matched.push_back(key);
        }
    }

    conn.append_write(RespWriter::array_header(matched.size()));
    for (const auto& k : matched) {
        conn.append_write(RespWriter::bulk_string(k));
    }
}

void register_key_commands(CommandTable& table) {
    table.register_command({"EXPIRE", 3, do_expire, true});
    table.register_command({"PEXPIRE", 3, do_pexpire, true});
    table.register_command({"PEXPIREAT", 3, do_pexpireat, true});
    table.register_command({"TTL", 2, do_ttl, false});
    table.register_command({"PTTL", 2, do_pttl, false});
    table.register_command({"PERSIST", 2, do_persist, true});
    table.register_command({"KEYS", 2, do_keys, false});
}

} // namespace kvforge
