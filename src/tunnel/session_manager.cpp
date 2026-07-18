#include "rmt/tunnel/session_manager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rmt/common/log.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/tunnel/device_manager.h"

namespace rmt::tunnel {

namespace {

constexpr std::size_t kReadBufSize = 16384;

}  // namespace

SessionManager::SessionManager(asio::io_context& io, DeviceManager& devices,
                               rmt::session::SessionIdAllocator& id_alloc)
    : io_(io), devices_(devices), id_alloc_(id_alloc) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

std::uint32_t SessionManager::create_session(
    const std::string& device_id,
    std::shared_ptr<TunnelConnection> tunnel) {
    auto sid = id_alloc_.allocate();
    if (sid == 0) {
        logger_.error("SessionManager: session ID exhausted");
        return 0;
    }

    SessionEntry entry;
    entry.state = SessionState::Idle;
    entry.tunnel = std::move(tunnel);
    entry.device_id = device_id;
    entry.read_buf.resize(kReadBufSize);

    sessions_[sid] = std::move(entry);

    logger_.info("SessionManager: session " + std::to_string(sid)
                 + " created (device=" + device_id + ")");
    return sid;
}

void SessionManager::attach_local_socket(
    std::uint32_t session_id,
    std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto* s = find_session(session_id);
    if (!s) {
        logger_.error("SessionManager: attach_local_socket for unknown session "
                      + std::to_string(session_id));
        return;
    }
    s->local_socket = std::move(socket);
}

void SessionManager::on_session_frame(std::uint32_t session_id,
                                       const rmt::protocol::Frame& frame) {
    auto* s = find_session(session_id);
    if (!s) {
        logger_.warn("SessionManager: frame for unknown session "
                     + std::to_string(session_id));
        return;
    }

    using rmt::protocol::MsgSessionOpened;
    using rmt::protocol::MsgSessionOpenFailed;
    using rmt::protocol::MsgSessionData;
    using rmt::protocol::MsgSessionHalfClose;
    using rmt::protocol::MsgCloseSession;

    switch (frame.header.type) {
    case MsgSessionOpened: {
        if (s->state != SessionState::Idle) {
            logger_.warn("SessionManager: SESSION_OPENED in unexpected state");
            return;
        }

        auto result = rmt::protocol::decode_session_opened(frame);
        if (auto* msg =
                std::get_if<rmt::protocol::SessionOpenedMessage>(&result)) {
            s->state = SessionState::Connected;
            logger_.info("SessionManager: session " + std::to_string(session_id)
                         + " active, target=" + msg->connected_host + ":"
                         + std::to_string(msg->connected_port));

            // Start bidirectional forwarding.
            if (s->local_socket) {
                start_local_read(session_id);
            }
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("SessionManager: SESSION_OPENED decode error: "
                          + err);
        }
        break;
    }

    case MsgSessionOpenFailed: {
        if (s->state != SessionState::Idle) {
            logger_.warn("SessionManager: SESSION_OPEN_FAILED in unexpected state");
            return;
        }

        auto result = rmt::protocol::decode_session_open_failed(frame);
        if (auto* msg =
                std::get_if<rmt::protocol::SessionOpenFailedMessage>(&result)) {
            logger_.error("SessionManager: session " + std::to_string(session_id)
                          + " open failed: " + msg->error_code
                          + " - " + msg->message);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("SessionManager: SESSION_OPEN_FAILED decode error: "
                          + err);
        }

        // Close the local client connection and clean up.
        transition_to_closed(session_id, "open-failed");
        break;
    }

    case MsgSessionData: {
        if (s->state != SessionState::Connected) {
            logger_.warn("SessionManager: SESSION_DATA in non-connected state");
            return;
        }

        auto payload = rmt::protocol::decode_session_data(frame);
        std::size_t len = payload.size();
        if (len > 0 && s->local_socket) {
            s->bytes_local_in += len;
            do_local_write(session_id, std::move(payload));
        }
        break;
    }

    case MsgSessionHalfClose: {
        if (s->state != SessionState::Connected) {
            logger_.warn("SessionManager: HALF_CLOSE in non-connected state");
            return;
        }

        auto result = rmt::protocol::decode_session_half_close(frame);
        if (auto* msg =
                std::get_if<rmt::protocol::SessionHalfCloseMessage>(&result)) {
            if (msg->direction == "write") {
                s->state = SessionState::HalfClosedRemote;
                logger_.info("SessionManager: session "
                             + std::to_string(session_id)
                             + " half-closed (remote write)");
                // Shutdown local socket write side.
                if (s->local_socket) {
                    asio::error_code ignored;
                    s->local_socket->shutdown(
                        asio::ip::tcp::socket::shutdown_send, ignored);
                }
            }
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("SessionManager: HALF_CLOSE decode error: " + err);
        }
        break;
    }

    case MsgCloseSession: {
        if (s->state == SessionState::Closed) {
            return;
        }

        auto result = rmt::protocol::decode_close_session(frame);
        std::string reason = "unknown";
        if (auto* msg =
                std::get_if<rmt::protocol::CloseSessionMessage>(&result)) {
            reason = msg->reason;
            logger_.info("SessionManager: session " + std::to_string(session_id)
                         + " close received: " + reason);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("SessionManager: CLOSE_SESSION decode error: " + err);
        }

        transition_to_closed(session_id, reason);
        break;
    }

    default:
        logger_.warn("SessionManager: unhandled session frame type 0x"
                     + std::to_string(static_cast<unsigned>(frame.header.type))
                     + " for session " + std::to_string(session_id));
        break;
    }
}

void SessionManager::forward_local_to_agent(
    std::uint32_t session_id, std::vector<std::uint8_t> data) {
    auto* s = find_session(session_id);
    if (!s || !s->tunnel) {
        return;
    }

    if (s->state != SessionState::Connected) {
        return;
    }

    std::size_t len = data.size();
    if (len == 0) {
        return;
    }

    auto frame = rmt::protocol::encode_session_data(
        session_id, data.data(), len);
    s->bytes_local_out += len;

    s->tunnel->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
        // Errors logged by TunnelConnection.
    });
}

void SessionManager::close_session(std::uint32_t session_id) {
    auto* s = find_session(session_id);
    if (!s || s->state == SessionState::Closed) {
        return;
    }

    logger_.info("SessionManager: closing session "
                 + std::to_string(session_id));

    // Send CLOSE_SESSION to the agent.
    if (s->tunnel && s->tunnel->state() == TunnelState::Connected) {
        rmt::protocol::CloseSessionMessage close_msg;
        close_msg.reason = "NORMAL";
        close_msg.message = "";
        auto frame = rmt::protocol::encode_close_session(close_msg);
        frame.header.session_id = session_id;
        s->tunnel->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
            // Errors logged by TunnelConnection.
        });
    }

    transition_to_closed(session_id, "local-close");
}

std::uint64_t SessionManager::bytes_local_in(std::uint32_t sid) const {
    auto* s = find_session(sid);
    return s ? s->bytes_local_in : 0;
}

std::uint64_t SessionManager::bytes_local_out(std::uint32_t sid) const {
    auto* s = find_session(sid);
    return s ? s->bytes_local_out : 0;
}

void SessionManager::set_on_session_closed(
    std::function<void(std::uint32_t)> cb) {
    on_session_closed_ = std::move(cb);
}

SessionState SessionManager::session_state(std::uint32_t session_id) const {
    auto* s = find_session(session_id);
    return s ? s->state : SessionState::Closed;
}

// ---- private ----------------------------------------------------------

SessionManager::SessionEntry* SessionManager::find_session(
    std::uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return nullptr;
    return &it->second;
}

const SessionManager::SessionEntry* SessionManager::find_session(
    std::uint32_t session_id) const {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return nullptr;
    return &it->second;
}

void SessionManager::start_local_read(std::uint32_t session_id) {
    auto* s = find_session(session_id);
    if (!s || !s->local_socket || s->reading_local) {
        return;
    }
    if (s->state != SessionState::Connected) {
        return;
    }

    s->reading_local = true;
    auto sid = session_id;
    s->local_socket->async_read_some(
        asio::buffer(s->read_buf),
        [this, sid](const asio::error_code& ec, std::size_t len) {
            auto* s2 = find_session(sid);
            if (!s2) return;
            s2->reading_local = false;

            if (ec) {
                if (ec == asio::error::eof) {
                    logger_.info("SessionManager: session "
                                 + std::to_string(sid)
                                 + " local client EOF");
                    // Send HALF_CLOSE to agent.
                    if (s2->tunnel
                        && s2->tunnel->state() == TunnelState::Connected) {
                        rmt::protocol::SessionHalfCloseMessage hc;
                        hc.direction = "write";
                        auto frame =
                            rmt::protocol::encode_session_half_close(hc);
                        frame.header.session_id = sid;
                        s2->tunnel->send_frame(frame,
                            [](rmt::ErrorCode /*ec*/) {});
                    }
                    if (s2->state == SessionState::Connected) {
                        s2->state = SessionState::HalfClosedLocal;
                    }
                } else if (ec != asio::error::operation_aborted) {
                    logger_.error("SessionManager: session "
                                  + std::to_string(sid)
                                  + " local read error: " + ec.message());
                    close_session(sid);
                }
                return;
            }

            if (len > 0 && s2->state == SessionState::Connected) {
                std::vector<std::uint8_t> data(
                    s2->read_buf.begin(), s2->read_buf.begin() + len);
                forward_local_to_agent(sid, std::move(data));
            }

            // Continue reading.
            if (s2->state == SessionState::Connected) {
                start_local_read(sid);
            }
        });
}

void SessionManager::do_local_write(std::uint32_t session_id,
                                     std::vector<std::uint8_t> data) {
    auto* s = find_session(session_id);
    if (!s || !s->local_socket) {
        return;
    }

    auto buf = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
    asio::async_write(*s->local_socket, asio::buffer(*buf),
        [this, session_id, buf](const asio::error_code& ec, std::size_t) {
            if (ec && ec != asio::error::operation_aborted) {
                logger_.error("SessionManager: session "
                              + std::to_string(session_id)
                              + " local write error: " + ec.message());
                close_session(session_id);
            }
        });
}

void SessionManager::transition_to_closed(std::uint32_t session_id,
                                           const std::string& reason) {
    auto* s = find_session(session_id);
    if (!s || s->state == SessionState::Closed) {
        return;
    }

    logger_.info("SessionManager: session " + std::to_string(session_id)
                 + " closed (" + reason + "), in="
                 + std::to_string(s->bytes_local_in)
                 + ", out=" + std::to_string(s->bytes_local_out));

    s->state = SessionState::Closed;

    if (on_session_closed_) {
        on_session_closed_(session_id);
    }

    cleanup_session(session_id);
}

void SessionManager::cleanup_session(std::uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;

    auto& s = it->second;

    // Close local socket.
    if (s.local_socket) {
        asio::error_code ignored;
        s.local_socket->cancel(ignored);
        s.local_socket->shutdown(asio::ip::tcp::socket::shutdown_both,
                                  ignored);
        s.local_socket->close(ignored);
        s.local_socket.reset();
    }

    // Release session ID.
    id_alloc_.release(session_id);

    // Remove from map (tunnel shared_ptr may keep the tunnel alive).
    sessions_.erase(it);
}

}  // namespace rmt::tunnel
