#include "components.hpp"
#include "../core/controller.hpp"
#include "renderer.hpp"

int main() {

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    auto test_entity = sim.world().entity();
    test_entity.set<simnet::ecs::Position3D>({0, 0, 0});
    test_entity.add<simnet::ecs::Renderable>();
    test_entity.set<simnet::ecs::Velocity3D>({1.5f, 0.3f, -0.8f});

    simnet::client::Renderer renderer(800, 450, "SimNetLab_Client", sim.world());

    while (renderer.is_running()) {

        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        renderer.begin_frame();

        renderer.update_cam();
        renderer.draw_world();

        renderer.draw_debug(steps, alpha, sim.current_tick());
        renderer.end_frame();
    }

    return 0;
}
