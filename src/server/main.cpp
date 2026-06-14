#include <atomic>
#include <csignal>


#include "network_server.hpp"
#include "telemetry.hpp"

namespace {
    std::atomic<bool> g_running(true);

    void signal_handler(int /*sig*/)
    {
        g_running = false;
    }
}

int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simnet::telemetry::init("SimNetLab_Server", "log/server_telemetry.log");

    simnet::server::network_server server(7777);

    // Main loop
    while (g_running && server.is_running()) {
        {
            TELEM_TRACY_ZONE("ServerFrame");
            server.service();
            TELEM_TRACY_FRAME("ServerFrame");
        }
        // Small sleep to avoid 100% CPU usage when idle, replaced with fixed tps accumulator.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    TELEM_LOG_INFO("Server shutting down");
    simnet::telemetry::shutdown();
    return 0;
}
