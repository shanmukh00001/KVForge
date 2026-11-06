#include "persistence/aof.hpp"
#include "protocol/resp_parser.hpp"
#include "commands/command_table.hpp"
#include "util/log.hpp"
#include "util/clock.hpp"
#include "util/int_parse.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

namespace kvforge {

static std::string to_upper_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

FsyncPolicy AOF::parse_policy(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "always") return FsyncPolicy::ALWAYS;
    if (lower == "no") return FsyncPolicy::NO;
    return FsyncPolicy::EVERYSEC; // Default
}

AOF::AOF(std::string filename, FsyncPolicy policy)
    : filename_(std::move(filename)), policy_(policy) {}

AOF::~AOF() {
    close();
}

void AOF::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool AOF::open_file(std::string& out_err) {
    // Mode 0600: read/write permissions only for owner (standard database security)
    fd_ = ::open(filename_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd_ < 0) {
        out_err = "Failed to open AOF file '" + filename_ + "': " + std::strerror(errno);
        return false;
    }

    struct stat st;
    if (::fstat(fd_, &st) == 0) {
        current_size_ = static_cast<size_t>(st.st_size);
    }

    return true;
}

bool AOF::sync(std::string& out_err) {
    if (fd_ >= 0) {
        if (::fsync(fd_) < 0) {
            out_err = "AOF fsync() failed: " + std::string(std::strerror(errno));
            return false;
        }
    }
    return true;
}

bool AOF::append_record(const std::vector<std::string>& cmd, std::string& out_err) {
    if (fd_ < 0 || cmd.empty()) return false;

    // Serialize command into a standard RESP array of bulk strings
    // e.g. ["SET", "k", "v"] -> "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n"
    std::string record = "*" + std::to_string(cmd.size()) + "\r\n";
    for (const auto& token : cmd) {
        record += "$" + std::to_string(token.size()) + "\r\n";
        record += token + "\r\n";
    }

    const char* p = record.data();
    size_t remaining = record.size();

    while (remaining > 0) {
        ssize_t n = ::write(fd_, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            out_err = "AOF write() failed: " + std::string(std::strerror(errno));
            return false;
        }
        p += n;
        remaining -= static_cast<size_t>(n);
    }

    current_size_ += record.size();

    // If policy is ALWAYS, fsync to physical disk immediately before replying to client
    if (policy_ == FsyncPolicy::ALWAYS) {
        return sync(out_err);
    }

    return true;
}

// Replays a single normalized command into ServerContext in replay mode
static bool replay_command(ServerContext& ctx, const Command& cmd, std::string& err) {
    if (cmd.empty()) return true;

    std::string name = to_upper_str(cmd[0]);
    uint64_t now_wall = wall_clock_time_ms();
    uint64_t now_mono = monotonic_time_ms();

    if (name == "SET" && cmd.size() >= 3) {
        const std::string& key = cmd[1];
        const std::string& val = cmd[2];

        // Check if command has PXAT <epoch_ms>
        int64_t pxat_ms = -1;
        for (size_t i = 3; i < cmd.size(); ++i) {
            if (to_upper_str(cmd[i]) == "PXAT" && i + 1 < cmd.size()) {
                parse_int64(cmd[i + 1], pxat_ms);
                break;
            }
        }

        if (pxat_ms > 0) {
            if (static_cast<uint64_t>(pxat_ms) <= now_wall) {
                // Key expired while server was offline; do not resurrect it!
                ctx.db.del(key);
                ctx.expiry_index.remove_expiry(key);
                return true;
            }
            ctx.db.set(key, val);
            uint64_t remaining = static_cast<uint64_t>(pxat_ms) - now_wall;
            ctx.expiry_index.set_expiry(key, now_mono + remaining);
        } else {
            ctx.db.set(key, val);
            ctx.expiry_index.remove_expiry(key);
        }
        return true;
    }

    if (name == "DEL") {
        for (size_t i = 1; i < cmd.size(); ++i) {
            ctx.db.del(cmd[i]);
            ctx.expiry_index.remove_expiry(cmd[i]);
        }
        return true;
    }

    if (name == "PEXPIREAT" && cmd.size() >= 3) {
        const std::string& key = cmd[1];
        int64_t pxat_ms = -1;
        if (parse_int64(cmd[2], pxat_ms) && pxat_ms > 0) {
            if (static_cast<uint64_t>(pxat_ms) <= now_wall) {
                ctx.db.del(key);
                ctx.expiry_index.remove_expiry(key);
            } else if (ctx.db.find(key) != nullptr) {
                uint64_t remaining = static_cast<uint64_t>(pxat_ms) - now_wall;
                ctx.expiry_index.set_expiry(key, now_mono + remaining);
            }
        }
        return true;
    }

    if (name == "PERSIST" && cmd.size() >= 2) {
        ctx.expiry_index.remove_expiry(cmd[1]);
        return true;
    }

    if ((name == "INCR" || name == "DECR" || name == "INCRBY" || name == "DECRBY") && cmd.size() >= 2) {
        const std::string& key = cmd[1];
        int64_t delta = 1;
        if (name == "DECR") delta = -1;
        else if (name == "INCRBY" && cmd.size() >= 3) {
            if (!parse_int64(cmd[2], delta)) { err = "Corrupt INCRBY argument"; return false; }
        } else if (name == "DECRBY" && cmd.size() >= 3) {
            int64_t val = 0;
            if (!parse_int64(cmd[2], val)) { err = "Corrupt DECRBY argument"; return false; }
            delta = -val;
        }

        DictEntry* entry = ctx.db.find(key);
        int64_t curr = 0;
        if (entry != nullptr) {
            if (!parse_int64(entry->value, curr)) {
                err = "AOF corrupted: entry is not an integer";
                return false;
            }
        }
        int64_t next_val = 0;
        if (!safe_add(curr, delta, next_val)) {
            err = "AOF integer overflow during replay";
            return false;
        }
        ctx.db.set(key, std::to_string(next_val));
        return true;
    }

    if (name == "MSET") {
        for (size_t i = 1; i + 1 < cmd.size(); i += 2) {
            ctx.db.set(cmd[i], cmd[i + 1]);
            ctx.expiry_index.remove_expiry(cmd[i]);
        }
        return true;
    }

    if (name == "FLUSHALL") {
        ctx.db.clear();
        ctx.expiry_index.clear();
        return true;
    }

    return true;
}

bool AOF::load_and_replay(const std::string& filename, ServerContext& ctx, std::string& out_err) {
    if (::access(filename.c_str(), F_OK) != 0) {
        return true; // File does not exist yet (first startup), clean state
    }

    // Open read-write so we can truncate partial tail if needed
    int fd = ::open(filename.c_str(), O_RDWR);
    if (fd < 0) {
        out_err = "Cannot open AOF for replay: " + std::string(std::strerror(errno));
        return false;
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        out_err = "fstat() failed on AOF file: " + std::string(std::strerror(errno));
        ::close(fd);
        return false;
    }

    size_t file_size = static_cast<size_t>(st.st_size);
    if (file_size == 0) {
        ::close(fd);
        return true;
    }

    std::string buf(file_size, '\0');
    size_t total_read = 0;
    while (total_read < file_size) {
        ssize_t n = ::read(fd, &buf[total_read], file_size - total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            out_err = "Failed reading AOF file: " + std::string(std::strerror(errno));
            ::close(fd);
            return false;
        }
        if (n == 0) break;
        total_read += static_cast<size_t>(n);
    }

    size_t pos = 0;
    size_t last_good = 0;

    while (pos < file_size) {
        Command cmd;
        ParseResult res = RespParser::parse(buf.data() + pos, file_size - pos, cmd);

        if (res.status == ParseStatus::OK) {
            std::string replay_err;
            if (!replay_command(ctx, cmd, replay_err)) {
                out_err = "Corrupt AOF command at offset " + std::to_string(pos) + ": " + replay_err;
                ::close(fd);
                return false;
            }
            pos += res.consumed;
            last_good = pos;
        } else if (res.status == ParseStatus::NEED_MORE) {
            // Partial record at EOF (server crashed mid-write)
            // Truncate file back to last valid complete record
            log_warn("Truncated incomplete AOF tail at offset " + std::to_string(last_good));
            if (::ftruncate(fd, static_cast<off_t>(last_good)) < 0) {
                out_err = "Failed to truncate partial AOF tail: " + std::string(std::strerror(errno));
                ::close(fd);
                return false;
            }
            break;
        } else {
            // Corruption detected in the file!
            out_err = "Corrupt AOF syntax at offset " + std::to_string(pos) + ": " + res.error_msg;
            ::close(fd);
            return false;
        }
    }

    ::close(fd);
    log_info("AOF replay complete: loaded " + std::to_string(ctx.db.size()) + " keys");
    return true;
}

} // namespace kvforge