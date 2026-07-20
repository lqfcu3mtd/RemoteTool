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
    std::shared_ptr<TunnelConnection> tunnel,
    const std::string& mapping_id) {
    auto sid = id_alloc_.allocate();
    if (sid == 0) {
        logger_.error("SessionManager: session ID exhausted");
        return 0;
    }

    // Phase 3b: enforce per-mapping concurrency limit.
    if (active_session_count() >= static_cast<std::size_t>(max_sessions_)) {
        logger_.warn("SessionManager: max sessions reached (" +
                     std::to_string(max_sessions_) + "), rejecting new session");
        id_alloc_.release(sid);
        return 0;
    }

    SessionEntry entry;
    entry.state = SessionState::Idle;
    entry.tunnel = std::move(tunnel);
    entry.device_id = device_id;
    entry.mapping_id = mapping_id;
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
        // Data from the agent is still valid after the local side has
        // half-closed (HalfClosedLocal): the remote→local direction stays
        // open until the agent half-closes or closes the session.
        if (s->state != SessionState::Connected &&
            s->state != SessionState::HalfClosedLocal) {
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
        if (s->state != SessionState::Connected &&
            s->state != SessionState::HalfClosedLocal) {
            logger_.warn("SessionManager: HALF_CLOSE in non-connected state");
            return;
        }

        auto result = rmt::protocol::decode_session_half_close(frame);
        if (auto* msg =
                std::get_if<rmt::protocol::SessionHalfCloseMessage>(&result)) {
            if (msg->direction == "write") {
                const bool both_sides =
                    (s->state == SessionState::HalfClosedLocal);
                s->state = SessionState::HalfClosedRemote;
                logger_.info("SessionManager: session "
                             + std::to_string(session_id)
                             + " half-closed (remote write)");
                // Defer FIN/close until the local write queue has drained:
                // SESSION_DATA frames precede this HALF_CLOSE on the tunnel,
                // and their payload must reach the local client first.
                s->local_write_shutdown_pending = true;
                if (both_sides) {
                    s->local_close_after_drain = true;
                }
                maybe_finish_local_close(session_id);
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

    // Local→agent data remains valid after the remote side half-closed
    // (HalfClosedRemote): the local→remote direction stays open.
    if (s->state != SessionState::Connected &&
        s->state != SessionState::HalfClosedRemote) {
        return;
    }

    std::size_t len = data.size();
    if (len == 0) {
        return;
    }

    auto frame = rmt::protocol::encode_session_data(
        session_id, data.data(), len);
    s->bytes_local_out += len;

    // Phase 3b: backpressure tracking.
    constexpr std::uint64_t kHighWater = 256 * 1024;
    constexpr std::uint64_t kLowWater  = 128 * 1024;
    s->pending_bytes += len;
    // Pause local read if we are above high-water.
    if (s->pending_bytes >= kHighWater && !s->read_paused) {
        s->read_paused = true;
        logger_.info("SessionManager: session " + std::to_string(session_id)
                     + " paused (backpressure, pending="
                     + std::to_string(s->pending_bytes) + ")");
    }

    auto sid_cb = session_id;
    s->tunnel->send_frame(frame, [this, sid_cb, len](rmt::ErrorCode /*ec*/) {
        auto* s2 = find_session(sid_cb);
        if (!s2) return;
        if (s2->pending_bytes >= len) s2->pending_bytes -= len;
        else s2->pending_bytes = 0;
        // Resume read when below low-water.
        if (s2->read_paused && s2->pending_bytes < kLowWater) {
            s2->read_paused = false;
            logger_.info("SessionManager: session " + std::to_string(sid_cb)
                         + " resumed (pending="
                         + std::to_string(s2->pending_bytes) + ")");
            start_local_read(sid_cb);
        }
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
    if (s->read_paused) {   // Phase 3b: backpressure
        return;
    }
    // Local reads continue while the local→agent direction is open.
    if (s->state != SessionState::Connected &&
        s->state != SessionState::HalfClosedRemote) {
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

    s->local_write_queue.push_back(
        std::make_shared<const std::vector<std::uint8_t>>(std::move(data)));
    if (!s->writing_local) {
        do_local_write_next(session_id);
    }
}

void SessionManager::do_local_write_next(std::uint32_t session_id) {
    auto* s = find_session(session_id);
    if (!s) {
        return;
    }
    if (!s->local_socket) {
        s->local_write_queue.clear();
        s->writing_local = false;
        maybe_finish_local_close(session_id);
        return;
    }
    if (s->local_write_queue.empty()) {
        s->writing_local = false;
        maybe_finish_local_close(session_id);
        return;
    }

    s->writing_local = true;
    auto buf = s->local_write_queue.front();
    asio::async_write(*s->local_socket, asio::buffer(*buf),
        [this, session_id, buf](const asio::error_code& ec, std::size_t) {
            auto* s2 = find_session(session_id);
            if (!s2) return;

            if (!s2->local_write_queue.empty()) {
                s2->local_write_queue.pop_front();
            }

            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    logger_.error("SessionManager: session "
                                  + std::to_string(session_id)
                                  + " local write error: " + ec.message());
                    close_session(session_id);
                    return;
                }
                // Aborted: the socket is being torn down; drop the rest.
                s2->local_write_queue.clear();
                s2->writing_local = false;
                return;
            }

            do_local_write_next(session_id);
        });
}

void SessionManager::maybe_finish_local_close(std::uint32_t session_id) {
    auto* s = find_session(session_id);
    if (!s || !s->local_write_shutdown_pending) {
        return;
    }
    if (s->writing_local || !s->local_write_queue.empty()) {
        return;  // still draining; the write completion handler re-calls us
    }

    s->local_write_shutdown_pending = false;
    if (s->local_socket) {
        asio::error_code ignored;
        s->local_socket->shutdown(asio::ip::tcp::socket::shutdown_send,
                                  ignored);
    }
    if (s->local_close_after_drain) {
        // Local write and remote write are both closed: the session is
        // finished (all queued data has been delivered).
        transition_to_closed(session_id, "both sides half-closed");
    }
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

std::size_t SessionManager::active_session_count() const {
    std::size_t count = 0;
    for (const auto& [id, entry] : sessions_) {
        if (entry.state != SessionState::Idle && entry.state != SessionState::Closed)
            ++count;
    }
    return count;
}

std::size_t SessionManager::active_sessions_for_mapping(
    const std::string& mapping_id) const {
    std::size_t count = 0;
    for (const auto& [id, entry] : sessions_) {
        if (entry.mapping_id == mapping_id &&
            entry.state != SessionState::Idle &&
            entry.state != SessionState::Closed)
            ++count;
    }
    return count;
}

void SessionManager::remove_all_sessions_for_device(const std::string& device_id) {
    // Collect matching session IDs first (cannot iterate+erase concurrently).
    std::vector<std::uint32_t> to_close;
    for (const auto& [id, entry] : sessions_) {
        if (entry.device_id == device_id)
            to_close.push_back(id);
    }
    for (auto id : to_close) {
        close_session(id);
    }
    logger_.info("SessionManager: removed " + std::to_string(to_close.size()) +
                 " sessions for device " + device_id);
}

}  // namespace rmt::tunnel
