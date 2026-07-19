#include "rmt/tunnel/agent_session_manager.h"

#include <string>
#include <utility>

#include "rmt/common/log.h"
#include "rmt/protocol/messages.h"
#include "rmt/security/target_whitelist.h"

namespace rmt::tunnel {

AgentSessionManager::AgentSessionManager(
    const rmt::security::TargetWhitelist& whitelist)
    : whitelist_(whitelist) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

void AgentSessionManager::attach(const std::shared_ptr<AgentConnection>& agent) {
    agent_ = agent;
    agent->set_on_frame([this](const protocol::Frame& frame) {
        on_frame(frame);
    });
}

void AgentSessionManager::clear_all() {
    if (!sessions_.empty()) {
        emit("closing " + std::to_string(sessions_.size()) +
             " session(s) (tunnel down)");
    }
    // AgentSession destructors clean up sockets; sends on the dead tunnel
    // fail harmlessly.
    sessions_.clear();
}

void AgentSessionManager::emit(std::string text) {
    logger_.info("AgentSessionManager: " + text);
    if (on_event_) on_event_(std::move(text));
}

void AgentSessionManager::on_frame(const protocol::Frame& frame) {
    const auto type = frame.header.type;

    if (type == protocol::MsgOpenSession) {
        handle_open_session(frame);
        return;
    }

    if (type == protocol::MsgSessionData ||
        type == protocol::MsgSessionHalfClose ||
        type == protocol::MsgCloseSession) {
        auto it = sessions_.find(frame.header.session_id);
        if (it == sessions_.end()) {
            logger_.warn("AgentSessionManager: frame for unknown session " +
                              std::to_string(frame.header.session_id));
            return;
        }
        it->second->on_session_frame(frame);
        return;
    }

    logger_.warn("AgentSessionManager: unexpected frame type 0x" +
                      std::to_string(static_cast<unsigned>(type)));
}

void AgentSessionManager::handle_open_session(const protocol::Frame& frame) {
    auto agent = agent_.lock();
    if (!agent) return;
    auto tunnel = agent->tunnel();
    if (!tunnel) {
        logger_.warn("AgentSessionManager: OPEN_SESSION but no live tunnel");
        return;
    }

    const auto sid = frame.header.session_id;
    auto decoded = protocol::decode_open_session(frame);
    if (auto* err = std::get_if<std::string>(&decoded)) {
        logger_.warn("AgentSessionManager: OPEN_SESSION decode error: " + *err);
        protocol::SessionOpenFailedMessage fail;
        fail.error_code = "PROTOCOL_ERROR";
        fail.message = *err;
        auto reply = protocol::encode_session_open_failed(fail);
        reply.header.session_id = sid;
        tunnel->send_frame(reply, [](rmt::ErrorCode) {});
        return;
    }
    const auto& msg = std::get<protocol::OpenSessionMessage>(decoded);

    if (sessions_.count(sid) != 0) {
        logger_.warn("AgentSessionManager: duplicate session id " +
                          std::to_string(sid));
        return;
    }

    auto session = std::make_shared<AgentSession>(agent->io(), tunnel, whitelist_);
    sessions_.emplace(sid, session);
    emit("session " + std::to_string(sid) + " open -> " + msg.target_host +
         ":" + std::to_string(msg.target_port));

    session->set_on_closed([this, sid](std::uint32_t) {
        emit("session " + std::to_string(sid) + " closed");
        // Defer the erase: this callback fires from inside the session's own
        // call stack, so the shared_ptr must not be dropped synchronously.
        auto agent = agent_.lock();
        if (!agent) return;
        asio::post(agent->io(), [this, sid] { sessions_.erase(sid); });
    });

    session->on_open_session(msg, sid);
}

}  // namespace rmt::tunnel
