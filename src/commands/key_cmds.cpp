#include "commands/command_table.hpp"
#include "util/int_parse.hpp"
#include "util/clock.hpp"

namespace kvforge {

// Handler: EXPIRE key seconds
static void do_expire(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    int64_t sec = 0;

    if (!parse_int64(cmd[2], sec)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0)); // Key does not exist
        return;
    }

    if (sec <= 0) {
        // Expire <= 0 deletes the key immediately!
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        conn.append_write(RespWriter::integer(1));
        return;
    }

    uint64_t expire_at = monotonic_time_ms() + (static_cast<uint64_t>(sec) * 1000);
    ctx.expiry_index.set_expiry(key, expire_at);
    conn.append_write(RespWriter::integer(1)); // Success
}

// Handler: PEXPIRE key milliseconds
static void do_pexpire(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    int64_t ms = 0;

    if (!parse_int64(cmd[2], ms)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0));
        return;
    }

    if (ms <= 0) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        conn.append_write(RespWriter::integer(1));
        return;
    }

    uint64_t expire_at = monotonic_time_ms() + static_cast<uint64_t>(ms);
    ctx.expiry_index.set_expiry(key, expire_at);
    conn.append_write(RespWriter::integer(1));
}

// Handler: TTL key (returns seconds)
static void do_ttl(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(-2)); // -2: Key does not exist
        return;
    }

    uint64_t expire_at = 0;
    if (!ctx.expiry_index.get_expiry(key, expire_at)) {
        conn.append_write(RespWriter::integer(-1)); // -1: Key exists but has no expiry
        return;
    }

    uint64_t now_ms = monotonic_time_ms();
    if (now_ms >= expire_at) {
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    int64_t remaining_sec = static_cast<int64_t>((expire_at - now_ms) / 1000);
    conn.append_write(RespWriter::integer(remaining_sec));
}

// Handler: PTTL key (returns milliseconds)
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
        conn.append_write(RespWriter::integer(-2));
        return;
    }

    int64_t remaining_ms = static_cast<int64_t>(expire_at - now_ms);
    conn.append_write(RespWriter::integer(remaining_ms));
}

// Handler: PERSIST key
static void do_persist(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key) || ctx.db.find(key) == nullptr) {
        conn.append_write(RespWriter::integer(0)); // Key missing
        return;
    }

    if (ctx.expiry_index.remove_expiry(key)) {
        conn.append_write(RespWriter::integer(1)); // Success: TTL removed
    } else {
        conn.append_write(RespWriter::integer(0)); // Key had no TTL to begin with
    }
}

void register_key_commands(CommandTable& table) {
    table.register_command({"EXPIRE", 3, do_expire, true});
    table.register_command({"PEXPIRE", 3, do_pexpire, true});
    table.register_command({"TTL", 2, do_ttl, false});
    table.register_command({"PTTL", 2, do_pttl, false});
    table.register_command({"PERSIST", 2, do_persist, true});
}

} // namespace kvforge