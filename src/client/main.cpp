#include <atomic>
#include <csignal>

#include "../core/controller.hpp"
#include "renderer.hpp"
#include "telemetry.hpp"


namespace {
    std::atomic<bool> g_running(true);

    void signal_handler(int sig)
    {
        g_running = false;
    }
}


int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simnet::telemetry::init("SimNetLab_Client", "log/client_telemetry.log");

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", sim.world());

    while (g_running && renderer.is_running()) {
        TELEM_TRACY_ZONE("ClientFrame");

        // Currently unused, kept as documentation of the available sim to render API
        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        renderer.render();
        TELEM_TRACY_FRAME("ClientFrame");
    }

    simnet::telemetry::shutdown();
    return 0;
}
