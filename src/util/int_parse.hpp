#pragma once

#include <string_view>
#include <cstdint>

namespace kvforge {

// Safely converts a string to a 64-bit signed integer.
// Returns true on success, false if:
// - The string has invalid characters (e.g. "12a4")
// - The string has whitespace or is empty
// - The number overflows/underflows 64-bit integer range
bool parse_int64(std::string_view str, int64_t& out_val);

// Safely adds two 64-bit signed integers.
// Returns true on success, false if (a + b) overflows or underflows.
bool safe_add(int64_t a, int64_t b, int64_t& out_result);

} // namespace kvforge