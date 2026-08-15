#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

import simnet.compression;
import simnet.core;
import simnet.packetization;

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

    [[nodiscard]] std::vector<std::vector<simnet::Byte>> packets(
        simnet::PacketizationSettings const& config,
        simnet::PacketGroupId group_id,
        std::vector<simnet::Byte> payload
    )
    {
        auto prepared = simnet::PreparedByteGroup{};
        REQUIRE(
            simnet::prepare_byte_group(config, group_id, std::move(payload), prepared).outcome ==
            simnet::GroupPreparationOutcome::Prepared
        );

        auto result = std::vector<std::vector<simnet::Byte>>{};
        auto scratch = std::vector<simnet::Byte>{};
        for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
        {
            auto const packet = simnet::serialize_group_chunk(config, prepared, index, scratch);
            result.emplace_back(packet.begin(), packet.end());
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

TEST_CASE("packetization preserves payloads with zero one or many splits", "[packetization]")
{
    SECTION("disabled")
    {
        auto const config = settings(false);
        auto const source = bytes(40U);
        auto prepared = simnet::PreparedByteGroup{};

        auto const report = simnet::prepare_byte_group(config, 1U, source, prepared);
        REQUIRE(report.outcome == simnet::GroupPreparationOutcome::Prepared);
        CHECK(report.chunk_count == 1U);
        CHECK(report.total_header_bytes == 0U);

        auto scratch = std::vector<simnet::Byte>{};
        auto const packet = simnet::serialize_group_chunk(config, prepared, 0U, scratch);
        auto state = simnet::ReassemblyState{};
        auto const completed =
            simnet::accept_group_packet(config, state, packet, simnet::Nanoseconds{});

        REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
        CHECK(completed.completed.bytes == source);
    }

    SECTION("enabled")
    {
        auto const config = settings();
        for (auto const count : {20U, 120U})
        {
            auto const source = bytes(count);
            auto const group_packets = packets(config, count, source);
            auto order = std::vector<std::size_t>(group_packets.size());
            std::iota(order.begin(), order.end(), 0U);

            auto state = simnet::ReassemblyState{};
            auto const completed = deliver(config, state, group_packets, order);

            REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
            CHECK(completed.completed.group_id == count);
            CHECK(completed.completed.bytes == source);
            CHECK(completed.completed.chunk_count == group_packets.size());
        }
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

TEST_CASE("reassembly accepts reordering and recovers after an incomplete group expires", "[packetization][loss]")
{
    auto const config = settings();
    auto const source = bytes(180U);
    auto const group_packets = packets(config, 7U, source);

    auto reverse = std::vector<std::size_t>(group_packets.size());
    std::iota(reverse.rbegin(), reverse.rend(), 0U);
    auto reverse_state = simnet::ReassemblyState{};
    auto const reversed = deliver(config, reverse_state, group_packets, reverse);

    REQUIRE(reversed.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(reversed.completed.bytes == source);

    auto incomplete_state = simnet::ReassemblyState{};
    for (auto index = std::size_t{1U}; index < group_packets.size(); ++index)
    {
        CHECK(
            simnet::accept_group_packet(
                config,
                incomplete_state,
                group_packets[index],
                simnet::Nanoseconds{}
            )
                .kind != simnet::ReassemblyResultKind::Complete
        );
    }

    simnet::expire_incomplete_groups(config, incomplete_state, simnet::Nanoseconds{1'000'000});
    CHECK(incomplete_state.incomplete.empty());

    auto forward = std::vector<std::size_t>(group_packets.size());
    std::iota(forward.begin(), forward.end(), 0U);
    auto const recovered = deliver(config, incomplete_state, group_packets, forward);

    REQUIRE(recovered.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(recovered.completed.bytes == source);
}

TEST_CASE("malformed packet input is rejected without poisoning later traffic", "[packetization][malformed]")
{
    auto const config = settings();
    auto const source = bytes(80U);
    auto const valid_packets = packets(config, 13U, source);

    auto state = simnet::ReassemblyState{};
    auto truncated = valid_packets.front();
    truncated.resize(simnet::packet_header_bytes - 1U);

    CHECK(
        simnet::accept_group_packet(config, state, truncated, simnet::Nanoseconds{}).kind ==
        simnet::ReassemblyResultKind::Invalid
    );
    CHECK(state.incomplete.empty());

    auto order = std::vector<std::size_t>(valid_packets.size());
    std::iota(order.begin(), order.end(), 0U);
    auto const recovered = deliver(config, state, valid_packets, order);

    REQUIRE(recovered.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(recovered.completed.bytes == source);
}

TEST_CASE("per-packet compression preserves complete packetized payloads", "[compression][packetization]")
{
    auto const config = settings();
    auto const source = std::vector<simnet::Byte>(50U, simnet::Byte{0U});
    auto transport_packets = packets(config, 7U, source);
    REQUIRE(transport_packets.size() > 1U);

    auto compressor = simnet::ZstdCompressor{};
    for (auto& packet : transport_packets)
    {
        auto encoded = std::vector<simnet::Byte>{};
        REQUIRE(
            simnet::compress_bytes(
                compressor,
                packet,
                1,
                {
                    .max_uncompressed_bytes = config.max_payload_bytes,
                    .max_output_bytes = config.max_payload_bytes,
                },
                simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
                encoded
            )
                .valid
        );
        packet = std::move(encoded);
    }

    auto decompressor = simnet::ZstdDecompressor{};
    auto decompression_scratch = std::vector<simnet::Byte>{};
    auto state = simnet::ReassemblyState{};
    auto completed = simnet::ReassemblyResult{};

    for (auto index = transport_packets.size(); index > 0U; --index)
    {
        auto application_packet = simnet::ByteSpan{transport_packets[index - 1U]};
        if (simnet::has_compression_envelope(application_packet))
        {
            REQUIRE(
                simnet::decompress_bytes(
                    decompressor,
                    application_packet,
                    {
                        .max_uncompressed_bytes = config.max_payload_bytes,
                        .max_output_bytes = config.max_payload_bytes,
                    },
                    decompression_scratch
                )
                    .valid
            );
            application_packet = decompression_scratch;
        }

        completed = simnet::accept_group_packet(
            config,
            state,
            application_packet,
            simnet::Nanoseconds{100U}
        );
    }

    REQUIRE(completed.kind == simnet::ReassemblyResultKind::Complete);
    CHECK(completed.completed.group_id == 7U);
    CHECK(completed.completed.bytes == source);
}