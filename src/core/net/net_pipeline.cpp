#include "net_pipeline.hpp"
#include <ranges>
#include "telemetry.hpp"

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
            char zone_name[128];
            std::snprintf(zone_name, sizeof(zone_name), "Proc_%s_out", proc->name());
            TELEM_TRACY_ZONE_DYN(zone_name, TELEM_COLOR_NET_SEND);
            proc->process_outgoing(peer, buffer, flags);
        }
    }

    void NetPipeline::apply_incoming(PeerState &peer, NetBuffer &buffer, SnapshotFlags flags)
    {
        TELEM_TRACY_ZONE_C("NetPipeline_apply_incoming", TELEM_COLOR_NET_RECV);
        TELEM_COUNTER_INC("net.pipeline_incoming_calls", 1);

        for (auto &processor: std::views::reverse(processors_)) {
            char zone_name[128];
            std::snprintf(zone_name, sizeof(zone_name), "Proc_%s_in", processor->name());
            TELEM_TRACY_ZONE_DYN(zone_name, TELEM_COLOR_NET_RECV);
            processor->process_incoming(peer, buffer, flags);
        }
    }
}
