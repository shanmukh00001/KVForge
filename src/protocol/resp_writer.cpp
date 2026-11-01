#include "protocol/resp_writer.hpp"

namespace kvforge {

std::string RespWriter::sanitize_crlf(std::string_view input) {
    std::string clean;
    clean.reserve(input.size());
    for (char c : input) {
        if (c == '\r' || c == '\n') {
            clean += ' ';
        } else {
            clean += c;
        }
    }
    return clean;
}

std::string RespWriter::simple_string(std::string_view str) {
    std::string out = "+";
    out += sanitize_crlf(str);
    out += "\r\n";
    return out;
}

std::string RespWriter::error(std::string_view msg) {
    std::string out = "-";
    out += sanitize_crlf(msg);
    out += "\r\n";
    return out;
}

std::string RespWriter::integer(int64_t num) {
    return ":" + std::to_string(num) + "\r\n";
}

std::string RespWriter::bulk_string(std::string_view data) {
    // Format: $<length>\r\n<data>\r\n
    // Note: Bulk strings are binary-safe, so they CAN contain \r\n inside their payload!
    std::string out = "$";
    out += std::to_string(data.size());
    out += "\r\n";
    out.append(data.data(), data.size());
    out += "\r\n";
    return out;
}

std::string RespWriter::null_bulk_string() {
    return "$-1\r\n";
}

std::string RespWriter::array_header(size_t count) {
    return "*" + std::to_string(count) + "\r\n";
}

std::string RespWriter::null_array() {
    return "*-1\r\n";
}

} // namespace kvforge