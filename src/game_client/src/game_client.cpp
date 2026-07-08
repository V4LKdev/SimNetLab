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
    namespace
    {
        [[nodiscard]] flecs::entity make_replicated_entity(flecs::world& world, BoidState const& boid)
        {
            return world.entity()
                .set<NetIdentity>({ .id = boid.id })
                .set<Position>({ .value = boid.position })
                .set<Heading>({ .value = boid.heading })
                .set<Hue>({ .value = boid.hue })
                .add<BoidTag>();
        }

        void update_replicated_entity(flecs::world& world, flecs::entity_t entity_id, BoidState const& boid)
        {
            auto entity = flecs::entity { world, entity_id };
            entity.set<Position>({ .value = boid.position });
            entity.set<Heading>({ .value = boid.heading });
            entity.set<Hue>({ .value = boid.hue });
        }

        void delete_entity_if_alive(flecs::world& world, flecs::entity_t entity_id)
        {
            if (entity_id != 0 && ecs_is_alive(world.c_ptr(), entity_id)) {
                ecs_delete(world.c_ptr(), entity_id);
            }
        }

        [[nodiscard]] bool delete_matches(
            ClientSnapshotPatch const& patch,
            std::size_t& delete_index,
            EntityNetId id
        ) noexcept
        {
            while (delete_index < patch.deletes.size() && patch.deletes[delete_index] < id) {
                ++delete_index;
            }
            if (delete_index < patch.deletes.size() && patch.deletes[delete_index] == id) {
                ++delete_index;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool upsert_before_current(
            ClientSnapshotPatch const& patch,
            std::size_t upsert_index,
            EntityNetId id
        ) noexcept
        {
            return upsert_index < patch.upserts.size() && patch.upserts[upsert_index].id < id;
        }

        [[nodiscard]] bool upsert_matches(
            ClientSnapshotPatch const& patch,
            std::size_t upsert_index,
            EntityNetId id
        ) noexcept
        {
            return upsert_index < patch.upserts.size() && patch.upserts[upsert_index].id == id;
        }

        void append_entity(ClientReplicationIndex& index, EntityNetId id, flecs::entity_t entity_id)
        {
            index.ids.push_back(id);
            index.entities.push_back(entity_id);
        }
    }

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

        auto& index = world.ensure<ClientReplicationIndex>();
        auto& clock = world.ensure<ClientReplicationClock>();
        if (patch.tick < clock.latest_tick) {
            report.valid = false;
            report.error = "stale patch tick";
            report.final_entities = static_cast<std::uint32_t>(index.size());
            return report;
        }
        report.previous_entities = static_cast<std::uint32_t>(index.size());

        if (patch.kind == SnapshotKind::Patch) {
            auto next_index = ClientReplicationIndex {};
            next_index.reserve(index.size() + patch.upserts.size());

            auto current_index = std::size_t {};
            auto upsert_index = std::size_t {};
            auto delete_index = std::size_t {};

            while (current_index < index.ids.size()) {
                auto const current_id = index.ids[current_index];

                while (upsert_before_current(patch, upsert_index, current_id)) {
                    auto const& boid = patch.upserts[upsert_index];
                    auto entity = make_replicated_entity(world, boid);
                    append_entity(next_index, boid.id, entity.id());
                    ++upsert_index;
                }

                if (delete_matches(patch, delete_index, current_id)) {
                    delete_entity_if_alive(world, index.entities[current_index]);
                    ++current_index;
                    continue;
                }

                if (upsert_matches(patch, upsert_index, current_id)) {
                    auto const& boid = patch.upserts[upsert_index];
                    update_replicated_entity(world, index.entities[current_index], boid);
                    append_entity(next_index, current_id, index.entities[current_index]);
                    ++upsert_index;
                    ++current_index;
                    continue;
                }

                append_entity(next_index, current_id, index.entities[current_index]);
                ++current_index;
            }

            while (upsert_index < patch.upserts.size()) {
                auto const& boid = patch.upserts[upsert_index];
                auto entity = make_replicated_entity(world, boid);
                append_entity(next_index, boid.id, entity.id());
                ++upsert_index;
            }

            index = std::move(next_index);
            clock.latest_tick = patch.tick;
            world.modified<ClientReplicationIndex>();
            world.modified<ClientReplicationClock>();

            report.final_entities = static_cast<std::uint32_t>(index.size());
            return report;
        }

        auto old_entities = index.entities;
        for (auto const entity_id : old_entities) {
            delete_entity_if_alive(world, entity_id);
        }

        auto next_index = ClientReplicationIndex {};
        next_index.reserve(patch.upserts.size());

        for (auto const& boid : patch.upserts) {
            auto entity = make_replicated_entity(world, boid);
            append_entity(next_index, boid.id, entity.id());
        }

        index = std::move(next_index);
        clock.latest_tick = patch.tick;
        world.modified<ClientReplicationIndex>();
        world.modified<ClientReplicationClock>();

        report.final_entities = static_cast<std::uint32_t>(index.size());
        return report;
    }
}
