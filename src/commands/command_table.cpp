#include "commands/command_table.hpp"
#include <algorithm>

namespace kvforge {

std::string CommandTable::to_lower(std::string_view str) {
    std::string out;
    out.reserve(str.size());
    for (char c : str) {
        out += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

CommandTable::CommandTable() {
    // Commands will be registered here or in separate modules
}

void CommandTable::register_command(const CommandSpec& spec) {
    commands_[to_lower(spec.name)] = spec;
}

void CommandTable::dispatch(ServerContext& ctx, const Command& cmd, Connection& conn) {
    if (cmd.empty()) {
        return;
    }

    ctx.total_commands_processed++;

    std::string cmd_name = to_lower(cmd[0]);
    auto it = commands_.find(cmd_name);

    // 1. Unknown Command Check
    if (it == commands_.end()) {
        std::string err = "-ERR unknown command '" + cmd_name + "'\r\n";
        conn.append_write(err);
        return;
    }

    const CommandSpec& spec = it->second;
    int argc = static_cast<int>(cmd.size());

    // 2. Arity Check
    bool arity_valid = false;
    if (spec.arity > 0) {
        arity_valid = (argc == spec.arity);
    } else if (spec.arity < 0) {
        arity_valid = (argc >= -spec.arity);
    }

    if (!arity_valid) {
        std::string err = "-ERR wrong number of arguments for '" + cmd_name + "' command\r\n";
        conn.append_write(err);
        return;
    }

    // 3. Execute the handler!
    spec.handler(ctx, cmd, conn);
}

} // namespace kvforge