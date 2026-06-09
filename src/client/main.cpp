#include "../core/ecs/components.hpp"
#include "../core/controller.hpp"
#include "renderer.hpp"

int main()
{
    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", sim.world());

    while (simnet::client::Renderer::is_running()) {
        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        renderer.render();
    }

    return 0;
}
