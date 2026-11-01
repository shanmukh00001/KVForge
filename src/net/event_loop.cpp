#include "net/event_loop.hpp"
#include "util/log.hpp"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <vector>

namespace kvforge {

static std::string get_errno_msg() {
    return std::string(std::strerror(errno));
}

EventLoop::EventLoop(uint32_t max_clients) : max_clients_(max_clients) {}

EventLoop::~EventLoop() {
    if (signal_fd_ >= 0) {
        ::close(signal_fd_);
        signal_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EventLoop::init(std::string& out_err) {
    // 1. Create epoll file descriptor
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        out_err = "epoll_create1() failed: " + get_errno_msg();
        return false;
    }

    // 2. Set up signalfd for SIGINT (Ctrl+C) and SIGTERM
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    // Block the signals so default OS signal handlers do not terminate the process
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        out_err = "sigprocmask() failed: " + get_errno_msg();
        return false;
    }

    signal_fd_ = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        out_err = "signalfd() failed: " + get_errno_msg();
        return false;
    }

    // Register signal_fd with epoll
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd_;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) < 0) {
        out_err = "epoll_ctl(signal_fd) failed: " + get_errno_msg();
        return false;
    }

    // Ignore SIGPIPE so writing to a closed socket does not crash our server
    signal(SIGPIPE, SIG_IGN);

    return true;
}

bool EventLoop::register_listener(Listener* listener, std::string& out_err) {
    listener_ = listener;
    if (!listener_ || !listener_->is_listening()) {
        out_err = "Cannot register uninitialized listener";
        return false;
    }

    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN; // Watch for incoming connections
    ev.data.fd = listener_->fd();

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener_->fd(), &ev) < 0) {
        out_err = "epoll_ctl(listener) failed: " + get_errno_msg();
        return false;
    }

    return true;
}

bool EventLoop::update_connection_events(int fd, uint32_t events) {
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void EventLoop::close_connection(int fd) {
    // epoll automatically removes closed file descriptors, but calling DEL is good hygiene
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    connections_.erase(fd);
    log_debug("Closed connection fd=" + std::to_string(fd) + 
              " (active: " + std::to_string(connections_.size()) + ")");
}

void EventLoop::handle_signal_event() {
    struct signalfd_siginfo fdsi;
    ssize_t s = ::read(signal_fd_, &fdsi, sizeof(fdsi));
    if (s == sizeof(fdsi)) {
        if (fdsi.ssi_signo == SIGINT) {
            log_info("Received SIGINT (Ctrl+C), shutting down gracefully...");
        } else if (fdsi.ssi_signo == SIGTERM) {
            log_info("Received SIGTERM, shutting down gracefully...");
        }
        stop();
    }
}


void EventLoop::handle_listener_event() {
    while (true) {
        std::string client_ip;
        uint16_t client_port = 0;
        std::string err;

        auto conn = listener_->accept_connection(client_ip, client_port, err);
        if (!conn) {
            if (!err.empty()) {
                log_warn("Failed to accept connection: " + err);
            }
            break;
        }

        int fd = conn->fd();

        if (connections_.size() >= max_clients_) {
            log_warn("Max clients reached (" + std::to_string(max_clients_) + 
                     "), rejecting client from " + client_ip + ":" + std::to_string(client_port));
            conn->append_write("-ERR max number of clients reached\r\n");
            std::string write_err;
            conn->write_to_socket(write_err);
            continue;
        }

        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = fd;

        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            log_error("epoll_ctl(ADD client) failed: " + get_errno_msg());
            continue;
        }

        if (ctx_) {
            ctx_->total_connections_received++;
        }

        log_debug("Accepted connection from " + client_ip + ":" + std::to_string(client_port) + 
                  " fd=" + std::to_string(fd) + " (active: " + std::to_string(connections_.size() + 1) + ")");

        connections_[fd] = std::move(conn);
    }
}

void EventLoop::handle_connection_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection* conn = it->second.get();
    std::string err;
    ssize_t n = conn->read_from_socket(err);

    if (n == 0) {
        // EOF: Client disconnected cleanly
        close_connection(fd);
        return;
    } else if (n < 0) {
        log_warn("Connection error on fd=" + std::to_string(fd) + ": " + err);
        close_connection(fd);
        return;
    }

    // Parse and dispatch all complete commands in read_buf_ (supports pipelining!)
    while (!conn->read_buf().empty()) {
        Command cmd;
        ParseResult res = RespParser::parse(conn->read_buf().data(), conn->read_buf().size(), cmd);

        if (res.status == ParseStatus::OK) {
            // Consume the bytes for this command from the read buffer
            conn->consume_read_buf(res.consumed);

            // Dispatch command through the command table!
            if (ctx_ && !cmd.empty()) {
                ctx_->command_table.dispatch(*ctx_, cmd, *conn);
            }
        } else if (res.status == ParseStatus::NEED_MORE) {
            // If empty line was parsed, consume spaces
            if (res.consumed > 0) {
                conn->consume_read_buf(res.consumed);
                continue;
            }
            // Incomplete command: leave bytes in read_buf_ and wait for next network packet
            break;
        } else {
            // Protocol error: send error message and close connection
            conn->append_write("-ERR " + res.error_msg + "\r\n");
            conn->set_close_after_write(true);
            conn->consume_read_buf(conn->read_buf().size());
            break;
        }
    }

    // Flush any pending replies to the socket
    conn->write_to_socket(err);

    if (conn->has_pending_write()) {
        update_connection_events(fd, EPOLLIN | EPOLLOUT);
    }
}
void EventLoop::handle_connection_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection* conn = it->second.get();
    std::string err;
    ssize_t n = conn->write_to_socket(err);

    if (n < 0) {
        log_warn("Write error on fd=" + std::to_string(fd) + ": " + err);
        close_connection(fd);
        return;
    }

    // If write buffer is completely empty, stop asking for EPOLLOUT
    if (!conn->has_pending_write()) {
        if (conn->close_after_write()) {
            close_connection(fd);
            return;
        }
        update_connection_events(fd, EPOLLIN);
    }
}

void EventLoop::run() {
    log_info("Event loop started");

    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (!stop_) {
        // Sleep until events happen (100ms timeout)
         // 1. Calculate dynamic epoll_wait timeout based on next expiring key!
        int timeout_ms = 100; // Default max timeout 100ms per PRD FR-EXP-04

        if (ctx_) {
            uint64_t next_ms = 0;
            if (ctx_->expiry_index.next_expiry_time(next_ms)) {
                uint64_t now_ms = monotonic_time_ms();
                if (now_ms >= next_ms) {
                    timeout_ms = 0; // Key already expired! Wake up immediately!
                } else {
                    uint64_t diff = next_ms - now_ms;
                    if (diff < 100) {
                        timeout_ms = static_cast<int>(diff);
                    }
                }
            }
        }
        int nfds = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
        // Sleep until network events happen or timer triggers!
        if (nfds < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, loop checks !stop_
            }
            log_error("epoll_wait() failed: " + get_errno_msg());
            break;
        }
        //process all ready networks
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == signal_fd_) {
                handle_signal_event();
            } else if (listener_ && fd == listener_->fd()) {
                handle_listener_event();
            } else {
                // Client connection event
                if (ev & (EPOLLERR | EPOLLHUP)) {
                    close_connection(fd);
                } else {
                    if (ev & EPOLLIN) {
                        handle_connection_read(fd);
                    }
                    if (ev & EPOLLOUT) {
                        handle_connection_write(fd);
                    }
                }
            }
        }

        // 3. Active Expiry Cleanup (1ms budget per iteration!)
        if (ctx_ && ctx_->expiry_index.size() > 0) {
            uint64_t now_ms = monotonic_time_ms();
            // Pop up to 100 expired keys from the Min-Heap
            auto expired = ctx_->expiry_index.pop_expired(now_ms, 100);
            for (const auto& key : expired) {
                ctx_->db.del(key);
                ctx_->expired_keys++;
            }
        }
    }

    log_info("Event loop stopped. Cleaning up " + std::to_string(connections_.size()) + " connections...");
    connections_.clear();
}

} // namespace kvforge