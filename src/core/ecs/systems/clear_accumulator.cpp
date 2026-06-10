#include <flecs.h>

#include "../../vec3.hpp"
#include "ecs/components.hpp"


namespace simnet::ecs {
    void clear_accumulator_system(flecs::iter &it)
    {
        while (it.next()) {
            for (auto i : it) {
                // ... actual logic (can be empty for now)
            }
        }
    }
}
