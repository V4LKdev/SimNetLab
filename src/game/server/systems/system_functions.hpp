#pragma once
#include <flecs.h>

namespace simnet::game::server {
    void boid_population_manager_system(flecs::iter &it);
}
