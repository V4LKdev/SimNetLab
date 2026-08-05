#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import simnet.app_protocol;
import simnet.core;
import simnet.packetization;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::PacketizationSettings settings(bool enabled = true)
    {
        return {
            .enabled = enabled,
            .max_payload_bytes = 64U,
            .max_group_bytes = 256U,
            .max_chunks_per_group = 16U,
            .max_in_flight_groups = 4U,
            .max_incomplete_bytes = 512U,
            .reassembly_timeout = simnet::Nanoseconds{1'000'000},
        };
    }

    [[nodiscard]] std::vector<simnet::Byte> bytes(std::size_t count, std::uint8_t seed = 0U)
    {
        auto result = std::vector<simnet::Byte>(count);
        for (auto index = std::size_t{}; index < count; ++index) {
            result[index] = static_cast<simnet::Byte>(seed + index);
        }
        return result;
    }

    void write_u16(std::vector<simnet::Byte>& packet, std::size_t offset, std::uint16_t value)
    {
        packet[offset] = static_cast<simnet::Byte>(value >> 8U);
        packet[offset + 1U] = static_cast<simnet::Byte>(value);
    }

    void write_u32(std::vector<simnet::Byte>& packet, std::size_t offset, std::uint32_t value)
    {
        packet[offset] = static_cast<simnet::Byte>(value >> 24U);
        packet[offset + 1U] = static_cast<simnet::Byte>(value >> 16U);
        packet[offset + 2U] = static_cast<simnet::Byte>(value >> 8U);
        packet[offset + 3U] = static_cast<simnet::Byte>(value);
    }

    [[nodiscard]] simnet::WorldSnapshot area_of_interest_source_snapshot()
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = 12U;
        snapshot.reserve(30U);
        for (auto index = std::uint32_t{}; index < 30U; ++index) {
            snapshot.ids.push_back(index + 1U);
            snapshot.classifications.push_back(
                simnet::EntityClassification{static_cast<std::uint8_t>((index % 3U) + 1U)}
            );
            snapshot.positions.push_back(
                index == 0U ? simnet::Vec3f{}
                            : simnet::Vec3f{
                                  .z = index <= 20U ? static_cast<float>(index)
                                                    : -static_cast<float>(index - 20U),
                              }
            );
            snapshot.headings.push_back({.z = 1.0F});
            snapshot.hues.push_back(static_cast<std::uint8_t>(index));
        }
        return snapshot;
    }

    [[nodiscard]] bool
    same_snapshot(simnet::WorldSnapshot const& left, simnet::WorldSnapshot const& right)
    {
        auto const vectors_match = [](std::vector<simnet::Vec3f> const& first,
                                      std::vector<simnet::Vec3f> const& second) {
            return first.size() == second.size()
                && std::equal(
                       first.begin(),
                       first.end(),
                       second.begin(),
                       [](simnet::Vec3f const& a, simnet::Vec3f const& b) {
                           return a.x == b.x && a.y == b.y && a.z == b.z;
                       }
                );
        };
        return left.tick == right.tick && left.ids == right.ids
            && left.classifications == right.classifications
            && vectors_match(left.positions, right.positions)
            && vectors_match(left.headings, right.headings) && left.hues == right.hues;
    }

    [[nodiscard]] std::vector<std::vector<simnet::Byte>> packets(
        simnet::PacketizationSettings const& config,
        simnet::PacketGroupId group_id,
        std::vector<simnet::Byte> payload
    )
    {
        auto prepared = simnet::PreparedByteGroup{};
        auto const report
            = simnet::prepare_byte_group(config, group_id, std::move(payload), prepared);
        REQUIRE(report.outcome == simnet::GroupPreparationOutcome::Prepared);
        auto result = std::vector<std::vector<simnet::Byte>>{};
        auto scratch = std::vector<simnet::Byte>{};
        for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index) {
            auto const view = simnet::serialize_group_chunk(config, prepared, index, scratch);
            result.emplace_back(view.begin(), view.end());
        }
        return result;
    }

    [[nodiscard]] simnet::ReassemblyResult deliver(
        simnet::PacketizationSettings const& config,
        simnet::ReassemblyState& state,
        std::vector<std::vector<simnet::Byte>> const& group_packets,
        std::vector<std::size_t> const& order
    )
    {
        auto result = simnet::ReassemblyResult{};
        for (auto const index : order) {
            result = simnet::accept_group_packet(
                config,
                state,
                group_packets[index],
                simnet::Nanoseconds{100U}
            );
        }
        return result;
    }
}

TEST_CASE("disabled packetization preserves one bounded opaque payload", "[packetization]")
{
    auto const config = settings(false);
    auto source = bytes(40U);
    auto prepared = simnet::PreparedByteGroup{};
    auto const report = simnet::prepare_byte_group(config, 1U, source, prepared);
    REQUIRE(report.outcome == simnet::GroupPreparationOutcome::Prepared);
    CHECK(report.chunk_count == 1U);
    CHECK(report.total_header_bytes == 0U);

    auto scratch = std::vector<simnet::Byte>{};
    auto const packet = simnet::serialize_group_chunk(config, prepared, 0U, scratch);
    auto state = simnet::ReassemblyState{};
    auto completed = simnet::accept_group_packet(
        config,
        state,
        {packet.begin(), packet.end()},
        simnet::Nanoseconds{}
    );
    REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(completed.completed.bytes == source);

    auto oversized = simnet::PreparedByteGroup{};
    auto const rejected = simnet::prepare_byte_group(config, 2U, bytes(65U), oversized);
    CHECK(rejected.outcome == simnet::GroupPreparationOutcome::Rejected);
}

TEST_CASE("enabled packetization handles one and many chunks", "[packetization]")
{
    auto const config = settings();
    for (auto const count : {20U, 120U}) {
        auto const source = bytes(count);
        auto const group_packets = packets(config, count, source);
        auto order = std::vector<std::size_t>(group_packets.size());
        std::iota(order.begin(), order.end(), 0U);
        auto state = simnet::ReassemblyState{};
        auto completed = deliver(config, state, group_packets, order);
        REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
        CHECK(completed.completed.group_id == count);
        CHECK(completed.completed.bytes == source);
        CHECK(completed.completed.chunk_count == group_packets.size());
    }
}

TEST_CASE("packet header uses the fixed network-order schema", "[packetization][protocol]")
{
    auto const config = settings();
    auto prepared = simnet::PreparedByteGroup{};
    REQUIRE(
        simnet::prepare_byte_group(config, 0x01020304U, bytes(20U), prepared).outcome
        == simnet::GroupPreparationOutcome::Prepared
    );
    auto scratch = std::vector<simnet::Byte>{};
    auto const packet = simnet::serialize_group_chunk(config, prepared, 0U, scratch);
    REQUIRE(packet.size() == simnet::packet_header_bytes + 20U);
    CHECK(packet[0] == simnet::Byte{0x53U});
    CHECK(packet[1] == simnet::Byte{0x4EU});
    CHECK(packet[2] == simnet::Byte{0x50U});
    CHECK(packet[3] == simnet::Byte{0x4BU});
    CHECK(packet[4] == simnet::Byte{0U});
    CHECK(packet[5] == simnet::Byte{1U});
    CHECK(packet[6] == simnet::Byte{0U});
    CHECK(packet[7] == simnet::Byte{1U});
    CHECK(packet[8] == simnet::Byte{1U});
    CHECK(packet[9] == simnet::Byte{1U});
    CHECK(packet[10] == simnet::Byte{2U});
    CHECK(packet[11] == simnet::Byte{3U});
    CHECK(packet[12] == simnet::Byte{4U});
    CHECK(packet[13] == simnet::Byte{0U});
    CHECK(packet[14] == simnet::Byte{0U});
    CHECK(packet[15] == simnet::Byte{0U});
    CHECK(packet[16] == simnet::Byte{1U});
    CHECK(packet[17] == simnet::Byte{0U});
    CHECK(packet[18] == simnet::Byte{0U});
    CHECK(packet[19] == simnet::Byte{0U});
    CHECK(packet[20] == simnet::Byte{20U});
    CHECK(packet[21] == simnet::Byte{0U});
    CHECK(packet[22] == simnet::Byte{0U});
    CHECK(packet[23] == simnet::Byte{0U});
    CHECK(packet[24] == simnet::Byte{20U});
}

TEST_CASE("reassembly accepts reverse and shuffled chunk order", "[packetization]")
{
    auto const config = settings();
    auto const source = bytes(180U);
    auto const group_packets = packets(config, 7U, source);

    auto reverse = std::vector<std::size_t>(group_packets.size());
    std::iota(reverse.rbegin(), reverse.rend(), 0U);
    auto reverse_state = simnet::ReassemblyState{};
    auto reversed = deliver(config, reverse_state, group_packets, reverse);
    REQUIRE(reversed.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(reversed.completed.bytes == source);

    auto shuffled = std::vector<std::size_t>{2U, 0U, 4U, 1U, 3U};
    REQUIRE(shuffled.size() == group_packets.size());
    auto shuffled_state = simnet::ReassemblyState{};
    auto shuffled_result = deliver(config, shuffled_state, group_packets, shuffled);
    REQUIRE(shuffled_result.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(shuffled_result.completed.bytes == source);
}

TEST_CASE("duplicates are idempotent or invalidate only their group", "[packetization]")
{
    auto const config = settings();
    auto const group_packets = packets(config, 9U, bytes(100U));
    auto state = simnet::ReassemblyState{};
    auto first
        = simnet::accept_group_packet(config, state, group_packets[0], simnet::Nanoseconds{});
    REQUIRE(first.kind == simnet::ReassemblyResultKind::Incomplete);
    auto duplicate
        = simnet::accept_group_packet(config, state, group_packets[0], simnet::Nanoseconds{});
    CHECK(duplicate.kind == simnet::ReassemblyResultKind::Duplicate);
    CHECK(state.report.duplicate_chunks == 1U);

    auto conflicting = group_packets[0];
    conflicting.back() = static_cast<simnet::Byte>(conflicting.back() ^ simnet::Byte{1U});
    auto conflict = simnet::accept_group_packet(config, state, conflicting, simnet::Nanoseconds{});
    CHECK(conflict.kind == simnet::ReassemblyResultKind::Invalid);
    CHECK(state.incomplete.empty());

    auto order = std::vector<std::size_t>(group_packets.size());
    std::iota(order.begin(), order.end(), 0U);
    auto recovered = deliver(config, state, group_packets, order);
    CHECK(recovered.kind == simnet::ReassemblyResultKind::Complete);
}

TEST_CASE("missing chunks expire without blocking a valid retry", "[packetization]")
{
    auto const config = settings();
    auto const group_packets = packets(config, 11U, bytes(120U));
    for (auto const missing : {0U, 1U, 3U}) {
        auto state = simnet::ReassemblyState{};
        for (auto index = std::size_t{}; index < group_packets.size(); ++index) {
            if (index != missing) {
                auto const result = simnet::accept_group_packet(
                    config,
                    state,
                    group_packets[index],
                    simnet::Nanoseconds{}
                );
                CHECK(result.kind != simnet::ReassemblyResultKind::Complete);
            }
        }
        simnet::expire_incomplete_groups(config, state, simnet::Nanoseconds{1'000'000});
        CHECK(state.incomplete.empty());
        CHECK(state.latest_committed_group == 0U);

        auto order = std::vector<std::size_t>(group_packets.size());
        std::iota(order.begin(), order.end(), 0U);
        auto recovered = deliver(config, state, group_packets, order);
        CHECK(recovered.kind == simnet::ReassemblyResultKind::Complete);
    }
}

TEST_CASE("invalid packet headers do not poison later traffic", "[packetization]")
{
    auto const config = settings();
    auto const valid_packets = packets(config, 13U, bytes(80U));
    for (auto offset : {0U, 4U, 6U, 13U, 15U, 17U, 21U}) {
        auto invalid = valid_packets.front();
        invalid[offset] = simnet::Byte{0xFFU};
        auto state = simnet::ReassemblyState{};
        auto const rejected
            = simnet::accept_group_packet(config, state, invalid, simnet::Nanoseconds{});
        CHECK(rejected.kind == simnet::ReassemblyResultKind::Invalid);
    }

    auto state = simnet::ReassemblyState{};
    auto truncated = valid_packets.front();
    truncated.resize(simnet::packet_header_bytes - 1U);
    CHECK(
        simnet::accept_group_packet(config, state, truncated, simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Invalid
    );
    auto order = std::vector<std::size_t>(valid_packets.size());
    std::iota(order.begin(), order.end(), 0U);
    CHECK(
        deliver(config, state, valid_packets, order).kind == simnet::ReassemblyResultKind::Complete
    );
}

TEST_CASE("header bounds reject explicit malformed fields", "[packetization][malformed]")
{
    auto const config = settings();
    auto const valid_packets = packets(config, 14U, bytes(100U));
    auto reject = [&](std::vector<simnet::Byte> packet) {
        auto state = simnet::ReassemblyState{};
        CHECK(
            simnet::accept_group_packet(config, state, std::move(packet), simnet::Nanoseconds{})
                .kind
            == simnet::ReassemblyResultKind::Invalid
        );
        CHECK(state.incomplete.empty());
    };

    auto invalid = valid_packets.front();
    write_u32(invalid, 9U, 0U);
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u16(invalid, 15U, 0U);
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u16(invalid, 15U, static_cast<std::uint16_t>(config.max_chunks_per_group + 1U));
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u16(invalid, 13U, 3U);
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u32(invalid, 17U, 0U);
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u32(invalid, 17U, std::numeric_limits<std::uint32_t>::max());
    reject(std::move(invalid));
    invalid = valid_packets.front();
    write_u32(invalid, 21U, 1U);
    reject(std::move(invalid));
    invalid = valid_packets.front();
    invalid.resize(config.max_payload_bytes + 1U);
    reject(std::move(invalid));
}

TEST_CASE("conflicting metadata destroys only the affected assembly", "[packetization]")
{
    auto const config = settings();
    auto const first = packets(config, 15U, bytes(100U));
    auto const second = packets(config, 16U, bytes(100U));
    auto state = simnet::ReassemblyState{};
    REQUIRE(
        simnet::accept_group_packet(config, state, first[0], simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Incomplete
    );
    REQUIRE(
        simnet::accept_group_packet(config, state, second[0], simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Incomplete
    );
    auto conflict = first[1];
    write_u32(conflict, 17U, 101U);
    CHECK(
        simnet::accept_group_packet(config, state, conflict, simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Invalid
    );
    REQUIRE(state.incomplete.size() == 1U);
    CHECK(state.incomplete.front().group_id == 16U);
}

TEST_CASE("reassembly enforces in-flight and byte budgets without eviction", "[packetization]")
{
    SECTION("in-flight group limit")
    {
        auto config = settings();
        config.max_in_flight_groups = 1U;
        config.max_incomplete_bytes = 256U;
        auto const first_packets = packets(config, 20U, bytes(100U));
        auto const second_packets = packets(config, 21U, bytes(100U));
        auto state = simnet::ReassemblyState{};
        REQUIRE(
            simnet::accept_group_packet(config, state, first_packets[0], simnet::Nanoseconds{}).kind
            == simnet::ReassemblyResultKind::Incomplete
        );
        CHECK(
            simnet::accept_group_packet(config, state, second_packets[0], simnet::Nanoseconds{})
                .kind
            == simnet::ReassemblyResultKind::LimitExceeded
        );
        CHECK(state.incomplete.size() == 1U);
        CHECK(state.incomplete.front().group_id == 20U);
    }

    SECTION("declared incomplete byte budget")
    {
        auto config = settings();
        config.max_group_bytes = 128U;
        config.max_incomplete_bytes = 160U;
        auto const first_packets = packets(config, 22U, bytes(100U));
        auto const second_packets = packets(config, 23U, bytes(100U));
        auto state = simnet::ReassemblyState{};
        REQUIRE(
            simnet::accept_group_packet(config, state, first_packets[0], simnet::Nanoseconds{}).kind
            == simnet::ReassemblyResultKind::Incomplete
        );
        CHECK(state.report.retained_incomplete_bytes == 100U);
        CHECK(
            simnet::accept_group_packet(config, state, second_packets[0], simnet::Nanoseconds{})
                .kind
            == simnet::ReassemblyResultKind::LimitExceeded
        );
        CHECK(state.report.retained_incomplete_bytes == 100U);
        CHECK(state.incomplete.front().group_id == 22U);
    }
}

TEST_CASE("canonical commit rejects stale groups and clears older assemblies", "[packetization]")
{
    auto const config = settings();
    auto const older = packets(config, 30U, bytes(100U));
    auto state = simnet::ReassemblyState{};
    REQUIRE(
        simnet::accept_group_packet(config, state, older[0], simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Incomplete
    );
    simnet::commit_reassembled_group(state, 31U);
    CHECK(state.incomplete.empty());
    CHECK(
        simnet::accept_group_packet(config, state, older[0], simnet::Nanoseconds{}).kind
        == simnet::ReassemblyResultKind::Stale
    );
    simnet::clear_reassembly_state(state);
    CHECK(state.latest_committed_group == 0U);
    CHECK(state.report.received_chunks == 0U);
}

TEST_CASE("settings reject arithmetic and allocation bound attacks", "[packetization]")
{
    auto config = settings();
    config.max_payload_bytes = simnet::packet_header_bytes;
    CHECK_THROWS(simnet::validate_packetization_settings(config));
    config = settings();
    config.max_group_bytes = simnet::maximum_packetized_group_bytes + 1U;
    CHECK_THROWS(simnet::validate_packetization_settings(config));
    config = settings();
    config.max_chunks_per_group = simnet::maximum_chunks_per_group + 1U;
    CHECK_THROWS(simnet::validate_packetization_settings(config));
    config = settings();
    config.max_incomplete_bytes = config.max_group_bytes - 1U;
    CHECK_THROWS(simnet::validate_packetization_settings(config));

    config = settings();
    auto prepared = simnet::PreparedByteGroup{};
    REQUIRE(
        simnet::prepare_byte_group(config, 1U, bytes(40U), prepared).outcome
        == simnet::GroupPreparationOutcome::Prepared
    );
    ++prepared.chunk_count;
    auto scratch = std::vector<simnet::Byte>{};
    CHECK_THROWS(simnet::serialize_group_chunk(config, prepared, 0U, scratch));
}

TEST_CASE(
    "AOI pipeline groups commit decode and ACK state only after complete reassembly",
    "[packetization][pipeline][aoi][transaction]"
)
{
    auto const source_snapshot = area_of_interest_source_snapshot();
    auto const interest = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto candidates = std::vector<std::uint32_t>(source_snapshot.size());
    std::iota(candidates.begin(), candidates.end(), 0U);

    for (auto const mode : {simnet::AreaOfInterestMode::Radius, simnet::AreaOfInterestMode::Fov}) {
        auto pipeline = simnet::PipelineDefinition{};
        pipeline.area_of_interest = {
            .mode = mode,
            .radius = 25.0F,
            .fov_degrees = mode == simnet::AreaOfInterestMode::Fov ? 90.0F : 0.0F,
        };
        auto live_server_state = simnet::ClientReplicationState{};
        auto candidate_server_state = live_server_state;
        auto scratch = simnet::PipelineScratch{};
        auto encoded = simnet::encode_snapshot(
            pipeline,
            candidate_server_state,
            scratch,
            {
                .snapshot = &source_snapshot,
                .interest_source = &interest,
                .candidate_indices = candidates,
            }
        );
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        REQUIRE(candidate_server_state.next_sequence == 2U);
        REQUIRE(live_server_state.next_sequence == 1U);

        auto config = settings();
        config.max_group_bytes = 4096U;
        config.max_chunks_per_group = 128U;
        config.max_incomplete_bytes = 8192U;
        auto group_packets
            = packets(config, encoded.update.sequence, std::move(encoded.update.bytes));
        REQUIRE(group_packets.size() > 1U);

        auto reassembly = simnet::ReassemblyState{};
        auto live_decode_state = simnet::ClientReplicationState{};
        auto ack = simnet::app::SnapshotAck{};
        for (auto index = std::size_t{}; index + 1U < group_packets.size(); ++index) {
            CHECK(
                simnet::accept_group_packet(
                    config,
                    reassembly,
                    group_packets[index],
                    simnet::Nanoseconds{}
                )
                    .kind
                == simnet::ReassemblyResultKind::Incomplete
            );
            CHECK(live_decode_state.latest_remote_sequence == 0U);
            CHECK(ack.newest_received_snapshot == 0U);
        }
        auto completed = simnet::accept_group_packet(
            config,
            reassembly,
            group_packets.back(),
            simnet::Nanoseconds{}
        );
        REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);

        auto candidate_decode_state = live_decode_state;
        auto const decoded = simnet::decode_update(
            pipeline,
            candidate_decode_state,
            {.bytes = completed.completed.bytes}
        );
        REQUIRE(decoded.report.valid);
        REQUIRE(decoded.report.sequence == completed.completed.group_id);
        auto reconstructed = simnet::WorldSnapshot{};
        REQUIRE(simnet::reconstruct_world_snapshot(nullptr, decoded.update, reconstructed).valid);
        REQUIRE(same_snapshot(reconstructed, encoded.resulting_snapshot));
        CHECK(live_decode_state.latest_remote_sequence == 0U);
        CHECK(ack.newest_received_snapshot == 0U);

        live_server_state = candidate_server_state;
        live_decode_state = candidate_decode_state;
        ack.newest_received_snapshot = decoded.report.sequence;
        ack.newest_applied_snapshot = decoded.report.sequence;
        simnet::commit_reassembled_group(reassembly, decoded.report.sequence);
        CHECK(live_server_state.next_sequence == 2U);
        CHECK(live_decode_state.latest_remote_sequence == decoded.report.sequence);
        CHECK(ack.newest_received_snapshot == decoded.report.sequence);
        CHECK(reassembly.latest_committed_group == decoded.report.sequence);
    }
}

TEST_CASE(
    "partial local submission leaves the Server candidate uncommitted",
    "[packetization][pipeline][transaction]"
)
{
    auto const source = area_of_interest_source_snapshot();
    auto const pipeline = simnet::PipelineDefinition{};
    auto live_state = simnet::ClientReplicationState{};
    auto candidate_state = live_state;
    auto scratch = simnet::PipelineScratch{};
    auto first = simnet::encode_snapshot(pipeline, candidate_state, scratch, {.snapshot = &source});
    REQUIRE(first.kind == simnet::EncodeResultKind::Update);
    auto config = settings();
    config.max_group_bytes = 4096U;
    config.max_chunks_per_group = 128U;
    config.max_incomplete_bytes = 8192U;
    auto prepared = simnet::PreparedByteGroup{};
    REQUIRE(
        simnet::prepare_byte_group(
            config,
            first.update.sequence,
            std::move(first.update.bytes),
            prepared
        )
            .outcome
        == simnet::GroupPreparationOutcome::Prepared
    );
    REQUIRE(prepared.chunk_count > 1U);
    auto serialization_scratch = std::vector<simnet::Byte>{};
    CHECK_FALSE(simnet::serialize_group_chunk(config, prepared, 0U, serialization_scratch).empty());

    CHECK(live_state.next_sequence == 1U);
    CHECK(candidate_state.next_sequence == 2U);
    auto retry_scratch = simnet::PipelineScratch{};
    auto retry
        = simnet::encode_snapshot(pipeline, live_state, retry_scratch, {.snapshot = &source});
    REQUIRE(retry.kind == simnet::EncodeResultKind::Update);
    CHECK(retry.update.sequence == first.update.sequence);
    CHECK(same_snapshot(retry.resulting_snapshot, first.resulting_snapshot));
}
