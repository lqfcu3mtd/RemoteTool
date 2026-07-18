#pragma once
// SessionManager — manages the lifecycle of port-forwarding sessions on the
// RemoteTool side (Phase 2, single session).
//
// Responsibilities:
//   - Allocate session IDs via SessionIdAllocator
//   - Track session state per session_id
//   - Forward data bidirectionally between local client socket and agent tunnel
//   - Handle SESSION_OPENED / SESSION_OPEN_FAILED / SESSION_DATA /
//     SESSION_HALF_CLOSE / CLOSE_SESSION frames from the Agent
//   - Accumulate byte statistics

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "rmt/common/log.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/session/session_id.h"
#include "rmt/tunnel/agent_session.h"
#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

class DeviceManager;

class SessionManager {
public:
    explicit SessionManager(asio::io_context& io, DeviceManager& devices,
                            rmt::session::SessionIdAllocator& id_alloc);

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // Allocate a new session ID, register the session with the given tunnel
    // connection, and return the allocated ID. The local client socket must be
    // attached separately via attach_local_socket().
    std::uint32_t create_session(const std::string& device_id,
                                 std::shared_ptr<TunnelConnection> tunnel);

    // Attach the accepted local client socket to an existing session.
    // After this, when the session becomes Active, bidirectional forwarding
    // will be started.
    void attach_local_socket(std::uint32_t session_id,
                             std::shared_ptr<asio::ip::tcp::socket> socket);

    // Handle an incoming session-scoped frame (SESSION_OPENED / _FAILED /
    // SESSION_DATA / SESSION_HALF_CLOSE / CLOSE_SESSION) dispatched from
    // DeviceManager.
    void on_session_frame(std::uint32_t session_id,
                          const rmt::protocol::Frame& frame);

    // Data received from the local client — encode + send to agent tunnel.
    void forward_local_to_agent(std::uint32_t session_id,
                                std::vector<std::uint8_t> data);

    // Gracefully close a session from the RemoteTool side (sends CLOSE_SESSION
    // to agent).
    void close_session(std::uint32_t session_id);

    // Phase 3b: concurrency limit per mapping (default 32).
    void set_max_sessions(int limit) noexcept { max_sessions_ = limit; }
    int max_sessions() const noexcept { return max_sessions_; }
    std::size_t active_session_count() const;

    // Phase 3b: clean up all sessions for a device when it goes offline.
    void remove_all_sessions_for_device(const std::string& device_id);

    // Byte statistics (Phase 2 requirement).
    std::uint64_t bytes_local_in(std::uint32_t sid) const;
    std::uint64_t bytes_local_out(std::uint32_t sid) const;

    // Session-specific closed callback (fired once when the session reaches
    // the Closed state).
    void set_on_session_closed(std::function<void(std::uint32_t)> cb);

    // ------ test helpers ------
    // Returns the current session state for testing.
    SessionState session_state(std::uint32_t session_id) const;

private:
    struct SessionEntry {
        SessionState state = SessionState::Idle;
        std::shared_ptr<TunnelConnection> tunnel;
        std::shared_ptr<asio::ip::tcp::socket> local_socket;
        std::uint64_t bytes_local_in = 0;
        std::uint64_t bytes_local_out = 0;
        std::string device_id;
        std::vector<std::uint8_t> read_buf;

        // Track whether we are currently reading from local_socket to avoid
        // overlapping reads.
        bool reading_local = false;
        // Phase 3b: backpressure — pause local read when pending bytes exceed
        // high-water mark (256 KiB), resume below low-water (128 KiB).
        std::uint64_t pending_bytes = 0;
        bool read_paused = false;
    };

    SessionEntry* find_session(std::uint32_t session_id);
    const SessionEntry* find_session(std::uint32_t session_id) const;

    void start_local_read(std::uint32_t session_id);
    void do_local_write(std::uint32_t session_id,
                        std::vector<std::uint8_t> data);
    void transition_to_closed(std::uint32_t session_id,
                              const std::string& reason);
    void cleanup_session(std::uint32_t session_id);

    asio::io_context& io_;
    DeviceManager& devices_;
    rmt::session::SessionIdAllocator& id_alloc_;
    std::unordered_map<std::uint32_t, SessionEntry> sessions_;
    int max_sessions_ = 32;  // Phase 3b: per-mapping concurrency limit
    std::function<void(std::uint32_t)> on_session_closed_;
    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
