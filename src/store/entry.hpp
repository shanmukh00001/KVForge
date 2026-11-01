#pragma once

#include <string>
#include <cstdint>

namespace kvforge {

// Represents a single key-value item in our database
struct DictEntry {
    std::string key;
    std::string value;
    DictEntry* next = nullptr; // Pointer to next entry in the bucket chain (collision list)

    DictEntry(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)), next(nullptr) {}
};

} // namespace kvforge