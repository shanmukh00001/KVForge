#include "net/connection.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <string>

namespace kvforge {

static std::string get_errno_str() {
    return std::string(std::strerror(errno));
}

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// Move constructor: transfers ownership of the socket fd from other to this
Connection::Connection(Connection&& other) noexcept 
    : fd_(other.fd_),
      read_buf_(std::move(other.read_buf_)),
      write_buf_(std::move(other.write_buf_)),
      close_after_write_(other.close_after_write_) {
    other.fd_ = -1; // Invalidate the old connection so it doesn't close the socket
}

// Move assignment operator
Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_); // Close our existing socket first
        }
        fd_ = other.fd_;
        read_buf_ = std::move(other.read_buf_);
        write_buf_ = std::move(other.write_buf_);
        close_after_write_ = other.close_after_write_;
        other.fd_ = -1;
    }
    return *this;
}

void Connection::consume_read_buf(size_t n) {
    if (n >= read_buf_.size()) {
        read_buf_.clear();
        // Return memory to OS heap if it previously swelled beyond 64 KiB
        if (read_buf_.capacity() > BUFFER_SHRINK_THRESHOLD) {
            read_buf_.shrink_to_fit();
        }
    } else {
        read_buf_.erase(0, n);
    }
}

void Connection::consume_write_buf(size_t n) {
    if (n >= write_buf_.size()) {
        write_buf_.clear();
        // Return memory to OS heap if it previously swelled beyond 64 KiB
        if (write_buf_.capacity() > BUFFER_SHRINK_THRESHOLD) {
            write_buf_.shrink_to_fit();
        }
    } else {
        write_buf_.erase(0, n);
    }
}
void Connection::append_write(const char* data, size_t len) {
    write_buf_.append(data, len);
}

void Connection::append_write(const std::string& data) {
    write_buf_.append(data);
}



ssize_t Connection::read_from_socket(std::string& out_err, size_t max_bytes) {
    char temp_buf[16384]; // Read in 16 KiB chunks (PRD architecture recommendation)
    ssize_t total_read = 0;

    while (static_cast<size_t>(total_read) < max_bytes) {
        size_t to_read = std::min(sizeof(temp_buf), max_bytes - static_cast<size_t>(total_read));
        ssize_t n = ::read(fd_, temp_buf, to_read);

        if (n > 0) {
            read_buf_.append(temp_buf, static_cast<size_t>(n));
            total_read += n;
            // If we read less than requested, the OS socket buffer is drained for now
            if (static_cast<size_t>(n) < to_read) {
                break;
            }
        } else if (n == 0) {
            // EOF: Client disconnected cleanly
            if (total_read > 0) {
                return total_read;
            }
            return 0;
        } else {
            // n == -1
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more data available right now
            } else if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            } else {
                out_err = "read() failed: " + get_errno_str();
                return -1;
            }
        }
    }

    return total_read;
}
ssize_t Connection::write_to_socket(std::string& out_err) {
    if (write_buf_.empty()) {
        return 0;
    }

    ssize_t total_written = 0;

    while (!write_buf_.empty()) {
        ssize_t n = ::write(fd_, write_buf_.data(), write_buf_.size());

        if (n > 0) {
            total_written += n;
            consume_write_buf(static_cast<size_t>(n));
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket outgoing buffer is full; will resume when EPOLLOUT triggers
                break;
            } else if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            } else {
                // Real socket error (e.g. Broken pipe / client abruptly closed)
                out_err = "write() failed: " + get_errno_str();
                return -1;
            }
        }
    }

    return total_written;
}

} // namespace kvforge