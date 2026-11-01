#include "config/config.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>


namespace kvforge {

void print_version() { std::cout << "kvforge 0.1.0\n"; }

void print_help() {
  std::cout
      << "Usage: kvforge [OPTIONS]\n\n"
         "Options:\n"
         "  --bind <ip>             Bind address (default: 127.0.0.1)\n"
         "  --port <port>           TCP port (default: 6380)\n"
         "  --max-clients <n>       Max simultaneous connections (default: "
         "10000)\n"
         "  --appendonly <yes|no>   Enable AOF persistence (default: no)\n"
         "  --appendfsync <policy>  AOF fsync policy: always, everysec, no "
         "(default: everysec)\n"
         "  --appendfilename <name> AOF filename (default: kvforge.aof)\n"
         "  --loglevel <level>      Log level: debug, info, warn, error "
         "(default: info)\n"
         "  --expire-budget-us <us> Max active-expiry time per tick in us "
         "(default: 1000)\n"
         "  --help                  Show this help and exit\n"
         "  --version               Show version and exit\n";
}

ConfigStatus parse_args(int argc, char *argv[], Config &out_config,
                        std::string &out_error) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      print_help();
      return ConfigStatus::EXIT_HELP;
    }

    if (arg == "--version" || arg == "-v") {
      print_version();
      return ConfigStatus::EXIT_VER;
    }

    // Helper lambda to check if a value was provided after the flag
    auto get_next_val = [&](std::string_view flag_name) -> const char * {
      if (i + 1 >= argc) {
        out_error = "Missing value for flag: " + std::string(flag_name);
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--bind") {
      const char *val = get_next_val("--bind");
      if (!val)
        return ConfigStatus::ERROR;
      out_config.bind_address = val;
    } else if (arg == "--port") {
      const char *val = get_next_val("--port");
      if (!val)
        return ConfigStatus::ERROR;
      try {
        int p = std::stoi(val);
        if (p < 1 || p > 65535) {
          out_error = "Invalid port (must be 1-65535): " + std::string(val);
          return ConfigStatus::ERROR;
        }
        out_config.port = static_cast<uint16_t>(p);
      } catch (...) {
        out_error = "Invalid port number: " + std::string(val);
        return ConfigStatus::ERROR;
      }
    } else if (arg == "--max-clients") {
      const char *val = get_next_val("--max-clients");
      if (!val)
        return ConfigStatus::ERROR;
      try {
        int mc = std::stoi(val);
        if (mc <= 0) {
          out_error = "max-clients must be positive: " + std::string(val);
          return ConfigStatus::ERROR;
        }
        out_config.max_clients = static_cast<uint32_t>(mc);
      } catch (...) {
        out_error = "Invalid max-clients: " + std::string(val);
        return ConfigStatus::ERROR;
      }
    } else if (arg == "--loglevel") {
      const char *val = get_next_val("--loglevel");
      if (!val)
        return ConfigStatus::ERROR;
      LogLevel lvl;
      if (!parse_log_level(val, lvl)) {
        out_error = "Invalid loglevel (must be debug, info, warn, error): " +
                    std::string(val);
        return ConfigStatus::ERROR;
      }
      out_config.log_level = lvl;
    } else if (arg == "--appendonly") {
      const char *val = get_next_val("--appendonly");
      if (!val)
        return ConfigStatus::ERROR;
      std::string s(val);
      if (s == "yes") {
        out_config.appendonly = true;
      } else if (s == "no") {
        out_config.appendonly = false;
      } else {
        out_error = "Invalid appendonly value (must be 'yes' or 'no'): " + s;
        return ConfigStatus::ERROR;
      }
    } else if (arg == "--appendfsync") {
      const char *val = get_next_val("--appendfsync");
      if (!val)
        return ConfigStatus::ERROR;
      std::string s(val);
      if (s == "always" || s == "everysec" || s == "no") {
        out_config.appendfsync = s;
      } else {
        out_error = "Invalid appendfsync value (must be 'always', 'everysec', "
                    "or 'no'): " +
                    s;
        return ConfigStatus::ERROR;
      }
    } else if (arg == "--appendfilename") {
      const char *val = get_next_val("--appendfilename");
      if (!val)
        return ConfigStatus::ERROR;
      out_config.appendfilename = val;
    } else if (arg == "--expire-budget-us") {
      const char *val = get_next_val("--expire-budget-us");
      if (!val)
        return ConfigStatus::ERROR;
      try {
        int b = std::stoi(val);
        if (b <= 0) {
          out_error = "expire-budget-us must be positive: " + std::string(val);
          return ConfigStatus::ERROR;
        }
        out_config.expire_budget_us = static_cast<uint32_t>(b);
      } catch (...) {
        out_error = "Invalid expire-budget-us: " + std::string(val);
        return ConfigStatus::ERROR;
      }
    } else {
      out_error = "Unknown option: " + std::string(arg);
      return ConfigStatus::ERROR;
    }
  }

  return ConfigStatus::OK;
}

} // namespace kvforge