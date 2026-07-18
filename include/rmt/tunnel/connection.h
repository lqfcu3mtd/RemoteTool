#pragma once
// TunnelConnection — TCP socket + FrameDecoder RAII wrapper (Phase 1, no TLS).
//
// Thread safety: All socket operations must be invoked from the same thread
// that runs asio::io_context. The class performs no internal locking. The
// caller is responsible for ensuring that every public method call, callback
// invocation, and the destructor happen on that single io_context thread.
//
// Destructor safety: If io_context has already been stopped at destruction
// time, the destructor uses a synchronous close path (cancel + shutdown +
// close) that does not require an active event loop. Callbacks (on_frame /
// on_closed) are NOT invoked from the destructor.

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/common/log.h"
#include "rmt/protocol/frame.h"

namespace rmt::tunnel {

enum class TunnelState { Disconnected, Connecting, Connected, Closed };

class TunnelConnection {
public:
    explicit TunnelConnection(asio::io_context& io);
    ~TunnelConnection();

    TunnelConnection(const TunnelConnection&) = delete;
    TunnelConnection& operator=(const TunnelConnection&) = delete;

    // Async connect to host:port. Phase 1 accepts IP literals only (no DNS).
    // On success: state to Connected, on_connected(Ok), do_read starts.
    // On failure: handle_error(InternalError), on_connected(error),
    //              on_closed(error).
    void connect(const std::string& host, std::uint16_t port,
                 std::function<void(rmt::ErrorCode)> on_connected);

    // Close the connection gracefully. Cancels pending async ops, drains the
    // write queue, fires on_closed(Normal), then closes the socket.
    void close();

    // Encode a frame and async-send. Writes are serialized: if a write is
    // already in progress the frame is queued. The callback is invoked when
    // this frame's write completes (Ok) or when the connection is torn down
    // before this frame could be sent (error code).
    void send_frame(const rmt::protocol::Frame& frame,
                    std::function<void(rmt::ErrorCode)> on_sent);

    // Register callback for each fully-decoded frame received on this socket.
    void set_on_frame(std::function<void(const rmt::protocol::Frame&)> callback);

    // Register callback invoked exactly once when the connection closes (remote
    // close, local close, or fatal error). After invocation the callback is
    // cleared so it will not fire again.
    void set_on_closed(std::function<void(rmt::ErrorCode)> callback);

    TunnelState state() const noexcept;
    const std::string& remote_address() const;

private:
    void do_read();
    void do_write();
    void handle_error(rmt::ErrorCode err);
    void close_socket();

    asio::io_context& io_;
    asio::ip::tcp::socket socket_;
    rmt::protocol::FrameDecoder decoder_;

    TunnelState state_ = TunnelState::Disconnected;
    std::string remote_address_;

    std::function<void(const rmt::protocol::Frame&)> on_frame_;
    std::function<void(rmt::ErrorCode)> on_closed_;

    struct WriteEntry {
        std::vector<std::uint8_t> data;
        std::function<void(rmt::ErrorCode)> callback;
    };
    std::deque<WriteEntry> write_queue_;
    bool writing_ = false;

    std::vector<std::uint8_t> read_buf_;

    rmt::common::Logger logger_;
};

}  // namespace rmt::tunnel
