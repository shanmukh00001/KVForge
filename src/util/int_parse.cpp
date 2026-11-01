#include "util/int_parse.hpp"
#include <climits>

namespace kvforge {

bool parse_int64(std::string_view str, int64_t& out_val) {
    if (str.empty()) return false;

    size_t i = 0;
    bool negative = false;

    if (str[0] == '-') {
        negative = true;
        i = 1;
    } else if (str[0] == '+') {
        i = 1;
    }

    // A lone "+" or "-" is not a valid number
    if (i >= str.size()) return false;

    uint64_t val = 0;
    const uint64_t MAX_LIMIT = negative 
        ? static_cast<uint64_t>(INT64_MAX) + 1ULL  // Absolute value of INT64_MIN
        : static_cast<uint64_t>(INT64_MAX);

    while (i < str.size()) {
        char c = str[i];
        if (c < '0' || c > '9') {
            return false; // Non-digit character found
        }

        uint64_t digit = static_cast<uint64_t>(c - '0');

        // Check if multiplying by 10 or adding digit would overflow 64 bits
        if (val > (MAX_LIMIT - digit) / 10) {
            return false;
        }

        val = val * 10 + digit;
        ++i;
    }

    if (negative) {
        if (val == MAX_LIMIT) {
            out_val = INT64_MIN;
        } else {
            out_val = -static_cast<int64_t>(val);
        }
    } else {
        out_val = static_cast<int64_t>(val);
    }

    return true;
}

bool safe_add(int64_t a, int64_t b, int64_t& out_result) {
    // Check for positive overflow
    if (b > 0 && a > (INT64_MAX - b)) {
        return false;
    }
    // Check for negative underflow
    if (b < 0 && a < (INT64_MIN - b)) {
        return false;
    }

    out_result = a + b;
    return true;
}

} // namespace kvforge