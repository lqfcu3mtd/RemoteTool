#include "rmt/tunnel/acceptor.h"

#include <memory>

#include "rmt/common/scope_guard.h"
#include "rmt/tunnel/connection.h"
#include "rmt/tunnel/device_manager.h"

namespace rmt::tunnel {

Acceptor::Acceptor(asio::io_context& io, DeviceManager& device_mgr)
    : io_(io), device_mgr_(device_mgr), acceptor_(io) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

Acceptor::~Acceptor() {
    stop();
}

void Acceptor::start(const std::string& host, std::uint16_t port) {
    if (started_) {
        logger_.warn("Acceptor::start: already started");
        return;
    }

    asio::error_code ec;
    auto addr = asio::ip::make_address(host, ec);
    if (ec) {
        logger_.error("Acceptor start: invalid address " + host + ": " + ec.message());
        return;
    }

    asio::ip::tcp::endpoint endpoint(addr, port);
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        logger_.error("Acceptor open failed: " + ec.message());
        return;
    }

    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        logger_.warn("Acceptor set_option reuse_address failed: " + ec.message());
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        logger_.error("Acceptor bind failed: " + ec.message());
        acceptor_.close(ec);
        return;
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        logger_.error("Acceptor listen failed: " + ec.message());
        acceptor_.close(ec);
        return;
    }

    started_ = true;
    logger_.info("Acceptor listening on " + host + ":" + std::to_string(port));
    do_accept();
}

void Acceptor::stop() {
    if (!started_) return;

    started_ = false;
    asio::error_code ec;
    acceptor_.close(ec);
    if (ec) {
        logger_.warn("Acceptor close error: " + ec.message());
    } else {
        logger_.info("Acceptor stopped");
    }
}

void Acceptor::do_accept() {
    if (!started_) return;

    auto socket = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*socket,
        [this, socket](const asio::error_code& ec) {
            if (ec) {
                if (ec == asio::error::operation_aborted) {
                    return;
                }
                logger_.error("Acceptor accept error: " + ec.message());
                if (started_) {
                    do_accept();
                }
                return;
            }

            auto conn = std::make_shared<TunnelConnection>(io_, std::move(*socket));
            device_mgr_.on_new_connection(conn);

            if (started_) {
                do_accept();
            }
        });
}

}  // namespace rmt::tunnel
