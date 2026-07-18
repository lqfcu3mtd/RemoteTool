#pragma once
// DeviceManager — manages Agent device lifecycle on the RemoteTool side.
//
// Responsibilities:
//   - Accept new connections from Acceptor (on_new_connection)
//   - Process HELLO frames: register/authenticate device, detect duplicates
//   - Process HEARTBEAT frames: reply with matching ACK
//   - Monitor heartbeat timeouts (30 s without any valid frame → offline)
//   - Fire on_device_online / on_device_offline callbacks
//
// Thread safety: single-threaded on the io_context thread. No internal locking.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/common/log.h"
#include "rmt/protocol/messages.h"

namespace rmt::tunnel {

class TunnelConnection;

struct DeviceEntry {
    std::string device_id;
    bool online = false;
    std::string agent_version;
    std::string platform;
    std::string remote_address;
    std::chrono::steady_clock::time_point last_frame_time;
};

class DeviceManager {
public:
    explicit DeviceManager(asio::io_context& io);
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // ===== Acceptor entry point =====
    // Register a newly accepted connection. Sets up frame/close callbacks and
    // begins async receive.
    void on_new_connection(std::shared_ptr<TunnelConnection> conn);

    // ===== Query =====
    std::size_t device_count() const noexcept;
    bool is_online(const std::string& device_id) const noexcept;

    // ===== User callbacks =====
    void set_on_device_online(std::function<void(const DeviceEntry&)> cb);
    void set_on_device_offline(std::function<void(const DeviceEntry&)> cb);

    // ===== Cleanup timer =====
    void start_cleanup_timer();

    // ------ test entry points (public for direct test access) ------

    void handle_hello(std::shared_ptr<TunnelConnection> conn,
                      const rmt::protocol::HelloMessage& hello);
    void handle_heartbeat(std::shared_ptr<TunnelConnection> conn,
                          const rmt::protocol::HeartbeatMessage& hb);
    void on_connection_closed(std::shared_ptr<TunnelConnection> conn,
                              rmt::ErrorCode err);
    void cleanup_timeouts();
    // Allow tests to age out a device for timeout verification.
    void set_last_frame_time_for_test(const std::string& device_id,
                                      std::chrono::steady_clock::time_point t);

private:
    void on_frame_received(std::shared_ptr<TunnelConnection> conn,
                           const rmt::protocol::Frame& frame);
    void send_hello_ack(std::shared_ptr<TunnelConnection> conn, bool accepted,
                        const std::string& error_code = "",
                        const std::string& message = "");
    void send_heartbeat_ack(std::shared_ptr<TunnelConnection> conn,
                            long long sequence);
    void remove_device(const std::string& device_id);

    // Internal entry: pairs a connection (shared ownership for lifetime) with
    // its device metadata. The raw pointer of conn_ is the map key.
    struct ConnEntry {
        std::shared_ptr<TunnelConnection> conn;
        DeviceEntry device;
    };

    asio::io_context& io_;

    // Map raw pointer (stable key) → connection + device metadata.
    std::unordered_map<TunnelConnection*, ConnEntry> connections_;

    std::function<void(const DeviceEntry&)> on_device_online_;
    std::function<void(const DeviceEntry&)> on_device_offline_;

    asio::steady_timer cleanup_timer_;

    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
