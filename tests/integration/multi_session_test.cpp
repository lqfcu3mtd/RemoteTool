// Phase 3 multi-session integration test.
// Verifies: MappingListener + SessionManager + AgentSession + echo server
// with 3 concurrent local clients.
// Uses random ports to avoid cross-test conflicts.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/security/target_whitelist.h"
#include "rmt/session/session_id.h"
#include "rmt/test.h"
#include "rmt/tunnel/acceptor.h"
#include "rmt/tunnel/agent_connection.h"
#include "rmt/tunnel/connection.h"
#include "rmt/tunnel/device_manager.h"
#include "rmt/tunnel/mapping_listener.h"
#include "rmt/tunnel/session_manager.h"

using namespace rmt;
using namespace rmt::tunnel;
using namespace rmt::protocol;

namespace {

void run_echo_once(asio::ip::tcp::acceptor& a) {
    auto s = std::make_shared<asio::ip::tcp::socket>(a.get_executor());
    a.accept(*s);
    std::vector<std::uint8_t> buf(65536);
    for (;;) {
        asio::error_code ec;
        auto n = s->read_some(asio::buffer(buf), ec);
        if (ec || n == 0) break;
        asio::write(*s, asio::buffer(buf, n), ec);
        if (ec) break;
    }
    s->close();
}

// Acceptor doesn't expose local_port directly; we bind manually.
std::uint16_t bind_acceptor(asio::ip::tcp::acceptor& a,
                             const std::string& host) {
    asio::ip::tcp::endpoint ep(asio::ip::make_address(host), 0);
    a.open(ep.protocol());
    a.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    a.bind(ep);
    a.listen();
    return a.local_endpoint().port();
}

}  // namespace

int main() {
    asio::io_context io;

    // ---- Echo server ----
    asio::ip::tcp::acceptor echo_acceptor(io);
    auto echo_port = bind_acceptor(echo_acceptor, "127.0.0.1");
    std::thread echo_thread([&] { run_echo_once(echo_acceptor); });

    // ---- Agent acceptor (random port) ----
    asio::ip::tcp::acceptor agent_raw(io);
    auto agent_port = bind_acceptor(agent_raw, "127.0.0.1");
    agent_raw.close();

    // ---- RemoteTool ----
    DeviceManager dm(io);
    Acceptor agent_acceptor(io, dm);
    session::SessionIdAllocator id_alloc;
    SessionManager sm(io, dm, id_alloc);

    std::atomic<bool> device_online{false};
    dm.set_on_device_online([&](const DeviceEntry&) { device_online = true; });

    agent_acceptor.start("127.0.0.1", agent_port);
    dm.start_cleanup_timer();

    // ---- Agent ----
    auto agent_cfg = AgentConfig{"127.0.0.1", agent_port, "MULTI-01",
                                  "0.1.0", "win-test"};
    auto agent = std::make_shared<AgentConnection>(io, agent_cfg);
    agent->start();

    // ---- Mapping listener (random port) ----
    asio::ip::tcp::acceptor map_raw(io);
    auto map_port = bind_acceptor(map_raw, "127.0.0.1");
    map_raw.close();
    MappingListener ml(io, sm, dm);
    ml.start("127.0.0.1", map_port, "MULTI-01", "map-multi",
             "127.0.0.1", echo_port, 5000);

    std::thread io_thread([&] { io.run(); });

    // Wait for agent online.
    bool online_ok = false;
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (device_online) { online_ok = true; break; }
    }
    RMT_CHECK_MSG(online_ok, "Agent did not reach ONLINE");

    // 3 concurrent clients
    const int N = 3;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> clients;
    for (int i = 0; i < N; ++i) {
        auto s = std::make_shared<asio::ip::tcp::socket>(io);
        s->connect(asio::ip::tcp::endpoint(
            asio::ip::make_address("127.0.0.1"), map_port));
        clients.push_back(s);
    }

    int matched = 0;
    for (int i = 0; i < N; ++i) {
        std::string payload = "C" + std::to_string(i) + "_PAYLOAD_";
        asio::error_code ec;
        asio::write(*clients[i], asio::buffer(payload), ec);
        if (ec) continue;

        std::vector<std::uint8_t> rbuf(payload.size());
        auto n = asio::read(*clients[i], asio::buffer(rbuf), ec);
        if (ec && ec != asio::error::eof) continue;
        std::string echo(rbuf.begin(), rbuf.begin() + static_cast<std::ptrdiff_t>(n));
        if (echo == payload) ++matched;
    }
    RMT_CHECK_MSG(matched == N,
                  ("matched=" + std::to_string(matched) + " / " + std::to_string(N)).c_str());

    for (auto& c : clients) c->close();
    agent->stop();
    ml.stop();
    io.stop();
    io_thread.join();
    echo_thread.join();

    auto& ctx = test::ctx();
    std::printf("\nmulti_session_test: %d passed, %d failed\n",
                ctx.passed, ctx.failed);
    return ctx.ok() ? 0 : 1;
}
