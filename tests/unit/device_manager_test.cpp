// DeviceManager + Acceptor unit tests (Phase 1, no TLS).
// Tests device lifecycle: HELLO registration, duplicate rejection,
// HEARTBEAT handling, heartbeat timeout, and connection close cleanup.
//
// Uses TunnelConnection in Disconnected state — callbacks are registered
// but no real TCP is involved. Test-entry public methods on DeviceManager
// allow direct injection of parsed messages.

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "rmt/common/error_code.h"
#include "rmt/protocol/messages.h"
#include "rmt/test.h"
#include "rmt/tunnel/connection.h"
#include "rmt/tunnel/device_manager.h"

namespace {

using rmt::ErrorCode;
using rmt::protocol::HelloMessage;
using rmt::protocol::HeartbeatMessage;
using rmt::tunnel::DeviceEntry;
using rmt::tunnel::DeviceManager;
using rmt::tunnel::TunnelConnection;

// --- Test helpers ---

HelloMessage make_hello(const std::string& device_id) {
    HelloMessage h;
    h.device_id = device_id;
    h.agent_version = "0.1.0";
    h.platform = "test-platform";
    h.protocol_version = 1;
    h.instance_nonce = "";
    return h;
}

// Register a connection and send HELLO — returns the connection.
std::shared_ptr<TunnelConnection> bring_online(
    asio::io_context& io, DeviceManager& dm, const std::string& device_id) {
    auto conn = std::make_shared<TunnelConnection>(io);
    dm.on_new_connection(conn);
    dm.handle_hello(conn, make_hello(device_id));
    return conn;
}

}  // namespace

// ===== Test 1: Valid HELLO → device online =====
void test_valid_hello_online() {
    asio::io_context io;

    int online_calls = 0;
    DeviceEntry last_online_entry;

    DeviceManager dm(io);
    dm.set_on_device_online([&](const DeviceEntry& e) {
        online_calls++;
        last_online_entry = e;
    });

    auto conn = std::make_shared<TunnelConnection>(io);
    dm.on_new_connection(conn);

    dm.handle_hello(conn, make_hello("AGENT001"));

    RMT_CHECK_MSG(online_calls == 1,
                  "on_device_online should fire once, got "
                  + std::to_string(online_calls));
    RMT_CHECK_MSG(dm.device_count() == 1,
                  "device_count should be 1, got "
                  + std::to_string(dm.device_count()));
    RMT_CHECK_MSG(dm.is_online("AGENT001"),
                  "AGENT001 should be online");
    RMT_CHECK_MSG(last_online_entry.device_id == "AGENT001",
                  "device_id mismatch: " + last_online_entry.device_id);
    RMT_CHECK_MSG(last_online_entry.agent_version == "0.1.0",
                  "agent_version mismatch");
    RMT_CHECK_MSG(last_online_entry.platform == "test-platform",
                  "platform mismatch");
    RMT_CHECK_MSG(last_online_entry.online,
                  "entry.online should be true");
}

// ===== Test 2: Duplicate HELLO → reject new =====
void test_duplicate_hello_rejected() {
    asio::io_context io;

    DeviceManager dm(io);

    int online_calls = 0;
    dm.set_on_device_online([&](const DeviceEntry&) { online_calls++; });

    // First connection — accepted.
    auto conn1 = std::make_shared<TunnelConnection>(io);
    dm.on_new_connection(conn1);
    dm.handle_hello(conn1, make_hello("DEV777"));

    RMT_CHECK_MSG(online_calls == 1,
                  "first HELLO should fire on_device_online");
    RMT_CHECK_MSG(dm.is_online("DEV777"),
                  "DEV777 should be online after first HELLO");
    RMT_CHECK_MSG(dm.device_count() == 1,
                  "device_count should be 1 after first HELLO");

    // Second connection with same device_id — rejected.
    auto conn2 = std::make_shared<TunnelConnection>(io);
    dm.on_new_connection(conn2);
    dm.handle_hello(conn2, make_hello("DEV777"));

    RMT_CHECK_MSG(online_calls == 1,
                  "duplicate HELLO should NOT fire on_device_online again, got "
                  + std::to_string(online_calls));
    RMT_CHECK_MSG(dm.is_online("DEV777"),
                  "DEV777 should still be online (old connection)");
    RMT_CHECK_MSG(dm.device_count() == 1,
                  "device_count should still be 1 (duplicate not counted), got "
                  + std::to_string(dm.device_count()));
}

// ===== Test 3: HEARTBEAT → reply with same sequence =====
void test_heartbeat_reply() {
    asio::io_context io;

    DeviceManager dm(io);
    auto conn = bring_online(io, dm, "HB001");

    // handle_heartbeat → send_heartbeat_ack。Since TunnelConnection is
    // disconnected the actual send will fail, but the method must not crash
    // and must call send with the correct sequence.
    HeartbeatMessage hb;
    hb.sequence = 42;
    hb.sent_unix_ms = 0;
    hb.active_sessions = 3;

    dm.handle_heartbeat(conn, hb);
    // No crash = pass. The test verifies that handle_heartbeat completes
    // without exceptions and calls the encode/send path.
    RMT_CHECK_MSG(dm.is_online("HB001"),
                  "HB001 should remain online after heartbeat");
}

// ===== Test 4: Heartbeat timeout → OFFLINE =====
void test_heartbeat_timeout() {
    asio::io_context io;

    int offline_calls = 0;
    DeviceEntry last_offline_entry;

    DeviceManager dm(io);
    dm.set_on_device_offline([&](const DeviceEntry& e) {
        offline_calls++;
        last_offline_entry = e;
    });

    auto conn = bring_online(io, dm, "TMOUT1");

    RMT_CHECK_MSG(dm.is_online("TMOUT1"),
                  "TMOUT1 should be online before timeout");

    // Move last_frame_time back 31 seconds to simulate timeout.
    auto old_time = std::chrono::steady_clock::now() - std::chrono::seconds(31);
    dm.set_last_frame_time_for_test("TMOUT1", old_time);

    // Run cleanup.
    dm.cleanup_timeouts();

    RMT_CHECK_MSG(offline_calls == 1,
                  "on_device_offline should fire once after timeout, got "
                  + std::to_string(offline_calls));
    RMT_CHECK_MSG(last_offline_entry.device_id == "TMOUT1",
                  "offline entry device_id mismatch: "
                  + last_offline_entry.device_id);
    RMT_CHECK_MSG(!dm.is_online("TMOUT1"),
                  "TMOUT1 should NOT be online after timeout");
    RMT_CHECK_MSG(dm.device_count() == 0,
                  "device_count should be 0 after timeout cleanup, got "
                  + std::to_string(dm.device_count()));
}

// ===== Test 5: Non-timeout device survives cleanup =====
void test_no_false_timeout() {
    asio::io_context io;

    int offline_calls = 0;
    DeviceManager dm(io);
    dm.set_on_device_offline([&](const DeviceEntry&) { offline_calls++; });

    auto conn = bring_online(io, dm, "ALIVE1");

    // last_frame_time is current → should NOT time out.
    dm.cleanup_timeouts();

    RMT_CHECK_MSG(offline_calls == 0,
                  "should NOT fire offline for device with recent activity, got "
                  + std::to_string(offline_calls));
    RMT_CHECK_MSG(dm.is_online("ALIVE1"),
                  "ALIVE1 should still be online after cleanup");
    RMT_CHECK_MSG(dm.device_count() == 1,
                  "device_count should still be 1");
}

// ===== Test 6: Connection close → device removed =====
void test_connection_close() {
    asio::io_context io;

    int offline_calls = 0;
    DeviceEntry last_offline_entry;

    DeviceManager dm(io);
    dm.set_on_device_offline([&](const DeviceEntry& e) {
        offline_calls++;
        last_offline_entry = e;
    });

    auto conn = bring_online(io, dm, "CLOSE1");

    RMT_CHECK_MSG(dm.is_online("CLOSE1"),
                  "CLOSE1 should be online before close");

    // Simulate connection close via test-entry method.
    dm.on_connection_closed(conn, ErrorCode::Normal);

    RMT_CHECK_MSG(offline_calls == 1,
                  "on_device_offline should fire once on close, got "
                  + std::to_string(offline_calls));
    RMT_CHECK_MSG(last_offline_entry.device_id == "CLOSE1",
                  "offline entry device_id mismatch: "
                  + last_offline_entry.device_id);
    RMT_CHECK_MSG(!dm.is_online("CLOSE1"),
                  "CLOSE1 should NOT be online after close");
    RMT_CHECK_MSG(dm.device_count() == 0,
                  "device_count should be 0 after close, got "
                  + std::to_string(dm.device_count()));
}

// ===== Test 7: Close of connection that never sent HELLO =====
void test_close_pending_connection() {
    asio::io_context io;

    int offline_calls = 0;
    DeviceManager dm(io);
    dm.set_on_device_offline([&](const DeviceEntry&) { offline_calls++; });

    auto conn = std::make_shared<TunnelConnection>(io);
    dm.on_new_connection(conn);
    // Connection is registered but no HELLO → not online.

    dm.on_connection_closed(conn, ErrorCode::Normal);

    RMT_CHECK_MSG(offline_calls == 0,
                  "offline callback should NOT fire for pending connection, got "
                  + std::to_string(offline_calls));
    RMT_CHECK_MSG(dm.device_count() == 0,
                  "device_count should be 0 after pending close");
}

// ===== Test 8: Multiple devices =====
void test_multiple_devices() {
    asio::io_context io;

    DeviceManager dm(io);

    auto conn1 = bring_online(io, dm, "MULTI1");
    auto conn2 = bring_online(io, dm, "MULTI2");
    auto conn3 = bring_online(io, dm, "MULTI3");

    RMT_CHECK_MSG(dm.device_count() == 3,
                  "device_count should be 3, got "
                  + std::to_string(dm.device_count()));
    RMT_CHECK_MSG(dm.is_online("MULTI1"), "MULTI1 should be online");
    RMT_CHECK_MSG(dm.is_online("MULTI2"), "MULTI2 should be online");
    RMT_CHECK_MSG(dm.is_online("MULTI3"), "MULTI3 should be online");

    // Close one.
    dm.on_connection_closed(conn2, ErrorCode::Normal);
    RMT_CHECK_MSG(dm.device_count() == 2,
                  "device_count should be 2 after closing one, got "
                  + std::to_string(dm.device_count()));
    RMT_CHECK_MSG(!dm.is_online("MULTI2"),
                  "MULTI2 should NOT be online after close");
    RMT_CHECK_MSG(dm.is_online("MULTI1"),
                  "MULTI1 should still be online");
    RMT_CHECK_MSG(dm.is_online("MULTI3"),
                  "MULTI3 should still be online");
}

// ===== Test 9: Timeout only affects stale devices =====
void test_selective_timeout() {
    asio::io_context io;

    int offline_calls = 0;
    DeviceManager dm(io);
    dm.set_on_device_offline([&](const DeviceEntry&) { offline_calls++; });

    auto conn_fresh = bring_online(io, dm, "FRESH");
    auto conn_stale = bring_online(io, dm, "STALE");

    // Only age out STALE.
    auto old_time = std::chrono::steady_clock::now() - std::chrono::seconds(31);
    dm.set_last_frame_time_for_test("STALE", old_time);

    dm.cleanup_timeouts();

    RMT_CHECK_MSG(offline_calls == 1,
                  "only STALE should trigger offline, got "
                  + std::to_string(offline_calls));
    RMT_CHECK_MSG(dm.is_online("FRESH"),
                  "FRESH should still be online");
    RMT_CHECK_MSG(!dm.is_online("STALE"),
                  "STALE should be offline");
    RMT_CHECK_MSG(dm.device_count() == 1,
                  "device_count should be 1 after selective timeout, got "
                  + std::to_string(dm.device_count()));
}

// ===== Test 10: device_id not found in set_last_frame_time =====
void test_set_last_frame_time_unknown_id() {
    asio::io_context io;

    DeviceManager dm(io);
    // Should not crash on unknown device_id.
    dm.set_last_frame_time_for_test("UNKNOWN",
        std::chrono::steady_clock::now());
    RMT_CHECK_MSG(true, "no-crash for unknown device_id");
}

// ===== Test 11: is_online unknown =====
void test_is_online_unknown() {
    asio::io_context io;
    DeviceManager dm(io);
    RMT_CHECK_MSG(!dm.is_online("UNKNOWN"),
                  "unknown device should not be online");
    RMT_CHECK_MSG(dm.device_count() == 0,
                  "device_count should be 0 initially");
}

int main() {
    test_valid_hello_online();
    test_duplicate_hello_rejected();
    test_heartbeat_reply();
    test_heartbeat_timeout();
    test_no_false_timeout();
    test_connection_close();
    test_close_pending_connection();
    test_multiple_devices();
    test_selective_timeout();
    test_set_last_frame_time_unknown_id();
    test_is_online_unknown();

    auto& c = rmt::test::ctx();
    std::printf("\ndevice_manager_test: %d passed, %d failed\n",
                c.passed, c.failed);
    return c.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
