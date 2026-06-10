
#include <flecs.h>

#include "ecs/components.hpp"

namespace simnet::ecs {
    void separation_system(flecs::iter &it)
    {
        while (it.next()) {
            for (auto i : it) {
                // ... actual logic (can be empty for now)
            }
        }
    }
}
