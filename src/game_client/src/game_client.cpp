module;

#include <algorithm>
#include <cstdint>
#include <flecs.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module simnet.game_client;

import :apply;
import :snapshot;
import simnet.game_shared;
import simnet.snapshot;

namespace simnet
{
    namespace
    {
        struct ClientReplicationState
        {
            std::vector<EntityNetId> ids;
            std::vector<flecs::entity_t> entities;
            Tick latest_tick{};

            [[nodiscard]] std::size_t size() const noexcept
            {
                return ids.size();
            }

            void reserve(std::size_t count)
            {
                ids.reserve(count);
                entities.reserve(count);
            }
        };

        [[nodiscard]] EntityKind supported_entity_kind(EntityClassification classification) noexcept
        {
            return *entity_kind_from_classification(classification);
        }

        [[nodiscard]] std::optional<std::string>
        unsupported_classification_error(SnapshotUpdate const& patch)
        {
            for (auto const& entity : patch.upserts)
            {
                if (!entity_kind_from_classification(entity.classification).has_value())
                {
                    return "unsupported entity classification " +
                           std::to_string(entity.classification.value());
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] flecs::entity
        make_replicated_entity(flecs::world& world, EntityState const& boid)
        {
            return world.entity()
                .set<EntityKindComponent>({
                    .value = supported_entity_kind(boid.classification),
                })
                .set<NetIdentity>({.id = boid.id})
                .set<Position>({.value = boid.position})
                .set<Heading>({.value = boid.heading})
                .set<Hue>({.value = boid.hue});
        }

        void update_replicated_entity(
            flecs::world& world,
            flecs::entity_t entity_id,
            EntityState const& boid
        )
        {
            auto entity = flecs::entity{world, entity_id};
            entity.set<EntityKindComponent>({
                .value = supported_entity_kind(boid.classification),
            });
            entity.set<Position>({.value = boid.position});
            entity.set<Heading>({.value = boid.heading});
            entity.set<Hue>({.value = boid.hue});
        }

        void delete_entity_if_alive(flecs::world& world, flecs::entity_t entity_id)
        {
            if (entity_id != 0 && ecs_is_alive(world.c_ptr(), entity_id))
            {
                ecs_delete(world.c_ptr(), entity_id);
            }
        }

        [[nodiscard]] bool delete_matches(
            SnapshotUpdate const& patch,
            std::size_t& delete_index,
            EntityNetId id
        ) noexcept
        {
            while (delete_index < patch.deletes.size() && patch.deletes[delete_index] < id)
            {
                ++delete_index;
            }
            if (delete_index < patch.deletes.size() && patch.deletes[delete_index] == id)
            {
                ++delete_index;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool upsert_before_current(
            SnapshotUpdate const& patch,
            std::size_t upsert_index,
            EntityNetId id
        ) noexcept
        {
            return upsert_index < patch.upserts.size() && patch.upserts[upsert_index].id < id;
        }

        [[nodiscard]] bool upsert_matches(
            SnapshotUpdate const& patch,
            std::size_t upsert_index,
            EntityNetId id
        ) noexcept
        {
            return upsert_index < patch.upserts.size() && patch.upserts[upsert_index].id == id;
        }

        void append_entity(ClientReplicationState& state, EntityNetId id, flecs::entity_t entity_id)
        {
            state.ids.push_back(id);
            state.entities.push_back(entity_id);
        }

        void reset_failed_snapshot(WorldSnapshot& snapshot, Tick tick)
        {
            snapshot.clear();
            snapshot.tick = tick;
        }

        [[nodiscard]] ApplyPatchReport initial_apply_report(SnapshotUpdate const& patch)
        {
            return {
                .tick = patch.tick,
                .kind = patch.kind,
                .previous_entities = 0,
                .final_entities = 0,
                .upsert_count = static_cast<std::uint32_t>(patch.upserts.size()),
                .delete_count = static_cast<std::uint32_t>(patch.deletes.size()),
                .valid = true,
                .error = {},
            };
        }

        [[nodiscard]] ApplyPatchReport rejected_apply_report(
            flecs::world const& world,
            SnapshotUpdate const& patch,
            std::string error
        )
        {
            auto report = initial_apply_report(patch);
            auto const count =
                static_cast<std::uint32_t>(world.get<ClientReplicationState>().size());
            report.previous_entities = count;
            report.final_entities = count;
            report.valid = false;
            report.error = std::move(error);
            return report;
        }
    }

    void register_client_game(flecs::world& world)
    {
        register_game_components(world);
        world.ensure<ClientReplicationState>();
    }

    std::size_t client_replicated_entity_count(flecs::world const& world) noexcept
    {
        return world.get<ClientReplicationState>().size();
    }

    Tick client_latest_replicated_tick(flecs::world const& world) noexcept
    {
        return world.get<ClientReplicationState>().latest_tick;
    }

    std::optional<EntityKind> client_entity_kind(flecs::world const& world, EntityNetId id) noexcept
    {
        auto const& state = world.get<ClientReplicationState>();
        auto const position = std::lower_bound(state.ids.begin(), state.ids.end(), id);
        if (position == state.ids.end() || *position != id)
        {
            return std::nullopt;
        }
        auto const offset = static_cast<std::size_t>(position - state.ids.begin());
        auto const entity = flecs::entity{world, state.entities[offset]};
        if (!entity.is_alive() || !entity.has<EntityKindComponent>())
        {
            return std::nullopt;
        }
        return entity.get<EntityKindComponent>().value;
    }

    ApplyPatchReport
    apply_client_snapshot_patch_unchecked(flecs::world& world, SnapshotUpdate const& patch)
    {
        auto report = initial_apply_report(patch);

        auto const& current_state = world.get<ClientReplicationState>();
        report.previous_entities = static_cast<std::uint32_t>(current_state.size());
        report.final_entities = report.previous_entities;
        if (patch.tick < current_state.latest_tick)
        {
            report.valid = false;
            report.error = "stale patch tick";
            return report;
        }
        if (auto error = unsupported_classification_error(patch); error.has_value())
        {
            report.valid = false;
            report.error = std::move(*error);
            return report;
        }

        auto& state = world.ensure<ClientReplicationState>();

        if (patch.kind == SnapshotKind::Patch)
        {
            auto next_state = ClientReplicationState{};
            next_state.reserve(state.size() + patch.upserts.size());

            auto current_index = std::size_t{};
            auto upsert_index = std::size_t{};
            auto delete_index = std::size_t{};

            while (current_index < state.ids.size())
            {
                auto const current_id = state.ids[current_index];

                while (upsert_before_current(patch, upsert_index, current_id))
                {
                    auto const& boid = patch.upserts[upsert_index];
                    auto entity = make_replicated_entity(world, boid);
                    append_entity(next_state, boid.id, entity.id());
                    ++upsert_index;
                }

                if (delete_matches(patch, delete_index, current_id))
                {
                    delete_entity_if_alive(world, state.entities[current_index]);
                    ++current_index;
                    continue;
                }

                if (upsert_matches(patch, upsert_index, current_id))
                {
                    auto const& boid = patch.upserts[upsert_index];
                    update_replicated_entity(world, state.entities[current_index], boid);
                    append_entity(next_state, current_id, state.entities[current_index]);
                    ++upsert_index;
                    ++current_index;
                    continue;
                }

                append_entity(next_state, current_id, state.entities[current_index]);
                ++current_index;
            }

            while (upsert_index < patch.upserts.size())
            {
                auto const& boid = patch.upserts[upsert_index];
                auto entity = make_replicated_entity(world, boid);
                append_entity(next_state, boid.id, entity.id());
                ++upsert_index;
            }

            state = std::move(next_state);
            state.latest_tick = patch.tick;
            world.modified<ClientReplicationState>();

            report.final_entities = static_cast<std::uint32_t>(state.size());
            return report;
        }

        for (auto const entity_id : state.entities)
        {
            delete_entity_if_alive(world, entity_id);
        }

        auto next_state = ClientReplicationState{};
        next_state.reserve(patch.upserts.size());

        for (auto const& boid : patch.upserts)
        {
            auto entity = make_replicated_entity(world, boid);
            append_entity(next_state, boid.id, entity.id());
        }

        state = std::move(next_state);
        state.latest_tick = patch.tick;
        world.modified<ClientReplicationState>();

        report.final_entities = static_cast<std::uint32_t>(state.size());
        return report;
    }

    ApplyPatchReport apply_client_snapshot_patch(flecs::world& world, SnapshotUpdate const& patch)
    {
        auto const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid)
        {
            return rejected_apply_report(world, patch, validation.message);
        }
        return apply_client_snapshot_patch_unchecked(world, patch);
    }

    ClientSnapshotExtractionReport
    extract_client_world_snapshot(flecs::world const& world, Tick tick, WorldSnapshot& out_snapshot)
    {
        auto report = ClientSnapshotExtractionReport{.tick = tick};
        auto const& state = world.get<ClientReplicationState>();
        if (state.ids.size() != state.entities.size())
        {
            reset_failed_snapshot(out_snapshot, tick);
            report.valid = false;
            report.error = "client replication index sizes do not match";
            return report;
        }

        out_snapshot.clear();
        out_snapshot.tick = tick;
        out_snapshot.reserve(state.size());
        for (auto index_position = std::size_t{}; index_position < state.size(); ++index_position)
        {
            auto const entity_id = state.entities[index_position];
            if (entity_id == 0 || !ecs_is_alive(world.c_ptr(), entity_id))
            {
                reset_failed_snapshot(out_snapshot, tick);
                report.valid = false;
                report.error = "client replication index references a dead entity";
                return report;
            }

            auto const entity = flecs::entity{world, entity_id};
            if (!entity.has<EntityKindComponent>() || !entity.has<NetIdentity>() ||
                !entity.has<Position>() || !entity.has<Heading>() || !entity.has<Hue>())
            {
                reset_failed_snapshot(out_snapshot, tick);
                report.valid = false;
                report.error = "client replicated entity is missing required components";
                return report;
            }

            auto const& identity = entity.get<NetIdentity>();
            if (identity.id != state.ids[index_position])
            {
                reset_failed_snapshot(out_snapshot, tick);
                report.valid = false;
                report.error = "client replication index identity does not match entity";
                return report;
            }

            auto const classification =
                classification_from_entity_kind(entity.get<EntityKindComponent>().value);
            if (!classification.has_value())
            {
                reset_failed_snapshot(out_snapshot, tick);
                report.valid = false;
                report.error = "client replicated entity kind is unsupported";
                return report;
            }

            out_snapshot.ids.push_back(identity.id);
            out_snapshot.classifications.push_back(*classification);
            out_snapshot.positions.push_back(entity.get<Position>().value);
            out_snapshot.headings.push_back(entity.get<Heading>().value);
            out_snapshot.hues.push_back(entity.get<Hue>().value);
        }

        auto const validation = validate_world_snapshot(out_snapshot);
        if (!validation.valid)
        {
            reset_failed_snapshot(out_snapshot, tick);
            report.valid = false;
            report.error = validation.message;
            return report;
        }

        report.entity_count = static_cast<std::uint32_t>(out_snapshot.size());
        return report;
    }
}
