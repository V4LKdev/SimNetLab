#pragma once
#include <flecs.h>
#include "telemetry/telemetry.hpp"

namespace simnet::game::server {
    void boid_population_manager_system(flecs::iter &it);

    void net_prepare_snapshot_system(flecs::iter &it);

    void net_compute_peer_visibility_system(flecs::iter &it);

    void net_pipeline_system(flecs::iter &it);
}
