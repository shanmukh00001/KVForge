#pragma once

#include "util/log.hpp"
#include <cstdint>
#include <string>

namespace kvforge {

struct Config {
  std::string bind_address = "127.0.0.1";
  uint16_t port = 6380;
  uint32_t max_clients = 10000;
  LogLevel log_level = LogLevel::INFO;

  // Persistence (AOF) settings
  bool appendonly = false;
  std::string appendfsync = "everysec";
  std::string appendfilename = "kvforge.aof";

  // Expiry engine budget in microseconds
  uint32_t expire_budget_us = 1000;
};

// Result of trying to parse command-line arguments
enum class ConfigStatus {
  OK,        // Configuration parsed successfully, start the server
  EXIT_HELP, // User typed --help, printed help, exit gracefully
  EXIT_VER,  // User typed --version, printed version, exit gracefully
  ERROR      // Invalid flag or bad value, printed error, exit with failure
};

// Function that parses argc and argv from main()
ConfigStatus parse_args(int argc, char *argv[], Config &out_config,
                        std::string &out_error);

// Print the exact --help message
void print_help();

// Print version message
void print_version();

} // namespace kvforge

/*Usage: kvforge [OPTIONS]

Options:
  --bind <ip>             Bind address (default: 127.0.0.1)
  --port <port>           TCP port (default: 6380)
  --max-clients <n>       Max simultaneous connections (default: 10000)
  --appendonly <yes|no>   Enable AOF persistence (default: no)
  --appendfsync <policy>  AOF fsync policy: always, everysec, no (default:
everysec)
  --appendfilename <name> AOF filename (default: kvforge.aof)
  --loglevel <level>      Log level: debug, info, warn, error (default: info)
  --expire-budget-us <us> Max active-expiry time per tick in us (default: 1000)
  --help                  Show this help and exit
  --version               Show version and exit*/