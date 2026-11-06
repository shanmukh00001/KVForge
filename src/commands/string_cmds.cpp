#include "commands/command_table.hpp"
#include "persistence/aof.hpp"
#include "util/int_parse.hpp"
#include <algorithm>

namespace kvforge {

static std::string to_upper(std::string_view str) {
    std::string out;
    out.reserve(str.size());
    for (char c : str) {
        out += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

static void do_get(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];

    if (check_and_expire(ctx, key)) {
        ctx.keyspace_misses++;
        conn.append_write(RespWriter::null_bulk_string());
        return;
    }

    DictEntry* entry = ctx.db.find(key);
    if (entry != nullptr) {
        ctx.keyspace_hits++;
        conn.append_write(RespWriter::bulk_string(entry->value));
    } else {
        ctx.keyspace_misses++;
        conn.append_write(RespWriter::null_bulk_string());
    }
}

static void do_set(ServerContext& ctx, const Command& cmd, Connection& conn) {
    const std::string& key = cmd[1];
    const std::string& value = cmd[2];

    bool opt_nx = false;
    bool opt_xx = false;
    int64_t expire_ms = -1;

    for (size_t i = 3; i < cmd.size(); ++i) {
        std::string opt = to_upper(cmd[i]);
        if (opt == "NX") {
            opt_nx = true;
        } else if (opt == "XX") {
            opt_xx = true;
        } else if (opt == "EX") {
            if (i + 1 >= cmd.size()) {
                conn.append_write(RespWriter::error("ERR syntax error"));
                return;
            }
            int64_t sec = 0;
            if (!parse_int64(cmd[++i], sec) || sec <= 0) {
                conn.append_write(RespWriter::error("ERR invalid expire time in 'set' command"));
                return;
            }
            expire_ms = sec * 1000;
        } else if (opt == "PX") {
            if (i + 1 >= cmd.size()) {
                conn.append_write(RespWriter::error("ERR syntax error"));
                return;
            }
            if (!parse_int64(cmd[++i], expire_ms) || expire_ms <= 0) {
                conn.append_write(RespWriter::error("ERR invalid expire time in 'set' command"));
                return;
            }
        } else {
            conn.append_write(RespWriter::error("ERR syntax error"));
            return;
        }
    }

    if (opt_nx && opt_xx) {
        conn.append_write(RespWriter::error("ERR syntax error"));
        return;
    }

    check_and_expire(ctx, key);
    DictEntry* existing = ctx.db.find(key);

    if (opt_nx && existing != nullptr) {
        conn.append_write(RespWriter::null_bulk_string());
        return;
    }

    if (opt_xx && existing == nullptr) {
        conn.append_write(RespWriter::null_bulk_string());
        return;
    }

    ctx.db.set(key, value);

    if (expire_ms > 0) {
        uint64_t abs_expire = monotonic_time_ms() + static_cast<uint64_t>(expire_ms);
        ctx.expiry_index.set_expiry(key, abs_expire);
    } else {
        ctx.expiry_index.remove_expiry(key);
    }

    // AOF Persistence: normalize relative EX/PX to absolute PXAT
    if (ctx.aof) {
        std::string aof_err;
        if (expire_ms > 0) {
            uint64_t abs_wall = wall_clock_time_ms() + static_cast<uint64_t>(expire_ms);
            ctx.aof->append_record({"SET", key, value, "PXAT", std::to_string(abs_wall)}, aof_err);
        } else {
            ctx.aof->append_record({"SET", key, value}, aof_err);
        }
    }

    conn.append_write(RespWriter::simple_string("OK"));
}

static void do_del(ServerContext& ctx, const Command& cmd, Connection& conn) {
    int64_t deleted_count = 0;
    std::vector<std::string> deleted_keys;

    for (size_t i = 1; i < cmd.size(); ++i) {
        check_and_expire(ctx, cmd[i]);
        if (ctx.db.del(cmd[i])) {
            ctx.expiry_index.remove_expiry(cmd[i]);
            deleted_count++;
            deleted_keys.push_back(cmd[i]);
        }
    }

    // AOF Persistence: only record keys that actually existed
    if (deleted_count > 0 && ctx.aof) {
        std::string aof_err;
        std::vector<std::string> aof_cmd = {"DEL"};
        aof_cmd.insert(aof_cmd.end(), deleted_keys.begin(), deleted_keys.end());
        ctx.aof->append_record(aof_cmd, aof_err);
    }

    conn.append_write(RespWriter::integer(deleted_count));
}

static void do_exists(ServerContext& ctx, const Command& cmd, Connection& conn) {
    int64_t exists_count = 0;
    for (size_t i = 1; i < cmd.size(); ++i) {
        if (!check_and_expire(ctx, cmd[i])) {
            if (ctx.db.find(cmd[i]) != nullptr) {
                exists_count++;
            }
        }
    }
    conn.append_write(RespWriter::integer(exists_count));
}

static void do_mset(ServerContext& ctx, const Command& cmd, Connection& conn) {
    size_t args_count = cmd.size() - 1;
    if (args_count % 2 != 0) {
        conn.append_write(RespWriter::error("ERR wrong number of arguments for 'mset' command"));
        return;
    }

    for (size_t i = 1; i < cmd.size(); i += 2) {
        ctx.db.set(cmd[i], cmd[i + 1]);
        ctx.expiry_index.remove_expiry(cmd[i]);
    }

    if (ctx.aof) {
        std::string aof_err;
        ctx.aof->append_record(cmd, aof_err);
    }

    conn.append_write(RespWriter::simple_string("OK"));
}

static void do_mget(ServerContext& ctx, const Command& cmd, Connection& conn) {
    size_t count = cmd.size() - 1;
    conn.append_write(RespWriter::array_header(count));

    for (size_t i = 1; i < cmd.size(); ++i) {
        if (check_and_expire(ctx, cmd[i])) {
            ctx.keyspace_misses++;
            conn.append_write(RespWriter::null_bulk_string());
        } else {
            DictEntry* entry = ctx.db.find(cmd[i]);
            if (entry != nullptr) {
                ctx.keyspace_hits++;
                conn.append_write(RespWriter::bulk_string(entry->value));
            } else {
                ctx.keyspace_misses++;
                conn.append_write(RespWriter::null_bulk_string());
            }
        }
    }
}

static void do_dbsize(ServerContext& ctx, const Command& /*cmd*/, Connection& conn) {
    conn.append_write(RespWriter::integer(static_cast<int64_t>(ctx.db.size())));
}

static void do_flushall(ServerContext& ctx, const Command& /*cmd*/, Connection& conn) {
    ctx.db.clear();
    ctx.expiry_index.clear();

    if (ctx.aof) {
        std::string aof_err;
        ctx.aof->append_record({"FLUSHALL"}, aof_err);
    }

    conn.append_write(RespWriter::simple_string("OK"));
}

static void apply_incr_by(ServerContext& ctx, const std::string& key, int64_t delta, const Command& cmd, Connection& conn) {
    check_and_expire(ctx, key);
    DictEntry* entry = ctx.db.find(key);
    int64_t current_val = 0;

    if (entry != nullptr) {
        if (!parse_int64(entry->value, current_val)) {
            conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
            return;
        }
    } else {
        current_val = 0;
    }

    int64_t result = 0;
    if (!safe_add(current_val, delta, result)) {
        conn.append_write(RespWriter::error("ERR increment or decrement would overflow"));
        return;
    }

    ctx.db.set(key, std::to_string(result));

    if (ctx.aof) {
        std::string aof_err;
        ctx.aof->append_record(cmd, aof_err);
    }

    conn.append_write(RespWriter::integer(result));
}

static void do_incr(ServerContext& ctx, const Command& cmd, Connection& conn) {
    apply_incr_by(ctx, cmd[1], 1, cmd, conn);
}

static void do_decr(ServerContext& ctx, const Command& cmd, Connection& conn) {
    apply_incr_by(ctx, cmd[1], -1, cmd, conn);
}

static void do_incrby(ServerContext& ctx, const Command& cmd, Connection& conn) {
    int64_t delta = 0;
    if (!parse_int64(cmd[2], delta)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }
    apply_incr_by(ctx, cmd[1], delta, cmd, conn);
}

static void do_decrby(ServerContext& ctx, const Command& cmd, Connection& conn) {
    int64_t delta = 0;
    if (!parse_int64(cmd[2], delta)) {
        conn.append_write(RespWriter::error("ERR value is not an integer or out of range"));
        return;
    }
    if (delta == INT64_MIN) {
        conn.append_write(RespWriter::error("ERR increment or decrement would overflow"));
        return;
    }
    apply_incr_by(ctx, cmd[1], -delta, cmd, conn);
}

void register_string_commands(CommandTable& table) {
    table.register_command({"GET", 2, do_get, false});
    table.register_command({"SET", -3, do_set, true});
    table.register_command({"DEL", -2, do_del, true});
    table.register_command({"EXISTS", -2, do_exists, false});
    table.register_command({"MSET", -3, do_mset, true});
    table.register_command({"MGET", -2, do_mget, false});
    table.register_command({"DBSIZE", 1, do_dbsize, false});
    table.register_command({"FLUSHALL", 1, do_flushall, true});

    table.register_command({"INCR", 2, do_incr, true});
    table.register_command({"DECR", 2, do_decr, true});
    table.register_command({"INCRBY", 3, do_incrby, true});
    table.register_command({"DECRBY", 3, do_decrby, true});
}

} // namespace kvforge
