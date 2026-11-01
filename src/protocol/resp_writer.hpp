#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace kvforge {

class RespWriter {
public:
    // Formats a Simple String: +<str>\r\n (e.g. "+OK\r\n", "+PONG\r\n")
    static std::string simple_string(std::string_view str);

    // Formats an Error: -<msg>\r\n (e.g. "-ERR unknown command\r\n")
    static std::string error(std::string_view msg);

    // Formats an Integer: :<num>\r\n (e.g. ":42\r\n", ":-1\r\n")
    static std::string integer(int64_t num);

    // Formats a Bulk String: $<len>\r\n<data>\r\n (e.g. "$5\r\nhello\r\n")
    static std::string bulk_string(std::string_view data);

    // Formats a Null Bulk String: $-1\r\n (used when a key does not exist)
    static std::string null_bulk_string();

    // Formats an Array header: *<count>\r\n
    static std::string array_header(size_t count);

    // Formats a Null Array: *-1\r\n
    static std::string null_array();

    // Helper to sanitize strings: replaces any stray \r or \n inside error/simple messages
    // with spaces to prevent RESP response injection attacks!
    static std::string sanitize_crlf(std::string_view input);
};

} // namespace kvforge