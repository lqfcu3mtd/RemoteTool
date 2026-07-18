#include "rmt/tunnel/agent_session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"

namespace rmt::tunnel {

AgentSession::AgentSession(asio::io_context& io,
                           std::shared_ptr<TunnelConnection> tunnel,
                           const rmt::security::TargetWhitelist& whitelist)
    : io_(io), tunnel_(std::move(tunnel)), whitelist_(whitelist) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

AgentSession::~AgentSession() {
    cleanup();
}

void AgentSession::on_open_session(const rmt::protocol::OpenSessionMessage& msg,
                                   rmt::protocol::SessionId session_id) {
    if (state_ != SessionState::Idle) {
        logger_.warn("AgentSession::on_open_session called in wrong state");
        return;
    }

    session_id_ = session_id;
    mapping_id_ = msg.mapping_id;
    target_host_ = msg.target_host;
    target_port_ = msg.target_port;

    logger_.info("AgentSession session_id=" + std::to_string(session_id)
                 + " OPEN " + msg.target_host + ":" + std::to_string(msg.target_port));

    // Check target whitelist.
    auto result = whitelist_.check(msg.target_host,
                                   static_cast<std::uint16_t>(msg.target_port));
    if (!result.allowed) {
        logger_.warn("AgentSession whitelist rejected: " + result.reason);

        rmt::protocol::SessionOpenFailedMessage fail;
        fail.error_code = "TARGET_NOT_ALLOWED";
        fail.message = result.reason;

        auto frame = rmt::protocol::encode_session_open_failed(fail);
        send_session_frame(std::move(frame));

        cleanup();
        return;
    }

    // Create target socket and connect asynchronously.
    state_ = SessionState::Connecting;
    target_socket_ = std::make_shared<asio::ip::tcp::socket>(io_);
    target_read_buf_.resize(16384);

    asio::error_code addr_ec;
    auto addr = asio::ip::make_address(msg.target_host, addr_ec);
    if (addr_ec) {
        logger_.error("AgentSession invalid target address: " + msg.target_host);

        rmt::protocol::SessionOpenFailedMessage fail;
        fail.error_code = "TARGET_CONNECTION_REFUSED";
        fail.message = "invalid target address: " + msg.target_host;

        auto frame = rmt::protocol::encode_session_open_failed(fail);
        send_session_frame(std::move(frame));

        cleanup();
        return;
    }

    asio::ip::tcp::endpoint endpoint(addr, static_cast<std::uint16_t>(msg.target_port));

    // Set up connect timeout.
    connect_timer_ = std::make_shared<asio::steady_timer>(io_);
    connect_timer_->expires_after(
        std::chrono::milliseconds(msg.connect_timeout_ms));

    auto self = shared_from_this();
    connect_timer_->async_wait([this, self, target_socket = target_socket_](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        // Timeout fired.
        if (state_ != SessionState::Connecting) {
            return;
        }

        logger_.error("AgentSession connect timeout to " + target_host_
                      + ":" + std::to_string(target_port_));

        asio::error_code ignored;
        target_socket->cancel(ignored);

        rmt::protocol::SessionOpenFailedMessage fail;
        fail.error_code = "TARGET_CONNECT_TIMEOUT";
        fail.message = "connection to " + target_host_
                     + ":" + std::to_string(target_port_) + " timed out";

        auto frame = rmt::protocol::encode_session_open_failed(fail);
        send_session_frame(std::move(frame));

        cleanup();
    });

    target_socket_->async_connect(endpoint,
        [this, self, target_socket = target_socket_](const asio::error_code& ec) {
            // Cancel the connect timer.
            if (connect_timer_) {
                asio::error_code ignored;
                connect_timer_->cancel(ignored);
            }

            if (state_ != SessionState::Connecting) {
                return;
            }

            if (ec) {
                logger_.error("AgentSession target connect failed: " + ec.message());

                rmt::protocol::SessionOpenFailedMessage fail;
                fail.error_code = "TARGET_CONNECTION_REFUSED";
                fail.message = ec.message();

                auto frame = rmt::protocol::encode_session_open_failed(fail);
                send_session_frame(std::move(frame));

                cleanup();
                return;
            }

            asio::error_code remote_ec;
            auto remote_ep = target_socket->remote_endpoint(remote_ec);
            std::string connected_host = target_host_;
            int connected_port = target_port_;
            if (!remote_ec) {
                connected_host = remote_ep.address().to_string();
                connected_port = remote_ep.port();
            }

            on_target_connected(connected_host, connected_port);
        });
}

void AgentSession::on_session_frame(const rmt::protocol::Frame& frame) {
    if (state_ == SessionState::Idle || state_ == SessionState::Closed) {
        return;
    }

    using rmt::protocol::MsgSessionData;
    using rmt::protocol::MsgSessionHalfClose;
    using rmt::protocol::MsgCloseSession;

    auto type = frame.header.type;

    if (type == MsgSessionData) {
        auto data = rmt::protocol::decode_session_data(frame);
        if (!data.empty()) {
            forward_data_to_target(std::move(data));
        }
    } else if (type == MsgSessionHalfClose) {
        auto result = rmt::protocol::decode_session_half_close(frame);
        if (auto* msg = std::get_if<rmt::protocol::SessionHalfCloseMessage>(&result)) {
            handle_half_close(*msg);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.warn("AgentSession SESSION_HALF_CLOSE decode error: " + err);
        }
    } else if (type == MsgCloseSession) {
        auto result = rmt::protocol::decode_close_session(frame);
        if (auto* msg = std::get_if<rmt::protocol::CloseSessionMessage>(&result)) {
            handle_close(*msg);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.warn("AgentSession CLOSE_SESSION decode error: " + err);
            handle_close(rmt::protocol::CloseSessionMessage{"PROTOCOL_ERROR", err});
        }
    } else {
        logger_.warn("AgentSession unexpected frame type 0x"
                     + std::to_string(static_cast<unsigned>(type)));
    }
}

void AgentSession::on_target_connected(
    const std::string& connected_host,
    int connected_port) {

    state_ = SessionState::Connected;

    rmt::protocol::SessionOpenedMessage opened;
    opened.mapping_id = mapping_id_;
    opened.connected_host = connected_host;
    opened.connected_port = connected_port;

    auto frame = rmt::protocol::encode_session_opened(opened);
    send_session_frame(std::move(frame));

    logger_.info("AgentSession session_id=" + std::to_string(session_id_)
                 + " connected to " + connected_host + ":" + std::to_string(connected_port));

    // Start reading from target.
    start_target_read();
}

void AgentSession::forward_data_to_target(std::vector<std::uint8_t> data) {
    if (target_write_closed_ || !target_socket_ || !target_socket_->is_open()) {
        return;
    }

    bytes_sent_ += data.size();

    target_write_queue_.push_back({std::move(data)});
    if (!writing_to_target_) {
        do_target_write();
    }
}

void AgentSession::do_target_write() {
    if (target_write_queue_.empty()) {
        writing_to_target_ = false;
        return;
    }

    writing_to_target_ = true;
    auto& entry = target_write_queue_.front();

    auto self = shared_from_this();
    asio::async_write(*target_socket_, asio::buffer(entry.data),
        [this, self](const asio::error_code& ec, std::size_t /*bytes*/) {
            if (target_write_queue_.empty()) {
                writing_to_target_ = false;
                return;
            }

            target_write_queue_.pop_front();

            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    logger_.error("AgentSession target write error: " + ec.message());
                }
                writing_to_target_ = false;
                target_write_queue_.clear();

                // If not already closing, notify RemoteTool.
                if (state_ != SessionState::Closed) {
                    rmt::protocol::CloseSessionMessage close;
                    close.reason = "TARGET_IO_ERROR";
                    close.message = ec.message();

                    auto frame = rmt::protocol::encode_close_session(close);
                    send_session_frame(std::move(frame));
                    cleanup();
                }
                return;
            }

            do_target_write();
        });
}

void AgentSession::forward_data_to_tunnel(std::vector<std::uint8_t> data) {
    bytes_received_ += data.size();

    auto frame = rmt::protocol::encode_session_data(
        session_id_, data.data(), data.size());
    frame.header.session_id = session_id_;

    if (tunnel_) {
        tunnel_->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
            // Errors logged by TunnelConnection.
        });
    }
}

void AgentSession::start_target_read() {
    if (!target_socket_ || target_read_closed_) {
        return;
    }

    auto self = shared_from_this();
    target_socket_->async_read_some(asio::buffer(target_read_buf_),
        [this, self](const asio::error_code& ec, std::size_t len) {
            if (ec) {
                if (ec == asio::error::eof) {
                    // Target closed -- treat as remote half-close.
                    if (state_ == SessionState::Connected
                        || state_ == SessionState::HalfClosedLocal) {
                        target_read_closed_ = true;

                        rmt::protocol::SessionHalfCloseMessage hc;
                        hc.direction = "write";

                        auto frame = rmt::protocol::encode_session_half_close(hc);
                        send_session_frame(std::move(frame));

                        if (state_ == SessionState::HalfClosedLocal) {
                            state_ = SessionState::Closed;
                            logger_.info("AgentSession session_id="
                                         + std::to_string(session_id_)
                                         + " fully closed (both sides)");
                            cleanup();
                        } else {
                            state_ = SessionState::HalfClosedRemote;
                            logger_.info("AgentSession session_id="
                                         + std::to_string(session_id_)
                                         + " remote half-closed (target EOF)");
                        }
                    }
                } else if (ec != asio::error::operation_aborted) {
                    logger_.error("AgentSession target read error: " + ec.message());

                    if (state_ != SessionState::Closed) {
                        rmt::protocol::CloseSessionMessage close;
                        close.reason = "TARGET_IO_ERROR";
                        close.message = ec.message();

                        auto frame = rmt::protocol::encode_close_session(close);
                        send_session_frame(std::move(frame));
                        cleanup();
                    }
                }
                return;
            }

            // Successful read -- forward to tunnel.
            if (len > 0) {
                std::vector<std::uint8_t> data(
                    target_read_buf_.begin(),
                    target_read_buf_.begin() + static_cast<std::ptrdiff_t>(len));
                forward_data_to_tunnel(std::move(data));
            }

            // Continue reading if still connected.
            if (state_ == SessionState::Connected
                || state_ == SessionState::HalfClosedLocal) {
                start_target_read();
            }
        });
}

void AgentSession::handle_half_close(const rmt::protocol::SessionHalfCloseMessage& /*msg*/) {
    if (state_ == SessionState::Closed) {
        return;
    }

    logger_.info("AgentSession session_id=" + std::to_string(session_id_)
                 + " received HALF_CLOSE");

    // Shutdown target write side.
    if (target_socket_ && target_socket_->is_open() && !target_write_closed_) {
        target_write_closed_ = true;
        // Drain any pending write queue.
        target_write_queue_.clear();
        writing_to_target_ = false;

        asio::error_code ignored;
        target_socket_->shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
    }

    if (state_ == SessionState::Connected) {
        state_ = SessionState::HalfClosedLocal;
    } else if (state_ == SessionState::HalfClosedRemote) {
        // Both sides half-closed -> fully closed.
        state_ = SessionState::Closed;
        cleanup();
    }
}

void AgentSession::handle_close(const rmt::protocol::CloseSessionMessage& /*msg*/) {
    if (state_ == SessionState::Closed) {
        return;
    }

    logger_.info("AgentSession session_id=" + std::to_string(session_id_)
                 + " received CLOSE");

    state_ = SessionState::Closed;
    cleanup();
}

void AgentSession::cleanup() {
    // Cancel connect timer.
    if (connect_timer_) {
        asio::error_code ignored;
        connect_timer_->cancel(ignored);
        connect_timer_.reset();
    }

    // Close target socket.
    if (target_socket_ && target_socket_->is_open()) {
        asio::error_code ignored;
        target_socket_->cancel(ignored);
        target_socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        target_socket_->close(ignored);
    }

    target_write_queue_.clear();
    writing_to_target_ = false;

    // Transition to Closed and fire on_closed callback exactly once.
    SessionState prev = state_;
    state_ = SessionState::Closed;

    if (prev != SessionState::Closed && on_closed_) {
        auto cb = std::move(on_closed_);
        on_closed_ = nullptr;
        cb(session_id_);
    }
}

void AgentSession::send_session_frame(rmt::protocol::Frame frame) {
    frame.header.session_id = session_id_;
    if (tunnel_) {
        tunnel_->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
            // Errors logged by TunnelConnection.
        });
    }
}

SessionState AgentSession::state() const noexcept {
    return state_;
}

bool AgentSession::is_active() const noexcept {
    return state_ != SessionState::Idle && state_ != SessionState::Closed;
}

rmt::protocol::SessionId AgentSession::session_id() const noexcept {
    return session_id_;
}

std::uint64_t AgentSession::bytes_to_target() const noexcept {
    return bytes_sent_;
}

std::uint64_t AgentSession::bytes_from_target() const noexcept {
    return bytes_received_;
}

void AgentSession::set_on_closed(std::function<void(std::uint32_t session_id)> cb) {
    on_closed_ = std::move(cb);
}

}  // namespace rmt::tunnel
