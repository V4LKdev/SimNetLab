module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @brief Bounded opaque byte-group packetization and reassembly.
export module simnet.packetization;

import simnet.core;

export namespace simnet
{
    using PacketGroupId = std::uint32_t;

    inline constexpr std::uint32_t packet_header_bytes = 25U;
    inline constexpr std::uint32_t maximum_packetized_group_bytes = 4U * 1024U * 1024U;
    inline constexpr std::uint32_t maximum_chunks_per_group = 4096U;
    inline constexpr std::uint32_t maximum_in_flight_groups = 64U;
    inline constexpr std::uint32_t maximum_incomplete_group_bytes = 8U * 1024U * 1024U;

    struct PacketizationSettings
    {
        bool enabled{true};
        std::uint32_t max_payload_bytes{1200U};
        std::uint32_t max_group_bytes{maximum_packetized_group_bytes};
        std::uint32_t max_chunks_per_group{maximum_chunks_per_group};
        std::uint32_t max_in_flight_groups{4U};
        std::uint32_t max_incomplete_bytes{maximum_incomplete_group_bytes};
        Nanoseconds reassembly_timeout{5'000'000'000};
    };

    enum class GroupPreparationOutcome : std::uint8_t
    {
        Prepared,
        Rejected
    };

    struct PacketizationReport
    {
        PacketGroupId group_id{};
        std::uint32_t group_bytes{};
        std::uint32_t chunk_count{};
        std::uint32_t total_header_bytes{};
        std::uint32_t total_packet_bytes{};
        GroupPreparationOutcome outcome{GroupPreparationOutcome::Rejected};
        std::string error{};
    };

    struct PreparedByteGroup
    {
        PacketGroupId group_id{};
        std::vector<Byte> bytes{};
        std::uint32_t chunk_payload_capacity{};
        std::uint16_t chunk_count{};
        PacketizationReport report{};
    };

    enum class ReassemblyResultKind : std::uint8_t
    {
        Incomplete,
        Complete,
        Duplicate,
        Stale,
        Invalid,
        LimitExceeded
    };

    struct CompletedByteGroup
    {
        PacketGroupId group_id{};
        std::uint32_t chunk_count{};
        std::uint32_t total_packet_bytes{};
        std::vector<Byte> bytes{};
    };

    struct ReassemblyResult
    {
        ReassemblyResultKind kind{ReassemblyResultKind::Invalid};
        CompletedByteGroup completed{};
        std::string error{};
    };

    struct ReassemblyReport
    {
        std::uint64_t received_chunks{};
        std::uint64_t unique_chunks{};
        std::uint64_t duplicate_chunks{};
        std::uint32_t incomplete_groups{};
        std::uint32_t retained_incomplete_bytes{};
        std::uint32_t latest_completed_group_bytes{};
        std::uint64_t completed_groups{};
        std::uint64_t expired_groups{};
        std::uint64_t invalid_groups{};
        std::uint64_t stale_groups{};
    };

    struct IncompleteByteGroup
    {
        PacketGroupId group_id{};
        std::uint32_t group_bytes{};
        std::uint32_t chunk_payload_capacity{};
        std::uint16_t chunk_count{};
        std::uint16_t received_count{};
        Nanoseconds first_received_time{};
        std::vector<Byte> bytes{};
        std::vector<std::uint64_t> received_chunks{};
    };

    struct ReassemblyState
    {
        PacketGroupId latest_committed_group{};
        std::vector<IncompleteByteGroup> incomplete{};
        ReassemblyReport report{};
    };

    void validate_packetization_settings(PacketizationSettings const& settings);

    [[nodiscard]] PacketizationReport prepare_byte_group(
        PacketizationSettings const& settings,
        PacketGroupId group_id,
        std::vector<Byte> bytes,
        PreparedByteGroup& prepared
    );

    /// Returns a view valid until `prepared` or `serialization_scratch` is changed.
    [[nodiscard]] ByteSpan serialize_group_chunk(
        PacketizationSettings const& settings,
        PreparedByteGroup const& prepared,
        std::uint16_t chunk_index,
        std::vector<Byte>& serialization_scratch
    );

    [[nodiscard]] ReassemblyResult accept_group_packet(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        std::vector<Byte> packet,
        Nanoseconds now
    );

    void expire_incomplete_groups(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        Nanoseconds now
    ) noexcept;

    /// Marks a group canonical and removes older incomplete groups.
    void commit_reassembled_group(ReassemblyState& state, PacketGroupId group_id) noexcept;

    void clear_reassembly_state(ReassemblyState& state) noexcept;
}
