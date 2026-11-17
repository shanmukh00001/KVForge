#include "protocol/resp_parser.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cassert>

using namespace kvforge;

static std::vector<Command> parse_incremental(const uint8_t* data, size_t size, size_t chunk_size) {
    std::vector<Command> commands;
    std::string buffer;
    buffer.reserve(size);
    size_t offset = 0;

    while (offset < size) {
        size_t n = (size - offset < chunk_size) ? (size - offset) : chunk_size;
        buffer.append(reinterpret_cast<const char*>(data + offset), n);
        offset += n;

        // Drain complete commands
        while (!buffer.empty()) {
            Command cmd;
            ParseResult res = RespParser::parse(buffer.data(), buffer.size(), cmd);
            if (res.status == ParseStatus::OK) {
                commands.push_back(std::move(cmd));
                buffer.erase(0, res.consumed);
            } else if (res.status == ParseStatus::NEED_MORE) {
                break;
            } else { // ERROR
                // Protocol error: return what was parsed up to the error
                return commands;
            }
        }
    }
    return commands;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    // Use byte 0 to determine chunking size for run B (1 to 16 bytes)
    size_t chunk_size_b = 1 + (data[0] % 16);
    const uint8_t* payload = data + 1;
    size_t payload_size = size - 1;

    // Run A: all bytes delivered in one chunk
    std::vector<Command> res_a = parse_incremental(payload, payload_size, payload_size);

    // Run B: bytes delivered in smaller chunks derived from data[0]
    std::vector<Command> res_b = parse_incremental(payload, payload_size, chunk_size_b);

    // Differential chunking invariant: chunking must never alter the parsed command sequence
    if (res_a != res_b) {
        __builtin_trap();
    }

    return 0;
}
