#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace kvforge {

// Forward declarations
class ServerContext;

enum class FsyncPolicy {
    ALWAYS,
    EVERYSEC,
    NO
};

class AOF {
public:
    AOF(std::string filename, FsyncPolicy policy);
    ~AOF();

    // Disable copy
    AOF(const AOF&) = delete;
    AOF& operator=(const AOF&) = delete;

    // Open file for appending (creates file if missing, permissions 0600)
    bool open_file(std::string& out_err);

    // Append a normalized command record to the AOF
    bool append_record(const std::vector<std::string>& cmd, std::string& out_err);

    // Explicitly sync OS cache to physical disk
    bool sync(std::string& out_err);

    // Flush and close file
    void close();

    // Load and replay an existing AOF file into ServerContext on startup.
    // Handles partial records at EOF by truncating. Fails if middle is corrupt.
    static bool load_and_replay(const std::string& filename, ServerContext& ctx, std::string& out_err);

    // Convert string policy ("always", "everysec", "no") to enum
    static FsyncPolicy parse_policy(const std::string& str);

    size_t current_size() const { return current_size_; }
    FsyncPolicy policy() const { return policy_; }

private:
    std::string filename_;
    FsyncPolicy policy_;
    int fd_ = -1;
    size_t current_size_ = 0;
};

} // namespace kvforge