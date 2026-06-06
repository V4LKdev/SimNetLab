#include "components.hpp"
#include "../core/controller.hpp"
#include "renderer.hpp"

int main() {

    simnet::sim::Simulation sim;
    simnet::core::TimestepController controller(sim);

    auto entities = 50;
    for (int i = 0; i < entities; ++i) {
        auto boid = sim.world().entity();

        float rx = static_cast<float>(rand() % 10 - 5);
        float ry = static_cast<float>(rand() % 10 - 5);
        float rz = static_cast<float>(rand() % 10 - 5);

        boid.set<simnet::ecs::Position3D>({rx,ry,rz});
        boid.set<simnet::ecs::Velocity3D>({0.1f,0,-0.1f});
        boid.set<simnet::ecs::Acceleration3D>({0,0,0});
        boid.add<simnet::ecs::Renderable>();
    }

    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", sim.world());

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
