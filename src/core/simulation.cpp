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

        world_.system<ecs::Position3D, ecs::Velocity3D>()
            .kind(flecs::OnUpdate)
            .run([](flecs::iter& it) {

                float dt = it.delta_time();

                while (it.next()) {
                    auto pos = it.field<ecs::Position3D>(0);
                    auto vel = it.field<ecs::Velocity3D>(1);

                    for (auto i : it) {
                        pos[i].x += vel[i].x * dt;
                        pos[i].y += vel[i].y * dt;
                        pos[i].z += vel[i].z * dt;

                        auto wrap = [](float& val) {
                            if (val > config::WORLD_HALF) { val -= 2.0f * config::WORLD_HALF; }
                            else if (val < -config::WORLD_HALF) { val += 2.0f * config::WORLD_HALF; }
                        };

                        wrap(pos[i].x);
                        wrap(pos[i].y);
                        wrap(pos[i].z);
                    }
                }
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
