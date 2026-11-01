#pragma once

#include "net/connection.hpp"
#include <string>
#include <memory>
#include <cstdint>

namespace kvforge {

class Listener {
public:
    Listener() = default;
    ~Listener();

    // Disable copy
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Start listening on bind_ip:port
    // Returns true on success, false on failure (sets out_err)
    bool start(const std::string& bind_ip, uint16_t port, std::string& out_err);

    // Stops listening and closes the listening socket
    void stop();

    // Accepts a pending incoming client connection
    // Returns:
    //   - std::unique_ptr<Connection> on success
    //   - nullptr with out_err.empty() if no pending connections (EAGAIN)
    //   - nullptr with out_err set if a real error occurred
    std::unique_ptr<Connection> accept_connection(std::string& out_client_ip, 
                                                  uint16_t& out_client_port, 
                                                  std::string& out_err);

    int fd() const { return listen_fd_; }
    bool is_listening() const { return listen_fd_ >= 0; }

private:
    int listen_fd_ = -1;
};

} // namespace kvforge