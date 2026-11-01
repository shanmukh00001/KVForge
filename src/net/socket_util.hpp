//configure raw socket file descriptors.

#pragma once

#include <string>

namespace kvforge {

// Makes a socket file descriptor non-blocking (O_NONBLOCK)
// Returns true on success, false on failure (sets out_err)
bool set_nonblocking(int fd, std::string& out_err);

// Allows immediate reuse of the port after server restart (SO_REUSEADDR)
bool set_reuse_addr(int fd, std::string& out_err);

// Disables Nagle's algorithm for low latency (TCP_NODELAY)
bool set_tcp_nodelay(int fd, std::string& out_err);

// Enables TCP keepalive so dead clients are eventually detected (SO_KEEPALIVE)
bool set_keepalive(int fd, std::string& out_err);

} // namespace kvforge