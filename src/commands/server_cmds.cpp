#include "commands/command_table.hpp"
#include "persistence/aof.hpp"
#include <chrono>
#include <unistd.h>
#include <iomanip>
#include <sstream>

namespace kvforge {

static std::string to_lower_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

static std::string format_bytes_human(size_t bytes) {
    std::ostringstream ss;
    if (bytes < 1024) {
        ss << bytes << "B";
    } else if (bytes < 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / 1024.0) << "K";
    } else if (bytes < 1024 * 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << "M";
    } else {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << "G";
    }
    return ss.str();
}

static void do_ping(ServerContext& /*ctx*/, const Command& cmd, Connection& conn) {
    if (cmd.size() == 1) {
        conn.append_write(RespWriter::simple_string("PONG"));
    } else {
        conn.append_write(RespWriter::bulk_string(cmd[1]));
    }
}

static void do_echo(ServerContext& /*ctx*/, const Command& cmd, Connection& conn) {
    conn.append_write(RespWriter::bulk_string(cmd[1]));
}

static void do_command(ServerContext& /*ctx*/, const Command& /*cmd*/, Connection& conn) {
    conn.append_write(RespWriter::array_header(0));
}

static void do_config(ServerContext& /*ctx*/, const Command& /*cmd*/, Connection& conn) {
    conn.append_write(RespWriter::array_header(0));
}



static void do_info(ServerContext& ctx, const Command& cmd, Connection& conn) {
    auto now_sec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    uint64_t uptime_sec = (ctx.start_time_sec > 0) ? (now_sec - ctx.start_time_sec) : 0;
    uint64_t uptime_days = uptime_sec / 86400;

    std::string sec_filter;
    if (cmd.size() >= 2) {
        sec_filter = to_lower_str(cmd[1]);
    }

    std::string s_server;
    s_server += "# Server\r\n";
    s_server += "kvforge_version:0.2.0\r\n";
    s_server += "os:Linux\r\n";
    s_server += "process_id:" + std::to_string(::getpid()) + "\r\n";
    s_server += "tcp_port:" + std::to_string(ctx.tcp_port) + "\r\n";
    s_server += "uptime_in_seconds:" + std::to_string(uptime_sec) + "\r\n";
    s_server += "uptime_in_days:" + std::to_string(uptime_days) + "\r\n";

    std::string s_clients;
    s_clients += "# Clients\r\n";
    s_clients += "connected_clients:" + std::to_string(ctx.connected_clients) + "\r\n";
    s_clients += "maxclients:" + std::to_string(ctx.max_clients) + "\r\n";

    size_t mem_bytes = ctx.db.approx_memory_usage();
    std::string s_memory;
    s_memory += "# Memory\r\n";
    s_memory += "used_memory:" + std::to_string(mem_bytes) + "\r\n";
    s_memory += "used_memory_human:" + format_bytes_human(mem_bytes) + "\r\n";

    std::string s_persistence;
    s_persistence += "# Persistence\r\n";
    s_persistence += "aof_enabled:" + std::string(ctx.aof ? "1" : "0") + "\r\n";
    s_persistence += "aof_current_size:" + std::to_string(ctx.aof ? ctx.aof->current_size() : 0) + "\r\n";
    s_persistence += "aof_fsync_policy:" + ctx.aof_fsync_policy + "\r\n";

    std::string s_stats;
    s_stats += "# Stats\r\n";
    s_stats += "total_connections_received:" + std::to_string(ctx.total_connections_received) + "\r\n";
    s_stats += "total_commands_processed:" + std::to_string(ctx.total_commands_processed) + "\r\n";
    s_stats += "keyspace_hits:" + std::to_string(ctx.keyspace_hits) + "\r\n";
    s_stats += "keyspace_misses:" + std::to_string(ctx.keyspace_misses) + "\r\n";
    s_stats += "expired_keys:" + std::to_string(ctx.expired_keys) + "\r\n";
    s_stats += "rehashing:" + std::string(ctx.db.is_rehashing() ? "1" : "0") + "\r\n";

    std::string s_keyspace;
    s_keyspace += "# Keyspace\r\n";
    s_keyspace += "db0:keys=" + std::to_string(ctx.db.size()) + 
                  ",expires=" + std::to_string(ctx.expiry_index.size()) + "\r\n";

    // Section filtering per API_SPECIFICATION.md §6
    if (!sec_filter.empty()) {
        if (sec_filter == "server") {
            conn.append_write(RespWriter::bulk_string(s_server));
        } else if (sec_filter == "clients") {
            conn.append_write(RespWriter::bulk_string(s_clients));
        } else if (sec_filter == "memory") {
            conn.append_write(RespWriter::bulk_string(s_memory));
        } else if (sec_filter == "persistence") {
            conn.append_write(RespWriter::bulk_string(s_persistence));
        } else if (sec_filter == "stats") {
            conn.append_write(RespWriter::bulk_string(s_stats));
        } else if (sec_filter == "keyspace") {
            conn.append_write(RespWriter::bulk_string(s_keyspace));
        } else {
            // Unknown section returns empty bulk string per spec
            conn.append_write(RespWriter::bulk_string(""));
        }
        return;
    }

    // Default: return all sections
    std::string full_info = s_server + "\r\n" +
                            s_clients + "\r\n" +
                            s_memory + "\r\n" +
                            s_persistence + "\r\n" +
                            s_stats + "\r\n" +
                            s_keyspace;
    conn.append_write(RespWriter::bulk_string(full_info));
}

void register_server_commands(CommandTable& table) {
    table.register_command({"PING", -1, do_ping, false});
    table.register_command({"ECHO", 2, do_echo, false});
    table.register_command({"COMMAND", -1, do_command, false});
    table.register_command({"CONFIG", -2, do_config, false});
    table.register_command({"INFO", -1, do_info, false});
        }

} // namespace kvforge
