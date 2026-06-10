
#include <flecs.h>

#include "ecs/components.hpp"

namespace simnet::ecs {
    void alignment_system(flecs::iter &it)
    {
        while (it.next()) {
            for (auto i : it) {
                // ... actual logic (can be empty for now)
            }
        }
    }
}
