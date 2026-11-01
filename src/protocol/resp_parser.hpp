#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace kvforge {

enum class ParseStatus {
    OK,         // Successfully parsed a complete command
    NEED_MORE,  // Incomplete data in buffer, wait for more network packets
    ERROR       // Malformed protocol syntax
};

struct ParseResult {
    ParseStatus status;
    size_t consumed = 0;      // Number of bytes consumed from input buffer
    std::string error_msg;    // Error description if status == ERROR
};

// Represents a parsed command: a list of string arguments
// e.g. ["SET", "mykey", "myval"]
using Command = std::vector<std::string>;

class RespParser {
public:
    // Max limits to protect against Denial of Service (DoS) memory exhaustion
    static constexpr size_t MAX_INLINE_LENGTH = 64 * 1024;        // 64 KB
    static constexpr size_t MAX_BULK_STRING_LENGTH = 64* 1024 * 1024; //current project limit (prd limit)
    static constexpr size_t MAX_ARRAY_ELEMENTS = 1024 * 1024;    // 1 million elements

    // Parses the next command from the buffer [data, data + len]
    static ParseResult parse(const char* data, size_t len, Command& out_cmd);

private:
    static ParseResult parse_resp_array(const char* data, size_t len, Command& out_cmd);
    static ParseResult parse_inline(const char* data, size_t len, Command& out_cmd);
};

} // namespace kvforge