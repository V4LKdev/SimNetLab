#pragma once
#include "net_buffer.hpp"
#include "peer_state.hpp"
#include "net_types.hpp"

namespace simnet::core::net::internal {
    class NetProcessor {
    public:
        virtual ~NetProcessor() = default;

        /// Process a buffer before sending (forward pass)
        virtual void process_outgoing(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags) = 0;

        /// Process a buffer after receiving (reverse pass)
        virtual void process_incoming(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags) = 0;
    };
}
