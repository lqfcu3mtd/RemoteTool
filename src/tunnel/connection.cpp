#include "rmt/tunnel/connection.h"

namespace rmt::tunnel {

TunnelConnection::TunnelConnection(asio::io_context& io)
    : io_(io), socket_(io) {
    read_buf_.resize(8192);
    logger_.set_level(rmt::common::LogLevel::Info);
}

TunnelConnection::TunnelConnection(asio::io_context& io, asio::ip::tcp::socket&& socket)
    : io_(io), socket_(std::move(socket)) {
    read_buf_.resize(8192);
    logger_.set_level(rmt::common::LogLevel::Info);

    asio::error_code ec;
    auto endpoint = socket_.remote_endpoint(ec);
    if (!ec) {
        remote_address_ = endpoint.address().to_string() + ":"
                        + std::to_string(endpoint.port());
        state_ = TunnelState::Connected;
        logger_.info("TunnelConnection accepted from " + remote_address_);
    } else {
        // Socket is not connected — leave state as Disconnected.
        // begin_receive() should not be called in this case.
        logger_.error("TunnelConnection accept: invalid socket ("
                      + ec.message() + ")");
    }
}

TunnelConnection::~TunnelConnection() {
    // Synchronous close: does not require an active io_context event loop.
    // Suitable when io_context may already be stopped at destruction time.
    // Callbacks (on_frame / on_closed) are NOT invoked from here.
    if (state_ != TunnelState::Closed && state_ != TunnelState::Disconnected) {
        close_socket();
    }
}

void TunnelConnection::connect(const std::string& host, std::uint16_t port,
                               std::function<void(rmt::ErrorCode)> on_connected) {
    if (state_ != TunnelState::Disconnected) {
        logger_.warn("TunnelConnection::connect called in wrong state");
        if (on_connected) on_connected(rmt::ErrorCode::InternalError);
        return;
    }

    state_ = TunnelState::Connecting;
    remote_address_ = host + ":" + std::to_string(port);

    asio::error_code ec;
    auto addr = asio::ip::make_address(host, ec);
    if (ec) {
        logger_.error("TunnelConnection connect: invalid address " + host);
        handle_error(rmt::ErrorCode::InternalError);
        if (on_connected) on_connected(rmt::ErrorCode::InternalError);
        return;
    }

    asio::ip::tcp::endpoint endpoint(addr, port);
    socket_.async_connect(endpoint,
        [this, cb = std::move(on_connected)](const asio::error_code& ec) {
            if (ec) {
                logger_.error("TunnelConnection connect failed: " + ec.message());
                handle_error(rmt::ErrorCode::InternalError);
                if (cb) cb(rmt::ErrorCode::InternalError);
                return;
            }
            state_ = TunnelState::Connected;
            logger_.info("TunnelConnection connected to " + remote_address_);
            if (cb) cb(rmt::ErrorCode::Ok);
            do_read();
        });
}

void TunnelConnection::close() {
    if (state_ == TunnelState::Closed || state_ == TunnelState::Disconnected) {
        return;
    }
    logger_.info("TunnelConnection closing " + remote_address_);
    handle_error(rmt::ErrorCode::Normal);
}

void TunnelConnection::send_frame(const rmt::protocol::Frame& frame,
                                  std::function<void(rmt::ErrorCode)> on_sent) {
    if (state_ != TunnelState::Connected) {
        logger_.warn("TunnelConnection send_frame on non-connected socket");
        if (on_sent) on_sent(rmt::ErrorCode::InternalError);
        return;
    }

    auto encoded = rmt::protocol::encode_frame(frame);
    write_queue_.push_back({std::move(encoded), std::move(on_sent)});

    if (!writing_) {
        do_write();
    }
}

void TunnelConnection::set_on_frame(
    std::function<void(const rmt::protocol::Frame&)> callback) {
    on_frame_ = std::move(callback);
}

void TunnelConnection::set_on_closed(std::function<void(rmt::ErrorCode)> callback) {
    on_closed_ = std::move(callback);
}

TunnelState TunnelConnection::state() const noexcept {
    return state_;
}

void TunnelConnection::begin_receive() {
    if (state_ != TunnelState::Connected) {
        logger_.warn("TunnelConnection::begin_receive called in wrong state");
        return;
    }
    do_read();
}

const std::string& TunnelConnection::remote_address() const {
    return remote_address_;
}

// ---- private ----------------------------------------------------------

void TunnelConnection::do_read() {
    socket_.async_read_some(asio::buffer(read_buf_),
        [this](const asio::error_code& ec, std::size_t len) {
            if (ec) {
                if (ec == asio::error::operation_aborted) {
                    return;
                }
                logger_.error("TunnelConnection read error: " + ec.message());
                handle_error(ec == asio::error::eof
                                 ? rmt::ErrorCode::LocalPeerClosed
                                 : rmt::ErrorCode::InternalError);
                return;
            }

            auto result = decoder_.consume(read_buf_.data(), len);

            if (result.status == rmt::protocol::DecodeStatus::Disconnect ||
                result.status == rmt::protocol::DecodeStatus::ProtocolError) {
                logger_.error("TunnelConnection frame decode error");
                handle_error(result.error);
                return;
            }

            for (auto& frame : result.frames) {
                if (on_frame_) {
                    on_frame_(std::move(frame));
                }
            }

            if (state_ == TunnelState::Connected) {
                do_read();
            }
        });
}

void TunnelConnection::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    auto& entry = write_queue_.front();

    asio::async_write(socket_, asio::buffer(entry.data),
        [this](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (write_queue_.empty()) {
                writing_ = false;
                return;
            }

            auto entry = std::move(write_queue_.front());
            write_queue_.pop_front();

            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    logger_.error("TunnelConnection write error: " + ec.message());
                }
                if (entry.callback) {
                    entry.callback(rmt::ErrorCode::InternalError);
                }
                handle_error(rmt::ErrorCode::InternalError);
                return;
            }

            if (entry.callback) {
                entry.callback(rmt::ErrorCode::Ok);
            }

            do_write();
        });
}

void TunnelConnection::handle_error(rmt::ErrorCode err) {
    // Drain write queue, notifying each pending entry with the error.
    while (!write_queue_.empty()) {
        auto entry = std::move(write_queue_.front());
        write_queue_.pop_front();
        if (entry.callback) {
            entry.callback(err);
        }
    }
    writing_ = false;

    // Transition to Closed and fire on_closed exactly once.
    TunnelState prev = state_;
    state_ = TunnelState::Closed;

    if (prev != TunnelState::Closed && prev != TunnelState::Disconnected) {
        decoder_.reset();
        if (on_closed_) {
            auto cb = std::move(on_closed_);
            on_closed_ = nullptr;
            cb(err);
        }
    }

    close_socket();
}

void TunnelConnection::close_socket() {
    asio::error_code ignored;
    socket_.cancel(ignored);
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}

}  // namespace rmt::tunnel
