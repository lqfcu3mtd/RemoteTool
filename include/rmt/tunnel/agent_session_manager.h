#pragma once
// AgentSessionManager — Agent-side session dispatcher (Phase 6).
//
// Mirrors the RemoteTool-side pattern (DeviceManager::set_on_unhandled_frame
// → SessionManager): the manager takes over AgentConnection::set_on_frame and
// routes session-scoped frames to per-session AgentSession handlers:
//
//   OPEN_SESSION      → decode, create AgentSession (target
//                       connect happens inside AgentSession)
//   SESSION_DATA / SESSION_HALF_CLOSE / CLOSE_SESSION
//                     → forward to the matching AgentSession by session_id
//
// Thread safety: all methods must run on the AgentConnection's io_context
// thread, except clear_all()/attach() which the app may post there.
//
// Lifetime: the manager must outlive the AgentConnection it is attached to.
// The frame callback captures a raw `this`; the app guarantees ordering by
// destroying the manager only after the io thread has stopped (or by
// re-creating both together).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "rmt/common/log.h"
#include "rmt/tunnel/agent_connection.h"
#include "rmt/tunnel/agent_session.h"

namespace rmt::tunnel {

class AgentSessionManager {
public:
    AgentSessionManager() = default;

    AgentSessionManager(const AgentSessionManager&) = delete;
    AgentSessionManager& operator=(const AgentSessionManager&) = delete;

    // Take over the connection's frame callback. Any previously installed
    // on_frame handler is replaced.
    void attach(const std::shared_ptr<AgentConnection>& agent);

    // Close and drop all sessions (e.g. after the tunnel went down or before
    // reconnecting with a fresh connection).
    void clear_all();

    std::size_t active_session_count() const noexcept { return sessions_.size(); }

    // Optional log/event hook (session opened / closed / failed). Fired on
    // the io thread; the app must marshal to the UI thread itself.
    void set_on_event(std::function<void(std::string)> cb) { on_event_ = std::move(cb); }

private:
    void on_frame(const protocol::Frame& frame);
    void handle_open_session(const protocol::Frame& frame);
    void emit(std::string text);

    std::weak_ptr<AgentConnection> agent_;
    std::unordered_map<std::uint32_t, std::shared_ptr<AgentSession>> sessions_;
    std::function<void(std::string)> on_event_;
    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
