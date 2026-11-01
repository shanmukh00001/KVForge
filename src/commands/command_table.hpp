#pragma once


#include "protocol/resp_parser.hpp"
#include "protocol/resp_writer.hpp"
#include "net/connection.hpp"
#include "store/hash_table.hpp"
#include "expiry/expiry_index.hpp"
#include "util/clock.hpp"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
// Registers PING, ECHO, COMMAND, CONFIG, INFO

namespace kvforge {

// Forward declaration of server state/context
struct ServerContext;

// Function signature for every Redis command handler:
// void handler(ServerContext& ctx, const Command& cmd, Connection& conn);
using CommandHandler = std::function<void(ServerContext& ctx, const Command& cmd, Connection& conn)>;

struct CommandSpec {
    std::string name;
    int arity;               // positive = exact match, negative = minimum match (-N means >= N)
    CommandHandler handler;
    bool is_write = false;   // true if command modifies state (for AOF persistence)
};

class CommandTable {
public:
    CommandTable();

    // Register a command in the table
    void register_command(const CommandSpec& spec);

    // Look up and dispatch a command.
    // Handles case-insensitivity, unknown command errors, and arity errors.
    void dispatch(ServerContext& ctx, const Command& cmd, Connection& conn);

private:
    // Helper to lowercase a string
    static std::string to_lower(std::string_view str);

    // Map: lowercased_command_name -> CommandSpec
    std::unordered_map<std::string, CommandSpec> commands_;
};

// Global/Shared Server Context (stats, start time, etc.)
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
};


void register_server_commands(CommandTable& table);
// Registers SET, GET, DEL, EXISTS, MSET, MGET, DBSIZE, FLUSHALL
void register_string_commands(CommandTable& table);

// Registers EXPIRE, PEXPIRE, TTL, PTTL, PERSIST
void register_key_commands(CommandTable& table);


// Checks if key is expired. If expired, deletes it and returns true.
// If valid or key has no expiry, returns false.
inline bool check_and_expire(ServerContext& ctx, const std::string& key) {
    uint64_t now_ms = monotonic_time_ms();
    if (ctx.expiry_index.is_expired(key, now_ms)) {
        ctx.db.del(key);
        ctx.expiry_index.remove_expiry(key);
        ctx.expired_keys++;
        return true; // Key was expired and deleted!
    }
    return false; // Key is alive
}


} // namespace kvforge