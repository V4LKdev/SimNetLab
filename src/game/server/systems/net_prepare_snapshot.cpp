#include "system_functions.hpp"

#include "core/net/net_manager.hpp"
#include "core/net/pipeline/net_pipeline_interfaces.hpp"
#include "game/server/app_context.hpp"
#include "game/server/components.hpp"
#include "game/shared/components.hpp"
#include "telemetry/telemetry.hpp"

namespace simnet::game::server {
    void net_prepare_snapshot_system(flecs::iter &it)
    {
        // --- 1. Get context ---
        const auto *ctx = static_cast<AppContext *>(it.world().get_ctx());
        if (!ctx || !ctx->net) {
            TELEM_LOG_WARN("NetPrepareSnapshot: missing NetManager in context");
            return;
        }
        const auto &world = it.world();
        auto *net = ctx->net;

        // --- 2. Read tick counter (const reference) ---
        const auto &tick = world.get<shared::SimTick>(); // returns const SimTick&
        const uint32_t tick_value = tick.value;

        // --- 3. Create new snapshot (SoA) ---
        auto &snap_query = ctx->snapshot_query;
        if (!snap_query) {
            TELEM_LOG_ERROR("NetPrepareSnapshot: missing cached query");
            return;
        }

        auto snapshot = std::make_shared<net::internal::NetworkSnapshot>();
        snapshot->tick = tick_value;

        // --- 4. Query all boids ---
        snap_query.each([&](const shared::Position &pos,
                            const shared::Heading &head,
                            const shared::Hue &hue,
                            const shared::NetworkId &net_id) {
            snapshot->entity_ids.push_back(net_id.value);
            snapshot->pos_x.push_back(pos.value.x());
            snapshot->pos_y.push_back(pos.value.y());
            snapshot->pos_z.push_back(pos.value.z());
            snapshot->heading_x.push_back(head.value.x());
            snapshot->heading_y.push_back(head.value.y());
            snapshot->heading_z.push_back(head.value.z());
            snapshot->hues.push_back(hue.value);
        });

        // --- 5. Push snapshot into NetManager's history ring ---
        net->push_snapshot(*snapshot);

        // --- 6. Store the snapshot in CurrentSnapshot singleton (mutable reference) ---
        auto &current = world.get_mut<server::CurrentSnapshot>(); // returns CurrentSnapshot&
        current.snapshot = std::move(snapshot);

        // --- 7. Telemetry ---
        TELEM_COUNTER_SET("net.snapshot_entity_count", static_cast<int64_t>(current.snapshot->entity_ids.size()));
        TELEM_COUNTER_INC("net.snapshot_prepared", 1);
        TELEM_LOG_TRACE("NetPrepareSnapshot: tick {}, entities {}", tick_value, current.snapshot->entity_ids.size());
    }
}
