// TunnelConnection unit tests.
// Uses a local asio echo server for end-to-end verification.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/protocol/frame.h"
#include "rmt/test.h"
#include "rmt/tunnel/connection.h"

using rmt::ErrorCode;
using rmt::protocol::Frame;
using rmt::protocol::MsgHeartbeat;
using rmt::protocol::MsgHeartbeatAck;
using rmt::tunnel::TunnelConnection;
using rmt::tunnel::TunnelState;

namespace {

// ---- simple thread-safe signal for test coordination ----
class Signal {
public:
    void notify() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            count_++;
        }
        cv_.notify_one();
    }
    void wait(int target) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this, target] { return count_ >= target; });
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    int count_ = 0;
};

// ---- echo server helpers ----
using TcpSocket = std::shared_ptr<asio::ip::tcp::socket>;

// Normal echo: read → write back → read again (infinite loop).
void start_normal_echo(TcpSocket sock) {
    auto buf = std::make_shared<std::vector<std::uint8_t>>(8192);
    sock->async_read_some(asio::buffer(*buf),
        [sock, buf](const asio::error_code& ec, std::size_t len) {
            if (ec) return;
            asio::async_write(*sock, asio::buffer(buf->data(), len),
                [sock, buf](const asio::error_code& ec2, std::size_t) {
                    if (ec2) return;
                    start_normal_echo(sock);
                });
        });
}

// Split echo: read → write first half → (post) write second half → continue.
void start_split_echo(TcpSocket sock) {
    auto buf = std::make_shared<std::vector<std::uint8_t>>(8192);
    sock->async_read_some(asio::buffer(*buf),
        [sock, buf](const asio::error_code& ec, std::size_t len) {
            if (ec || len == 0) return;
            if (len == 1) {
                asio::async_write(*sock, asio::buffer(buf->data(), len),
                    [sock, buf](const asio::error_code& ec2, std::size_t) {
                        if (ec2) return;
                        start_split_echo(sock);
                    });
                return;
            }
            std::size_t half = len / 2;
            asio::async_write(*sock, asio::buffer(buf->data(), half),
                [sock, buf, len, half](const asio::error_code& ec2, std::size_t) {
                    if (ec2) return;
                    asio::post(sock->get_executor(),
                        [sock, buf, len, half]() {
                            asio::async_write(*sock,
                                asio::buffer(buf->data() + half, len - half),
                                [sock, buf](const asio::error_code& ec3, std::size_t) {
                                    if (ec3) return;
                                    start_split_echo(sock);
                                });
                        });
                });
        });
}

// Echo once then close: read → write back → close socket.
void start_close_after_one_echo(TcpSocket sock) {
    auto buf = std::make_shared<std::vector<std::uint8_t>>(8192);
    sock->async_read_some(asio::buffer(*buf),
        [sock, buf](const asio::error_code& ec, std::size_t len) {
            if (ec) return;
            asio::async_write(*sock, asio::buffer(buf->data(), len),
                [sock, buf](const asio::error_code& ec2, std::size_t) {
                    if (ec2) return;
                    asio::error_code ignored;
                    sock->shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
                    sock->close(ignored);
                });
        });
}

// Close immediately without echoing.
void start_accept_then_close(TcpSocket sock) {
    asio::error_code ignored;
    sock->close(ignored);
}

// Set up an echo listener on ::1:0 (random port).
// Returns the port and the echo socket so the caller can close it to stop
// the echo loop (which allows io_context::run() to return).
struct EchoHandle {
    uint16_t port = 0;
    TcpSocket sock;
};

EchoHandle listen_echo(asio::io_context& io,
                       std::function<void(TcpSocket)> echo_handler) {
    auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(
        io, asio::ip::tcp::endpoint(asio::ip::make_address("::1"), 0));
    uint16_t port = acceptor->local_endpoint().port();

    auto sock = std::make_shared<asio::ip::tcp::socket>(io);
    // Capture acceptor so it stays alive until the accept completes.
    acceptor->async_accept(*sock,
        [acceptor, sock, handler = std::move(echo_handler)](const asio::error_code& ec) {
            if (!ec) handler(sock);
        });

    return {port, sock};
}

// ---- tests ----

void test_connect_send_receive() {
    asio::io_context io;
    Signal sig;

    bool frame_received = false;
    bool closed_received = false;

    auto echo = listen_echo(io, start_normal_echo);

    TunnelConnection conn(io);

    conn.set_on_frame([&](const Frame& f) {
        RMT_CHECK(f.header.type == MsgHeartbeat);
        RMT_CHECK(f.payload.size() == 2);
        RMT_CHECK(f.payload[0] == static_cast<std::uint8_t>('{'));
        RMT_CHECK(f.payload[1] == static_cast<std::uint8_t>('}'));
        RMT_CHECK_MSG(f.header.session_id == 0, "session_id must be 0 for control");
        frame_received = true;
        sig.notify();
    });

    conn.set_on_closed([&](ErrorCode /*e*/) {
        closed_received = true;
        sig.notify();
    });

    conn.connect("::1", echo.port, [&](ErrorCode e) {
        RMT_CHECK(e == ErrorCode::Ok);
        RMT_CHECK(conn.state() == TunnelState::Connected);
        RMT_CHECK_MSG(conn.remote_address() == "::1:" + std::to_string(echo.port),
                      "remote_address mismatch");

        Frame f;
        f.header.type = MsgHeartbeat;
        f.payload = {static_cast<std::uint8_t>('{'), static_cast<std::uint8_t>('}')};
        conn.send_frame(f, [&](ErrorCode se) {
            RMT_CHECK(se == ErrorCode::Ok);
            sig.notify();
        });
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: send callback (1) + frame received (1) = 2 signals.
    sig.wait(2);

    RMT_CHECK(frame_received);

    conn.close();
    sig.wait(3);  // + on_closed

    RMT_CHECK(closed_received);
    RMT_CHECK(conn.state() == TunnelState::Closed);

    // Close echo socket to break the echo loop.
    asio::error_code ignored;
    echo.sock->close(ignored);
    work.reset();
    t.join();
}

void test_multiple_frames() {
    asio::io_context io;
    Signal sig;

    std::vector<Frame> received;
    const int kNumFrames = 3;

    auto echo = listen_echo(io, start_normal_echo);

    TunnelConnection conn(io);

    conn.set_on_frame([&](const Frame& f) {
        received.push_back(f);
        sig.notify();
    });

    conn.set_on_closed([&](ErrorCode /*e*/) {
        sig.notify();
    });

    conn.connect("::1", echo.port, [&](ErrorCode e) {
        RMT_CHECK(e == ErrorCode::Ok);

        for (int i = 0; i < kNumFrames; ++i) {
            Frame f;
            f.header.type = MsgHeartbeat;
            f.payload.push_back(static_cast<std::uint8_t>(i));
            conn.send_frame(f, [&](ErrorCode se) {
                RMT_CHECK(se == ErrorCode::Ok);
                sig.notify();
            });
        }
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: 3 sends + 3 receives = 6 signals.
    sig.wait(6);

    RMT_CHECK_MSG(static_cast<int>(received.size()) == kNumFrames,
                  "expected 3 frames received");
    for (int i = 0; i < kNumFrames; ++i) {
        RMT_CHECK(received[i].header.type == MsgHeartbeat);
        RMT_CHECK(received[i].payload.size() == 1);
        RMT_CHECK(received[i].payload[0] == static_cast<std::uint8_t>(i));
    }

    conn.close();
    sig.wait(7);  // + on_closed

    RMT_CHECK(conn.state() == TunnelState::Closed);

    asio::error_code ignored;
    echo.sock->close(ignored);
    work.reset();
    t.join();
}

void test_fragmented_input() {
    asio::io_context io;
    Signal sig;

    bool frame_received = false;

    auto echo = listen_echo(io, start_split_echo);

    TunnelConnection conn(io);

    conn.set_on_frame([&](const Frame& f) {
        RMT_CHECK(f.header.type == MsgHeartbeat);
        RMT_CHECK(f.payload.size() == 10);
        frame_received = true;
        sig.notify();
    });

    conn.set_on_closed([&](ErrorCode /*e*/) {
        sig.notify();
    });

    conn.connect("::1", echo.port, [&](ErrorCode e) {
        RMT_CHECK(e == ErrorCode::Ok);

        Frame f;
        f.header.type = MsgHeartbeat;
        for (std::uint8_t v = 0; v < 10; ++v) f.payload.push_back(v);
        conn.send_frame(f, [&](ErrorCode se) {
            RMT_CHECK(se == ErrorCode::Ok);
            sig.notify();
        });
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: send callback (1) + frame received (1) = 2 signals.
    sig.wait(2);

    RMT_CHECK(frame_received);

    conn.close();
    sig.wait(3);  // + on_closed

    asio::error_code ignored;
    echo.sock->close(ignored);
    work.reset();
    t.join();
}

void test_connection_failure() {
    asio::io_context io;
    Signal sig;

    ErrorCode closed_error = ErrorCode::Ok;
    bool connect_called = false;

    TunnelConnection conn(io);

    conn.set_on_closed([&](ErrorCode e) {
        closed_error = e;
        sig.notify();
    });

    // Connect to a port that has no listener.
    conn.connect("::1", 55555, [&](ErrorCode e) {
        RMT_CHECK(e != ErrorCode::Ok);
        connect_called = true;
        sig.notify();
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: on_closed (1) + on_connected (1) = 2 signals.
    sig.wait(2);

    RMT_CHECK(connect_called);
    RMT_CHECK(closed_error == ErrorCode::InternalError);
    RMT_CHECK(conn.state() == TunnelState::Closed);

    work.reset();
    t.join();
}

void test_remote_close() {
    asio::io_context io;
    Signal sig;

    ErrorCode closed_error = ErrorCode::Ok;
    bool frame_received = false;

    // Echo server that closes after one frame.
    auto echo = listen_echo(io, start_close_after_one_echo);

    TunnelConnection conn(io);

    conn.set_on_frame([&](const Frame& f) {
        frame_received = true;
        RMT_CHECK(f.header.type == MsgHeartbeatAck);
        sig.notify();
    });

    conn.set_on_closed([&](ErrorCode e) {
        closed_error = e;
        sig.notify();
    });

    conn.connect("::1", echo.port, [&](ErrorCode e) {
        RMT_CHECK(e == ErrorCode::Ok);

        Frame f;
        f.header.type = MsgHeartbeatAck;
        f.payload = {static_cast<std::uint8_t>('{'), static_cast<std::uint8_t>('}')};
        conn.send_frame(f, [&](ErrorCode se) {
            RMT_CHECK(se == ErrorCode::Ok);
            sig.notify();
        });
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: send callback (1) + frame received (1) + on_closed (1) = 3.
    sig.wait(3);

    RMT_CHECK(frame_received);
    RMT_CHECK_MSG(closed_error == ErrorCode::LocalPeerClosed,
                  "expected LocalPeerClosed on remote close");
    RMT_CHECK(conn.state() == TunnelState::Closed);

    work.reset();
    t.join();
}

void test_send_after_close() {
    asio::io_context io;
    Signal sig;

    bool send_rejected = false;

    // Echo server that closes immediately — the connect will still succeed
    // (TCP handshake completes before server close) so we test close()+send.
    auto echo = listen_echo(io, start_accept_then_close);

    TunnelConnection conn(io);

    conn.set_on_closed([&](ErrorCode /*e*/) {
        sig.notify();
    });

    conn.connect("::1", echo.port, [&](ErrorCode e) {
        RMT_CHECK(e == ErrorCode::Ok);

        conn.close();

        // send after close — must return error immediately.
        Frame f;
        f.header.type = MsgHeartbeat;
        conn.send_frame(f, [&](ErrorCode se) {
            RMT_CHECK(se != ErrorCode::Ok);
            send_rejected = true;
            sig.notify();
        });
    });

    auto work = asio::make_work_guard(io);
    std::thread t([&]() { io.run(); });

    // Wait for: on_closed (1) + send callback (1) = 2 signals.
    sig.wait(2);

    RMT_CHECK(send_rejected);
    RMT_CHECK(conn.state() == TunnelState::Closed);

    work.reset();
    t.join();
}

}  // namespace

int main() {
    test_connect_send_receive();
    test_multiple_frames();
    test_fragmented_input();
    test_connection_failure();
    test_remote_close();
    test_send_after_close();

    auto& c = rmt::test::ctx();
    std::printf("connection_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
