module;

#include <algorithm>
#include <cstdint>
#include <flecs.h>
#include <utility>
#include <vector>

module simnet.game_server;

import :snapshot;
import simnet.game_shared;
import simnet.snapshot;

namespace simnet
{
    namespace
    {
        [[nodiscard]] flecs::entity_t find_authoritative_boid_id(
            flecs::world& world,
            EntityNetId id
        )
        {
            auto found = flecs::entity_t {};
            world.each<NetIdentity>([&](flecs::entity entity, NetIdentity const& identity) {
                if (found == 0 && identity.id == id && entity.has<BoidTag>()) {
                    found = entity.id();
                }
            });
            return found;
        }

        void set_authoritative_boid_components(flecs::entity entity, BoidState const& boid)
        {
            entity.set<NetIdentity>({ .id = boid.id });
            entity.set<Position>({ .value = boid.position });
            entity.set<Heading>({ .value = boid.heading });
            entity.set<Hue>({ .value = boid.hue });
            entity.add<BoidTag>();
        }

        void reset_failed_snapshot(WorldSnapshot& snapshot, Tick tick)
        {
            snapshot.clear();
            snapshot.tick = tick;
        }
    }

    void register_server_game(flecs::world& world)
    {
        register_game_components(world);
    }

    flecs::entity upsert_authoritative_boid(flecs::world& world, BoidState const& boid)
    {
        auto const existing = find_authoritative_boid_id(world, boid.id);
        auto entity = existing == 0 ? world.entity() : flecs::entity { world, existing };
        set_authoritative_boid_components(entity, boid);
        return entity;
    }

    bool delete_authoritative_boid(flecs::world& world, EntityNetId id)
    {
        auto const existing = find_authoritative_boid_id(world, id);
        if (existing == 0) {
            return false;
        }

        ecs_delete(world.c_ptr(), existing);
        return true;
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
