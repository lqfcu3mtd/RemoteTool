#pragma once
// AgentSession -- Agent-side session handler for single-session port forwarding
// (Phase 2, PROTOCOL_SPEC.md sections 6.7-6.12).

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>

#include "rmt/common/log.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

enum class SessionState {
    Idle,
    Connecting,
    Connected,
    HalfClosedLocal,
    HalfClosedRemote,
    Closed,
};

class AgentSession : public std::enable_shared_from_this<AgentSession> {
public:
    AgentSession(asio::io_context& io,
                 std::shared_ptr<TunnelConnection> tunnel);
    ~AgentSession();

    AgentSession(const AgentSession&) = delete;
    AgentSession& operator=(const AgentSession&) = delete;

    void on_open_session(const rmt::protocol::OpenSessionMessage& msg,
                         rmt::protocol::SessionId session_id);

    void on_session_frame(const rmt::protocol::Frame& frame);

    bool is_active() const noexcept;
    SessionState state() const noexcept;
    rmt::protocol::SessionId session_id() const noexcept;

    std::uint64_t bytes_to_target() const noexcept;
    std::uint64_t bytes_from_target() const noexcept;

    void set_on_closed(std::function<void(std::uint32_t session_id)> cb);

    // Optional human-readable event hook (GUI event log). Emitted for
    // whitelist rejections, target connect failures, connect success and
    // target I/O errors — the failures are otherwise only on stderr.
    void set_on_event(std::function<void(std::string)> cb);

private:
    void on_target_connected(const std::string& connected_host,
                             int connected_port);
    void forward_data_to_target(std::vector<std::uint8_t> data);
    void forward_data_to_tunnel(std::vector<std::uint8_t> data);
    void handle_close(const rmt::protocol::CloseSessionMessage& msg);
    void handle_half_close(const rmt::protocol::SessionHalfCloseMessage& msg);
    void cleanup();
    void start_target_read();
    void do_target_write();
    // Send FIN to the target once the local side half-closed AND all queued
    // writes have drained. Must not be done eagerly: clearing the queue or
    // shutting down early would drop client data not yet written.
    void maybe_shutdown_target_write();
    void send_session_frame(rmt::protocol::Frame frame);
    void emit(std::string text);

    asio::io_context& io_;
    std::shared_ptr<TunnelConnection> tunnel_;
    std::shared_ptr<asio::ip::tcp::socket> target_socket_;
    SessionState state_ = SessionState::Idle;
    rmt::protocol::SessionId session_id_ = 0;
    std::string mapping_id_;
    std::string target_host_;
    int target_port_ = 0;
    std::uint64_t bytes_sent_ = 0;
    std::uint64_t bytes_received_ = 0;
    std::shared_ptr<asio::steady_timer> connect_timer_;
    struct TargetWriteEntry { std::vector<std::uint8_t> data; };
    std::deque<TargetWriteEntry> target_write_queue_;
    bool writing_to_target_ = false;
    std::vector<std::uint8_t> target_read_buf_;
    bool target_read_closed_ = false;
    bool target_write_closed_ = false;
    bool target_write_shutdown_done_ = false;
    std::function<void(std::uint32_t session_id)> on_closed_;
    std::function<void(std::string)> on_event_;
    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
