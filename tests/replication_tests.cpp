#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <flecs.h>
#include <optional>
#include <utility>
#include <vector>

import simnet.core;
import simnet.compression;
import simnet.app_protocol;
import simnet.app_snapshot_delivery;
import simnet.game_client;
import simnet.game_server;
import simnet.game_shared;
import simnet.pipeline;
import simnet.snapshot;
import simnet.transport;

namespace
{
    constexpr auto boid_count = std::uint32_t{10};

    [[nodiscard]] simnet::EntityState
    test_boid(simnet::EntityNetId id, std::uint32_t index, simnet::Tick tick)
    {
        auto const base = static_cast<float>(index);
        return {
            .id = id,
            .classification = simnet::boid_entity_classification,
            .position = {base * 2.0F + static_cast<float>(tick) * 0.25F, base * 0.5F, 0.0F},
            .heading = {1.0F, 0.0F, 0.0F},
            .hue = static_cast<std::uint8_t>((index * 23U) & 0xFFU),
        };
    }

    void populate_world(flecs::world& world, simnet::Tick tick)
    {
        for (std::uint32_t index = 0; index < boid_count; ++index)
        {
            simnet::EntityNetId const id = index + 1U;
            static_cast<void>(simnet::upsert_authoritative_boid(world, test_boid(id, index, tick)));
        }
        if (tick == 2)
        {
            static_cast<void>(simnet::delete_authoritative_boid(world, boid_count));
        }
    }

    void record_received(simnet::app::SnapshotAck& ack, simnet::SequenceId sequence)
    {
        auto const previous = ack.newest_received_snapshot;
        if (previous == 0)
        {
            ack.newest_received_snapshot = sequence;
            return;
        }

        auto const shift = sequence - previous;
        auto const shifted_history = shift >= 32U ? 0U : ack.received_mask << shift;
        ack.received_mask = shifted_history | (1U << (shift - 1U));
        ack.newest_received_snapshot = sequence;
    }

    [[nodiscard]] simnet::WorldSnapshot single_boid_snapshot(simnet::Tick tick)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.ids.push_back(1);
        snapshot.classifications.push_back(simnet::boid_entity_classification);
        snapshot.positions.push_back({static_cast<float>(tick), 0.0F, 0.0F});
        snapshot.headings.push_back({1.0F, 0.0F, 0.0F});
        snapshot.hues.push_back(0);
        return snapshot;
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

    void
    check_client_world_matches_snapshot(flecs::world& world, simnet::WorldSnapshot const& snapshot)
    {
        auto const facts = client_entity_facts(world);
        REQUIRE(facts.size() == snapshot.size());
        for (auto entity_index = std::size_t{}; entity_index < facts.size(); ++entity_index)
        {
            auto const expected_kind =
                simnet::entity_kind_from_classification(snapshot.classifications[entity_index]);
            REQUIRE(expected_kind.has_value());
            CHECK(facts[entity_index].id == snapshot.ids[entity_index]);
            CHECK(facts[entity_index].kind == *expected_kind);
            CHECK(facts[entity_index].position.x == snapshot.positions[entity_index].x);
            CHECK(facts[entity_index].position.y == snapshot.positions[entity_index].y);
            CHECK(facts[entity_index].position.z == snapshot.positions[entity_index].z);
            CHECK(facts[entity_index].heading.x == snapshot.headings[entity_index].x);
            CHECK(facts[entity_index].heading.y == snapshot.headings[entity_index].y);
            CHECK(facts[entity_index].heading.z == snapshot.headings[entity_index].z);
            CHECK(facts[entity_index].hue == snapshot.hues[entity_index]);
        }
    }
}

TEST_CASE("game entity classification mapping is exact", "[replication][classification]")
{
    CHECK(simnet::boid_entity_classification == simnet::EntityClassification{1U});
    CHECK(simnet::player_entity_classification == simnet::EntityClassification{2U});
    CHECK(
        simnet::classification_from_entity_kind(simnet::EntityKind::Boid) ==
        simnet::boid_entity_classification
    );
    CHECK(
        simnet::classification_from_entity_kind(simnet::EntityKind::Player) ==
        simnet::player_entity_classification
    );
    CHECK(
        simnet::entity_kind_from_classification(simnet::boid_entity_classification) ==
        simnet::EntityKind::Boid
    );
    CHECK(
        simnet::entity_kind_from_classification(simnet::player_entity_classification) ==
        simnet::EntityKind::Player
    );
    CHECK_FALSE(
        simnet::entity_kind_from_classification(simnet::EntityClassification{247U}).has_value()
    );
}

TEST_CASE(
    "shared component registration is canonical across Flecs worlds",
    "[replication][registration]"
)
{
    auto first_world = flecs::world{};
    auto second_world = flecs::world{};
    simnet::register_game_components(first_world);
    simnet::register_game_components(second_world);

    auto constexpr component_names = std::array{
        "simnet::EntityKindComponent",
        "simnet::NetIdentity",
        "simnet::Position",
        "simnet::Heading",
        "simnet::Hue",
    };
    auto const first_ids = std::array{
        first_world.id<simnet::EntityKindComponent>(),
        first_world.id<simnet::NetIdentity>(),
        first_world.id<simnet::Position>(),
        first_world.id<simnet::Heading>(),
        first_world.id<simnet::Hue>(),
    };
    auto const second_ids = std::array{
        second_world.id<simnet::EntityKindComponent>(),
        second_world.id<simnet::NetIdentity>(),
        second_world.id<simnet::Position>(),
        second_world.id<simnet::Heading>(),
        second_world.id<simnet::Hue>(),
    };

    for (std::size_t index = 0; index < component_names.size(); ++index)
    {
        auto const first_component = first_world.lookup(component_names[index]);
        auto const second_component = second_world.lookup(component_names[index]);
        REQUIRE(first_component.is_alive());
        REQUIRE(second_component.is_alive());
        CHECK(first_component.id() == first_ids[index]);
        CHECK(second_component.id() == second_ids[index]);
        CHECK(first_component.id() == second_component.id());
    }
    for (std::size_t first = 0; first < first_ids.size(); ++first)
    {
        for (std::size_t second = first + 1; second < first_ids.size(); ++second)
        {
            CHECK(first_ids[first] != first_ids[second]);
            CHECK(second_ids[first] != second_ids[second]);
        }
    }
    CHECK(first_world.lookup("simnet::EntityKind").id() == 0U);
    CHECK(second_world.lookup("simnet::EntityKind").id() == 0U);
}

TEST_CASE("five-tick replication contract remains intact", "[replication]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.send_interval.interval_ticks = 2;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(400.0F);

    auto server_game = simnet::ServerGameRuntime{simnet::BoidSimulationSettings{}};
    auto server_world = flecs::world{};
    auto client_world = flecs::world{};
    simnet::register_server_game(server_world, server_game);
    simnet::register_client_game(client_world);

    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto acknowledged_snapshot = simnet::WorldSnapshot{};
    auto acknowledged_sequence = simnet::SequenceId{};
    auto ack = simnet::app::SnapshotAck{};
    auto emitted_index = std::size_t{};
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto compressed_bytes = std::vector<simnet::Byte>{};
    auto decompressed_bytes = std::vector<simnet::Byte>{};

    constexpr auto expected_sequences = std::array<simnet::SequenceId, 3>{1, 2, 3};
    constexpr auto expected_baselines = std::array<simnet::SequenceId, 3>{0, 1, 2};
    constexpr auto expected_kinds = std::array{
        simnet::SnapshotKind::FullReplace,
        simnet::SnapshotKind::Patch,
        simnet::SnapshotKind::Patch,
    };

    for (simnet::Tick tick = 0; tick < 5; ++tick)
    {
        populate_world(server_world, tick);

        auto snapshot = simnet::WorldSnapshot{};
        auto const extraction = simnet::extract_world_snapshot(server_world, tick, snapshot);
        REQUIRE(extraction.valid);

        auto const encoded = simnet::encode_snapshot_unchecked(
            pipeline,
            encode_state,
            encode_scratch,
            {
                .snapshot = &snapshot,
                .baseline_snapshot = acknowledged_sequence != 0 ? &acknowledged_snapshot : nullptr,
                .baseline_sequence = acknowledged_sequence,
            }
        );
        if (encoded.kind == simnet::EncodeResultKind::Skipped)
        {
            continue;
        }

        REQUIRE(emitted_index < expected_sequences.size());
        CHECK(encoded.update.sequence == expected_sequences[emitted_index]);
        CHECK(encoded.report.baseline_sequence == expected_baselines[emitted_index]);
        CHECK(encoded.report.snapshot_kind == expected_kinds[emitted_index]);

        auto const compression = simnet::compress_bytes(
            compressor,
            encoded.update.bytes,
            1,
            {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
            simnet::CompressionEnvelopePolicy::Always,
            compressed_bytes
        );
        REQUIRE(compression.valid);
        auto const decompression = simnet::decompress_bytes(
            decompressor,
            compressed_bytes,
            {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
            decompressed_bytes
        );
        REQUIRE(decompression.valid);
        REQUIRE(decompressed_bytes == encoded.update.bytes);

        auto const decoded = simnet::decode_update(
            pipeline,
            decode_state,
            {
                .bytes = decompressed_bytes,
            }
        );
        REQUIRE(decoded.report.valid);

        auto const applied =
            simnet::apply_client_snapshot_patch_unchecked(client_world, decoded.update);
        REQUIRE(applied.valid);

        record_received(ack, decoded.report.sequence);
        ack.newest_applied_snapshot = decoded.report.sequence;
        acknowledged_snapshot = encoded.resulting_snapshot;
        acknowledged_sequence = decoded.report.sequence;
        ++emitted_index;
    }

    REQUIRE(emitted_index == 3);
    CHECK(ack.newest_received_snapshot == 3);
    CHECK(ack.received_mask == 3);
    CHECK(ack.newest_applied_snapshot == 3);

    CHECK(acknowledged_snapshot.tick == 4U);
    CHECK(acknowledged_snapshot.size() == boid_count);
    check_client_world_matches_snapshot(client_world, acknowledged_snapshot);
}

TEST_CASE(
    "authoritative classifications survive extraction reconstruction and Client application",
    "[replication][classification][player]"
)
{
    auto server_game = simnet::ServerGameRuntime{simnet::BoidSimulationSettings{}};
    auto server_world = flecs::world{};
    simnet::register_server_game(server_world, server_game);
    REQUIRE(simnet::upsert_authoritative_boid(server_world, test_boid(1U, 0U, 7U)).is_alive());
    auto const first_player = simnet::spawn_authoritative_player(server_world);
    auto const second_player = simnet::spawn_authoritative_player(server_world);
    REQUIRE(first_player == 2U);
    REQUIRE(second_player == 3U);

    auto authoritative = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(server_world, 7U, authoritative).valid);
    CHECK(authoritative.ids == std::vector<simnet::EntityNetId>{1U, 2U, 3U});
    CHECK(
        authoritative.classifications == std::vector<simnet::EntityClassification>{
                                             simnet::boid_entity_classification,
                                             simnet::player_entity_classification,
                                             simnet::player_entity_classification,
                                         }
    );

    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques = simnet::PipelineTechniqueFlags::Delta;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const encoded = simnet::encode_snapshot_unchecked(
        pipeline,
        encode_state,
        encode_scratch,
        {.snapshot = &authoritative}
    );
    auto const decoded =
        simnet::decode_update(pipeline, decode_state, {.bytes = encoded.update.bytes});
    REQUIRE(decoded.report.valid);

    auto delivery = simnet::app::SnapshotDeliveryState{};
    auto retained_initial = encoded.resulting_snapshot;
    auto const initial_retention = simnet::app::plan_snapshot_retention(delivery, retained_initial);
    REQUIRE(initial_retention.valid);
    simnet::app::commit_submitted_snapshot(
        delivery,
        encoded.update.sequence,
        std::move(retained_initial),
        encoded.report.snapshot_kind,
        simnet::Nanoseconds{1U},
        initial_retention
    );
    REQUIRE(
        simnet::app::promote_snapshot_ack(
            delivery,
            encoded.update.sequence,
            simnet::Nanoseconds{2U}
        ) == simnet::app::AckPromotionOutcome::Promoted
    );

    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(nullptr, decoded.update, reconstructed).valid
    );
    CHECK(reconstructed.classifications == authoritative.classifications);

    auto client_world = flecs::world{};
    simnet::register_client_game(client_world);
    auto const applied =
        simnet::apply_client_snapshot_patch_unchecked(client_world, decoded.update);
    REQUIRE(applied.valid);
    check_client_world_matches_snapshot(client_world, reconstructed);
    auto initial_client_facts = client_entity_facts(client_world);
    REQUIRE(initial_client_facts.size() == 3U);
    CHECK(initial_client_facts[0].kind == simnet::EntityKind::Boid);
    CHECK(initial_client_facts[1].kind == simnet::EntityKind::Player);
    CHECK(initial_client_facts[2].kind == simnet::EntityKind::Player);

    REQUIRE(simnet::delete_authoritative_player(server_world, first_player));
    auto after_disconnect = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(server_world, 8U, after_disconnect).valid);
    CHECK(std::ranges::find(after_disconnect.ids, first_player) == after_disconnect.ids.end());
    CHECK(std::ranges::find(after_disconnect.ids, 1U) != after_disconnect.ids.end());
    CHECK(std::ranges::find(after_disconnect.ids, second_player) != after_disconnect.ids.end());

    REQUIRE(delivery.acknowledged.has_value());
    auto const deletion = simnet::encode_snapshot_unchecked(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &after_disconnect,
            .baseline_snapshot = &delivery.acknowledged->snapshot,
            .baseline_sequence = delivery.latest_acknowledged_sequence,
        }
    );
    REQUIRE(deletion.kind == simnet::EncodeResultKind::Update);
    CHECK(deletion.update.sequence == 2U);
    CHECK(deletion.report.baseline_sequence == 1U);
    CHECK(deletion.report.snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(deletion.report.delete_count == 1U);

    auto const decoded_deletion =
        simnet::decode_update(pipeline, decode_state, {.bytes = deletion.update.bytes});
    REQUIRE(decoded_deletion.report.valid);
    CHECK(decoded_deletion.update.deletes == std::vector<simnet::EntityNetId>{first_player});
    auto observer_canonical = simnet::WorldSnapshot{};
    REQUIRE(
        simnet::reconstruct_world_snapshot_unchecked(
            &delivery.acknowledged->snapshot,
            decoded_deletion.update,
            observer_canonical
        )
            .valid
    );
    CHECK(std::ranges::find(observer_canonical.ids, first_player) == observer_canonical.ids.end());
    CHECK(std::ranges::find(observer_canonical.ids, 1U) != observer_canonical.ids.end());
    CHECK(std::ranges::find(observer_canonical.ids, second_player) != observer_canonical.ids.end());
    CHECK(observer_canonical.ids == deletion.resulting_snapshot.ids);

    auto const applied_deletion =
        simnet::apply_client_snapshot_patch_unchecked(client_world, decoded_deletion.update);
    REQUIRE(applied_deletion.valid);
    check_client_world_matches_snapshot(client_world, observer_canonical);
    auto final_client_facts = client_entity_facts(client_world);
    REQUIRE(final_client_facts.size() == 2U);
    CHECK(final_client_facts[0].id == 1U);
    CHECK(final_client_facts[0].kind == simnet::EntityKind::Boid);
    CHECK(final_client_facts[1].id == second_player);
    CHECK(final_client_facts[1].kind == simnet::EntityKind::Player);

    auto retained_deletion = deletion.resulting_snapshot;
    auto const deletion_retention =
        simnet::app::plan_snapshot_retention(delivery, retained_deletion);
    REQUIRE(deletion_retention.valid);
    simnet::app::commit_submitted_snapshot(
        delivery,
        deletion.update.sequence,
        std::move(retained_deletion),
        deletion.report.snapshot_kind,
        simnet::Nanoseconds{3U},
        deletion_retention
    );
    REQUIRE(delivery.submitted.size() == 1U);
    CHECK(delivery.submitted.front().sequence == 2U);
    CHECK(delivery.submitted.front().snapshot.ids == observer_canonical.ids);
}

TEST_CASE("Patch reconstruction removes requested entities", "[replication][snapshot]")
{
    auto baseline = simnet::WorldSnapshot{};
    baseline.tick = 1U;
    for (std::uint32_t index = 0; index < 3U; ++index)
    {
        auto const boid = test_boid(index + 1U, index, baseline.tick);
        baseline.ids.push_back(boid.id);
        baseline.classifications.push_back(boid.classification);
        baseline.positions.push_back(boid.position);
        baseline.headings.push_back(boid.heading);
        baseline.hues.push_back(boid.hue);
    }

    auto const patch = simnet::SnapshotUpdate{
        .tick = 2U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {},
        .deletes = {2U},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(patch).valid);

    auto reconstructed = simnet::WorldSnapshot{};
    REQUIRE(simnet::reconstruct_world_snapshot(&baseline, patch, reconstructed).valid);
    CHECK(reconstructed.tick == patch.tick);
    CHECK(reconstructed.ids == std::vector<simnet::EntityNetId>{1U, 3U});
    CHECK(
        reconstructed.classifications ==
        std::vector<simnet::EntityClassification>(2U, simnet::boid_entity_classification)
    );
    REQUIRE(reconstructed.positions.size() == 2U);
    CHECK(reconstructed.positions[0].x == baseline.positions[0].x);
    CHECK(reconstructed.positions[0].y == baseline.positions[0].y);
    CHECK(reconstructed.positions[0].z == baseline.positions[0].z);
    CHECK(reconstructed.positions[1].x == baseline.positions[2].x);
    CHECK(reconstructed.positions[1].y == baseline.positions[2].y);
    CHECK(reconstructed.positions[1].z == baseline.positions[2].z);
    REQUIRE(reconstructed.headings.size() == 2U);
    CHECK(reconstructed.headings[0].x == baseline.headings[0].x);
    CHECK(reconstructed.headings[0].y == baseline.headings[0].y);
    CHECK(reconstructed.headings[0].z == baseline.headings[0].z);
    CHECK(reconstructed.headings[1].x == baseline.headings[2].x);
    CHECK(reconstructed.headings[1].y == baseline.headings[2].y);
    CHECK(reconstructed.headings[1].z == baseline.headings[2].z);
    REQUIRE(reconstructed.hues.size() == 2U);
    CHECK(reconstructed.hues[0] == baseline.hues[0]);
    CHECK(reconstructed.hues[1] == baseline.hues[2]);
}

TEST_CASE(
    "Client patch application accepts equal ticks and rejects stale ticks transactionally",
    "[replication][snapshot][validation]"
)
{
    auto world = flecs::world{};
    simnet::register_client_game(world);
    auto initial = simnet::SnapshotUpdate{
        .tick = 5U,
        .kind = simnet::SnapshotKind::FullReplace,
        .upserts = {test_boid(1U, 0U, 5U)},
        .deletes = {},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(initial).valid);
    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(world, initial).valid);

    auto canonical = simnet::WorldSnapshot{};
    REQUIRE(simnet::reconstruct_world_snapshot_unchecked(nullptr, initial, canonical).valid);
    check_client_world_matches_snapshot(world, canonical);

    auto const equal_tick = simnet::SnapshotUpdate{
        .tick = 5U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {},
        .deletes = {},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(equal_tick).valid);
    auto const equal_tick_report = simnet::apply_client_snapshot_patch_unchecked(world, equal_tick);
    CHECK(equal_tick_report.valid);
    CHECK(equal_tick_report.tick == 5U);
    check_client_world_matches_snapshot(world, canonical);

    auto stale = simnet::SnapshotUpdate{
        .tick = 4U,
        .kind = simnet::SnapshotKind::Patch,
        .upserts = {test_boid(1U, 2U, 4U)},
        .deletes = {},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(stale).valid);
    auto const stale_report = simnet::apply_client_snapshot_patch_unchecked(world, stale);
    CHECK_FALSE(stale_report.valid);
    CHECK(stale_report.error == "stale patch tick");
    CHECK(stale_report.previous_entities == 1U);
    CHECK(stale_report.final_entities == 1U);
    check_client_world_matches_snapshot(world, canonical);
}

TEST_CASE(
    "Client rejects unsupported classifications before any Flecs mutation",
    "[replication][classification][validation]"
)
{
    auto world = flecs::world{};
    simnet::register_client_game(world);
    auto boid = test_boid(1U, 0U, 5U);
    auto player = test_boid(2U, 1U, 5U);
    player.classification = simnet::player_entity_classification;
    auto const initial = simnet::SnapshotUpdate{
        .tick = 5U,
        .kind = simnet::SnapshotKind::FullReplace,
        .upserts = {boid, player},
        .deletes = {},
    };
    REQUIRE(simnet::validate_client_snapshot_patch(initial).valid);
    REQUIRE(simnet::apply_client_snapshot_patch_unchecked(world, initial).valid);

    auto canonical = simnet::WorldSnapshot{};
    REQUIRE(simnet::reconstruct_world_snapshot_unchecked(nullptr, initial, canonical).valid);
    check_client_world_matches_snapshot(world, canonical);
    auto before_entities = std::vector<std::pair<simnet::EntityNetId, flecs::entity_t>>{};
    auto identity_query = world.query_builder<const simnet::NetIdentity>().build();
    identity_query.each(
        [&](flecs::iter& iterator, std::size_t row, simnet::NetIdentity const& identity)
        {
            before_entities.push_back({identity.id, iterator.entity(row).id()});
        }
    );
    std::ranges::sort(before_entities);

    auto changed = test_boid(1U, 9U, 6U);
    auto unsupported = test_boid(3U, 3U, 6U);
    unsupported.classification = simnet::EntityClassification{247U};
    auto const rejected = simnet::apply_client_snapshot_patch_unchecked(
        world,
        {
            .tick = 6U,
            .kind = simnet::SnapshotKind::Patch,
            .upserts = {changed, unsupported},
            .deletes = {2U},
        }
    );
    CHECK_FALSE(rejected.valid);
    CHECK(rejected.error == "unsupported entity classification 247");
    CHECK(rejected.tick == 6U);
    CHECK(rejected.kind == simnet::SnapshotKind::Patch);
    CHECK(rejected.previous_entities == 2U);
    CHECK(rejected.final_entities == 2U);
    CHECK(rejected.upsert_count == 2U);
    CHECK(rejected.delete_count == 1U);
    check_client_world_matches_snapshot(world, canonical);

    auto after_entities = std::vector<std::pair<simnet::EntityNetId, flecs::entity_t>>{};
    identity_query.each(
        [&](flecs::iter& iterator, std::size_t row, simnet::NetIdentity const& identity)
        {
            after_entities.push_back({identity.id, iterator.entity(row).id()});
        }
    );
    std::ranges::sort(after_entities);
    CHECK(after_entities == before_entities);
    auto const facts = client_entity_facts(world);
    REQUIRE(facts.size() == 2U);
    CHECK(facts[0].kind == simnet::EntityKind::Boid);
    CHECK(facts[1].kind == simnet::EntityKind::Player);
}

TEST_CASE("authoritative boid mutations use a private indexed lifecycle", "[replication]")
{
    auto game = simnet::ServerGameRuntime{simnet::BoidSimulationSettings{}};
    auto world = flecs::world{};
    simnet::register_server_game(world, game);

    auto boids = std::vector<simnet::EntityState>{};
    boids.push_back(test_boid(1, 0, 0));
    boids.push_back(test_boid(2, 1, 0));
    boids.push_back(test_boid(3, 2, 0));
    auto const appended = simnet::append_authoritative_boids(world, boids);
    REQUIRE(appended.success());
    CHECK(appended.spawned_count == 3U);
    CHECK(simnet::authoritative_boid_count(world) == 3U);
    auto kind_query =
        world.query_builder<const simnet::EntityKindComponent, const simnet::NetIdentity>().build();
    auto kind_count = std::size_t{};
    kind_query.each(
        [&](simnet::EntityKindComponent const& kind, simnet::NetIdentity const&)
        {
            CHECK(kind.value == simnet::EntityKind::Boid);
            ++kind_count;
        }
    );
    CHECK(kind_count == 3U);

    auto updated = test_boid(2, 9, 4);
    updated.hue = 201U;
    REQUIRE(simnet::upsert_authoritative_boid(world, updated).is_alive());
    CHECK(simnet::authoritative_boid_count(world) == 3U);
    REQUIRE(simnet::delete_authoritative_boid(world, 1));
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto snapshot = simnet::WorldSnapshot{};
    auto const extracted = simnet::extract_world_snapshot(world, 4, snapshot);
    REQUIRE(extracted.valid);
    CHECK(snapshot.ids == std::vector<simnet::EntityNetId>{2, 3});
    CHECK(snapshot.hues[0] == 201U);
    auto const ids_capacity = snapshot.ids.capacity();

    auto updated_again = test_boid(3, 12, 5);
    updated_again.hue = 99U;
    REQUIRE(simnet::upsert_authoritative_boid(world, updated_again).is_alive());
    auto const extracted_again = simnet::extract_world_snapshot(world, 5, snapshot);
    REQUIRE(extracted_again.valid);
    CHECK(snapshot.tick == 5U);
    CHECK(snapshot.ids == std::vector<simnet::EntityNetId>{2, 3});
    CHECK(snapshot.positions[1].x == updated_again.position.x);
    CHECK(snapshot.positions[1].y == updated_again.position.y);
    CHECK(snapshot.positions[1].z == updated_again.position.z);
    CHECK(snapshot.hues[1] == 99U);
    CHECK(snapshot.ids.capacity() >= ids_capacity);

    auto invalid = std::vector<simnet::EntityState>{
        test_boid(4, 3, 4),
        test_boid(4, 4, 4),
    };
    auto const rejected = simnet::append_authoritative_boids(world, invalid);
    CHECK_FALSE(rejected.success());
    CHECK(rejected.error == simnet::AuthoritativeSpawnError::NonAscendingIds);
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto overlapping = std::vector<simnet::EntityState>{test_boid(3, 3, 4)};
    auto const overlap_rejected = simnet::append_authoritative_boids(world, overlapping);
    CHECK_FALSE(overlap_rejected.success());
    CHECK(overlap_rejected.error == simnet::AuthoritativeSpawnError::ExistingIdOverlap);
    CHECK(overlap_rejected.failing_index == std::optional<std::size_t>{0U});
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    auto malformed = std::vector<simnet::EntityState>{test_boid(4, 3, 4)};
    malformed.front().heading = {};
    auto const malformed_rejected = simnet::append_authoritative_boids(world, malformed);
    CHECK_FALSE(malformed_rejected.success());
    CHECK(malformed_rejected.error == simnet::AuthoritativeSpawnError::InvalidBoidState);
    CHECK(malformed_rejected.failing_index == std::optional<std::size_t>{0U});
    CHECK(simnet::authoritative_boid_count(world) == 2U);

    REQUIRE(simnet::prepare_server_game_runtime(world, game));
    REQUIRE(world.progress(1.0F / 60.0F));
    REQUIRE(game.last_step_report().valid);
    auto remapped_snapshot = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(world, 6, remapped_snapshot).valid);
    CHECK(remapped_snapshot.ids == std::vector<simnet::EntityNetId>{2, 3});
}

TEST_CASE(
    "authoritative extraction validates query ownership before snapshot commit",
    "[replication]"
)
{
    auto game = simnet::ServerGameRuntime{simnet::BoidSimulationSettings{}};
    auto world = flecs::world{};
    simnet::register_server_game(world, game);

    auto initial = std::vector<simnet::EntityState>{
        test_boid(2, 1, 0),
        test_boid(3, 2, 0),
    };
    REQUIRE(simnet::append_authoritative_boids(world, initial).success());

    auto snapshot = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(world, 1, snapshot).valid);
    auto const ids_capacity = snapshot.ids.capacity();

    auto const inserted = simnet::upsert_authoritative_boid(world, test_boid(1, 0, 2));
    REQUIRE(inserted.is_alive());
    auto const sorted = simnet::extract_world_snapshot(world, 2, snapshot);
    REQUIRE(sorted.valid);
    CHECK(snapshot.ids == std::vector<simnet::EntityNetId>{1, 2, 3});
    CHECK(snapshot.ids.capacity() >= ids_capacity);

    inserted.remove<simnet::Heading>();
    auto const invalid = simnet::extract_world_snapshot(world, 3, snapshot);
    CHECK_FALSE(invalid.valid);
    CHECK(snapshot.tick == 3U);
    CHECK(snapshot.empty());
}

TEST_CASE("evicted acknowledged snapshot falls back to FullReplace", "[replication]")
{
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    auto encode_state = simnet::ClientReplicationState{};
    auto decode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const first_snapshot = single_boid_snapshot(1);

    auto const first_update = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {.snapshot = &first_snapshot}
    );
    REQUIRE(first_update.report.snapshot_kind == simnet::SnapshotKind::FullReplace);

    auto retained_snapshots = std::deque<std::pair<simnet::SequenceId, simnet::WorldSnapshot>>{};
    retained_snapshots.push_back({first_update.update.sequence, first_snapshot});
    auto acknowledged_baseline = std::optional{first_update.update.sequence};
    retained_snapshots.clear();

    auto const retained = std::find_if(
        retained_snapshots.begin(),
        retained_snapshots.end(),
        [sequence = *acknowledged_baseline](auto const& entry)
        {
            return entry.first == sequence;
        }
    );
    if (retained == retained_snapshots.end())
    {
        acknowledged_baseline.reset();
    }

    auto const next_snapshot = single_boid_snapshot(2);
    auto const next_update = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &next_snapshot,
            .baseline_snapshot = nullptr,
            .baseline_sequence = 0,
        }
    );
    CHECK_FALSE(acknowledged_baseline.has_value());
    CHECK(next_update.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
    CHECK(next_update.report.baseline_sequence == 0);

    auto const decoded = simnet::decode_update(
        pipeline,
        decode_state,
        {
            .bytes = next_update.update.bytes,
        }
    );
    CHECK(decoded.report.valid);
    CHECK(decoded.report.snapshot_kind == simnet::SnapshotKind::FullReplace);
}
