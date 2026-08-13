module;

#include <cstdint>
#include <flecs.h>
#include <optional>
#include <string>
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
            auto const entity_kind = entity_kind_from_classification(classification);
            if (entity_kind.has_value())
            {
                return *entity_kind;
            }
            std::unreachable();
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
        make_replicated_entity(flecs::world& world, EntityState const& entity_state)
        {
            return world.entity()
                .set<EntityKindComponent>({
                    .value = supported_entity_kind(entity_state.classification),
                })
                .set<NetIdentity>({.id = entity_state.id})
                .set<Position>({.value = entity_state.position})
                .set<Heading>({.value = entity_state.heading})
                .set<Hue>({.value = entity_state.hue});
        }

        void update_replicated_entity(
            flecs::world& world,
            flecs::entity_t entity_id,
            EntityState const& entity_state
        )
        {
            auto entity = flecs::entity{world, entity_id};
            entity.set<EntityKindComponent>({
                .value = supported_entity_kind(entity_state.classification),
            });
            entity.set<Position>({.value = entity_state.position});
            entity.set<Heading>({.value = entity_state.heading});
            entity.set<Hue>({.value = entity_state.hue});
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

        void apply_patch_to_world(
            flecs::world& world,
            ClientReplicationState& state,
            SnapshotUpdate const& patch
        )
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
                    auto const& entity_state = patch.upserts[upsert_index];
                    auto entity = make_replicated_entity(world, entity_state);
                    next_state.ids.push_back(entity_state.id);
                    next_state.entities.push_back(entity.id());
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
                    auto const& entity_state = patch.upserts[upsert_index];
                    update_replicated_entity(world, state.entities[current_index], entity_state);
                    next_state.ids.push_back(current_id);
                    next_state.entities.push_back(state.entities[current_index]);
                    ++upsert_index;
                    ++current_index;
                    continue;
                }

                next_state.ids.push_back(current_id);
                next_state.entities.push_back(state.entities[current_index]);
                ++current_index;
            }

            while (upsert_index < patch.upserts.size())
            {
                auto const& entity_state = patch.upserts[upsert_index];
                auto entity = make_replicated_entity(world, entity_state);
                next_state.ids.push_back(entity_state.id);
                next_state.entities.push_back(entity.id());
                ++upsert_index;
            }

            state = std::move(next_state);
        }

        void apply_full_replace_to_world(
            flecs::world& world,
            ClientReplicationState& state,
            SnapshotUpdate const& patch
        )
        {
            for (auto const entity_id : state.entities)
            {
                delete_entity_if_alive(world, entity_id);
            }

            auto next_state = ClientReplicationState{};
            next_state.reserve(patch.upserts.size());

            for (auto const& entity_state : patch.upserts)
            {
                auto entity = make_replicated_entity(world, entity_state);
                next_state.ids.push_back(entity_state.id);
                next_state.entities.push_back(entity.id());
            }

            state = std::move(next_state);
        }
    }

    void register_client_game(flecs::world& world)
    {
        register_game_components(world);
        world.ensure<ClientReplicationState>();
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
            apply_patch_to_world(world, state, patch);
        }
        else
        {
            apply_full_replace_to_world(world, state, patch);
        }

        state.latest_tick = patch.tick;
        world.modified<ClientReplicationState>();

        report.final_entities = static_cast<std::uint32_t>(state.size());
        return report;
    }

}
