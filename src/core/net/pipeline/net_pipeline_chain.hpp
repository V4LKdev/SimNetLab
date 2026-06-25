#pragma once
#include <vector>
#include <memory>
#include "core/net/pipeline/net_pipeline_interfaces.hpp"

namespace simnet::core::net::internal {
    class PipelineChain {
    public:
        void add_filter(std::unique_ptr<IEntityFilter> f)
        {
            filters_.push_back(std::move(f));
        }

        void set_serializer(std::unique_ptr<ISerializer> s)
        {
            serializer_ = std::move(s);
        }

        void add_encoder(std::unique_ptr<IEncoder> e)
        {
            encoders_.push_back(std::move(e));
        }

        /// Execute the pipeline for one peer.  ctx.active_indices is filtered,
        /// then serialized, then post‑encoded into `final_buffer`.
        void execute(PipelineContext &ctx, NetBuffer &final_buffer) const
        {
            // 1. filters
            for (auto &f: filters_) {
                f->filter(ctx);
            }
            // 2. serializer
            NetBuffer raw;
            if (serializer_) {
                serializer_->serialize(ctx, raw);
            }
            // 3. encoders
            NetBuffer temp = std::move(raw);
            for (auto &e: encoders_) {
                NetBuffer encoded;
                e->encode(temp, encoded);
                temp = std::move(encoded);
            }
            final_buffer = std::move(temp);
        }

    private:
        std::vector<std::unique_ptr<IEntityFilter> > filters_;
        std::unique_ptr<ISerializer> serializer_;
        std::vector<std::unique_ptr<IEncoder> > encoders_;
    };
}
