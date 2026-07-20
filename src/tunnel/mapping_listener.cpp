#include "rmt/tunnel/mapping_listener.h"

#include <cstdint>
#include <memory>
#include <string>

#include "rmt/common/log.h"
#include "rmt/protocol/messages.h"
#include "rmt/tunnel/connection.h"
#include "rmt/tunnel/device_manager.h"
#include "rmt/tunnel/session_manager.h"

namespace rmt::tunnel {

MappingListener::MappingListener(asio::io_context& io,
                                 SessionManager& session_mgr,
                                 DeviceManager& devices)
    : io_(io), session_mgr_(session_mgr), devices_(devices) {
    logger_.set_level(rmt::common::LogLevel::Info);
}

rmt::ErrorCode MappingListener::start(const std::string& bind_host,
                            std::uint16_t bind_port,
                            const std::string& device_id,
                            const std::string& mapping_id,
                            const std::string& target_host,
                            std::uint16_t target_port,
                            int connect_timeout_ms) {
    // Save mapping parameters.
    device_id_ = device_id;
    mapping_id_ = mapping_id;
    target_host_ = target_host;
    target_port_ = target_port;
    connect_timeout_ms_ = connect_timeout_ms;

    // Create acceptor.
    acceptor_ = std::make_shared<asio::ip::tcp::acceptor>(io_);
    asio::error_code ec;

    auto addr = asio::ip::make_address(bind_host, ec);
    if (ec) {
        logger_.error("MappingListener: invalid bind address " + bind_host);
        acceptor_.reset();
        return rmt::ErrorCode::InvalidPayload;
    }

    asio::ip::tcp::endpoint ep{addr, bind_port};
    acceptor_->open(ep.protocol(), ec);
    if (ec) {
        logger_.error("MappingListener: open failed: " + ec.message());
        acceptor_.reset();
        return rmt::ErrorCode::InternalError;
    }

    acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);

    acceptor_->bind(ep, ec);
    if (ec) {
        logger_.error("MappingListener: bind failed: " + ec.message());
        acceptor_.reset();
        return rmt::ErrorCode::InternalError;
    }

    acceptor_->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        logger_.error("MappingListener: listen failed: " + ec.message());
        acceptor_.reset();
        return rmt::ErrorCode::InternalError;
    }

    auto lep = acceptor_->local_endpoint(ec);
    if (!ec) {
        logger_.info("MappingListener: listening on "
                     + lep.address().to_string() + ":"
                     + std::to_string(lep.port())
                     + " -> device=" + device_id
                     + ", target=" + target_host + ":"
                     + std::to_string(target_port));
    }

    do_accept();
    return rmt::ErrorCode::Ok;
}

void MappingListener::stop() {
    if (acceptor_) {
        asio::error_code ec;
        acceptor_->close(ec);
        acceptor_.reset();
        logger_.info("MappingListener: stopped");
    }
}

std::uint16_t MappingListener::local_port() const {
    if (!acceptor_) return 0;
    asio::error_code ec;
    auto ep = acceptor_->local_endpoint(ec);
    if (ec) return 0;
    return ep.port();
}

void MappingListener::do_accept() {
    if (!acceptor_) return;

    accept_socket_ = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_->async_accept(*accept_socket_,
        [this](const asio::error_code& ec) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    logger_.error("MappingListener: accept error: "
                                  + ec.message());
                }
                return;
            }

            // Move the accepted socket out.
            auto local_sock = std::move(accept_socket_);
            accept_socket_.reset();

            // Check that the device is still online.
            if (!devices_.is_online(device_id_)) {
                logger_.error("MappingListener: device " + device_id_
                              + " is offline — rejecting accepted connection");
                asio::error_code ignored;
                local_sock->close(ignored);
                do_accept();
                return;
            }

            // Get the agent tunnel for this device.
            auto tunnel = devices_.get_connection(device_id_);
            if (!tunnel) {
                logger_.error("MappingListener: no tunnel for device "
                              + device_id_);
                asio::error_code ignored;
                local_sock->close(ignored);
                do_accept();
                return;
            }

            // Create session.
            auto session_id = session_mgr_.create_session(device_id_, tunnel,
                                                          mapping_id_);
            if (session_id == 0) {
                logger_.error("MappingListener: failed to create session");
                asio::error_code ignored;
                local_sock->close(ignored);
                do_accept();
                return;
            }

            // Attach the local client socket to the session.
            session_mgr_.attach_local_socket(session_id, local_sock);

            // Send OPEN_SESSION to the agent.
            rmt::protocol::OpenSessionMessage open_msg;
            open_msg.mapping_id = mapping_id_;
            open_msg.target_host = target_host_;
            open_msg.target_port = static_cast<int>(target_port_);
            open_msg.connect_timeout_ms = connect_timeout_ms_;

            auto frame = rmt::protocol::encode_open_session(open_msg);
            frame.header.session_id = session_id;
            tunnel->send_frame(frame,
                [this, session_id](rmt::ErrorCode send_ec) {
                    if (send_ec != rmt::ErrorCode::Ok) {
                        logger_.error("MappingListener: failed to send "
                                      "OPEN_SESSION for session "
                                      + std::to_string(session_id));
                        session_mgr_.close_session(session_id);
                    }
                });

            logger_.info("MappingListener: accepted connection → session "
                         + std::to_string(session_id)
                         + " (mapping=" + mapping_id_ + ")");

            // Continue accepting.
            do_accept();
        });
}

}  // namespace rmt::tunnel
