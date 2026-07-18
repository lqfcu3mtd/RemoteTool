#pragma once
// Acceptor — TCP listener for incoming Agent connections (Phase 1, no TLS).
//
// Accepts connections on a host:port pair, wraps each accepted socket into a
// TunnelConnection, and hands it to DeviceManager for frame processing and
// device lifecycle management.
//
// Thread safety: all operations must run on the asio::io_context thread.

#include <cstdint>
#include <memory>
#include <string>

#include <asio.hpp>

#include "rmt/common/log.h"

namespace rmt::tunnel {

class DeviceManager;

class Acceptor {
public:
    Acceptor(asio::io_context& io, DeviceManager& device_mgr);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void start(const std::string& host, std::uint16_t port);
    void stop();

private:
    void do_accept();

    asio::io_context& io_;
    DeviceManager& device_mgr_;
    asio::ip::tcp::acceptor acceptor_;
    bool started_ = false;
    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
