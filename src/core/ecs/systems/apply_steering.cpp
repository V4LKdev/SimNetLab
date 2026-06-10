#include <flecs.h>

#include "ecs/components.hpp"

namespace simnet::ecs {
    void apply_steering_system(flecs::iter &it)
    {
        while (it.next()) {
            for (auto i : it) {
                // ... actual logic (can be empty for now)
            }
        }
    }
}
