module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <flecs.h>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <simnet/telemetry_trace.hpp>

module simnet.game_server;

import :snapshot;
import simnet.game_shared;
import simnet.snapshot;
import simnet.telemetry;

namespace
{
    struct AuthoritativeReplicationIndex
    {
        std::vector<simnet::EntityNetId> ids {};
        std::vector<flecs::entity_t> entities {};

        void reserve(std::size_t count)
        {
            ids.reserve(count);
            entities.reserve(count);
        }
    };

    [[nodiscard]] bool valid_index(AuthoritativeReplicationIndex const& index) noexcept
    {
        return index.ids.size() == index.entities.size();
    }

    [[nodiscard]] bool valid_boid_state(simnet::BoidState const& boid) noexcept
    {
        return boid.id != 0U
            && simnet::is_finite(boid.position)
            && simnet::is_finite(boid.heading)
            && std::abs(simnet::length(boid.heading) - 1.0F) <= simnet::heading_normalization_tolerance;
    }

    [[nodiscard]] auto find_index(AuthoritativeReplicationIndex const& index, simnet::EntityNetId id)
    {
        return std::lower_bound(index.ids.begin(), index.ids.end(), id);
    }

    void set_authoritative_boid_components(flecs::entity entity, simnet::BoidState const& boid)
    {
        entity.set<simnet::NetIdentity>({ .id = boid.id });
        entity.set<simnet::Position>({ .value = boid.position });
        entity.set<simnet::Heading>({ .value = boid.heading });
        entity.set<simnet::Hue>({ .value = boid.hue });
        entity.add<simnet::BoidTag>();
    }

    void reset_failed_snapshot(simnet::WorldSnapshot& snapshot, simnet::Tick tick)
    {
        snapshot.clear();
        snapshot.tick = tick;
    }

}

namespace simnet
{
    std::string_view authoritative_spawn_error_name(AuthoritativeSpawnError error) noexcept
    {
        switch (error) {
        case AuthoritativeSpawnError::None: return "none";
        case AuthoritativeSpawnError::CountOutOfRange: return "count_out_of_range";
        case AuthoritativeSpawnError::ZeroId: return "zero_id";
        case AuthoritativeSpawnError::NonAscendingIds: return "non_ascending_ids";
        case AuthoritativeSpawnError::ExistingIdOverlap: return "existing_id_overlap";
        case AuthoritativeSpawnError::InvalidBoidState: return "invalid_boid_state";
        case AuthoritativeSpawnError::InvalidIndexState: return "invalid_index_state";
        case AuthoritativeSpawnError::FlecsBulkInsertFailed: return "flecs_bulk_insert_failed";
        }
        return "unknown";
    }

    void register_server_game(flecs::world& world)
    {
        register_game_components(world);
        world.component<AuthoritativeReplicationIndex>("simnet::detail::AuthoritativeReplicationIndex");
        world.ensure<AuthoritativeReplicationIndex>();
    }

    flecs::entity upsert_authoritative_boid(flecs::world& world, BoidState const& boid)
    {
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index) || !valid_boid_state(boid)) {
            return {};
        }

        auto position = find_index(index, boid.id);
        auto offset = static_cast<std::size_t>(position - index.ids.begin());
        if (position != index.ids.end() && *position == boid.id) {
            auto entity = flecs::entity { world, index.entities[offset] };
            if (!entity.is_alive()) {
                return {};
            }
            set_authoritative_boid_components(entity, boid);
            return entity;
        }

        index.reserve(index.ids.size() + 1U);
        position = find_index(index, boid.id);
        offset = static_cast<std::size_t>(position - index.ids.begin());
        auto entity = world.entity();
        set_authoritative_boid_components(entity, boid);
        index.ids.insert(position, boid.id);
        index.entities.insert(index.entities.begin() + static_cast<std::ptrdiff_t>(offset), entity.id());
        world.modified<AuthoritativeReplicationIndex>();
        return entity;
    }

    AuthoritativeSpawnReport append_authoritative_boids(
        flecs::world& world,
        std::span<const BoidState> boids
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn", LogCategory::Simulation);
        auto report = AuthoritativeSpawnReport { .requested_count = boids.size() };
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index)) {
            report.error = AuthoritativeSpawnError::InvalidIndexState;
            return report;
        }
        if (boids.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            report.error = AuthoritativeSpawnError::CountOutOfRange;
            return report;
        }
        if (boids.empty()) {
            return report;
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_validation", LogCategory::Simulation);
            auto const current_maximum = index.ids.empty() ? EntityNetId {} : index.ids.back();
            for (std::size_t offset = 0; offset < boids.size(); ++offset) {
                auto const& boid = boids[offset];
                if (boid.id == 0U) {
                    report.error = AuthoritativeSpawnError::ZeroId;
                    report.failing_index = offset;
                    return report;
                }
                if (offset != 0U && boid.id <= boids[offset - 1U].id) {
                    report.error = AuthoritativeSpawnError::NonAscendingIds;
                    report.failing_index = offset;
                    return report;
                }
                if (boid.id <= current_maximum) {
                    report.error = AuthoritativeSpawnError::ExistingIdOverlap;
                    report.failing_index = offset;
                    return report;
                }
                if (!valid_boid_state(boid)) {
                    report.error = AuthoritativeSpawnError::InvalidBoidState;
                    report.failing_index = offset;
                    return report;
                }
            }
        }

        auto identities = std::vector<NetIdentity> {};
        auto positions = std::vector<Position> {};
        auto headings = std::vector<Heading> {};
        auto hues = std::vector<Hue> {};
        auto entities = std::vector<flecs::entity_t> {};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_component_arrays", LogCategory::Simulation);
            identities.reserve(boids.size());
            positions.reserve(boids.size());
            headings.reserve(boids.size());
            hues.reserve(boids.size());
            entities.resize(boids.size());
            index.reserve(index.ids.size() + boids.size());
            for (auto const& boid : boids) {
                identities.push_back({ .id = boid.id });
                positions.push_back({ .value = boid.position });
                headings.push_back({ .value = boid.heading });
                hues.push_back({ .value = boid.hue });
            }
        }

        auto ids = std::array<ecs_id_t, 5> {
            world.id<NetIdentity>(),
            world.id<Position>(),
            world.id<Heading>(),
            world.id<Hue>(),
            world.id<BoidTag>(),
        };
        auto data = std::array<void*, 5> {
            identities.data(),
            positions.data(),
            headings.data(),
            hues.data(),
            nullptr,
        };
        auto populate = ecs_bulk_desc_t {};
        populate.count = static_cast<std::int32_t>(boids.size());
        std::copy(ids.begin(), ids.end(), populate.ids);
        populate.data = data.data();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_flecs", LogCategory::Simulation);
            auto const* created = ecs_bulk_init(world.c_ptr(), &populate);
            if (created == nullptr) {
                report.error = AuthoritativeSpawnError::FlecsBulkInsertFailed;
                return report;
            }
            std::copy_n(created, boids.size(), entities.begin());
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_index_append", LogCategory::Simulation);
            for (std::size_t offset = 0; offset < boids.size(); ++offset) {
                index.ids.push_back(boids[offset].id);
                index.entities.push_back(entities[offset]);
            }
            world.modified<AuthoritativeReplicationIndex>();
        }
        report.spawned_count = boids.size();
        SIMNET_TRACE_PLOT("server.authoritative_index_size", static_cast<double>(index.ids.size()));
        return report;
    }

    bool delete_authoritative_boid(flecs::world& world, EntityNetId id)
    {
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index)) {
            return false;
        }
        auto const position = find_index(index, id);
        if (position == index.ids.end() || *position != id) {
            return false;
        }
        auto const offset = static_cast<std::size_t>(position - index.ids.begin());
        auto const entity = index.entities[offset];
        if (!ecs_is_alive(world.c_ptr(), entity)) {
            return false;
        }
        ecs_delete(world.c_ptr(), entity);
        index.ids.erase(position);
        index.entities.erase(index.entities.begin() + static_cast<std::ptrdiff_t>(offset));
        world.modified<AuthoritativeReplicationIndex>();
        return true;
    }

    std::size_t authoritative_boid_count(flecs::world const& world) noexcept
    {
        auto const& index = world.get<AuthoritativeReplicationIndex>();
        return valid_index(index) ? index.ids.size() : 0U;
    }

    ServerSnapshotExtractionReport extract_world_snapshot(
        flecs::world const& world,
        Tick tick,
        WorldSnapshot& out_snapshot
    )
    {
        auto report = ServerSnapshotExtractionReport { .tick = tick };
        auto gathered = std::vector<BoidState> {};

        world.each(
            [&](flecs::entity entity,
                NetIdentity const& identity,
                Position const& position,
                Heading const& heading,
                Hue const& hue) {
                if (!entity.has<BoidTag>()) {
                    return;
                }
                gathered.push_back({
                    .id = identity.id,
                    .position = position.value,
                    .heading = heading.value,
                    .hue = hue.value,
                });
            }
        );

        std::sort(gathered.begin(), gathered.end(), [](BoidState const& left, BoidState const& right) {
            return left.id < right.id;
        });

        auto snapshot = WorldSnapshot {};
        snapshot.tick = tick;
        snapshot.reserve(gathered.size());
        for (auto const& boid : gathered) {
            snapshot.ids.push_back(boid.id);
            snapshot.positions.push_back(boid.position);
            snapshot.headings.push_back(boid.heading);
            snapshot.hues.push_back(boid.hue);
        }

        auto const validation = validate_world_snapshot(snapshot);
        if (!validation.valid) {
            reset_failed_snapshot(out_snapshot, tick);
            report.valid = false;
            report.error = validation.message;
            return report;
        }

        out_snapshot = std::move(snapshot);
        report.entity_count = static_cast<std::uint32_t>(out_snapshot.size());
        return report;
    }
}
