// SessionManager unit tests (Phase 2, single session)
//
// Tests session lifecycle, state transitions, data forwarding, and statistics.
// Uses a disconnected TunnelConnection (no real TCP) and manually triggers
// on_session_frame to simulate agent responses.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/session/session_id.h"
#include "rmt/test.h"
#include "rmt/tunnel/connection.h"
#include "rmt/tunnel/device_manager.h"
#include "rmt/tunnel/session_manager.h"

using rmt::ErrorCode;
using rmt::protocol::Frame;
using rmt::session::SessionIdAllocator;
using rmt::tunnel::DeviceManager;
using rmt::tunnel::SessionManager;
using rmt::tunnel::SessionState;
using rmt::tunnel::TunnelConnection;

namespace {

struct Ctx {
    asio::io_context io;
    SessionIdAllocator id_alloc;
    DeviceManager devices;
    SessionManager session_mgr;

    Ctx() : devices(io), session_mgr(io, devices, id_alloc) {}

    // Helper: create a disconnected tunnel and a session with a local socket.
    struct Session {
        std::shared_ptr<TunnelConnection> tunnel;
        std::shared_ptr<asio::ip::tcp::socket> local_sock;
        std::uint32_t id = 0;
    };

    Session create_session_with_socket() {
        Session s;
        s.tunnel = std::make_shared<TunnelConnection>(io);
        s.local_sock = std::make_shared<asio::ip::tcp::socket>(io);
        s.id = session_mgr.create_session("test-device", s.tunnel);
        if (s.id != 0) {
            session_mgr.attach_local_socket(s.id, s.local_sock);
        }
        return s;
    }

    Frame make_session_opened(std::uint32_t sid, const std::string& mapping,
                              const std::string& host, int port) {
        rmt::protocol::SessionOpenedMessage msg;
        msg.mapping_id = mapping;
        msg.connected_host = host;
        msg.connected_port = port;
        auto frame = rmt::protocol::encode_session_opened(msg);
        frame.header.session_id = sid;
        return frame;
    }

    Frame make_session_open_failed(std::uint32_t sid,
                                    const std::string& code,
                                    const std::string& message) {
        rmt::protocol::SessionOpenFailedMessage msg;
        msg.error_code = code;
        msg.message = message;
        auto frame = rmt::protocol::encode_session_open_failed(msg);
        frame.header.session_id = sid;
        return frame;
    }

    Frame make_session_data(std::uint32_t sid,
                             const std::vector<std::uint8_t>& data) {
        auto frame = rmt::protocol::encode_session_data(sid, data.data(),
                                                        data.size());
        frame.header.session_id = sid;
        return frame;
    }

    Frame make_close_session(std::uint32_t sid,
                              const std::string& reason,
                              const std::string& message) {
        rmt::protocol::CloseSessionMessage msg;
        msg.reason = reason;
        msg.message = message;
        auto frame = rmt::protocol::encode_close_session(msg);
        frame.header.session_id = sid;
        return frame;
    }

    Frame make_half_close(std::uint32_t sid) {
        rmt::protocol::SessionHalfCloseMessage msg;
        msg.direction = "write";
        auto frame = rmt::protocol::encode_session_half_close(msg);
        frame.header.session_id = sid;
        return frame;
    }

    void cleanup_session(Session& s) {
        if (s.local_sock) {
            asio::error_code ignored;
            s.local_sock->close(ignored);
        }
        if (s.tunnel) {
            s.tunnel->close();
        }
    }
};

// ===== Test: create_session =====
void test_create_session() {
    Ctx c;
    auto s = c.create_session_with_socket();

    RMT_CHECK_MSG(s.id >= 1, "session ID should be >= 1");
    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Idle,
                  "state should be Idle after create");
    RMT_CHECK_MSG(c.session_mgr.bytes_local_in(s.id) == 0,
                  "bytes_in starts at 0");
    RMT_CHECK_MSG(c.session_mgr.bytes_local_out(s.id) == 0,
                  "bytes_out starts at 0");

    c.cleanup_session(s);
}

// ===== Test: SESSION_OPENED transition =====
void test_session_opened() {
    Ctx c;
    auto s = c.create_session_with_socket();

    auto opened = c.make_session_opened(s.id, "mapping-1", "10.0.0.1", 80);
    c.session_mgr.on_session_frame(s.id, opened);

    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Connected,
                  "state should be Connected after SESSION_OPENED");

    c.cleanup_session(s);
}

// ===== Test: SESSION_OPEN_FAILED → Closed =====
void test_session_open_failed() {
    Ctx c;

    bool closed_fired = false;
    std::uint32_t closed_sid = 0;
    c.session_mgr.set_on_session_closed(
        [&](std::uint32_t sid) {
            closed_fired = true;
            closed_sid = sid;
        });

    auto s = c.create_session_with_socket();

    auto failed = c.make_session_open_failed(
        s.id, "TARGET_CONNECTION_REFUSED", "Connection refused");
    c.session_mgr.on_session_frame(s.id, failed);

    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Closed,
                  "state should be Closed after SESSION_OPEN_FAILED");
    RMT_CHECK_MSG(closed_fired, "on_session_closed should fire");
    RMT_CHECK_MSG(closed_sid == s.id, "closed callback should get the right session ID");

    c.cleanup_session(s);
}

// ===== Test: close_session from RemoteTool side =====
void test_close_session_local() {
    Ctx c;

    bool closed_fired = false;
    c.session_mgr.set_on_session_closed(
        [&](std::uint32_t /*sid*/) { closed_fired = true; });

    auto s = c.create_session_with_socket();

    // Transition to Connected first.
    auto opened = c.make_session_opened(s.id, "mapping-2", "10.0.0.2", 443);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    // Now close from local side.
    c.session_mgr.close_session(s.id);

    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Closed,
                  "state should be Closed after local close");
    RMT_CHECK_MSG(closed_fired, "on_session_closed should fire");

    c.cleanup_session(s);
}

// ===== Test: CLOSE_SESSION from agent =====
void test_close_session_remote() {
    Ctx c;

    bool closed_fired = false;
    c.session_mgr.set_on_session_closed(
        [&](std::uint32_t /*sid*/) { closed_fired = true; });

    auto s = c.create_session_with_socket();

    // Transition to Connected first.
    auto opened = c.make_session_opened(s.id, "mapping-3", "10.0.0.3", 8080);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    // Remote sends CLOSE_SESSION.
    auto close = c.make_close_session(s.id, "NORMAL", "done");
    c.session_mgr.on_session_frame(s.id, close);

    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Closed,
                  "state should be Closed after remote close");
    RMT_CHECK_MSG(closed_fired, "on_session_closed should fire");

    c.cleanup_session(s);
}

// ===== Test: SESSION_HALF_CLOSE =====
void test_half_close_remote() {
    Ctx c;
    auto s = c.create_session_with_socket();

    auto opened = c.make_session_opened(s.id, "mapping-hc", "10.0.0.4", 22);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    auto hc = c.make_half_close(s.id);
    c.session_mgr.on_session_frame(s.id, hc);

    RMT_CHECK_MSG(
        c.session_mgr.session_state(s.id) == SessionState::HalfClosedRemote,
        "state should be HalfClosedRemote after HALF_CLOSE");

    c.cleanup_session(s);
}

// ===== Test: SESSION_DATA forwarding (agent → local) =====
void test_session_data_incoming() {
    Ctx c;
    auto s = c.create_session_with_socket();

    auto opened = c.make_session_opened(s.id, "data-mapping", "10.0.0.5", 9999);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    // Manually feed SESSION_DATA from agent.
    std::vector<std::uint8_t> test_payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    // "Hello"
    auto data_frame = c.make_session_data(s.id, test_payload);
    c.session_mgr.on_session_frame(s.id, data_frame);

    // Verify bytes_in was accumulated.
    std::uint64_t bytes_in = c.session_mgr.bytes_local_in(s.id);
    RMT_CHECK_MSG(bytes_in == test_payload.size(),
                  "bytes_local_in should equal payload size ("
                  + std::to_string(test_payload.size()) + "), got "
                  + std::to_string(bytes_in));

    c.cleanup_session(s);
}

// ===== Test: forward_local_to_agent statistics =====
void test_local_forward_stats() {
    Ctx c;
    auto s = c.create_session_with_socket();

    // Must be Connected to forward.
    auto opened = c.make_session_opened(s.id, "stats-mapping", "10.0.0.6", 7777);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    std::vector<std::uint8_t> data1 = {0x01, 0x02, 0x03, 0x04, 0x05};
    c.session_mgr.forward_local_to_agent(s.id, data1);
    std::uint64_t out1 = c.session_mgr.bytes_local_out(s.id);
    RMT_CHECK_MSG(out1 == 5,
                  "bytes_local_out should be 5 after first forward, got "
                  + std::to_string(out1));

    std::vector<std::uint8_t> data2 = {0x0A, 0x0B, 0x0C};
    c.session_mgr.forward_local_to_agent(s.id, data2);
    std::uint64_t out2 = c.session_mgr.bytes_local_out(s.id);
    RMT_CHECK_MSG(out2 == 8,
                  "bytes_local_out should be 8 after second forward, got "
                  + std::to_string(out2));

    c.cleanup_session(s);
}

// ===== Test: no forward when not Connected =====
void test_no_forward_when_not_connected() {
    Ctx c;
    auto s = c.create_session_with_socket();

    // Still in Idle state.
    std::vector<std::uint8_t> data = {0x01, 0x02};
    c.session_mgr.forward_local_to_agent(s.id, data);
    RMT_CHECK_MSG(c.session_mgr.bytes_local_out(s.id) == 0,
                  "bytes_local_out should be 0 when not Connected");

    c.cleanup_session(s);
}

// ===== Test: unknown session ID =====
void test_unknown_session() {
    Ctx c;
    // on_session_frame with unknown ID should not crash.
    auto frame = c.make_session_opened(9999, "x", "1.2.3.4", 1);
    c.session_mgr.on_session_frame(9999, frame);
    RMT_CHECK_MSG(true, "unknown session ID should not crash");

    RMT_CHECK_MSG(c.session_mgr.bytes_local_in(9999) == 0,
                  "bytes_in for unknown session should be 0");
    RMT_CHECK_MSG(c.session_mgr.session_state(9999) == SessionState::Closed,
                  "state for unknown session should be Closed");
}

// ===== Test: session ID reuse (release → allocate → new ID) =====
void test_session_id_allocation() {
    Ctx c;
    auto s1 = c.create_session_with_socket();
    std::uint32_t id1 = s1.id;
    RMT_CHECK_MSG(id1 >= 1, "first ID >= 1");

    // Close the session (which releases the ID).
    auto opened = c.make_session_opened(s1.id, "m", "1.1.1.1", 1);
    c.session_mgr.on_session_frame(s1.id, opened);
    c.session_mgr.close_session(s1.id);

    // Create a new session — should get a different ID.
    auto s2 = c.create_session_with_socket();
    std::uint32_t id2 = s2.id;
    RMT_CHECK_MSG(id2 != id1, "new session should get different ID ("
                  + std::to_string(id1) + " != " + std::to_string(id2) + ")");

    c.cleanup_session(s1);
    c.cleanup_session(s2);
}

// ===== Test: on_session_frame should ignore frames for closed sessions =====
void test_ignore_frame_on_closed() {
    Ctx c;
    auto s = c.create_session_with_socket();

    auto opened = c.make_session_opened(s.id, "m", "1.1.1.1", 1);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Connected);

    // Close the session.
    c.session_mgr.close_session(s.id);
    RMT_CHECK(c.session_mgr.session_state(s.id) == SessionState::Closed);

    // After close, session should not be in sessions_ map anymore
    // (cleanup removes it). Sending another frame should not crash.
    auto data_frame = c.make_session_data(s.id,
        {0x01, 0x02, 0x03});
    c.session_mgr.on_session_frame(s.id, data_frame);
    RMT_CHECK_MSG(true, "frame on closed session should not crash");

    c.cleanup_session(s);
}

// ===== Test: empty tunnel (Disconnected state) — basic state tracking =====
void test_disconnected_tunnel_state() {
    Ctx c;
    auto s = c.create_session_with_socket();

    RMT_CHECK_MSG(s.tunnel->state() == rmt::tunnel::TunnelState::Disconnected,
                  "tunnel should be Disconnected initially");
    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Idle,
                  "session Idle on disconnected tunnel");

    // SESSION_OPENED should still transition state.
    auto opened = c.make_session_opened(s.id, "m-dc", "10.0.0.7", 9000);
    c.session_mgr.on_session_frame(s.id, opened);
    RMT_CHECK_MSG(c.session_mgr.session_state(s.id) == SessionState::Connected,
                  "state transitions work even with disconnected tunnel");

    c.cleanup_session(s);
}

// ===== Phase 3a: multiple concurrent sessions — independent lifecycle =====
void test_multi_session_independent() {
    Ctx c;
    auto s1 = c.create_session_with_socket();
    auto s2 = c.create_session_with_socket();
    auto s3 = c.create_session_with_socket();

    // Open all three.
    c.session_mgr.on_session_frame(s1.id, c.make_session_opened(s1.id, "m1", "10.0.0.1", 1));
    c.session_mgr.on_session_frame(s2.id, c.make_session_opened(s2.id, "m2", "10.0.0.2", 2));
    c.session_mgr.on_session_frame(s3.id, c.make_session_opened(s3.id, "m3", "10.0.0.3", 3));

    RMT_CHECK(c.session_mgr.session_state(s1.id) == SessionState::Connected);
    RMT_CHECK(c.session_mgr.session_state(s2.id) == SessionState::Connected);
    RMT_CHECK(c.session_mgr.session_state(s3.id) == SessionState::Connected);

    // Close s2 only. s1 and s3 must remain untouched.
    c.session_mgr.close_session(s2.id);
    RMT_CHECK(c.session_mgr.session_state(s1.id) == SessionState::Connected);
    // s2 is now erased — query via a known-existing session's method is safe,
    // but session_state on a deleted ID is undefined. We just verify s1/s3.
    RMT_CHECK(c.session_mgr.session_state(s3.id) == SessionState::Connected);

    // Clean up the remaining sessions.
    c.session_mgr.close_session(s1.id);
    c.session_mgr.close_session(s3.id);
}

}  // namespace

int main() {
    test_create_session();
    test_session_opened();
    test_session_open_failed();
    test_close_session_local();
    test_close_session_remote();
    test_half_close_remote();
    test_session_data_incoming();
    test_local_forward_stats();
    test_no_forward_when_not_connected();
    test_unknown_session();
    test_session_id_allocation();
    test_ignore_frame_on_closed();
    test_disconnected_tunnel_state();
    test_multi_session_independent();

    auto& ctx = rmt::test::ctx();
    std::printf("\nsession_manager_test: %d passed, %d failed\n",
                ctx.passed, ctx.failed);
    return ctx.ok() ? 0 : 1;
}
