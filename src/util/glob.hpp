#pragma once

#include <string_view>

namespace kvforge {

// Matches a string against a glob pattern.
// Supported syntax (API_SPECIFICATION.md §5.3):
//   *        matches 0 or more characters
//   ?        matches any single character
//   [abc]    matches 'a', 'b', or 'c'
//   [a-z]    matches any character in range 'a'..'z'
//   [^a]     matches any character except 'a'
//   \x       escapes character x (literal match)
bool glob_match(std::string_view pattern, std::string_view str);

} // namespace kvforge
