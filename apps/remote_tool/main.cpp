// RemoteTool entry point — Phase 1 (TCP + HELLO/HEARTBEAT + device tracking).
#include <cstdio>
#include <asio.hpp>

#include "rmt/tunnel/acceptor.h"
#include "rmt/tunnel/device_manager.h"

int main() {
    asio::io_context io;

    rmt::tunnel::DeviceManager dm(io);
    rmt::tunnel::Acceptor acceptor(io, dm);

    dm.set_on_device_online([](const rmt::tunnel::DeviceEntry& entry) {
        std::printf("[device online]  %-16s  %s  %s\n",
                    entry.device_id.c_str(),
                    entry.remote_address.c_str(),
                    entry.agent_version.c_str());
    });
    dm.set_on_device_offline([](const rmt::tunnel::DeviceEntry& entry) {
        std::printf("[device offline] %s\n", entry.device_id.c_str());
    });

    // Bind to all interfaces on the default agent port.
    acceptor.start("0.0.0.0", 4433);
    dm.start_cleanup_timer();

    std::printf("RemoteTool listening on :4433\n");
    io.run();
    return 0;
}
