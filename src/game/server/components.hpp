#pragma once

#include "core/net/pipeline/net_pipeline_interfaces.hpp"
#include "core/net/pipeline/net_pipeline_chain.hpp"

namespace simnet::game::server {
    struct CurrentSnapshot {
        std::shared_ptr<net::internal::NetworkSnapshot> snapshot;
    };

    struct SnapshotSequence {
        uint32_t value = 0;
    };

    /// Singleton holding the global immutable pipeline chain for all peers.
    struct NetPipelineChain {
        net::internal::PipelineChain chain;
    };
}
