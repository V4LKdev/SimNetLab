#include "net_pipeline.hpp"
#include <ranges>
#include "telemetry/telemetry.hpp"

namespace simnet::core::net::internal {
    void NetPipeline::add_processor(std::unique_ptr<NetProcessor> processor)
    {
        processors_.push_back(std::move(processor));
    }

    void NetPipeline::apply_outgoing(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags)
    {
        TELEM_TRACY_ZONE_C("NetPipeline_apply_outgoing", TELEM_COLOR_NET_SEND);
        TELEM_COUNTER_INC("net.pipeline_outgoing_calls", 1);

        for (auto &proc: processors_) {
            proc->process_outgoing(peer, buffer, flags);
        }
    }

    void NetPipeline::apply_incoming(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags)
    {
        TELEM_TRACY_ZONE_C("NetPipeline_apply_incoming", TELEM_COLOR_NET_RECV);
        TELEM_COUNTER_INC("net.pipeline_incoming_calls", 1);

        for (auto &processor: std::views::reverse(processors_)) {
            processor->process_incoming(peer, buffer, flags);
        }
    }
}
