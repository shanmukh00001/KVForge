#pragma once

#include "net/listener.hpp"
#include "net/connection.hpp"
#include "commands/command_table.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace kvforge {

class EventLoop {
public:
    explicit EventLoop(uint32_t max_clients = 10000);
    ~EventLoop();

    // Disable copy
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Initialize epoll and signalfd
    bool init(std::string& out_err);

    // Register our listening socket with epoll
    bool register_listener(Listener* listener, std::string& out_err);

    // Run the event loop (runs until SIGINT/SIGTERM or stop() is called)
    void run();

    // Request loop to stop
    void stop() { stop_ = true; }

    void set_context(ServerContext* ctx) { ctx_ = ctx; }
private:
    // Event handlers
    void handle_listener_event();
    void handle_signal_event();
    void handle_connection_read(int fd);
    void handle_connection_write(int fd);

    // Connection lifecycle
    void close_connection(int fd);
    bool update_connection_events(int fd, uint32_t events);

    int epoll_fd_ = -1;
    int signal_fd_ = -1;
    bool stop_ = false;
    uint32_t max_clients_ = 10000;

    Listener* listener_ = nullptr;
    // Map of active client connections: fd -> Connection
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;

    ServerContext* ctx_ = nullptr;
};

} // namespace kvforge