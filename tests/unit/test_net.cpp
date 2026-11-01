#include <gtest/gtest.h>
#include "net/socket_util.hpp"
#include "net/listener.hpp"
#include "net/connection.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace kvforge;

// Test 1: Test socket flags utility
TEST(SocketUtilTest, NonBlockingAndReuse) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);

    std::string err;
    EXPECT_TRUE(set_nonblocking(fd, err)) << err;
    EXPECT_TRUE(set_reuse_addr(fd, err)) << err;
    EXPECT_TRUE(set_tcp_nodelay(fd, err)) << err;
    EXPECT_TRUE(set_keepalive(fd, err)) << err;

    ::close(fd);
}

// Test 2: Listener lifecycle and accepting a real client
TEST(ListenerTest, BindAndAccept) {
    Listener listener;
    std::string err;
    uint16_t test_port = 6389;

    // Start listening on 127.0.0.1:6389
    ASSERT_TRUE(listener.start("127.0.0.1", test_port, err)) << err;
    EXPECT_TRUE(listener.is_listening());
    EXPECT_GE(listener.fd(), 0);

    // Create a client socket and connect to the listener
    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port);
    ::inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    int conn_res = ::connect(client_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    ASSERT_EQ(conn_res, 0);

    // Accept the connection on the listener
    std::string client_ip;
    uint16_t client_port = 0;
    auto server_conn = listener.accept_connection(client_ip, client_port, err);

    ASSERT_NE(server_conn, nullptr);
    EXPECT_EQ(client_ip, "127.0.0.1");
    EXPECT_GT(client_port, 0);

    // Send data from client to server
    const char* msg = "PING\r\n";
    ssize_t sent = ::write(client_fd, msg, 6);
    EXPECT_EQ(sent, 6);

    // Server reads the data
    ssize_t read_bytes = server_conn->read_from_socket(err);
    EXPECT_EQ(read_bytes, 6);
    EXPECT_EQ(server_conn->read_buf(), "PING\r\n");

    // Clean up
    ::close(client_fd);
    listener.stop();
    EXPECT_FALSE(listener.is_listening());
}

// Test 3: Connection buffers and consume
TEST(ConnectionTest, BufferManagement) {
    Connection conn(-1); // Dummy connection for buffer testing

    conn.append_write("HELLO ", 6);
    conn.append_write(std::string("WORLD"));
    EXPECT_EQ(conn.write_buf(), "HELLO WORLD");
    EXPECT_TRUE(conn.has_pending_write());

    conn.consume_write_buf(6);
    EXPECT_EQ(conn.write_buf(), "WORLD");

    conn.consume_write_buf(10); // Consume more than remaining
    EXPECT_TRUE(conn.write_buf().empty());
    EXPECT_FALSE(conn.has_pending_write());
}