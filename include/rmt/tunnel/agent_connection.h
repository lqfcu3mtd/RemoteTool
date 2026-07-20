#pragma once
// AgentConnection — Agent-side tunnel connection state machine (Phase 1, no TLS).
//
// Manages: TCP connect → HELLO handshake → periodic HEARTBEAT →
// timeout detection → disconnect + backoff reconnect.
//
// Lifetime: must be owned via std::shared_ptr (inherits from enable_shared_from_this).
// All async callbacks use weak_ptr to survive the object being destroyed before
// a timer/async-op fires.
//
// Thread safety: all operations must run on the asio::io_context thread.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "rmt/common/log.h"
#include "rmt/protocol/frame.h"
#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

enum class AgentState {
    Disconnected,
    Connecting,
    WaitHelloAck,
    Online,
    Error,
};

struct AgentConfig {
    std::string server_host;
    std::uint16_t server_port = 0;
    std::string device_id;             // 1-64 chars, ASCII alphanum + - _ .
    std::string agent_version = "0.1.0";
    std::string platform = "windows-x86_64";
    int min_reconnect_ms = 1000;
    int max_reconnect_ms = 30000;
    int auth_error_wait_ms = 30000;
};

class AgentConnection : public std::enable_shared_from_this<AgentConnection> {
public:
    explicit AgentConnection(asio::io_context& io, AgentConfig config);
    ~AgentConnection();

    AgentConnection(const AgentConnection&) = delete;
    AgentConnection& operator=(const AgentConnection&) = delete;

    void start();
    void stop();

    AgentState state() const noexcept;

    void set_on_state_change(std::function<void(AgentState)> cb);
    void set_on_frame(std::function<void(const protocol::Frame&)> cb);

    // Access the live tunnel connection (nullptr while not connected). The
    // returned shared_ptr keeps the tunnel alive for session handlers even if
    // the AgentConnection later tears it down and reconnects.
    std::shared_ptr<TunnelConnection> tunnel() const noexcept { return connection_; }

    // Expose the io_context so session handlers can be constructed on it.
    asio::io_context& io() noexcept { return io_; }

private:
    // Internal helpers
    void do_connect();
    void on_tcp_connect(rmt::ErrorCode ec);
    void send_hello();
    void on_server_frame(const protocol::Frame& frame);
    void on_tunnel_closed(rmt::ErrorCode ec);
    void handle_hello_ack(const protocol::Frame& frame);
    void handle_heartbeat_ack(const protocol::Frame& frame);

    // Heartbeat
    void start_heartbeat_timer();
    void on_heartbeat_timer(const asio::error_code& ec);
    void send_heartbeat();

    // Timeout / watchdog
    void reset_frame_watchdog();
    void on_frame_watchdog(const asio::error_code& ec);
    void on_hello_timeout(const asio::error_code& ec);

    // Reconnect
    void schedule_reconnect();
    void on_reconnect_timer(const asio::error_code& ec);
    int compute_backoff_ms() const;

    // State management
    void set_state(AgentState s);
    void teardown_connection();
    void cancel_all_timers();
    bool is_stopping() const noexcept { return stopping_; }

    asio::io_context& io_;
    AgentConfig config_;
    AgentState state_ = AgentState::Disconnected;
    bool stopping_ = false;

    std::shared_ptr<TunnelConnection> connection_;

    static constexpr long long kHelloTimeoutMs = 5000;
    static constexpr long long kDefaultHeartbeatIntervalMs = 10000;
    static constexpr long long kDefaultHeartbeatTimeoutMs = 30000;

    asio::steady_timer hello_timeout_timer_;
    asio::steady_timer heartbeat_timer_;
    asio::steady_timer frame_watchdog_timer_;
    asio::steady_timer reconnect_timer_;

    long long heartbeat_sequence_ = 0;
    long long heartbeat_interval_ms_ = kDefaultHeartbeatIntervalMs;
    long long heartbeat_timeout_ms_ = kDefaultHeartbeatTimeoutMs;

    int reconnect_attempt_ = 0;

    std::function<void(AgentState)> on_state_change_;
    std::function<void(const protocol::Frame&)> on_frame_;

    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
