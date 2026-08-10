module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module simnet.packetization;

import simnet.core;

namespace
{
    constexpr std::uint32_t packet_magic = 0x534E504BU;
    constexpr std::uint16_t packet_protocol_version = 1U;
    constexpr std::uint16_t packet_schema_version = 1U;
    constexpr std::uint8_t byte_group_chunk_kind = 1U;

    struct PacketHeader
    {
        std::uint32_t magic{};
        std::uint16_t protocol{};
        std::uint16_t schema{};
        std::uint8_t kind{};
        simnet::PacketGroupId group_id{};
        std::uint16_t chunk_index{};
        std::uint16_t chunk_count{};
        std::uint32_t group_bytes{};
        std::uint32_t chunk_bytes{};
    };

    [[nodiscard]] bool read_header(simnet::ByteSpan bytes, PacketHeader& header) noexcept
    {
        auto offset = std::size_t{};
        return simnet::read_big_endian(bytes, offset, header.magic) &&
               simnet::read_big_endian(bytes, offset, header.protocol) &&
               simnet::read_big_endian(bytes, offset, header.schema) &&
               simnet::read_byte(bytes, offset, header.kind) &&
               simnet::read_big_endian(bytes, offset, header.group_id) &&
               simnet::read_big_endian(bytes, offset, header.chunk_index) &&
               simnet::read_big_endian(bytes, offset, header.chunk_count) &&
               simnet::read_big_endian(bytes, offset, header.group_bytes) &&
               simnet::read_big_endian(bytes, offset, header.chunk_bytes) &&
               offset == simnet::packet_header_bytes;
    }

    [[nodiscard]] bool header_identity_is_valid(PacketHeader const& header) noexcept
    {
        return header.magic == packet_magic && header.protocol == packet_protocol_version &&
               header.schema == packet_schema_version && header.kind == byte_group_chunk_kind;
    }

    [[nodiscard]] bool header_fields_are_within_bounds(
        simnet::PacketizationSettings const& settings,
        PacketHeader const& header,
        std::size_t packet_bytes
    ) noexcept
    {
        return header.group_id != 0U && header.group_bytes != 0U &&
               header.group_bytes <= settings.max_group_bytes && header.chunk_count != 0U &&
               header.chunk_count <= settings.max_chunks_per_group &&
               header.chunk_index < header.chunk_count &&
               header.chunk_bytes == packet_bytes - simnet::packet_header_bytes &&
               packet_bytes <= settings.max_payload_bytes;
    }

    [[nodiscard]] std::uint64_t ceiling_divide(std::uint64_t value, std::uint64_t divisor)
    {
        return value / divisor + (value % divisor != 0U ? 1U : 0U);
    }

    void update_current_report(simnet::ReassemblyState& state) noexcept
    {
        state.report.incomplete_groups = static_cast<std::uint32_t>(state.incomplete.size());
        auto bytes = std::uint64_t{};
        for (auto const& group : state.incomplete)
        {
            bytes += group.group_bytes;
        }
        state.report.retained_incomplete_bytes = static_cast<std::uint32_t>(bytes);
    }

    [[nodiscard]] bool
    chunk_received(simnet::IncompleteByteGroup const& group, std::uint16_t index) noexcept
    {
        auto const word = static_cast<std::size_t>(index / 64U);
        auto const bit = static_cast<std::uint64_t>(1U) << (index % 64U);
        return (group.received_chunks[word] & bit) != 0U;
    }

    void mark_chunk_received(simnet::IncompleteByteGroup& group, std::uint16_t index) noexcept
    {
        auto const word = static_cast<std::size_t>(index / 64U);
        auto const bit = static_cast<std::uint64_t>(1U) << (index % 64U);
        group.received_chunks[word] |= bit;
        ++group.received_count;
    }

    void
    erase_incomplete_group(simnet::ReassemblyState& state, simnet::PacketGroupId group_id) noexcept
    {
        auto const found =
            std::ranges::find(state.incomplete, group_id, &simnet::IncompleteByteGroup::group_id);
        if (found == state.incomplete.end())
        {
            return;
        }

        state.incomplete.erase(found);
        update_current_report(state);
    }

    [[nodiscard]] simnet::ReassemblyResult
    reject_group(simnet::ReassemblyState& state, simnet::PacketGroupId group_id, std::string error)
    {
        erase_incomplete_group(state, group_id);
        ++state.report.invalid_groups;
        return {
            .kind = simnet::ReassemblyResultKind::Invalid,
            .group_id = group_id,
            .error = std::move(error),
        };
    }

    [[nodiscard]] simnet::ReassemblyResult accept_raw_group(
        simnet::PacketizationSettings const& settings,
        simnet::ReassemblyState& state,
        simnet::ByteSpan packet
    )
    {
        if (packet.empty() || packet.size() > settings.max_payload_bytes ||
            packet.size() > settings.max_group_bytes)
        {
            ++state.report.invalid_groups;
            return {
                .kind = simnet::ReassemblyResultKind::Invalid,
                .error = "disabled packetization payload is outside configured bounds"
            };
        }

        ++state.report.unique_chunks;
        ++state.report.completed_groups;
        state.report.latest_completed_group_bytes = static_cast<std::uint32_t>(packet.size());
        return {
            .kind = simnet::ReassemblyResultKind::Complete,
            .completed = {
                .chunk_count = 1U,
                .total_packet_bytes = static_cast<std::uint32_t>(packet.size()),
                .bytes = std::vector<simnet::Byte>{packet.begin(), packet.end()},
            },
        };
    }

    struct IncompleteGroupAdmission
    {
        simnet::IncompleteByteGroup* group{};
        simnet::ReassemblyResult rejection{};
    };

    [[nodiscard]] IncompleteGroupAdmission admit_incomplete_group(
        simnet::PacketizationSettings const& settings,
        simnet::ReassemblyState& state,
        PacketHeader const& header,
        std::uint32_t chunk_payload_capacity,
        simnet::Nanoseconds now
    )
    {
        auto found = std::ranges::find(
            state.incomplete,
            header.group_id,
            &simnet::IncompleteByteGroup::group_id
        );
        if (found != state.incomplete.end())
        {
            auto const metadata_matches = found->group_bytes == header.group_bytes &&
                                          found->chunk_count == header.chunk_count &&
                                          found->chunk_payload_capacity == chunk_payload_capacity;
            if (!metadata_matches)
            {
                return {
                    .rejection = reject_group(
                        state,
                        header.group_id,
                        "packet metadata conflicts with the incomplete group"
                    )
                };
            }
            return {.group = &*found};
        }

        auto const retained =
            static_cast<std::uint64_t>(state.report.retained_incomplete_bytes) + header.group_bytes;
        if (state.incomplete.size() >= settings.max_in_flight_groups ||
            retained > settings.max_incomplete_bytes)
        {
            return {
                .rejection = {
                    .kind = simnet::ReassemblyResultKind::LimitExceeded,
                    .group_id = header.group_id,
                    .error = "packet group exceeds reassembly resource limits"
                }
            };
        }

        state.incomplete.push_back({
            .group_id = header.group_id,
            .group_bytes = header.group_bytes,
            .chunk_payload_capacity = chunk_payload_capacity,
            .chunk_count = header.chunk_count,
            .first_received_time = now,
            .bytes = std::vector<simnet::Byte>(header.group_bytes),
            .received_chunks = std::vector<std::uint64_t>(
                (static_cast<std::size_t>(header.chunk_count) + 63U) / 64U
            ),
        });
        update_current_report(state);
        return {.group = &state.incomplete.back()};
    }

    [[nodiscard]] simnet::ReassemblyResult accept_validated_chunk(
        simnet::ReassemblyState& state,
        simnet::IncompleteByteGroup& group,
        PacketHeader const& header,
        simnet::ByteSpan payload,
        std::size_t offset
    )
    {
        if (chunk_received(group, header.chunk_index))
        {
            auto const matches = std::equal(
                payload.begin(),
                payload.end(),
                group.bytes.begin() + static_cast<std::ptrdiff_t>(offset)
            );
            if (!matches)
            {
                return reject_group(
                    state,
                    header.group_id,
                    "duplicate packet chunk payload conflicts"
                );
            }

            ++state.report.duplicate_chunks;
            return {.kind = simnet::ReassemblyResultKind::Duplicate, .group_id = header.group_id};
        }

        std::copy(
            payload.begin(),
            payload.end(),
            group.bytes.begin() + static_cast<std::ptrdiff_t>(offset)
        );
        mark_chunk_received(group, header.chunk_index);
        ++state.report.unique_chunks;
        if (group.received_count != group.chunk_count)
        {
            return {.kind = simnet::ReassemblyResultKind::Incomplete, .group_id = header.group_id};
        }

        auto completed = simnet::CompletedByteGroup{
            .group_id = group.group_id,
            .chunk_count = group.chunk_count,
            .total_packet_bytes =
                group.group_bytes +
                static_cast<std::uint32_t>(group.chunk_count) * simnet::packet_header_bytes,
            .bytes = std::move(group.bytes),
        };
        erase_incomplete_group(state, header.group_id);
        ++state.report.completed_groups;
        state.report.latest_completed_group_bytes =
            static_cast<std::uint32_t>(completed.bytes.size());
        return {
            .kind = simnet::ReassemblyResultKind::Complete,
            .group_id = completed.group_id,
            .completed = std::move(completed)
        };
    }
}

namespace simnet
{
    void validate_packetization_settings(PacketizationSettings const& settings)
    {
        if (settings.max_payload_bytes == 0U || settings.max_group_bytes == 0U)
        {
            throw std::runtime_error("packetization byte limits must be positive");
        }
        if (settings.max_group_bytes > maximum_packetized_group_bytes)
        {
            throw std::runtime_error("packetization group limit exceeds implementation bound");
        }
        if (settings.max_chunks_per_group == 0U ||
            settings.max_chunks_per_group > maximum_chunks_per_group)
        {
            throw std::runtime_error("packetization chunk limit exceeds implementation bound");
        }
        if (settings.max_in_flight_groups == 0U ||
            settings.max_in_flight_groups > maximum_in_flight_groups)
        {
            throw std::runtime_error("packetization in-flight limit exceeds implementation bound");
        }
        if (settings.max_incomplete_bytes < settings.max_group_bytes ||
            settings.max_incomplete_bytes > maximum_incomplete_group_bytes)
        {
            throw std::runtime_error("packetization incomplete byte budget is invalid");
        }
        if (settings.reassembly_timeout.count() <= 0)
        {
            throw std::runtime_error("packetization reassembly timeout must be positive");
        }
        if (!settings.enabled)
        {
            return;
        }
        if (settings.max_payload_bytes <= packet_header_bytes)
        {
            throw std::runtime_error("packetization payload limit cannot fit header and payload");
        }
        auto const capacity = settings.max_payload_bytes - packet_header_bytes;
        auto const maximum_encodable =
            static_cast<std::uint64_t>(capacity) * settings.max_chunks_per_group;
        if (settings.max_group_bytes > maximum_encodable)
        {
            throw std::runtime_error("packetization group limit exceeds configured chunk capacity");
        }
    }

    PacketizationReport prepare_byte_group(
        PacketizationSettings const& settings,
        PacketGroupId group_id,
        std::vector<Byte> bytes,
        PreparedByteGroup& prepared
    )
    {
        validate_packetization_settings(settings);
        auto report = PacketizationReport{
            .group_id = group_id,
        };
        if (group_id == 0U)
        {
            report.error = "packet group id 0 is reserved";
            return report;
        }
        if (bytes.empty() || bytes.size() > settings.max_group_bytes)
        {
            report.error = "packet group byte count is outside configured bounds";
            return report;
        }
        report.group_bytes = static_cast<std::uint32_t>(bytes.size());

        auto capacity = settings.max_payload_bytes;
        auto count = std::uint64_t{1U};
        if (settings.enabled)
        {
            capacity -= packet_header_bytes;
            count = ceiling_divide(bytes.size(), capacity);
            if (count == 0U || count > settings.max_chunks_per_group ||
                count > std::numeric_limits<std::uint16_t>::max())
            {
                report.error = "packet group chunk count is outside configured bounds";
                return report;
            }
        }
        else if (bytes.size() > settings.max_payload_bytes)
        {
            report.error = "disabled packetization payload exceeds hard payload limit";
            return report;
        }

        auto const total_headers = settings.enabled ? count * packet_header_bytes : 0U;
        auto const total_packet_bytes = total_headers + bytes.size();
        if (total_headers > std::numeric_limits<std::uint32_t>::max() ||
            total_packet_bytes > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "packet group aggregate byte count exceeds uint32 range";
            return report;
        }

        report.chunk_count = static_cast<std::uint32_t>(count);
        report.total_header_bytes = static_cast<std::uint32_t>(total_headers);
        report.total_packet_bytes = static_cast<std::uint32_t>(total_packet_bytes);
        report.outcome = GroupPreparationOutcome::Prepared;
        prepared = {
            .group_id = group_id,
            .bytes = std::move(bytes),
            .chunk_payload_capacity = capacity,
            .chunk_count = static_cast<std::uint16_t>(count),
            .report = report,
        };
        return report;
    }

    ByteSpan serialize_group_chunk(
        PacketizationSettings const& settings,
        PreparedByteGroup const& prepared,
        std::uint16_t chunk_index,
        std::vector<Byte>& serialization_scratch
    )
    {
        validate_packetization_settings(settings);
        auto const expected_capacity = settings.enabled
                                           ? settings.max_payload_bytes - packet_header_bytes
                                           : settings.max_payload_bytes;
        auto const expected_count = settings.enabled
                                        ? ceiling_divide(prepared.bytes.size(), expected_capacity)
                                        : std::uint64_t{1U};
        auto const expected_headers = settings.enabled ? expected_count * packet_header_bytes : 0U;
        auto const expected_total = expected_headers + prepared.bytes.size();
        if (prepared.report.outcome != GroupPreparationOutcome::Prepared ||
            prepared.group_id == 0U || prepared.bytes.empty() ||
            prepared.bytes.size() > settings.max_group_bytes ||
            (!settings.enabled && prepared.bytes.size() > settings.max_payload_bytes) ||
            prepared.chunk_payload_capacity != expected_capacity || expected_count == 0U ||
            expected_count > settings.max_chunks_per_group ||
            expected_count > std::numeric_limits<std::uint16_t>::max() ||
            prepared.chunk_count != expected_count || chunk_index >= prepared.chunk_count ||
            prepared.report.group_id != prepared.group_id ||
            prepared.report.group_bytes != prepared.bytes.size() ||
            prepared.report.chunk_count != expected_count ||
            prepared.report.total_header_bytes != expected_headers ||
            prepared.report.total_packet_bytes != expected_total)
        {
            throw std::runtime_error("prepared packet group metadata is invalid");
        }
        if (!settings.enabled)
        {
            return prepared.bytes;
        }

        auto const offset = static_cast<std::size_t>(chunk_index) * prepared.chunk_payload_capacity;
        auto const remaining = prepared.bytes.size() - offset;
        auto const chunk_bytes = std::min<std::size_t>(remaining, prepared.chunk_payload_capacity);
        serialization_scratch.clear();
        serialization_scratch.reserve(packet_header_bytes + chunk_bytes);
        simnet::append_big_endian(serialization_scratch, packet_magic);
        simnet::append_big_endian(serialization_scratch, packet_protocol_version);
        simnet::append_big_endian(serialization_scratch, packet_schema_version);
        simnet::append_byte(serialization_scratch, byte_group_chunk_kind);
        simnet::append_big_endian(serialization_scratch, prepared.group_id);
        simnet::append_big_endian(serialization_scratch, chunk_index);
        simnet::append_big_endian(serialization_scratch, prepared.chunk_count);
        simnet::append_big_endian(
            serialization_scratch,
            static_cast<std::uint32_t>(prepared.bytes.size())
        );
        simnet::append_big_endian(serialization_scratch, static_cast<std::uint32_t>(chunk_bytes));
        serialization_scratch.insert(
            serialization_scratch.end(),
            prepared.bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            prepared.bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_bytes)
        );
        return serialization_scratch;
    }

    void expire_incomplete_groups(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        Nanoseconds now
    ) noexcept
    {
        auto const before = state.incomplete.size();
        std::erase_if(
            state.incomplete,
            [&](IncompleteByteGroup const& group)
            {
                return now >= group.first_received_time &&
                       now - group.first_received_time >= settings.reassembly_timeout;
            }
        );
        state.report.expired_groups += before - state.incomplete.size();
        update_current_report(state);
    }

    ReassemblyResult accept_group_packet(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        ByteSpan packet,
        Nanoseconds now
    )
    {
        validate_packetization_settings(settings);
        ++state.report.received_chunks;
        expire_incomplete_groups(settings, state, now);

        if (!settings.enabled)
        {
            return accept_raw_group(settings, state, packet);
        }

        auto header = PacketHeader{};
        if (packet.size() < packet_header_bytes || !read_header(packet, header))
        {
            ++state.report.invalid_groups;
            return {
                .kind = ReassemblyResultKind::Invalid,
                .error = "packet is shorter than the complete header"
            };
        }
        if (!header_identity_is_valid(header))
        {
            ++state.report.invalid_groups;
            return {
                .kind = ReassemblyResultKind::Invalid,
                .error = "packet header identity or version is invalid"
            };
        }
        if (!header_fields_are_within_bounds(settings, header, packet.size()))
        {
            return reject_group(
                state,
                header.group_id,
                "packet header fields are outside configured bounds"
            );
        }
        if (header.group_id <= state.latest_committed_group)
        {
            ++state.report.stale_groups;
            return {.kind = ReassemblyResultKind::Stale, .group_id = header.group_id};
        }

        auto const capacity = settings.max_payload_bytes - packet_header_bytes;
        auto const expected_count = ceiling_divide(header.group_bytes, capacity);
        auto const expected_offset = static_cast<std::uint64_t>(header.chunk_index) * capacity;
        if (expected_count != header.chunk_count || expected_offset >= header.group_bytes)
        {
            return reject_group(
                state,
                header.group_id,
                "packet chunk count or offset is inconsistent"
            );
        }
        auto const expected_bytes = std::min<std::uint64_t>(
            capacity,
            static_cast<std::uint64_t>(header.group_bytes) - expected_offset
        );
        if (header.chunk_bytes != expected_bytes)
        {
            return reject_group(
                state,
                header.group_id,
                "packet chunk payload size is inconsistent"
            );
        }

        auto admission = admit_incomplete_group(settings, state, header, capacity, now);
        if (admission.group == nullptr)
        {
            return std::move(admission.rejection);
        }

        return accept_validated_chunk(
            state,
            *admission.group,
            header,
            packet.subspan(packet_header_bytes),
            static_cast<std::size_t>(expected_offset)
        );
    }

    void commit_reassembled_group(ReassemblyState& state, PacketGroupId group_id) noexcept
    {
        if (group_id <= state.latest_committed_group)
        {
            return;
        }
        state.latest_committed_group = group_id;
        std::erase_if(
            state.incomplete,
            [group_id](IncompleteByteGroup const& group)
            {
                return group.group_id < group_id;
            }
        );
        update_current_report(state);
    }

    void clear_reassembly_state(ReassemblyState& state) noexcept
    {
        state = {};
    }
}
