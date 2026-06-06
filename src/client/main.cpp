#include "../core/controller.hpp"
#include "renderer.hpp"

int main() {

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    simnet::client::Renderer renderer(800, 450, "SimNetLab_Client");

    while (renderer.is_running()) {

        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        renderer.begin_frame();

        renderer.draw_world();

        renderer.draw_debug(steps, alpha, sim.current_tick());
        renderer.end_frame();
    }

    return 0;
}
