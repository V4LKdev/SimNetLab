#pragma once
#include <flecs.h>
#include "telemetry/telemetry.hpp"

namespace simnet::game::server {
    void boid_population_manager_system(flecs::iter &it);

    void build_global_snapshot_system(flecs::iter &it);

    void send_snapshots_system(flecs::iter &it);
}
