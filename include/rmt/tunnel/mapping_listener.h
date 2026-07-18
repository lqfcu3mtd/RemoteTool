#pragma once
// MappingListener — listens on a local TCP port. When a client connects, it
// creates a Session via SessionManager and forwards the connection through
// the agent tunnel (Phase 2, single session).

#include <cstdint>
#include <memory>
#include <string>

#include <asio.hpp>

#include "rmt/common/log.h"
#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

class SessionManager;
class DeviceManager;

class MappingListener {
public:
    MappingListener(asio::io_context& io, SessionManager& session_mgr,
                    DeviceManager& devices);

    MappingListener(const MappingListener&) = delete;
    MappingListener& operator=(const MappingListener&) = delete;

    // Start listening on bind_host:bind_port. When a local client connects,
    // create a session for the given device_id/mapping_id that forwards to
    // target_host:target_port on the agent side.
    void start(const std::string& bind_host, std::uint16_t bind_port,
               const std::string& device_id, const std::string& mapping_id,
               const std::string& target_host, std::uint16_t target_port,
               int connect_timeout_ms = 10000);

    // Stop accepting new connections and close the acceptor.
    void stop();

    // Returns the actual listening port (useful when bind_port was 0).
    std::uint16_t local_port() const;

private:
    void do_accept();

    asio::io_context& io_;
    SessionManager& session_mgr_;
    DeviceManager& devices_;
    std::shared_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::shared_ptr<asio::ip::tcp::socket> accept_socket_;

    // Saved parameters for the mapping.
    std::string device_id_;
    std::string mapping_id_;
    std::string target_host_;
    std::uint16_t target_port_ = 0;
    int connect_timeout_ms_ = 10000;

    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
