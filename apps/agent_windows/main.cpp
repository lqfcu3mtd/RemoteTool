// Windows Agent entry point — Phase 1 (TCP connect + HELLO/HEARTBEAT + reconnect).
#include <cstdio>
#include <memory>
#include <asio.hpp>

#include "rmt/tunnel/agent_connection.h"

int main() {
    asio::io_context io;

    // Phase 1: hard-coded config. Config-file loading arrives in a later phase.
    auto cfg = rmt::tunnel::AgentConfig{
        /*server_host*/      "127.0.0.1",
        /*server_port*/      4433,
        /*device_id*/        "AGENT001",
        /*agent_version*/    "0.1.0",
        /*platform*/         "windows-x86_64",
    };

    auto agent = std::make_shared<rmt::tunnel::AgentConnection>(io, cfg);
    agent->set_on_state_change([](rmt::tunnel::AgentState state) {
        const char* names[] = {"Disconnected", "Connecting", "WaitHelloAck",
                               "Online", "Error"};
        std::printf("[state] %s\n", names[static_cast<int>(state)]);
    });

    agent->start();
    io.run();
    return 0;
}
