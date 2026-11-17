#include "util/glob.hpp"

namespace kvforge {

bool glob_match(std::string_view pattern, std::string_view str) {
    size_t p = 0, s = 0;
    size_t plen = pattern.size(), slen = str.size();

    while (p < plen && s < slen) {
        if (pattern[p] == '*') {
            // Collapse multiple consecutive asterisks
            while (p + 1 < plen && pattern[p + 1] == '*') {
                p++;
            }
            if (p + 1 == plen) {
                return true; // Trailing '*' matches everything remaining
            }
            // Recursively attempt matching from every subsequent position
            for (size_t i = s; i <= slen; ++i) {
                if (glob_match(pattern.substr(p + 1), str.substr(i))) {
                    return true;
                }
            }
            return false;
        } else if (pattern[p] == '?') {
            p++;
            s++;
        } else if (pattern[p] == '[') {
            p++;
            bool negate = false;
            if (p < plen && pattern[p] == '^') {
                negate = true;
                p++;
            }
            bool match = false;
            while (p < plen && pattern[p] != ']') {
                if (p + 2 < plen && pattern[p + 1] == '-') {
                    char start = pattern[p];
                    char end = pattern[p + 2];
                    if (str[s] >= start && str[s] <= end) {
                        match = true;
                    }
                    p += 3;
                } else {
                    if (pattern[p] == str[s]) {
                        match = true;
                    }
                    p++;
                }
            }
            if (p < plen && pattern[p] == ']') {
                p++;
            }
            if (negate) match = !match;
            if (!match) return false;
            s++;
        } else if (pattern[p] == '\\') {
            p++;
            if (p < plen) {
                if (pattern[p] != str[s]) return false;
                p++;
                s++;
            } else {
                return false;
            }
        } else {
            if (pattern[p] != str[s]) return false;
            p++;
            s++;
        }
    }

    while (p < plen && pattern[p] == '*') {
        p++;
    }

    return (p == plen && s == slen);
}

} // namespace kvforge
