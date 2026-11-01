#include "protocol/resp_parser.hpp"
#include <cstring>
#include <algorithm>

namespace kvforge {

// Helper: Finds the position of "\r\n" in a memory buffer.
// Returns nullptr if not found.
static const char* find_crlf(const char* start, const char* end) {
    for (const char* p = start; p + 1 < end; ++p) {
        if (*p == '\r' && *(p + 1) == '\n') {
            return p;
        }
    }
    return nullptr;
}

// Helper: Safely parses a signed integer string (e.g. "3" or "-1")
static bool parse_int(const char* start, const char* end, int64_t& out_val) {
    if (start >= end) return false;
    
    bool negative = false;
    const char* p = start;
    if (*p == '-') {
        negative = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    
    if (p >= end) return false;

    int64_t val = 0;
    while (p < end) {
        if (*p < '0' || *p > '9') return false; // Non-digit character
        int digit = *p - '0';
        // Check for 64-bit integer overflow
        if (val > (INT64_MAX - digit) / 10) {
            return false;
        }
        val = val * 10 + digit;
        ++p;
    }

    out_val = negative ? -val : val;
    return true;
}

ParseResult RespParser::parse(const char* data, size_t len, Command& out_cmd) {
    out_cmd.clear();
    if (len == 0) {
        return {ParseStatus::NEED_MORE, 0, ""};
    }

    // In RESP2, array commands start with '*'
    // Everything else is treated as an inline plain-text command (e.g. PING\r\n)
    if (data[0] == '*') {
        return parse_resp_array(data, len, out_cmd);
    } else {
        return parse_inline(data, len, out_cmd);
    }
}

ParseResult RespParser::parse_inline(const char* data, size_t len, Command& out_cmd) {
    const char* end = data + len;
    
    // Find either \r\n or just \n (to support both standard clients and Linux netcat)
    const char* newline_pos = nullptr;
    size_t newline_bytes = 0;

    for (const char* p = data; p < end; ++p) {
        if (*p == '\n') {
            newline_pos = p;
            newline_bytes = 1;
            // If preceded by \r, it's a 2-byte \r\n
            if (p > data && *(p - 1) == '\r') {
                newline_pos = p - 1;
                newline_bytes = 2;
            }
            break;
        }
    }

    if (!newline_pos) {
        if (len > MAX_INLINE_LENGTH) {
            return {ParseStatus::ERROR, 0, "Protocol error: inline command too long"};
        }
        return {ParseStatus::NEED_MORE, 0, ""};
    }

    size_t line_len = newline_pos - data;
    size_t total_consumed = line_len + newline_bytes;

    // Tokenize space-separated words
    size_t i = 0;
    while (i < line_len) {
        // Skip leading spaces
        while (i < line_len && (data[i] == ' ' || data[i] == '\t')) {
            ++i;
        }
        if (i >= line_len) break;

        // Find end of current token
        size_t token_start = i;
        while (i < line_len && data[i] != ' ' && data[i] != '\t') {
            ++i;
        }

        out_cmd.emplace_back(data + token_start, i - token_start);
    }

    if (out_cmd.empty()) {
        return {ParseStatus::NEED_MORE, total_consumed, ""};
    }

    return {ParseStatus::OK, total_consumed, ""};
}
ParseResult RespParser::parse_resp_array(const char* data, size_t len, Command& out_cmd) {
    const char* start = data;
    const char* end = data + len;

    // 1. Parse the array header: "*<count>\r\n"
    const char* crlf = find_crlf(start, end);
    if (!crlf) {
        return {ParseStatus::NEED_MORE, 0, ""};
    }

    int64_t element_count = 0;
    if (!parse_int(start + 1, crlf, element_count)) {
        return {ParseStatus::ERROR, 0, "Protocol error: invalid multibulk length"};
    }

    if (element_count <= 0) {
        // Null or empty array (*0 or *-1)
        size_t consumed = (crlf - start) + 2;
        return {ParseStatus::OK, consumed, ""};
    }

    if (static_cast<size_t>(element_count) > MAX_ARRAY_ELEMENTS) {
        return {ParseStatus::ERROR, 0, "Protocol error: multibulk length exceeds limit"};
    }

    const char* p = crlf + 2; // Advance past "*<count>\r\n"

    // 2. Parse each bulk string element: "$<len>\r\n<data>\r\n"
    for (int64_t i = 0; i < element_count; ++i) {
        if (p >= end) {
            return {ParseStatus::NEED_MORE, 0, ""};
        }

        if (*p != '$') {
            return {ParseStatus::ERROR, 0, "Protocol error: expected '$'"};
        }

        const char* next_crlf = find_crlf(p, end);
        if (!next_crlf) {
            return {ParseStatus::NEED_MORE, 0, ""};
        }

        int64_t str_len = 0;
        if (!parse_int(p + 1, next_crlf, str_len)) {
            return {ParseStatus::ERROR, 0, "Protocol error: invalid bulk length"};
        }

        if (str_len < -1) {
            return {ParseStatus::ERROR, 0, "Protocol error: negative bulk length"};
        }

        if (str_len == -1) {
            // Null bulk string inside array
            out_cmd.emplace_back(""); // Empty string placeholder
            p = next_crlf + 2;
            continue;
        }

        if (static_cast<size_t>(str_len) > MAX_BULK_STRING_LENGTH) {
            return {ParseStatus::ERROR, 0, "Protocol error: bulk string exceeds max length"};
        }

        const char* payload_start = next_crlf + 2;
        const char* payload_end = payload_start + str_len;

        // Check if we have the payload PLUS the trailing \r\n (str_len + 2 bytes)
        if (payload_end + 2 > end) {
            return {ParseStatus::NEED_MORE, 0, ""};
        }

        // Verify trailing \r\n
        if (*payload_end != '\r' || *(payload_end + 1) != '\n') {
            return {ParseStatus::ERROR, 0, "Protocol error: missing bulk string trailing CRLF"};
        }

        out_cmd.emplace_back(payload_start, static_cast<size_t>(str_len));
        p = payload_end + 2; // Advance to next element
    }

    size_t total_consumed = p - start;
    return {ParseStatus::OK, total_consumed, ""};
}

} // namespace kvforge