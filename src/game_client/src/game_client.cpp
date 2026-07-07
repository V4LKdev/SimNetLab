module;

#include <cstdint>
#include <flecs.h>
#include <utility>
#include <vector>

module simnet.game_client;

import :apply;
import simnet.game_shared;
import simnet.snapshot;

namespace simnet
{
    void register_client_game(flecs::world& world)
    {
        register_game_components(world);
        world.component<ClientReplicationIndex>("simnet::ClientReplicationIndex");
        world.component<ClientReplicationClock>("simnet::ClientReplicationClock");
        world.ensure<ClientReplicationIndex>();
        world.ensure<ClientReplicationClock>();
    }

    ApplyPatchReport apply_client_snapshot_patch(flecs::world& world, ClientSnapshotPatch const& patch)
    {
        auto report = ApplyPatchReport {
            .tick = patch.tick,
            .kind = patch.kind,
            .previous_entities = 0,
            .final_entities = 0,
            .upsert_count = static_cast<std::uint32_t>(patch.upserts.size()),
            .delete_count = static_cast<std::uint32_t>(patch.deletes.size()),
            .valid = true,
            .error = {},
        };

        auto const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid) {
            report.valid = false;
            report.error = validation.message;
            return report;
        }

        if (patch.kind == SnapshotKind::Patch) {
            report.valid = false;
            report.error = "Patch apply not implemented yet";
            return report;
        }

        register_client_game(world);
        auto& index = world.ensure<ClientReplicationIndex>();
        auto& clock = world.ensure<ClientReplicationClock>();
        report.previous_entities = static_cast<std::uint32_t>(index.size());

        auto old_entities = index.entities;
        for (auto const entity_id : old_entities) {
            if (entity_id != 0 && ecs_is_alive(world.c_ptr(), entity_id)) {
                ecs_delete(world.c_ptr(), entity_id);
            }
        }

        auto next_index = ClientReplicationIndex {};
        next_index.reserve(patch.upserts.size());

        for (auto const& boid : patch.upserts) {
            auto entity = world.entity()
                .set<NetIdentity>({ .id = boid.id })
                .set<Position>({ .value = boid.position })
                .set<Heading>({ .value = boid.heading })
                .set<Hue>({ .value = boid.hue })
                .add<BoidTag>();

            next_index.ids.push_back(boid.id);
            next_index.entities.push_back(entity.id());
        }

        index = std::move(next_index);
        clock.latest_tick = patch.tick;
        world.modified<ClientReplicationIndex>();
        world.modified<ClientReplicationClock>();

        report.final_entities = static_cast<std::uint32_t>(index.size());
        return report;
    }
}
