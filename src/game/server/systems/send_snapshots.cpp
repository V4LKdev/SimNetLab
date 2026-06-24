
#include "app_context.hpp"
#include "system_functions.hpp"
#include "components.hpp"
#include "game/shared/game_shared.hpp"

namespace simnet::game::server {
    void send_snapshots_system(flecs::iter &it)
    {
        AppContext *ctx = static_cast<AppContext *>(it.world().get_ctx());
        if (!ctx || !ctx->net) return;

        if (ctx->net->get_connected_peer_ids().empty()) return;

        auto &snap = it.world().get_mut<GlobalSnapshot>();
        if (snap.entities.empty()) return;

        auto sim_tick = it.world().get<shared::SimTick>();
        auto snap_seq = it.world().get_mut<SnapshotSequence>();
        snap_seq.value++;

        net::internal::ReplicationSnapshot snapshot;
        snapshot.tick = sim_tick.value;
        snapshot.sequence = snap_seq.value;
        snapshot.entities = std::move(snap.entities);
        snap.entities.clear();

        ctx->net->broadcast_snapshot(snapshot);
    }
}
