#include "util/log.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>


namespace kvforge {

// Default log level is INFO
static LogLevel g_current_level = LogLevel::INFO;

void set_log_level(LogLevel level) { g_current_level = level; }

LogLevel get_log_level() { return g_current_level; }

bool parse_log_level(std::string_view str, LogLevel &out_level) {
  std::string lower(str);
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower == "debug") {
    out_level = LogLevel::DEBUG;
    return true;
  } else if (lower == "info") {
    out_level = LogLevel::INFO;
    return true;
  } else if (lower == "warn" || lower == "warning") {
    out_level = LogLevel::WARN;
    return true;
  } else if (lower == "error") {
    out_level = LogLevel::ERROR;
    return true;
  }
  return false;
}

static void emit_log(LogLevel level, const char *level_tag,
                     std::string_view msg) {
  if (level < g_current_level) {
    return; // Ignore messages below our active log level
  }

  // Get current time with millisecond precision
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);

  std::tm tm_buf;
  gmtime_r(&now_c, &tm_buf); // UTC time

  char time_str[32];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &tm_buf);

  // Format: YYYY-MM-DDTHH:MM:SS.mmmZ [LEVEL] message
  std::cerr << time_str << '.' << std::setfill('0') << std::setw(3)
            << ms.count() << "Z " << level_tag << ' ' << msg << '\n';
}

void log_debug(std::string_view msg) {
  emit_log(LogLevel::DEBUG, "[DEBUG]", msg);
}
void log_info(std::string_view msg) { emit_log(LogLevel::INFO, "[INFO]", msg); }
void log_warn(std::string_view msg) { emit_log(LogLevel::WARN, "[WARN]", msg); }
void log_error(std::string_view msg) {
  emit_log(LogLevel::ERROR, "[ERROR]", msg);
}

} // namespace kvforge