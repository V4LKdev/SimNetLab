#include "system_functions.hpp"

#include "game/server/app_context.hpp"
#include "game/server/components.hpp"
#include "game/shared/components.hpp"
#include "core/net/net_manager.hpp"
#include "core/config/sim_config.hpp"
#include "telemetry/telemetry.hpp"
#include <numeric>


namespace simnet::game::server {
    void net_compute_peer_visibility_system(flecs::iter &it)
    {
        // --- 1. Get context ---
        const auto *ctx = static_cast<AppContext *>(it.world().get_ctx());
        if (!ctx || !ctx->net) {
            TELEM_LOG_WARN("NetComputeVisibility: missing NetManager");
            return;
        }
        const auto &world = it.world();
        const auto *net = ctx->net;

        // --- 2. Read CurrentSnapshot (const reference) ---
        const auto &current = world.get<server::CurrentSnapshot>(); // returns const CurrentSnapshot&
        if (!current.snapshot) {
            TELEM_LOG_WARN("NetComputeVisibility: CurrentSnapshot not ready");
            return;
        }
        const auto &snapshot = *current.snapshot;
        const size_t total_entities = snapshot.entity_ids.size();

        // --- 3. Read config (const reference) ---
        const auto &config = world.get<config::SimConfig>(); // returns const SimConfig&

        // --- 4. Get all connected peers ---
        auto peer_ids = net->get_connected_peer_ids();
        if (peer_ids.empty()) {
            return;
        }

        // --- 5. Prepare full indices (used for all peers when AoI is off) ---
        std::vector<uint32_t> full_indices(total_entities);
        std::iota(full_indices.begin(), full_indices.end(), 0u);

        // --- 6. For each peer, compute visible set ---
        for (auto id: peer_ids) {
            auto &peer_state = net->get_peer_state(id);
            if (!peer_state.is_handshake_complete()) {
                continue;
            }

            if (config.enable_aoi_filter) {
                // TODO: query spatial grid with peer's viewport.
                // const auto& grid = world.get<shared::SpatialGrid>();
                // const auto& viewport = peer_state.viewport_center();
                // auto indices = grid.query(viewport, radius);
                // peer_state.set_visible_indices(std::move(indices));
                peer_state.set_visible_indices(full_indices);
            } else {
                peer_state.set_visible_indices(full_indices);
            }
        }

        // --- 7. Telemetry ---
        TELEM_COUNTER_SET("net.visibility_peers_updated", static_cast<int64_t>(peer_ids.size()));
        TELEM_LOG_TRACE("NetComputeVisibility: updated visibility for {} peers, {} entities each",
                        peer_ids.size(), total_entities);
    }
}
