#pragma once

#include "protocol/resp_parser.hpp"
#include "protocol/resp_writer.hpp"
#include "net/connection.hpp"
#include "store/hash_table.hpp"
#include "expiry/expiry_index.hpp"
#include "persistence/aof.hpp"
#include "util/clock.hpp"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <unordered_map>

namespace kvforge {

struct ServerContext;

using CommandHandler = std::function<void(ServerContext& ctx, const Command& cmd, Connection& conn)>;

struct CommandSpec {
    std::string name;
    int arity;
    CommandHandler handler;
    bool is_write = false;
};

class CommandTable {
public:
    CommandTable();
    void register_command(const CommandSpec& spec);
    void dispatch(ServerContext& ctx, const Command& cmd, Connection& conn);

private:
    static std::string to_lower(std::string_view str);
    std::unordered_map<std::string, CommandSpec> commands_;
};

struct ServerContext {
    uint64_t start_time_sec = 0;
    uint64_t total_commands_processed = 0;
    uint64_t total_connections_received = 0;
    uint64_t keyspace_hits = 0;
    uint64_t expired_keys = 0;
    uint64_t keyspace_misses = 0;
    HashTable db;
    ExpiryIndex expiry_index; 
    CommandTable command_table;
    AOF* aof = nullptr;
};

void register_server_commands(CommandTable& table);
void register_string_commands(CommandTable& table);
void register_key_commands(CommandTable& table);

inline bool check_and_expire(ServerContext& ctx, const std::string& key) {
    uint64_t now_ms = monotonic_time_ms();
    if (ctx.expiry_index.is_expired(key, now_ms)) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        if (ctx.aof) {
            std::string aof_err;
            ctx.aof->append_record({"DEL", key}, aof_err);
        }
        return true;
    }
    return false;
}

} // namespace kvforge
