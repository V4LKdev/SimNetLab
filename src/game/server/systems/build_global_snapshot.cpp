
#include "system_functions.hpp"
#include "game/server/components.hpp"

#include "game/shared/game_shared.hpp"

namespace simnet::game::server {
    void build_global_snapshot_system(flecs::iter &it)
    {
        const auto &cfg = it.world().get<config::SimConfig>();
        const auto &tick = it.world().get<shared::SimTick>();

        if (tick.value % cfg.snapshot_send_interval != 0) {
            it.world().get_mut<GlobalSnapshot>().entities.clear();
            return;
        }

        auto &snap = it.world().ensure<GlobalSnapshot>();
        snap.entities.clear();

        auto q = it.world().query_builder<const shared::Position, const shared::Heading, const shared::Hue, const
                    shared::NetworkId>()
                .with<shared::Boid>()
                .build();

        q.each([&](const shared::Position &pos, const shared::Heading &hd, const shared::Hue &hue,
                   const shared::NetworkId &nid) {
            snap.entities.push_back({nid.value, pos.value, hd.value, hue.value});
        });

        // Deterministic order
        std::ranges::sort(snap.entities, {},
                          [](const auto &e) { return e.network_id; });
    }
}
