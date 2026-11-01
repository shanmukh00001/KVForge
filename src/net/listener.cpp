#include "net/listener.hpp"
#include "net/socket_util.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace kvforge {

static std::string get_errno_msg() {
    return std::string(std::strerror(errno));
}

Listener::~Listener() {
    stop();
}

void Listener::stop() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

bool Listener::start(const std::string& bind_ip, uint16_t port, std::string& out_err) {
    stop(); // Close any previously open listener

    // 1. Create IPv4 TCP socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        out_err = "socket() failed: " + get_errno_msg();
        return false;
    }

    // 2. Allow immediate port reuse (SO_REUSEADDR)
    if (!set_reuse_addr(listen_fd_, out_err)) {
        stop();
        return false;
    }

    // 3. Make listening socket non-blocking
    if (!set_nonblocking(listen_fd_, out_err)) {
        stop();
        return false;
    }

    // 4. Configure address and port structure
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port); // Convert port to Network Byte Order (Big Endian)

    if (::inet_pton(AF_INET, bind_ip.c_str(), &addr.sin_addr) <= 0) {
        out_err = "Invalid bind IP address: " + bind_ip;
        stop();
        return false;
    }

    // 5. Bind socket to the address and port
    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        out_err = "bind(" + bind_ip + ":" + std::to_string(port) + ") failed: " + get_errno_msg();
        stop();
        return false;
    }

    // 6. Listen for incoming connections (backlog = 511, matching Redis)
    if (::listen(listen_fd_, 511) < 0) {
        out_err = "listen() failed: " + get_errno_msg();
        stop();
        return false;
    }

    return true;
}

std::unique_ptr<Connection> Listener::accept_connection(std::string& out_client_ip, 
                                                        uint16_t& out_client_port, 
                                                        std::string& out_err) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    std::memset(&client_addr, 0, sizeof(client_addr));

    // accept4 automatically sets SOCK_NONBLOCK on the new client socket!
    int client_fd = ::accept4(listen_fd_, 
                              reinterpret_cast<struct sockaddr*>(&client_addr), 
                              &client_len, 
                              SOCK_NONBLOCK);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No more incoming clients in queue right now
            out_err.clear();
            return nullptr;
        } else if (errno == EINTR) {
            // Interrupted by signal
            out_err.clear();
            return nullptr;
        } else {
            out_err = "accept4() failed: " + get_errno_msg();
            return nullptr;
        }
    }

    // Configure the client socket for low latency
    std::string util_err;
    set_tcp_nodelay(client_fd, util_err);
    set_keepalive(client_fd, util_err);

    // Convert binary client IP and port to human-readable strings
    char ip_str[INET_ADDRSTRLEN];
    if (::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str))) {
        out_client_ip = ip_str;
    } else {
        out_client_ip = "unknown";
    }
    out_client_port = ntohs(client_addr.sin_port);

    return std::make_unique<Connection>(client_fd);
}

} // namespace kvforge