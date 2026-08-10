#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import simnet.app_protocol;
import simnet.app_common;
import simnet.app_compression_dictionary;
import simnet.compression;
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
        for (auto index = std::size_t{}; index < count; ++index)
        {
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
        for (auto index = std::uint32_t{}; index < 30U; ++index)
        {
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
        auto const vectors_match =
            [](std::vector<simnet::Vec3f> const& first, std::vector<simnet::Vec3f> const& second)
        {
            return first.size() == second.size() &&
                   std::equal(
                       first.begin(),
                       first.end(),
                       second.begin(),
                       [](simnet::Vec3f const& a, simnet::Vec3f const& b)
                       {
                           return a.x == b.x && a.y == b.y && a.z == b.z;
                       }
                   );
        };
        return left.tick == right.tick && left.ids == right.ids &&
               left.classifications == right.classifications &&
               vectors_match(left.positions, right.positions) &&
               vectors_match(left.headings, right.headings) && left.hues == right.hues;
    }

    [[nodiscard]] std::vector<std::vector<simnet::Byte>> packets(
        simnet::PacketizationSettings const& config,
        simnet::PacketGroupId group_id,
        std::vector<simnet::Byte> payload
    )
    {
        auto prepared = simnet::PreparedByteGroup{};
        auto const report =
            simnet::prepare_byte_group(config, group_id, std::move(payload), prepared);
        REQUIRE(report.outcome == simnet::GroupPreparationOutcome::Prepared);
        auto result = std::vector<std::vector<simnet::Byte>>{};
        auto scratch = std::vector<simnet::Byte>{};
        for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
        {
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
        for (auto const index : order)
        {
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
    for (auto const count : {20U, 120U})
    {
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
        simnet::prepare_byte_group(config, 0x01020304U, bytes(20U), prepared).outcome ==
        simnet::GroupPreparationOutcome::Prepared
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
    auto first =
        simnet::accept_group_packet(config, state, group_packets[0], simnet::Nanoseconds{});
    REQUIRE(first.kind == simnet::ReassemblyResultKind::Incomplete);
    auto duplicate =
        simnet::accept_group_packet(config, state, group_packets[0], simnet::Nanoseconds{});
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
    for (auto const missing : {0U, 1U, 3U})
    {
        auto state = simnet::ReassemblyState{};
        for (auto index = std::size_t{}; index < group_packets.size(); ++index)
        {
            if (index != missing)
            {
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
    for (auto offset : {0U, 4U, 6U, 13U, 15U, 17U, 21U})
    {
        auto invalid = valid_packets.front();
        invalid[offset] = simnet::Byte{0xFFU};
        auto state = simnet::ReassemblyState{};
        auto const rejected =
            simnet::accept_group_packet(config, state, invalid, simnet::Nanoseconds{});
        CHECK(rejected.kind == simnet::ReassemblyResultKind::Invalid);
    }

    auto state = simnet::ReassemblyState{};
    auto truncated = valid_packets.front();
    truncated.resize(simnet::packet_header_bytes - 1U);
    CHECK(
        simnet::accept_group_packet(config, state, truncated, simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Invalid
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
    auto reject = [&](std::vector<simnet::Byte> packet)
    {
        auto state = simnet::ReassemblyState{};
        CHECK(
            simnet::accept_group_packet(config, state, std::move(packet), simnet::Nanoseconds{})
                .kind == simnet::ReassemblyResultKind::Invalid
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
        simnet::accept_group_packet(config, state, first[0], simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Incomplete
    );
    REQUIRE(
        simnet::accept_group_packet(config, state, second[0], simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Incomplete
    );
    auto conflict = first[1];
    write_u32(conflict, 17U, 101U);
    CHECK(
        simnet::accept_group_packet(config, state, conflict, simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Invalid
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
            simnet::accept_group_packet(config, state, first_packets[0], simnet::Nanoseconds{})
                .kind == simnet::ReassemblyResultKind::Incomplete
        );
        CHECK(
            simnet::accept_group_packet(config, state, second_packets[0], simnet::Nanoseconds{})
                .kind == simnet::ReassemblyResultKind::LimitExceeded
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
            simnet::accept_group_packet(config, state, first_packets[0], simnet::Nanoseconds{})
                .kind == simnet::ReassemblyResultKind::Incomplete
        );
        CHECK(state.report.retained_incomplete_bytes == 100U);
        CHECK(
            simnet::accept_group_packet(config, state, second_packets[0], simnet::Nanoseconds{})
                .kind == simnet::ReassemblyResultKind::LimitExceeded
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
        simnet::accept_group_packet(config, state, older[0], simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Incomplete
    );
    simnet::commit_reassembled_group(state, 31U);
    CHECK(state.incomplete.empty());
    CHECK(
        simnet::accept_group_packet(config, state, older[0], simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Stale
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
        simnet::prepare_byte_group(config, 1U, bytes(40U), prepared).outcome ==
        simnet::GroupPreparationOutcome::Prepared
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

    for (auto const mode : {simnet::AreaOfInterestMode::Radius, simnet::AreaOfInterestMode::Fov})
    {
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
        auto group_packets =
            packets(config, encoded.update.sequence, std::move(encoded.update.bytes));
        REQUIRE(group_packets.size() > 1U);

        auto reassembly = simnet::ReassemblyState{};
        auto live_decode_state = simnet::ClientReplicationState{};
        auto ack = simnet::app::SnapshotAck{};
        for (auto index = std::size_t{}; index + 1U < group_packets.size(); ++index)
        {
            CHECK(
                simnet::accept_group_packet(
                    config,
                    reassembly,
                    group_packets[index],
                    simnet::Nanoseconds{}
                )
                    .kind == simnet::ReassemblyResultKind::Incomplete
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
            .outcome == simnet::GroupPreparationOutcome::Prepared
    );
    REQUIRE(prepared.chunk_count > 1U);
    auto serialization_scratch = std::vector<simnet::Byte>{};
    CHECK_FALSE(simnet::serialize_group_chunk(config, prepared, 0U, serialization_scratch).empty());

    CHECK(live_state.next_sequence == 1U);
    CHECK(candidate_state.next_sequence == 2U);
    auto retry_scratch = simnet::PipelineScratch{};
    auto retry =
        simnet::encode_snapshot(pipeline, live_state, retry_scratch, {.snapshot = &source});
    REQUIRE(retry.kind == simnet::EncodeResultKind::Update);
    CHECK(retry.update.sequence == first.update.sequence);
    CHECK(same_snapshot(retry.resulting_snapshot, first.resulting_snapshot));
}

TEST_CASE(
    "per-packet compression preserves packet ordering and complete group bytes",
    "[compression][packetization][roundtrip]"
)
{
    auto config = settings();
    auto source = std::vector<simnet::Byte>(50U, simnet::Byte{0U});
    auto const group_packets = packets(config, 7U, source);
    REQUIRE(group_packets.size() == 2U);

    auto compressor = simnet::ZstdCompressor{};
    auto transport_packets = std::vector<std::vector<simnet::Byte>>{};
    auto compressed_count = std::uint32_t{};
    auto raw_count = std::uint32_t{};
    for (auto const& packet : group_packets)
    {
        auto output = std::vector<simnet::Byte>{};
        auto const report = simnet::compress_bytes(
            compressor,
            packet,
            1,
            {
                .max_uncompressed_bytes = config.max_payload_bytes,
                .max_output_bytes = config.max_payload_bytes,
            },
            simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
            output
        );
        REQUIRE(report.valid);
        if (report.encoding == simnet::CompressionEncoding::Zstd)
        {
            ++compressed_count;
        }
        else
        {
            ++raw_count;
        }
        transport_packets.push_back(std::move(output));
    }
    CHECK(compressed_count == 1U);
    CHECK(raw_count == 1U);

    auto decompressor = simnet::ZstdDecompressor{};
    auto decompression_scratch = std::vector<simnet::Byte>{};
    auto state = simnet::ReassemblyState{};
    auto result = simnet::ReassemblyResult{};
    for (auto const index : {1U, 0U})
    {
        auto application_packet = simnet::ByteSpan{transport_packets[index]};
        if (simnet::has_compression_envelope(application_packet))
        {
            auto const report = simnet::decompress_bytes(
                decompressor,
                application_packet,
                {
                    .max_uncompressed_bytes = config.max_payload_bytes,
                    .max_output_bytes = config.max_payload_bytes,
                },
                decompression_scratch
            );
            REQUIRE(report.valid);
            application_packet = decompression_scratch;
        }
        result = simnet::accept_group_packet(
            config,
            state,
            application_packet,
            simnet::Nanoseconds{100U}
        );
    }
    REQUIRE(result.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(result.completed.group_id == 7U);
    CHECK(result.completed.bytes == source);
}

TEST_CASE(
    "equal group IDs remain isolated across peer compression and reassembly state",
    "[peer][compression][packetization]"
)
{
    auto const config = settings();
    auto const first_source = std::vector<simnet::Byte>(100U, simnet::Byte{0x11U});
    auto const second_source = std::vector<simnet::Byte>(100U, simnet::Byte{0x22U});
    auto first_compressor = simnet::ZstdCompressor{};
    auto second_compressor = simnet::ZstdCompressor{};
    auto first_encoded = std::vector<simnet::Byte>{};
    auto second_encoded = std::vector<simnet::Byte>{};
    auto const compression_limits = simnet::CompressionLimits{
        .max_uncompressed_bytes = config.max_group_bytes,
        .max_output_bytes = config.max_group_bytes,
    };
    REQUIRE(
        simnet::compress_bytes(
            first_compressor,
            first_source,
            1,
            compression_limits,
            simnet::CompressionEnvelopePolicy::Always,
            first_encoded
        )
            .valid
    );
    REQUIRE(
        simnet::compress_bytes(
            second_compressor,
            second_source,
            1,
            compression_limits,
            simnet::CompressionEnvelopePolicy::Always,
            second_encoded
        )
            .valid
    );

    auto const first_packets = packets(config, 1U, first_encoded);
    auto const second_packets = packets(config, 1U, second_encoded);
    auto first_order = std::vector<std::size_t>(first_packets.size());
    auto second_order = std::vector<std::size_t>(second_packets.size());
    std::iota(first_order.begin(), first_order.end(), 0U);
    std::iota(second_order.begin(), second_order.end(), 0U);
    auto first_reassembly = simnet::ReassemblyState{};
    auto second_reassembly = simnet::ReassemblyState{};
    auto const first_complete = deliver(config, first_reassembly, first_packets, first_order);
    auto const second_complete = deliver(config, second_reassembly, second_packets, second_order);
    REQUIRE(first_complete.kind == simnet::ReassemblyResultKind::Complete);
    REQUIRE(second_complete.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(first_complete.completed.group_id == second_complete.completed.group_id);
    CHECK(first_complete.completed.bytes != second_complete.completed.bytes);

    auto first_decompressor = simnet::ZstdDecompressor{};
    auto second_decompressor = simnet::ZstdDecompressor{};
    auto first_decoded = std::vector<simnet::Byte>{};
    auto second_decoded = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::decompress_bytes(
            first_decompressor,
            first_complete.completed.bytes,
            compression_limits,
            first_decoded
        )
            .valid
    );
    REQUIRE(
        simnet::decompress_bytes(
            second_decompressor,
            second_complete.completed.bytes,
            compression_limits,
            second_decoded
        )
            .valid
    );
    CHECK(first_decoded == first_source);
    CHECK(second_decoded == second_source);
}

TEST_CASE(
    "invalid compressed packets never enter reassembly and later traffic succeeds",
    "[compression][packetization][transaction]"
)
{
    auto config = settings();
    auto source = std::vector<simnet::Byte>(39U, simnet::Byte{0U});
    auto const group_packets = packets(config, 3U, source);
    REQUIRE(group_packets.size() == 1U);

    auto compressor = simnet::ZstdCompressor{};
    auto compressed = std::vector<simnet::Byte>{};
    auto const encoded = simnet::compress_bytes(
        compressor,
        group_packets.front(),
        1,
        {
            .max_uncompressed_bytes = config.max_payload_bytes,
            .max_output_bytes = config.max_payload_bytes,
        },
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        compressed
    );
    REQUIRE(encoded.valid);
    REQUIRE(encoded.encoding == simnet::CompressionEncoding::Zstd);

    auto malformed = compressed;
    malformed.back() ^= simnet::Byte{0xFFU};
    auto decompressor = simnet::ZstdDecompressor{};
    auto restored = std::vector<simnet::Byte>{};
    CHECK_FALSE(
        simnet::decompress_bytes(
            decompressor,
            malformed,
            {
                .max_uncompressed_bytes = config.max_payload_bytes,
                .max_output_bytes = config.max_payload_bytes,
            },
            restored
        )
            .valid
    );

    auto state = simnet::ReassemblyState{};
    CHECK(state.report.received_chunks == 0U);
    auto const decoded = simnet::decompress_bytes(
        decompressor,
        compressed,
        {
            .max_uncompressed_bytes = config.max_payload_bytes,
            .max_output_bytes = config.max_payload_bytes,
        },
        restored
    );
    REQUIRE(decoded.valid);
    auto const completed =
        simnet::accept_group_packet(config, state, restored, simnet::Nanoseconds{100U});
    REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(completed.completed.bytes == source);
}

TEST_CASE(
    "compression preparation failure leaves pipeline candidate state uncommitted",
    "[compression][pipeline][transaction]"
)
{
    auto const source = area_of_interest_source_snapshot();
    auto const pipeline = simnet::PipelineDefinition{};
    auto live_state = simnet::ClientReplicationState{};
    auto candidate_state = live_state;
    auto scratch = simnet::PipelineScratch{};
    auto const encoded =
        simnet::encode_snapshot(pipeline, candidate_state, scratch, {.snapshot = &source});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    REQUIRE(candidate_state.next_sequence == 2U);

    auto compressor = simnet::ZstdCompressor{};
    auto output = std::vector<simnet::Byte>{};
    auto const compressed = simnet::compress_bytes(
        compressor,
        encoded.update.bytes,
        1,
        {
            .max_uncompressed_bytes = 4096U,
            .max_output_bytes = simnet::compression_envelope_bytes,
        },
        simnet::CompressionEnvelopePolicy::Always,
        output
    );
    CHECK_FALSE(compressed.valid);
    CHECK(live_state.next_sequence == 1U);
    CHECK_FALSE(live_state.incremental_seeded);
    CHECK(live_state.incremental_cursor == 0U);
}

TEST_CASE(
    "failed dictionary decompression leaves reassembly uncommitted for recovery",
    "[compression][dictionary][packetization][recovery][transaction]"
)
{
    auto config = settings();
    config.max_group_bytes = 4096U;
    config.max_chunks_per_group = 128U;
    config.max_incomplete_bytes = 8192U;
    auto dictionary = simnet::app::load_compression_dictionary({
        .mode = simnet::app::CompressionMode::WholeUpdate,
        .level = 1,
        .dictionary = "pipeline_v1",
    });
    REQUIRE(dictionary.has_value());

    auto source = std::vector<simnet::Byte>(1024U, simnet::Byte{0x2aU});
    auto compressor = simnet::ZstdCompressor{};
    auto valid = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes_with_dictionary(
            compressor,
            dictionary->dictionary,
            source,
            {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
            valid
        )
            .valid
    );
    auto malformed = valid;
    malformed.pop_back();

    auto reassembly = simnet::ReassemblyState{};
    auto const malformed_packets = packets(config, 41U, malformed);
    auto malformed_order = std::vector<std::size_t>(malformed_packets.size());
    std::iota(malformed_order.begin(), malformed_order.end(), 0U);
    auto completed = deliver(config, reassembly, malformed_packets, malformed_order);
    REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);

    auto decompressor = simnet::ZstdDecompressor{};
    auto restored = bytes(3U, 9U);
    auto const unchanged = restored;
    CHECK_FALSE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary->dictionary,
            completed.completed.bytes,
            {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
            restored
        )
            .valid
    );
    CHECK(restored == unchanged);
    CHECK(reassembly.latest_committed_group == 0U);

    auto const recovery_packets = packets(config, 41U, valid);
    auto recovery_order = std::vector<std::size_t>(recovery_packets.size());
    std::iota(recovery_order.begin(), recovery_order.end(), 0U);
    completed = deliver(config, reassembly, recovery_packets, recovery_order);
    REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
    REQUIRE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary->dictionary,
            completed.completed.bytes,
            {.max_uncompressed_bytes = 4096U, .max_output_bytes = 4096U},
            restored
        )
            .valid
    );
    CHECK(restored == source);
    simnet::commit_reassembled_group(reassembly, completed.completed.group_id);
    CHECK(reassembly.latest_committed_group == 41U);
}

TEST_CASE(
    "compression and packetization preserve an exact FOV LOD Patch",
    "[compression][packetization][pipeline][aoi][lod][transaction]"
)
{
    auto source = area_of_interest_source_snapshot();
    auto candidates = std::vector<std::uint32_t>(source.size());
    std::iota(candidates.begin(), candidates.end(), 0U);
    auto pipeline = simnet::PipelineDefinition{};
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::BitPacking;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;
    pipeline.techniques |= simnet::PipelineTechniqueFlags::DeltaFieldMask;
    pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);
    pipeline.area_of_interest = {
        .mode = simnet::AreaOfInterestMode::Fov,
        .radius = 25.0F,
        .fov_degrees = 90.0F,
    };
    pipeline.level_of_detail = {
        .mode = simnet::LevelOfDetailMode::DistanceBands,
        .near_distance = 5.0F,
        .medium_distance = 15.0F,
        .medium_interval_ticks = 2U,
        .far_interval_ticks = 4U,
    };
    auto const interest = simnet::InterestSource{
        .position = {},
        .forward = {.z = 1.0F},
        .source_entity_id = 1U,
    };
    auto encode_state = simnet::ClientReplicationState{};
    auto encode_scratch = simnet::PipelineScratch{};
    auto const full = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &source,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    source.tick = 13U;
    for (auto& position : source.positions)
    {
        position.y += 1.0F;
    }
    auto const encoded = simnet::encode_snapshot(
        pipeline,
        encode_state,
        encode_scratch,
        {
            .snapshot = &source,
            .baseline_snapshot = &full.resulting_snapshot,
            .baseline_sequence = full.update.sequence,
            .interest_source = &interest,
            .candidate_indices = candidates,
        }
    );
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
    REQUIRE(encoded.report.snapshot_kind == simnet::SnapshotKind::Patch);

    auto config = settings();
    config.max_group_bytes = 4096U;
    config.max_chunks_per_group = 128U;
    config.max_incomplete_bytes = 8192U;
    auto dictionary = simnet::app::load_compression_dictionary({
        .mode = simnet::app::CompressionMode::WholeUpdate,
        .level = 1,
        .dictionary = "pipeline_v1",
    });
    REQUIRE(dictionary.has_value());
    for (auto const treatment : {0, 1, 2})
    {
        auto const whole_update = treatment != 0;
        auto const use_dictionary = treatment == 2;
        auto compressor = simnet::ZstdCompressor{};
        auto decompressor = simnet::ZstdDecompressor{};
        auto compressed_group = std::vector<simnet::Byte>{};
        auto group_source = encoded.update.bytes;
        if (whole_update)
        {
            auto const limits = simnet::CompressionLimits{
                .max_uncompressed_bytes = 4096U,
                .max_output_bytes = 4096U,
            };
            auto const report = use_dictionary ? simnet::compress_bytes_with_dictionary(
                                                     compressor,
                                                     dictionary->dictionary,
                                                     group_source,
                                                     limits,
                                                     compressed_group
                                                 )
                                               : simnet::compress_bytes(
                                                     compressor,
                                                     group_source,
                                                     1,
                                                     limits,
                                                     simnet::CompressionEnvelopePolicy::Always,
                                                     compressed_group
                                                 );
            REQUIRE(report.valid);
            group_source = compressed_group;
        }

        auto transport_packets = packets(config, encoded.update.sequence, group_source);
        if (!whole_update)
        {
            for (auto& packet : transport_packets)
            {
                auto compressed_packet = std::vector<simnet::Byte>{};
                auto const report = simnet::compress_bytes(
                    compressor,
                    packet,
                    1,
                    {
                        .max_uncompressed_bytes = config.max_payload_bytes,
                        .max_output_bytes = config.max_payload_bytes,
                    },
                    simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
                    compressed_packet
                );
                REQUIRE(report.valid);
                packet = std::move(compressed_packet);
            }
        }

        auto reassembly = simnet::ReassemblyState{};
        auto completed = simnet::ReassemblyResult{};
        auto packet_scratch = std::vector<simnet::Byte>{};
        for (auto index = transport_packets.size(); index > 0U; --index)
        {
            auto packet = simnet::ByteSpan{transport_packets[index - 1U]};
            if (!whole_update && simnet::has_compression_envelope(packet))
            {
                auto const report = simnet::decompress_bytes(
                    decompressor,
                    packet,
                    {
                        .max_uncompressed_bytes = config.max_payload_bytes,
                        .max_output_bytes = config.max_payload_bytes,
                    },
                    packet_scratch
                );
                REQUIRE(report.valid);
                packet = packet_scratch;
            }
            completed =
                simnet::accept_group_packet(config, reassembly, packet, simnet::Nanoseconds{100U});
        }
        REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);

        auto decoded_group = simnet::ByteSpan{completed.completed.bytes};
        auto decoded_scratch = std::vector<simnet::Byte>{};
        if (whole_update)
        {
            auto const limits = simnet::CompressionLimits{
                .max_uncompressed_bytes = 4096U,
                .max_output_bytes = 4096U,
            };
            auto const report = use_dictionary ? simnet::decompress_bytes_with_dictionary(
                                                     decompressor,
                                                     dictionary->dictionary,
                                                     decoded_group,
                                                     limits,
                                                     decoded_scratch
                                                 )
                                               : simnet::decompress_bytes(
                                                     decompressor,
                                                     decoded_group,
                                                     limits,
                                                     decoded_scratch
                                                 );
            REQUIRE(report.valid);
            decoded_group = decoded_scratch;
        }
        auto decode_state = simnet::ClientReplicationState{};
        REQUIRE(
            simnet::decode_update(pipeline, decode_state, {.bytes = full.update.bytes}).report.valid
        );
        auto const decoded = simnet::decode_update(
            pipeline,
            decode_state,
            {
                .bytes = decoded_group,
                .baseline_snapshot = &full.resulting_snapshot,
                .baseline_sequence = full.update.sequence,
            }
        );
        REQUIRE(decoded.report.valid);
        REQUIRE(decoded.report.sequence == completed.completed.group_id);
        auto reconstructed = simnet::WorldSnapshot{};
        REQUIRE(
            simnet::reconstruct_world_snapshot(
                &full.resulting_snapshot,
                decoded.update,
                reconstructed
            )
                .valid
        );
        CHECK(same_snapshot(reconstructed, encoded.resulting_snapshot));
    }
}
