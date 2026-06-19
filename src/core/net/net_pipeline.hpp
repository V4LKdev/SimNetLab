#pragma once
#include <memory>
#include <vector>
#include "peer_state.hpp"
#include "net_processor.hpp"

namespace simnet::core::net::internal {
    class NetPipeline {
    public:
        void add_processor(std::unique_ptr<NetProcessor> processor);

        /// Apply outgoing processors in forward order (0 -> N-1)
        void apply_outgoing(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags);

        /// Apply incoming processors in reverse order (N-1 -> 0)
        void apply_incoming(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags);

    private:
        std::vector<std::unique_ptr<NetProcessor> > processors_;
    };
}
