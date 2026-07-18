// AgentConnection unit tests.
// Simulates a minimal RemoteTool server with asio acceptor.
// Tests: happy-path HELLO handshake, heartbeat sending, watchdog timeout,
//        stop(), and auth rejection.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/test.h"
#include "rmt/tunnel/agent_connection.h"
#include "rmt/tunnel/connection.h"

using rmt::ErrorCode;
using rmt::protocol::Frame;
using rmt::protocol::FrameDecoder;
using rmt::protocol::MessageType;
using rmt::tunnel::AgentConnection;
using rmt::tunnel::AgentConfig;
using rmt::tunnel::AgentState;

namespace {

// Mock server: accepts connections, reads frames, sends configured responses.
// For timeout tests, silent=true means the server only responds to HELLO
// (so agent gets Online) but stays silent afterwards → watchdog fires.
class MockServer : public std::enable_shared_from_this<MockServer> {
public:
    explicit MockServer(asio::io_context& io)
        : io_(io), acceptor_(io) {}

    bool start() {
        asio::error_code ec;
        auto ep = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0);
        acceptor_.open(ep.protocol(), ec);
        if (ec) return false;
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(ep, ec);
        if (ec) return false;
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) return false;
        do_accept();
        return true;
    }

    void stop() {
        asio::error_code ignored;
        acceptor_.close(ignored);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (client_sock_) {
                client_sock_->close(ignored);
            }
        }
    }

    std::uint16_t port() const {
        asio::error_code ec;
        auto ep = acceptor_.local_endpoint(ec);
        return ec ? 0 : ep.port();
    }

    // Server response configuration
    bool accepted = true;
    int heartbeat_interval_ms = 1000;  // must be >= 1000 per protocol validation
    int heartbeat_timeout_ms = 3000;   // must be >= 2 * interval_ms
    bool silent = false;  // If true, reply to HELLO but NOT to HEARTBEAT

    // Observed state
    std::atomic<bool> hello_received{false};
    std::atomic<bool> hello_with_correct_device_id{false};
    std::atomic<bool> heartbeat_received{false};
    std::atomic<long long> last_heartbeat_seq{0};

    std::string device_id_expected;

private:
    void do_accept() {
        auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
        acceptor_.async_accept(*sock, [this, sock](const asio::error_code& ec) {
            if (ec) return;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                client_sock_ = sock;
            }
            do_read(sock);
            do_accept();
        });
    }

    using ClientSocket = std::shared_ptr<asio::ip::tcp::socket>;

    void do_read(ClientSocket sock) {
        auto buf = std::make_shared<std::vector<std::uint8_t>>(8192);
        sock->async_read_some(asio::buffer(*buf),
            [this, sock, buf](const asio::error_code& ec, std::size_t len) {
                if (ec) return;
                auto result = decoder_.consume(buf->data(), len);
                for (auto& frame : result.frames) {
                    if (frame.header.type == static_cast<MessageType>(0x01)) {  // MsgHello
                        handle_hello(sock, frame);
                    } else if (frame.header.type == static_cast<MessageType>(0x03)) {  // MsgHeartbeat
                        handle_heartbeat(sock, frame);
                    }
                }
                do_read(sock);
            });
    }

    void handle_hello(ClientSocket sock, const Frame& frame) {
        hello_received = true;
        auto result = rmt::protocol::decode_hello(frame);
        if (auto* msg = std::get_if<rmt::protocol::HelloMessage>(&result)) {
            hello_with_correct_device_id = (msg->device_id == device_id_expected);
        }

        rmt::protocol::HelloAckMessage ack;
        ack.accepted = accepted;
        ack.server_version = "0.1.0";
        ack.heartbeat_interval_ms = heartbeat_interval_ms;
        ack.heartbeat_timeout_ms = heartbeat_timeout_ms;
        ack.max_sessions = 128;
        if (!ack.accepted) {
            ack.error_code = "DEVICE_DISABLED";
            ack.message = "test auth rejection";
        }

        send_frame_to_client(sock, rmt::protocol::encode_hello_ack(ack));
    }

    void handle_heartbeat(ClientSocket sock, const Frame& frame) {
        heartbeat_received = true;
        auto result = rmt::protocol::decode_heartbeat(frame);
        if (auto* msg = std::get_if<rmt::protocol::HeartbeatMessage>(&result)) {
            last_heartbeat_seq = msg->sequence;
        }

        if (silent) return;

        rmt::protocol::HeartbeatAckMessage ack;
        ack.sequence = last_heartbeat_seq.load();
        ack.received_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        send_frame_to_client(sock, rmt::protocol::encode_heartbeat_ack(ack));
    }

    void send_frame_to_client(ClientSocket sock, const Frame& frame) {
        auto encoded = rmt::protocol::encode_frame(frame);
        auto buf = std::make_shared<std::vector<std::uint8_t>>(std::move(encoded));
        asio::async_write(*sock, asio::buffer(*buf),
            [sock, buf](const asio::error_code& /*ec*/, std::size_t /*n*/) {});
    }

    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    FrameDecoder decoder_;

    std::mutex mtx_;
    std::shared_ptr<asio::ip::tcp::socket> client_sock_;
};

AgentConfig make_config(std::uint16_t port, const std::string& device_id = "TEST001") {
    AgentConfig cfg;
    cfg.server_host = "127.0.0.1";
    cfg.server_port = port;
    cfg.device_id = device_id;
    cfg.agent_version = "0.1.0";
    cfg.platform = "windows-x86_64";
    cfg.min_reconnect_ms = 100;
    cfg.max_reconnect_ms = 500;
    return cfg;
}

// ---- Tests ---------------------------------------------------------------

void test_happy_path() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST001";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 3000;
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port()));
    agent->start();

    // Drive the io_context long enough for TCP connect + HELLO + HELLO_ACK
    io.run_for(std::chrono::milliseconds(500));

    RMT_CHECK(agent->state() == AgentState::Online);
    RMT_CHECK(server->hello_received.load());
    RMT_CHECK(server->hello_with_correct_device_id.load());

    server->stop();
    agent->stop();
}

void test_heartbeat_send() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST002";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 5000;
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST002"));
    agent->start();

    // Get to Online first
    io.run_for(std::chrono::milliseconds(500));
    RMT_CHECK(agent->state() == AgentState::Online);

    // Reset heartbeat flag after HELLO processing
    server->heartbeat_received = false;

    // Wait for one heartbeat to fire (1000ms interval + margin)
    io.run_for(std::chrono::milliseconds(1500));

    RMT_CHECK(server->heartbeat_received.load());
    RMT_CHECK_MSG(server->last_heartbeat_seq.load() >= 1,
                  "heartbeat sequence should be >= 1");

    server->stop();
    agent->stop();
}

void test_watchdog_timeout() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST003";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 2000;  // min valid: 2 * interval_ms
    server->silent = true;  // replies to HELLO but not HEARTBEAT → watchdog fires
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST003"));

    // Track that watchdog fired (state changed away from Online)
    std::atomic<bool> watchdog_fired{false};
    agent->set_on_state_change([&](AgentState s) {
        if (s == AgentState::Disconnected) {
            watchdog_fired = true;
        }
    });

    agent->start();

    // Get to Online (server sends HELLO_ACK)
    io.run_for(std::chrono::milliseconds(500));
    RMT_CHECK(agent->state() == AgentState::Online);

    // Wait for watchdog timeout: 2000ms timeout + margin.
    io.run_for(std::chrono::milliseconds(3000));

    // Verify watchdog fired (state changed to Disconnected at least once)
    RMT_CHECK_MSG(watchdog_fired.load(),
                  "watchdog should fire and transition to Disconnected");

    server->stop();
    agent->stop();
}

void test_stop_disconnects() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST004";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 5000;
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST004"));
    agent->start();

    // Get to Online
    io.run_for(std::chrono::milliseconds(500));
    RMT_CHECK(agent->state() == AgentState::Online);

    // Stop and verify no reconnect
    agent->stop();
    io.run_for(std::chrono::milliseconds(200));

    RMT_CHECK(agent->state() == AgentState::Disconnected);

    // Wait a bit more and verify state stays Disconnected
    io.run_for(std::chrono::milliseconds(1000));
    RMT_CHECK(agent->state() == AgentState::Disconnected);

    server->stop();
}

void test_auth_rejection() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST005";
    server->accepted = false;
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 3000;
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST005"));
    agent->start();

    // Process the HELLO and rejected HELLO_ACK
    io.run_for(std::chrono::milliseconds(500));

    RMT_CHECK(agent->state() == AgentState::Error);

    // Verify it doesn't try to reconnect
    io.run_for(std::chrono::milliseconds(1000));
    RMT_CHECK(agent->state() == AgentState::Error);

    server->stop();
    agent->stop();
}

void test_reconnect_after_disconnect() {
    // Full reconnect cycle: connect → online → server drops → agent reconnects
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST006";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 10000;  // long timeout, won't fire in test
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST006"));
    agent->start();

    // Get Online
    io.run_for(std::chrono::milliseconds(500));
    RMT_CHECK(agent->state() == AgentState::Online);

    // Kill the server → agent detects disconnect → reconnects
    server->stop();

    // Run to let agent detect server close and enter reconnect cycle
    io.run_for(std::chrono::milliseconds(1000));

    // Should have detected close and entered reconnect cycle (not Error)
    RMT_CHECK_MSG(agent->state() != AgentState::Error,
                  "agent should attempt reconnect after server close");

    agent->stop();
}

void test_state_callback() {
    asio::io_context io;
    auto server = std::make_shared<MockServer>(io);
    server->device_id_expected = "TEST007";
    server->heartbeat_interval_ms = 1000;
    server->heartbeat_timeout_ms = 5000;
    RMT_CHECK(server->start());

    auto agent = std::make_shared<AgentConnection>(io, make_config(server->port(), "TEST007"));

    std::atomic<int> cb_count{0};
    std::atomic<AgentState> last_state{AgentState::Disconnected};
    agent->set_on_state_change([&](AgentState s) {
        ++cb_count;
        last_state = s;
    });

    agent->start();
    io.run_for(std::chrono::milliseconds(500));

    RMT_CHECK_MSG(cb_count.load() >= 3,
                  "state callback should fire for Connecting, WaitHelloAck, Online");
    RMT_CHECK(last_state.load() == AgentState::Online);

    server->stop();
    agent->stop();
}

}  // namespace

int main() {
    test_happy_path();
    test_heartbeat_send();
    test_watchdog_timeout();
    test_stop_disconnects();
    test_auth_rejection();
    test_reconnect_after_disconnect();
    test_state_callback();

    auto& ctx = rmt::test::ctx();
    std::printf("\nagent_connection_test: %d passed, %d failed\n",
                ctx.passed, ctx.failed);
    return ctx.ok() ? 0 : 1;
}
