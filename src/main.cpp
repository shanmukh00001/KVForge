#include "persistence/aof.hpp"
#include "config/config.hpp"
#include "util/log.hpp"
#include <sys/resource.h>
#include "net/listener.hpp"
#include "net/event_loop.hpp"
#include "commands/command_table.hpp"
#include <iostream>
#include <chrono>
#include <memory>

int main(int argc, char* argv[]) {
    kvforge::Config config;
    std::string err_msg;

    kvforge::ConfigStatus status = kvforge::parse_args(argc, argv, config, err_msg);

    if (status == kvforge::ConfigStatus::EXIT_HELP || status == kvforge::ConfigStatus::EXIT_VER) {
        return 0; // User just wanted info, exit cleanly
    }

    if (status == kvforge::ConfigStatus::ERROR) {
        std::cerr << "Configuration error: " << err_msg << std::endl;
        std::cerr << "Try 'kvforge --help' for more information." << std::endl;
        return 1; // Non-zero exit code indicates an error in Linux
    }

    // Set the logging level chosen by the user
    kvforge::set_log_level(config.log_level);

    // Check and adjust OS file descriptor limits (RLIMIT_NOFILE)
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
        rlim_t needed = static_cast<rlim_t>(config.max_clients) + 32;
        if (limit.rlim_cur < needed) {
            rlim_t old_limit = limit.rlim_cur;
            limit.rlim_cur = std::min(needed, limit.rlim_max);
            if (setrlimit(RLIMIT_NOFILE, &limit) == 0) {
                kvforge::log_info("Increased file descriptor limit from " + 
                                  std::to_string(old_limit) + " to " + std::to_string(limit.rlim_cur));
            } else {
                kvforge::log_warn("File descriptor limit (" + std::to_string(limit.rlim_cur) + 
                                  ") is below max_clients (" + std::to_string(config.max_clients) + 
                                  "). Run 'ulimit -n " + std::to_string(needed) + "' to raise it.");
            }
        }
    }

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

    // 2. AOF Persistence Initialization
    std::unique_ptr<kvforge::AOF> aof;
    if (config.appendonly) {
        if (!kvforge::AOF::load_and_replay(config.appendfilename, ctx, err_msg)) {
            kvforge::log_error("AOF recovery failed: " + err_msg);
            return 1;
        }

        aof = std::make_unique<kvforge::AOF>(
            config.appendfilename,
            kvforge::AOF::parse_policy(config.appendfsync)
        );
        if (!aof->open_file(err_msg)) {
            kvforge::log_error("Failed to open AOF file: " + err_msg);
            return 1;
        }
        ctx.aof = aof.get();
        kvforge::log_info("AOF persistence enabled (policy: " + config.appendfsync + 
                          ", file: " + config.appendfilename + ")");
    }

    // 3. Initialize Listener
    kvforge::Listener listener;
    if (!listener.start(config.bind_address, config.port, err_msg)) {
        kvforge::log_error("Failed to start listener: " + err_msg);
        return 1;
    }

    kvforge::log_info("Ready to accept connections on " + config.bind_address + ":" + std::to_string(config.port));

    // 4. Initialize EventLoop
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

    // 5. Run the server! (Blocks here processing events until Ctrl+C)
    loop.run();
    kvforge::log_info("KVForge server shut down cleanly");

    return 0;
}
