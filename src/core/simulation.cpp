#include "simulation.hpp"
#include "components.hpp"
#include "config.hpp"

namespace simnet::sim {

    constexpr float SIM_DT_SECONDS = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

    Simulation::Simulation()
    {
        world_.component<ecs::Position3D>();
        world_.component<ecs::Velocity3D>();
        world_.component<ecs::Acceleration3D>();
        world_.component<ecs::Renderable>();

        world_.set<ecs::BoidConfig>({
            5.0f,
            5.0f,
            3.0f,
            1.5f,
            0.5f,
            0.8f,
            0.4f,
            10.0f
        });
    }

    void Simulation::step()
    {
        world_.progress(SIM_DT_SECONDS);

        ++tick_;
    }

    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }
}
