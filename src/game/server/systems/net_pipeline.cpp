#include "system_functions.hpp"

#include <chrono>
#include "core/config/sim_config.hpp"
#include "core/net/net_manager.hpp"
#include "core/net/pipeline/net_pipeline_chain.hpp"
#include "core/net/pipeline/net_pipeline_interfaces.hpp"
#include "game/server/app_context.hpp"
#include "game/server/components.hpp"
#include "game/shared/components.hpp"
#include "telemetry/telemetry.hpp"

namespace simnet::game::server {
    void net_pipeline_system(flecs::iter &it)
    {
        // --- 1. Get context ---
        const auto *ctx = static_cast<AppContext *>(it.world().get_ctx());
        if (!ctx || !ctx->net) {
            TELEM_LOG_WARN("NetPipeline: missing NetManager");
            return;
        }
        const auto &world = it.world();
        auto *net = ctx->net;

        // --- 2. Read CurrentSnapshot (const reference) ---
        const auto &current = world.get<server::CurrentSnapshot>(); // returns const CurrentSnapshot&
        if (!current.snapshot) {
            TELEM_LOG_WARN("NetPipeline: CurrentSnapshot not ready");
            return;
        }
        const auto &snapshot = *current.snapshot;
        const uint64_t tick = snapshot.tick;

        // --- 3. Read SimConfig (const reference) ---
        const auto &config = world.get<config::SimConfig>(); // returns const SimConfig&

        // --- 4. Read global pipeline chain (mutable reference) ---
        auto &pipeline_comp = world.get_mut<server::NetPipelineChain>(); // returns NetPipelineChain&
        auto &chain = pipeline_comp.chain;

        // --- 5. Get all connected peers ---
        auto peer_ids = net->get_connected_peer_ids();
        if (peer_ids.empty()) {
            return;
        }

        // --- 6. Process each peer ---
        for (auto id: peer_ids) {
            auto start_time = std::chrono::steady_clock::now();

            // 6a. Get peer state
            auto &peer_state = net->get_peer_state(id);
            if (!peer_state.is_handshake_complete()) {
                continue;
            }

            // 6b. Get visible indices (take a copy for mutation)
            std::vector<uint32_t> active_indices = peer_state.visible_indices();
            if (active_indices.empty()) {
                continue;
            }

            // 6c. Resolve delta baseline (if enabled)
            std::shared_ptr<const net::internal::NetworkSnapshot> baseline = nullptr;
            if (config.enable_delta_compression) {
                uint64_t ack_tick = peer_state.last_acknowledged_tick();
                if (ack_tick != 0) {
                    baseline = net->get_baseline(ack_tick);
                }
            }

            // 6d. Increment sequence number
            peer_state.increment_sequence();
            uint32_t sequence = peer_state.next_sequence();

            // 6e. Build PipelineContext
            net::internal::PipelineContext pipe_ctx{
                .peer_id = id,
                .sequence = sequence,
                .current_tick = tick,
                .snapshot = snapshot,
                .baseline = std::move(baseline),
                .peer_state = peer_state,
                .active_indices = active_indices
            };

            // 6f. Execute pipeline
            net::internal::NetBuffer final_buffer;
            chain.execute(pipe_ctx, final_buffer);

            // 6g. Send if we have data
            if (final_buffer.size() > 0) {
                net->send_to_peer(
                    id,
                    final_buffer,
                    static_cast<uint8_t>(net::internal::NetChannel::Snapshot),
                    net::internal::TransportReliability::unreliable_fragmented
                );

                auto end_time = std::chrono::steady_clock::now();
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
                TELEM_HISTOGRAM_ADD("net.pipeline_peer_time_us", static_cast<double>(elapsed_us));
            }
        }

        TELEM_COUNTER_SET("net.pipeline_peers_processed", static_cast<int64_t>(peer_ids.size()));
        TELEM_LOG_TRACE("NetPipeline: processed {} peers at tick {}", peer_ids.size(), tick);
    }
}
