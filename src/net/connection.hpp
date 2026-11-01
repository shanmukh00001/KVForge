#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

namespace kvforge {

class Connection {
public:
    // Resource limits per client connection
    static constexpr size_t MAX_QUERY_BUF_SIZE = 128 * 1024 * 1024; // 128 MiB
    static constexpr size_t MAX_OUTPUT_BUF_SIZE = 64 * 1024 * 1024;  // 64 MiB
    static constexpr size_t BUFFER_SHRINK_THRESHOLD = 64 * 1024;    // 64 KiB

    bool is_output_buf_exceeded() const {
        return write_buf_.size() > MAX_OUTPUT_BUF_SIZE;
    }

    bool is_query_buf_exceeded() const {
        return read_buf_.size() > MAX_QUERY_BUF_SIZE;
    }
    explicit Connection(int fd);
    ~Connection();

    // Disable copy constructors (sockets cannot be duplicated safely)
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Allow moving connections
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    int fd() const { return fd_; }

    // Read buffer management
    std::string& read_buf() { return read_buf_; }
    const std::string& read_buf() const { return read_buf_; }
    void consume_read_buf(size_t n);

    // Write buffer management
    std::string& write_buf() { return write_buf_; }
    const std::string& write_buf() const { return write_buf_; }
    void append_write(const char* data, size_t len);
    void append_write(const std::string& data);
    void consume_write_buf(size_t n);

    bool has_pending_write() const { return !write_buf_.empty(); }

    // Flag to mark that this connection should close once write_buf is completely sent
    bool close_after_write() const { return close_after_write_; }
    void set_close_after_write(bool v) { close_after_write_ = v; }

    // Read and Write methods over non-blocking socket
    // Returns: > 0 (bytes transferred), 0 (connection closed by peer), -1 (EAGAIN or error)
    ssize_t read_from_socket(std::string& out_err,size_t max_bytes=64*1024);
    ssize_t write_to_socket(std::string& out_err);

private:
    int fd_ = -1;
    std::string read_buf_;
    std::string write_buf_;
    bool close_after_write_ = false;
};

} // namespace kvforge