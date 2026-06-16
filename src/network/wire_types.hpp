#pragma once
#include <cstdint>
#include <vector>

#include "math/vec3.hpp"

/**
 *  Intermediate representations for network data.
 */
namespace simnet::net {
    /// One replicated entity's state for a snapshot
    struct ReplicatedEntity {
        uint32_t network_id = 0;
        Vec3 position = Vec3::zero();
        Vec3 heading = Vec3::forward();
        uint8_t hue = 0;
    };

    /// Full snapshot payload before serialization
    struct ReplicationSnapshot {
        uint32_t tick = 0;
        uint32_t sequence = 0;
        std::vector<ReplicatedEntity> entities;
    };
}
