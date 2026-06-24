#pragma once

#include "core/net/net_snapshot.hpp"

namespace simnet::game::server {
    struct GlobalSnapshot {
        std::vector<net::internal::ReplicatedEntity> entities;
    };

    struct SnapshotSequence {
        uint32_t value = 0;
    };
}
