#pragma once // #pragma once tells the compiler: "Only read this file once per
             // compilation unit, even if it gets included multiple times

#include <string>
#include <string_view>

namespace kvforge {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };
// An enumeration (enum) is a user-defined type consisting of named integer
// constants.

// Set minimum level to print (messages below this level are ignored)
void set_log_level(LogLevel level);
// Definition: A non-owning "read-only window" looking at an existing string in
// memory. It just stores two tiny numbers: a pointer to the start of the
// characters and a length.
LogLevel get_log_level();
// With std::string_view: Zero copies, zero memory allocations! It simply points
// directly to "Hello" in program memory

// Helper to convert string (e.g. "debug", "info") to LogLevel enum
bool parse_log_level(std::string_view str, LogLevel &out_level);

// Core logging functions
void log_debug(std::string_view msg);
void log_info(std::string_view msg);
void log_warn(std::string_view msg);
void log_error(std::string_view msg);

} // namespace kvforge