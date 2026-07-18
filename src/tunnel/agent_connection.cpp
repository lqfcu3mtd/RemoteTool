#include "rmt/tunnel/agent_connection.h"

#include <chrono>
#include <random>
#include <string>

#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"

namespace rmt::tunnel {
namespace {

// Thread-safe random number generator for jitter
std::mt19937& rng() {
    static std::mt19937 gen([]() {
        std::random_device rd;
        return std::mt19937(rd());
    }());
    return gen;
}

}  // namespace

AgentConnection::AgentConnection(asio::io_context& io, AgentConfig config)
    : io_(io),
      config_(std::move(config)),
      hello_timeout_timer_(io),
      heartbeat_timer_(io),
      frame_watchdog_timer_(io),
      reconnect_timer_(io) {
}

AgentConnection::~AgentConnection() {
    // If the user forgot to call stop() and the object is being destroyed,
    // tear down synchronously.  All timers are already cancelled by the
    // steady_timer destructors.
    stopping_ = true;
    teardown_connection();
}

void AgentConnection::start() {
    if (stopping_) {
        logger_.warn("AgentConnection::start called while stopping");
        return;
    }

    if (state_ == AgentState::Online || state_ == AgentState::Error) {
        logger_.warn("AgentConnection::start called in unexpected state");
        return;
    }

    reconnect_attempt_ = 0;
    do_connect();
}

void AgentConnection::stop() {
    if (stopping_) return;
    stopping_ = true;

    logger_.info("AgentConnection stopping");
    cancel_all_timers();
    teardown_connection();

    if (state_ != AgentState::Disconnected) {
        set_state(AgentState::Disconnected);
    }
}

AgentState AgentConnection::state() const noexcept {
    return state_;
}

void AgentConnection::set_on_state_change(std::function<void(AgentState)> cb) {
    on_state_change_ = std::move(cb);
}

void AgentConnection::set_on_frame(std::function<void(const protocol::Frame&)> cb) {
    on_frame_ = std::move(cb);
}

// ---- private implementation ------------------------------------------------

void AgentConnection::do_connect() {
    if (is_stopping()) return;

    set_state(AgentState::Connecting);

    connection_ = std::make_unique<TunnelConnection>(io_);

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());

    connection_->set_on_frame([weak](const protocol::Frame& frame) {
        if (auto self = weak.lock()) {
            self->on_server_frame(frame);
        }
    });

    connection_->set_on_closed([weak](rmt::ErrorCode ec) {
        if (auto self = weak.lock()) {
            self->on_tunnel_closed(ec);
        }
    });

    connection_->connect(config_.server_host, config_.server_port,
        [weak](rmt::ErrorCode ec) {
            if (auto self = weak.lock()) {
                self->on_tcp_connect(ec);
            }
        });
}

void AgentConnection::on_tcp_connect(rmt::ErrorCode ec) {
    if (is_stopping()) return;

    if (ec != rmt::ErrorCode::Ok) {
        logger_.warn("AgentConnection TCP connect failed to "
                     + config_.server_host + ":" + std::to_string(config_.server_port));
        teardown_connection();
        schedule_reconnect();
        return;
    }

    logger_.info("AgentConnection TCP connected to "
                 + config_.server_host + ":" + std::to_string(config_.server_port));
    reconnect_attempt_ = 0;  // reset on successful connect
    send_hello();
}

void AgentConnection::send_hello() {
    if (is_stopping()) return;

    protocol::HelloMessage hello;
    hello.device_id = config_.device_id;
    hello.agent_version = config_.agent_version;
    hello.platform = config_.platform;
    hello.protocol_version = 1;

    auto frame = protocol::encode_hello(hello);

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    connection_->send_frame(frame, [weak](rmt::ErrorCode /*ec*/) {
        // Best-effort send; errors handled by on_tunnel_closed
    });

    set_state(AgentState::WaitHelloAck);

    // Start HELLO_ACK timeout (5 seconds)
    hello_timeout_timer_.expires_after(std::chrono::milliseconds(kHelloTimeoutMs));
    hello_timeout_timer_.async_wait([weak](const asio::error_code& ec) {
        if (auto self = weak.lock()) {
            self->on_hello_timeout(ec);
        }
    });

    reset_frame_watchdog();
}

void AgentConnection::on_server_frame(const protocol::Frame& frame) {
    if (is_stopping()) return;

    // Any valid frame resets the watchdog
    reset_frame_watchdog();

    switch (frame.header.type) {
        case protocol::MsgHelloAck:
            handle_hello_ack(frame);
            break;
        case protocol::MsgHeartbeatAck:
            handle_heartbeat_ack(frame);
            break;
        default:
            // Session and other frames forwarded to upper layer
            if (on_frame_) {
                on_frame_(frame);
            }
            break;
    }
}

void AgentConnection::on_tunnel_closed(rmt::ErrorCode ec) {
    if (is_stopping()) return;

    logger_.info(std::string("AgentConnection tunnel closed: ") + std::string(rmt::to_string(ec)));

    // Defer teardown + reconnect to avoid destroying the TunnelConnection
    // from within its own callback chain (use-after-free).
    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    asio::post(io_, [weak]() {
        if (auto self = weak.lock()) {
            if (self->is_stopping()) return;

            self->teardown_connection();

            // Don't reconnect from Error state
            if (self->state_ == AgentState::Error) {
                return;
            }

            self->schedule_reconnect();
        }
    });
}

void AgentConnection::handle_hello_ack(const protocol::Frame& frame) {
    if (state_ != AgentState::WaitHelloAck) {
        logger_.warn("AgentConnection received HELLO_ACK in unexpected state");
        return;
    }

    // Cancel hello timeout
    hello_timeout_timer_.cancel();

    auto result = protocol::decode_hello_ack(frame);
    auto* ack = std::get_if<protocol::HelloAckMessage>(&result);
    if (!ack) {
        auto* err = std::get_if<std::string>(&result);
        logger_.error(std::string("AgentConnection HELLO_ACK decode error: ") +
                      (err ? *err : "unknown"));
        // Close connection; on_tunnel_closed will defer teardown + reconnect
        if (connection_) {
            connection_->close();
        } else {
            schedule_reconnect();
        }
        return;
    }

    if (!ack->accepted) {
        logger_.error(std::string("AgentConnection authentication rejected: ") +
                      ack->error_code + " - " + ack->message);
        set_state(AgentState::Error);
        // Close connection; on_tunnel_closed will defer teardown
        if (connection_) {
            connection_->close();
        } else {
            teardown_connection();
        }
        return;
    }

    // Update heartbeat parameters from server
    heartbeat_interval_ms_ = ack->heartbeat_interval_ms;
    heartbeat_timeout_ms_ = ack->heartbeat_timeout_ms;
    heartbeat_sequence_ = 0;

    logger_.info("AgentConnection online, heartbeat_interval="
                 + std::to_string(heartbeat_interval_ms_) + "ms, timeout="
                 + std::to_string(heartbeat_timeout_ms_) + "ms");

    set_state(AgentState::Online);
    start_heartbeat_timer();

    // Reset watchdog with server-configured timeout (on_server_frame
    // already reset it with the old default; re-arm with the correct value).
    reset_frame_watchdog();
}

void AgentConnection::handle_heartbeat_ack(const protocol::Frame& frame) {
    auto result = protocol::decode_heartbeat_ack(frame);
    auto* ack = std::get_if<protocol::HeartbeatAckMessage>(&result);
    if (!ack) {
        auto* err = std::get_if<std::string>(&result);
        logger_.warn(std::string("AgentConnection HEARTBEAT_ACK decode error: ") +
                     (err ? *err : "unknown"));
        return;
    }

    // sequence mismatch is NOT an error, only log
    if (ack->sequence != heartbeat_sequence_) {
        logger_.warn("AgentConnection HEARTBEAT_ACK sequence mismatch: got "
                     + std::to_string(ack->sequence) + ", expected "
                     + std::to_string(heartbeat_sequence_));
    }
}

void AgentConnection::start_heartbeat_timer() {
    if (is_stopping()) return;
    if (state_ != AgentState::Online) return;

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    heartbeat_timer_.expires_after(std::chrono::milliseconds(heartbeat_interval_ms_));
    heartbeat_timer_.async_wait([weak](const asio::error_code& ec) {
        if (auto self = weak.lock()) {
            self->on_heartbeat_timer(ec);
        }
    });
}

void AgentConnection::on_heartbeat_timer(const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) return;
    if (is_stopping()) return;

    if (state_ != AgentState::Online) return;

    send_heartbeat();
    start_heartbeat_timer();
}

void AgentConnection::send_heartbeat() {
    if (is_stopping()) return;
    if (!connection_ || connection_->state() != TunnelState::Connected) return;

    ++heartbeat_sequence_;

    protocol::HeartbeatMessage hb;
    hb.sequence = heartbeat_sequence_;
    hb.sent_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    hb.active_sessions = 0;  // Phase 1: no sessions yet

    auto frame = protocol::encode_heartbeat(hb);

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    connection_->send_frame(frame, [weak](rmt::ErrorCode ec) {
        if (ec != rmt::ErrorCode::Ok) {
            if (auto self = weak.lock()) {
                self->logger_.warn("AgentConnection heartbeat send error: "
                                   + std::string(rmt::to_string(ec)));
            }
        }
    });
}

void AgentConnection::reset_frame_watchdog() {
    if (is_stopping()) return;

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    frame_watchdog_timer_.cancel();
    frame_watchdog_timer_.expires_after(std::chrono::milliseconds(heartbeat_timeout_ms_));
    frame_watchdog_timer_.async_wait([weak](const asio::error_code& ec) {
        if (auto self = weak.lock()) {
            self->on_frame_watchdog(ec);
        }
    });
}

void AgentConnection::on_frame_watchdog(const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) return;
    if (is_stopping()) return;

    logger_.warn("AgentConnection frame watchdog expired ("
                 + std::to_string(heartbeat_timeout_ms_) + "ms without valid frame)");
    teardown_connection();
    schedule_reconnect();
}

void AgentConnection::on_hello_timeout(const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) return;
    if (is_stopping()) return;

    if (state_ != AgentState::WaitHelloAck) return;

    logger_.warn("AgentConnection HELLO_ACK timeout (" +
                 std::to_string(kHelloTimeoutMs) + "ms)");
    teardown_connection();
    schedule_reconnect();
}

void AgentConnection::schedule_reconnect() {
    if (is_stopping()) return;
    if (state_ == AgentState::Error) return;

    // Transition to Disconnected if we were previously connected
    if (state_ != AgentState::Disconnected) {
        set_state(AgentState::Disconnected);
    }

    ++reconnect_attempt_;
    int delay_ms = compute_backoff_ms();
    logger_.info("AgentConnection scheduling reconnect in " + std::to_string(delay_ms) + "ms");

    auto weak = std::weak_ptr<AgentConnection>(shared_from_this());
    reconnect_timer_.expires_after(std::chrono::milliseconds(delay_ms));
    reconnect_timer_.async_wait([weak](const asio::error_code& ec) {
        if (auto self = weak.lock()) {
            self->on_reconnect_timer(ec);
        }
    });
}

void AgentConnection::on_reconnect_timer(const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) return;
    if (is_stopping()) return;

    logger_.info("AgentConnection reconnect timer fired");
    do_connect();
}

int AgentConnection::compute_backoff_ms() const {
    // reconnect_attempt_ is incremented by schedule_reconnect() before this
    // is called. attempt 1 = 1s, 2 = 2s, 3 = 4s, 4 = 8s, 5 = 16s, 6+ = 30s (capped).
    int attempt = reconnect_attempt_;

    int base = config_.min_reconnect_ms;
    // Double for each attempt: 1→1s, 2→2s, 3→4s, 4→8s, 5→16s, 6+→30s (capped)
    for (int i = 1; i < attempt; ++i) {
        base *= 2;
        if (base > config_.max_reconnect_ms) {
            base = config_.max_reconnect_ms;
            break;
        }
    }

    // ±20% random jitter
    std::uniform_real_distribution<double> dist(-0.2, 0.2);
    double jitter = dist(rng());
    int result = static_cast<int>(static_cast<double>(base) * (1.0 + jitter));

    // Clamp to valid range
    if (result < 0) result = 0;
    if (result > config_.max_reconnect_ms) result = config_.max_reconnect_ms;

    return result;
}

void AgentConnection::set_state(AgentState s) {
    if (state_ == s) return;

    AgentState old = state_;
    state_ = s;

    // Clear reconnect attempts when transitioning to non-Connecting states
    // (except when scheduling reconnect, where we want to increment)
    if (s == AgentState::Online) {
        reconnect_attempt_ = 0;
    }

    logger_.info(std::string("AgentConnection state change: ") +
                 (old == AgentState::Disconnected ? "Disconnected" :
                  old == AgentState::Connecting ? "Connecting" :
                  old == AgentState::WaitHelloAck ? "WaitHelloAck" :
                  old == AgentState::Online ? "Online" :
                  old == AgentState::Error ? "Error" : "Unknown") +
                 " -> " +
                 (s == AgentState::Disconnected ? "Disconnected" :
                  s == AgentState::Connecting ? "Connecting" :
                  s == AgentState::WaitHelloAck ? "WaitHelloAck" :
                  s == AgentState::Online ? "Online" :
                  s == AgentState::Error ? "Error" : "Unknown"));

    if (on_state_change_) {
        on_state_change_(s);
    }
}

void AgentConnection::teardown_connection() {
    if (connection_) {
        connection_.reset();
    }
}

void AgentConnection::cancel_all_timers() {
    asio::error_code ignored;
    hello_timeout_timer_.cancel(ignored);
    heartbeat_timer_.cancel(ignored);
    frame_watchdog_timer_.cancel(ignored);
    reconnect_timer_.cancel(ignored);
}

}  // namespace rmt::tunnel
