#include "config/config.hpp"
#include "util/log.hpp"
#include "net/listener.hpp"
#include "net/event_loop.hpp"
#include "commands/command_table.hpp"
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    kvforge::Config config;
    std::string err_msg;

    kvforge::ConfigStatus status = kvforge::parse_args(argc, argv, config, err_msg);

    if (status == kvforge::ConfigStatus::EXIT_HELP || status == kvforge::ConfigStatus::EXIT_VER) {
        return 0; // User just wanted info, exit cleanly
    }

    if (status == kvforge::ConfigStatus::ERROR) {
        std::cerr << "Configuration error: " << err_msg << "\n";
        std::cerr << "Try 'kvforge --help' for more information.\n";
        return 1; // Non-zero exit code indicates an error in Linux
    }

    // Set the logging level chosen by the user
    kvforge::set_log_level(config.log_level);

    kvforge::log_info("KVForge v0.1.0 starting");
    // 1. Initialize Server Context & Command Table
    kvforge::ServerContext ctx;
    ctx.start_time_sec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    kvforge::register_server_commands(ctx.command_table);
    kvforge::register_string_commands(ctx.command_table);
    kvforge::register_key_commands(ctx.command_table);

    // 1. Initialize Listener
    kvforge::Listener listener;


    if (!listener.start(config.bind_address, config.port, err_msg)) {
        kvforge::log_error("Failed to start listener: " + err_msg);
        return 1;
    }

    kvforge::log_info("Ready to accept connections on " + config.bind_address + ":" + std::to_string(config.port));

     // 2. Initialize EventLoop
    kvforge::EventLoop loop(config.max_clients);
    loop.set_context(&ctx);

    if (!loop.init(err_msg)) {
        kvforge::log_error("Failed to initialize event loop: " + err_msg);
        return 1;
    }

    if (!loop.register_listener(&listener, err_msg)) {
        kvforge::log_error("Failed to register listener with event loop: " + err_msg);
        return 1;
    }
    // 3. Run the server! (Blocks here processing events until Ctrl+C)

    
    loop.run();
    kvforge::log_info("KVForge server shut down cleanly");
    // kvforge::log_info("KVForge v0.1.0 starting");
    // kvforge::log_info("Configured to listen on " + config.bind_address + ":" + std::to_string(config.port));
    // kvforge::log_info("Max clients: " + std::to_string(config.max_clients));

    return 0;
}