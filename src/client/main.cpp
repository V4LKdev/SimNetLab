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

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", sim.world());

    while (g_running && simnet::client::Renderer::is_running()) {
        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        renderer.render();
    }

    return 0;
}
