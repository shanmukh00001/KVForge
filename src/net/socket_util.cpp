#include "net/socket_util.hpp"

#include <fcntl.h>
#include <netinet/tcp.h>
#include <netinet/in.h>   // IPPROTO_TCP
#include <netinet/tcp.h>  // TCP_NODELAY
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace kvforge {

// Helper to format errno into a human-readable string
static std::string get_errno_msg() {
    return std::string(std::strerror(errno));
}

bool set_nonblocking(int fd, std::string& out_err) {
    // 1. Get existing file descriptor flags
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        out_err = "fcntl(F_GETFL) failed: " + get_errno_msg();
        return false;
    }
  //Linux normally assigns the lowest unused non-negative number in your process:
    // 2. Add the O_NONBLOCK flag and write it back
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        out_err = "fcntl(F_SETFL, O_NONBLOCK) failed: " + get_errno_msg();
        return false;
    }
    return true;
}

bool set_reuse_addr(int fd, std::string& out_err) {
    int optval = 1; // 1 means "enable"
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        out_err = "setsockopt(SO_REUSEADDR) failed: " + get_errno_msg();
        return false;
    }
    return true;
}

bool set_tcp_nodelay(int fd, std::string& out_err) {
    int optval = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
        out_err = "setsockopt(TCP_NODELAY) failed: " + get_errno_msg();
        return false;
    }
    return true;
}

bool set_keepalive(int fd, std::string& out_err) {
    int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        out_err = "setsockopt(SO_KEEPALIVE) failed: " + get_errno_msg();
        return false;
    }
    return true;
}

} // namespace kvforge