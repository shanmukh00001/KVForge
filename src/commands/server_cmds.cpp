#include "commands/command_table.hpp"
#include <chrono>

namespace kvforge {

// Handler: PING [message]
static void do_ping(ServerContext& /*ctx*/, const Command& cmd, Connection& conn) {
    if (cmd.size() == 1) {
        conn.append_write(RespWriter::simple_string("PONG"));
    } else {
        // PING with an echo argument returns as a bulk string
        conn.append_write(RespWriter::bulk_string(cmd[1]));
    }
}

// Handler: ECHO message
static void do_echo(ServerContext& /*ctx*/, const Command& cmd, Connection& conn) {
    conn.append_write(RespWriter::bulk_string(cmd[1]));
}

// Handler: COMMAND (stub for redis-cli)
static void do_command(ServerContext& /*ctx*/, const Command& /*cmd*/, Connection& conn) {
    // Return an empty array so redis-cli connects cleanly without errors
    conn.append_write(RespWriter::array_header(0));
}

// Handler: CONFIG GET parameter
static void do_config(ServerContext& /*ctx*/, const Command& /*cmd*/, Connection& conn) {
    conn.append_write(RespWriter::array_header(0));
}

// Handler: INFO [section]
static void do_info(ServerContext& ctx, const Command& /*cmd*/, Connection& conn) {
    auto now_sec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    uint64_t uptime = (ctx.start_time_sec > 0) ? (now_sec - ctx.start_time_sec) : 0;

    std::string info;
    info += "# Server\r\n";
    info += "kvforge_version:0.1.0\r\n";
    info += "uptime_in_seconds:" + std::to_string(uptime) + "\r\n";
    info += "\r\n# Stats\r\n";
    info += "total_commands_processed:" + std::to_string(ctx.total_commands_processed) + "\r\n";
    info += "total_connections_received:" + std::to_string(ctx.total_connections_received) + "\r\n";
    info += "\r\n# Keyspace\r\n";
    info += "db0:keys=" + std::to_string(ctx.db.size()) + "\r\n";
    info += "rehashing:" + std::string(ctx.db.is_rehashing() ? "1" : "0") + "\r\n";
    conn.append_write(RespWriter::bulk_string(info));
}

// Function to register all server commands into the CommandTable
void register_server_commands(CommandTable& table) {
    table.register_command({"PING", -1, do_ping, false});
    table.register_command({"ECHO", 2, do_echo, false});
    table.register_command({"COMMAND", -1, do_command, false});
    table.register_command({"CONFIG", -2, do_config, false});
    table.register_command({"INFO", -1, do_info, false});
}

} // namespace kvforge