#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <vector>

import simnet.core;
import simnet.game_client;
import simnet.game_server;
import simnet.game_shared;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::EntityState
    test_boid(simnet::EntityNetId id, float x, simnet::Tick tick)
    {
        return {
            .id = id,
            .classification = simnet::boid_entity_classification,
            .position = {x + static_cast<float>(tick) * 0.25F, 0.0F, 0.0F},
            .heading = {1.0F, 0.0F, 0.0F},
            .hue = static_cast<std::uint8_t>(id * 23U),
        };
    }

    struct ClientEntityFacts
    {
        simnet::EntityNetId id{};
        simnet::EntityKind kind{simnet::EntityKind::Boid};
        simnet::Vec3f position{};
        simnet::Vec3f heading{};
        std::uint8_t hue{};
    };

    [[nodiscard]] std::vector<ClientEntityFacts> client_entity_facts(flecs::world& world)
    {
        auto facts = std::vector<ClientEntityFacts>{};
        auto query = world
                         .query_builder<
                             const simnet::NetIdentity,
                             const simnet::EntityKindComponent,
                             const simnet::Position,
                             const simnet::Heading,
                             const simnet::Hue>()
                         .build();

        query.each(
            [&](simnet::NetIdentity const& identity,
                simnet::EntityKindComponent const& kind,
                simnet::Position const& position,
                simnet::Heading const& heading,
                simnet::Hue const& hue)
            {
                facts.push_back({
                    .id = identity.id,
                    .kind = kind.value,
                    .position = position.value,
                    .heading = heading.value,
                    .hue = hue.value,
                });
            }
        );

        std::ranges::sort(facts, {}, &ClientEntityFacts::id);
        return facts;
    }

    void check_client_world_matches_snapshot(
        flecs::world& world,
        simnet::WorldSnapshot const& snapshot
    )
    {
        auto const facts = client_entity_facts(world);
        REQUIRE(facts.size() == snapshot.size());

        for (auto index = std::size_t{}; index < facts.size(); ++index)
        {
            auto const expected_kind =
                simnet::entity_kind_from_classification(snapshot.classifications[index]);
            REQUIRE(expected_kind.has_value());

            CHECK(facts[index].id == snapshot.ids[index]);
            CHECK(facts[index].kind == *expected_kind);
            CHECK(facts[index].position.x == snapshot.positions[index].x);
            CHECK(facts[index].position.y == snapshot.positions[index].y);
            CHECK(facts[index].position.z == snapshot.positions[index].z);
            CHECK(facts[index].heading.x == snapshot.headings[index].x);
            CHECK(facts[index].heading.y == snapshot.headings[index].y);
            CHECK(facts[index].heading.z == snapshot.headings[index].z);
            CHECK(facts[index].hue == snapshot.hues[index]);
        }
    }
}

TEST_CASE(
    "authoritative snapshots replicate to the Client through FullReplace and Delta Patch",
    "[replication]"
)
{
    auto server_game = simnet::ServerGameRuntime{simnet::BoidSimulationSettings{}};
    auto server_world = flecs::world{};
    simnet::register_server_game(server_world, server_game);

    auto const initial_boids = std::vector<simnet::EntityState>{
        test_boid(1U, 1.0F, 10U),
    };
    REQUIRE(simnet::append_authoritative_boids(server_world, initial_boids).success());

    auto const player_id = simnet::spawn_authoritative_player(server_world);
    REQUIRE(player_id == 2U);

    auto authoritative = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(server_world, 10U, authoritative).valid);
    CHECK(authoritative.ids == std::vector<simnet::EntityNetId>{1U, 2U});
    CHECK(
        authoritative.classifications ==
        std::vector<simnet::EntityClassification>{
            simnet::boid_entity_classification,
            simnet::player_entity_classification,
        }
    );

    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};

    auto const full =
        simnet::encode_snapshot(pipeline, encode_state, scratch, {.snapshot = &authoritative});
    REQUIRE(full.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    auto const decoded_full =
        simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes});
    REQUIRE(decoded_full.report.valid);

    auto client_world = flecs::world{};
    simnet::register_client_game(client_world);
    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(client_world, decoded_full.update).valid);
    check_client_world_matches_snapshot(client_world, full.resulting_snapshot);

    REQUIRE(simnet::delete_authoritative_player(server_world, player_id));
    auto const spawned = std::vector<simnet::EntityState>{
        test_boid(3U, 3.0F, 11U),
    };
    REQUIRE(simnet::append_authoritative_boids(server_world, spawned).success());

    auto next_authoritative = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(server_world, 11U, next_authoritative).valid);
    CHECK(next_authoritative.ids == std::vector<simnet::EntityNetId>{1U, 3U});

    auto const patch = simnet::encode_snapshot(
        pipeline,
        encode_state,
        scratch,
        {
            .snapshot = &next_authoritative,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
        }
    );
    REQUIRE(patch.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(patch.report.upsert_count == 1U);
    CHECK(patch.report.delete_count == 1U);

    auto const decoded_patch =
        simnet::decode_update(pipeline, decode_state, {.bytes = patch.update.bytes});
    REQUIRE(decoded_patch.report.valid);
    CHECK(decoded_patch.update.deletes == std::vector<simnet::EntityNetId>{player_id});

    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &full.resulting_snapshot,
            decoded_patch.update,
            reconstructed
        )
            .valid
    );
    CHECK(reconstructed.ids == std::vector<simnet::EntityNetId>{1U, 3U});
    CHECK(reconstructed.ids == patch.resulting_snapshot.ids);

    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(client_world, decoded_patch.update).valid);
    check_client_world_matches_snapshot(client_world, reconstructed);
}

TEST_CASE("Client replication rejects stale patches without rolling state backward", "[replication][stale]")
{
    auto world = flecs::world{};
    simnet::register_client_game(world);

    auto const initial = simnet::SnapshotUpdate{
        .tick = 5U,
        .kind = simnet::SnapshotKind::FullReplace,
        .upserts = {test_boid(1U, 0.0F, 5U)},
    };
    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(world, initial).valid);

    auto const newer = simnet::SnapshotUpdate{
        .tick = 6U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {test_boid(1U, 10.0F, 6U)},
    };
    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(world, newer).valid);

    auto initial_canonical = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(nullptr, initial, initial_canonical).valid
    );
    auto canonical = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(&initial_canonical, newer, canonical).valid
    );
    check_client_world_matches_snapshot(world, canonical);

    auto const stale = simnet::SnapshotUpdate{
        .tick = 5U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {test_boid(1U, -20.0F, 5U)},
    };
    auto const rejected = simnet::apply_client_snapshot_patch_unchecked(world, stale);

    CHECK_FALSE(rejected.valid);
    CHECK(rejected.error == "stale patch tick");
    check_client_world_matches_snapshot(world, canonical);
}