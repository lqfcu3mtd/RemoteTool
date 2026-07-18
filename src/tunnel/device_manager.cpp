#include "rmt/tunnel/device_manager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rmt/protocol/frame.h"
#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

namespace {

constexpr auto kCleanupInterval = std::chrono::seconds(5);
constexpr auto kHeartbeatTimeout = std::chrono::seconds(30);
constexpr auto kServerVersion = "0.1.0";
constexpr int kHeartbeatIntervalMs = 10000;
constexpr int kHeartbeatTimeoutMs = 30000;
constexpr int kMaxSessions = 128;

}  // namespace

DeviceManager::DeviceManager(asio::io_context& io)
    : io_(io), cleanup_timer_(io) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

DeviceManager::~DeviceManager() {
    asio::error_code ignored;
    cleanup_timer_.cancel(ignored);
    for (auto& kv : connections_) {
        if (kv.second.conn) {
            kv.second.conn->close();
        }
    }
    connections_.clear();
}

// ===== Acceptor entry =====

void DeviceManager::on_new_connection(std::shared_ptr<TunnelConnection> conn) {
    if (!conn) {
        logger_.error("DeviceManager::on_new_connection called with null conn");
        return;
    }

    auto* raw = conn.get();
    ConnEntry entry;
    entry.conn = conn;
    entry.device.remote_address = conn->remote_address();

    connections_[raw] = std::move(entry);

    // Capture shared_ptr in callbacks to keep the connection alive.
    conn->set_on_frame([this, conn](const rmt::protocol::Frame& frame) {
        on_frame_received(conn, frame);
    });
    conn->set_on_closed([this, conn](rmt::ErrorCode err) {
        on_connection_closed(conn, err);
    });

    conn->begin_receive();
}

// ===== Query =====

std::size_t DeviceManager::device_count() const noexcept {
    std::size_t count = 0;
    for (const auto& kv : connections_) {
        if (kv.second.device.online) ++count;
    }
    return count;
}

bool DeviceManager::is_online(const std::string& device_id) const noexcept {
    for (const auto& kv : connections_) {
        if (kv.second.device.online
            && kv.second.device.device_id == device_id) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<TunnelConnection> DeviceManager::get_connection(
    const std::string& device_id) const {
    for (const auto& kv : connections_) {
        if (kv.second.device.online
            && kv.second.device.device_id == device_id) {
            return kv.second.conn;
        }
    }
    return nullptr;
}

// ===== User callbacks =====

void DeviceManager::set_on_device_online(
    std::function<void(const DeviceEntry&)> cb) {
    on_device_online_ = std::move(cb);
}

void DeviceManager::set_on_device_offline(
    std::function<void(const DeviceEntry&)> cb) {
    on_device_offline_ = std::move(cb);
}

// ===== Cleanup timer =====

void DeviceManager::start_cleanup_timer() {
    cleanup_timer_.expires_after(kCleanupInterval);
    cleanup_timer_.async_wait([this](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        cleanup_timeouts();
        start_cleanup_timer();
    });
}

// ===== Frame dispatch =====

void DeviceManager::on_frame_received(std::shared_ptr<TunnelConnection> conn,
                                       const rmt::protocol::Frame& frame) {
    auto* raw = conn.get();
    auto it = connections_.find(raw);
    if (it == connections_.end()) {
        logger_.warn("Frame received from unknown connection ("
                     + conn->remote_address() + ")");
        return;
    }

    // Update last_frame_time for every valid frame (per PROTOCOL_SPEC §10).
    it->second.device.last_frame_time = std::chrono::steady_clock::now();

    using rmt::protocol::MsgHello;
    using rmt::protocol::MsgHeartbeat;

    auto type = frame.header.type;

    if (type == MsgHello) {
        auto result = rmt::protocol::decode_hello(frame);
        if (auto* hello = std::get_if<rmt::protocol::HelloMessage>(&result)) {
            handle_hello(conn, *hello);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("DeviceManager: HELLO decode error: " + err
                          + " from " + conn->remote_address());
            conn->close();
        }
    } else if (type == MsgHeartbeat) {
        auto result = rmt::protocol::decode_heartbeat(frame);
        if (auto* hb = std::get_if<rmt::protocol::HeartbeatMessage>(&result)) {
            handle_heartbeat(conn, *hb);
        } else {
            auto& err = std::get<std::string>(result);
            logger_.error("DeviceManager: HEARTBEAT decode error: " + err
                          + " from " + conn->remote_address());
            conn->close();
        }
    } else {
        if (on_unhandled_frame_) {
            on_unhandled_frame_(conn, frame);
        } else {
            logger_.warn("DeviceManager: unexpected frame type 0x"
                         + std::to_string(static_cast<unsigned>(type))
                         + " from " + conn->remote_address());
        }
    }
}

// ===== HELLO =====

void DeviceManager::handle_hello(std::shared_ptr<TunnelConnection> conn,
                                  const rmt::protocol::HelloMessage& hello) {
    auto* raw = conn.get();
    auto it = connections_.find(raw);
    if (it == connections_.end()) {
        logger_.error("handle_hello: connection not found for "
                      + hello.device_id);
        return;
    }

    // PROTOCOL_SPEC §8.1: duplicate device_id → reject new, don't preempt old.
    if (is_online(hello.device_id)) {
        logger_.warn("DeviceManager: duplicate device_id \""
                     + hello.device_id
                     + "\" — rejecting new connection from "
                     + conn->remote_address());
        send_hello_ack(conn, false, "DEVICE_BUSY",
                       "device \"" + hello.device_id + "\" is already online");
        return;
    }

    auto& dev = it->second.device;
    dev.device_id = hello.device_id;
    dev.online = true;
    dev.agent_version = hello.agent_version;
    dev.platform = hello.platform;
    dev.last_frame_time = std::chrono::steady_clock::now();
    // remote_address already set by on_new_connection.

    logger_.info("DeviceManager: device \"" + hello.device_id
                 + "\" online (" + dev.remote_address + ")");

    send_hello_ack(conn, true);

    if (on_device_online_) {
        on_device_online_(dev);
    }
}

// ===== HEARTBEAT =====

void DeviceManager::handle_heartbeat(std::shared_ptr<TunnelConnection> conn,
                                      const rmt::protocol::HeartbeatMessage& hb) {
    auto* raw = conn.get();
    auto it = connections_.find(raw);
    if (it == connections_.end()) {
        logger_.error("handle_heartbeat: connection not found");
        return;
    }

    // Reply with same sequence (PROTOCOL_SPEC §10).
    send_heartbeat_ack(conn, hb.sequence);
}

// ===== Connection closed =====

void DeviceManager::on_connection_closed(std::shared_ptr<TunnelConnection> conn,
                                          rmt::ErrorCode /*err*/) {
    auto* raw = conn.get();
    auto it = connections_.find(raw);
    if (it == connections_.end()) {
        return;
    }

    bool was_online = it->second.device.online;
    std::string device_id = it->second.device.device_id;
    DeviceEntry entry_copy = it->second.device;

    connections_.erase(it);

    if (was_online) {
        logger_.info("DeviceManager: device \"" + device_id
                     + "\" offline (connection closed)");
        if (on_device_offline_) {
            on_device_offline_(entry_copy);
        }
    }
}

// ===== Timeout cleanup =====

void DeviceManager::cleanup_timeouts() {
    auto now = std::chrono::steady_clock::now();

    // Collect timed-out device IDs first (don't modify map while iterating).
    std::vector<std::string> timed_out_ids;
    for (auto& kv : connections_) {
        if (kv.second.device.online) {
            auto elapsed = now - kv.second.device.last_frame_time;
            if (elapsed > kHeartbeatTimeout) {
                timed_out_ids.push_back(kv.second.device.device_id);
            }
        }
    }

    for (const auto& device_id : timed_out_ids) {
        for (auto it = connections_.begin(); it != connections_.end(); ++it) {
            if (it->second.device.device_id == device_id
                && it->second.device.online) {
                logger_.warn("DeviceManager: device \"" + device_id
                             + "\" heartbeat timeout (30 s) — taking offline");

                it->second.device.online = false;
                DeviceEntry entry_copy = it->second.device;

                auto conn = it->second.conn;
                if (conn) {
                    // Clear on_closed to avoid duplicate offline notification.
                    conn->set_on_closed(nullptr);
                    conn->close();
                }

                if (on_device_offline_) {
                    on_device_offline_(entry_copy);
                }
                break;
            }
        }
    }
}

// ===== Test helpers =====

void DeviceManager::set_last_frame_time_for_test(
    const std::string& device_id,
    std::chrono::steady_clock::time_point t) {
    for (auto& kv : connections_) {
        if (kv.second.device.device_id == device_id) {
            kv.second.device.last_frame_time = t;
            return;
        }
    }
}

// ===== Private helpers =====

void DeviceManager::send_hello_ack(std::shared_ptr<TunnelConnection> conn,
                                    bool accepted,
                                    const std::string& error_code,
                                    const std::string& message) {
    rmt::protocol::HelloAckMessage ack;
    ack.accepted = accepted;
    ack.server_version = kServerVersion;
    ack.heartbeat_interval_ms = kHeartbeatIntervalMs;
    ack.heartbeat_timeout_ms = kHeartbeatTimeoutMs;
    ack.max_sessions = accepted ? kMaxSessions : 0;
    ack.error_code = error_code;
    ack.message = message;

    auto frame = rmt::protocol::encode_hello_ack(ack);
    conn->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
        // Errors logged by TunnelConnection.
    });
}

void DeviceManager::send_heartbeat_ack(std::shared_ptr<TunnelConnection> conn,
                                        long long sequence) {
    rmt::protocol::HeartbeatAckMessage ack;
    ack.sequence = sequence;
    auto now = std::chrono::system_clock::now();
    ack.received_unix_ms = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());

    auto frame = rmt::protocol::encode_heartbeat_ack(ack);
    conn->send_frame(frame, [](rmt::ErrorCode /*ec*/) {
        // Errors logged by TunnelConnection.
    });
}

void DeviceManager::set_on_unhandled_frame(
    std::function<void(std::shared_ptr<TunnelConnection>,
                       const rmt::protocol::Frame&)> cb) {
    on_unhandled_frame_ = std::move(cb);
}

void DeviceManager::remove_device(const std::string& device_id) {
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        if (it->second.device.device_id == device_id) {
            if (it->second.conn) {
                it->second.conn->close();
            }
            connections_.erase(it);
            return;
        }
    }
}

}  // namespace rmt::tunnel
