module;

#include <cstdint>
#include <string>
#include <vector>

/// @brief Bounded opaque byte-group packetization and reassembly.
export module simnet.packetization;

import simnet.core;

export namespace simnet
{
    using PacketGroupId = std::uint32_t;

    /// Fixed SNPK packet header byte count.
    inline constexpr std::uint32_t packet_header_bytes = 25U;
    /// Maximum configurable encoded group size for reassembled payloads.
    inline constexpr std::uint32_t maximum_packetized_group_bytes = 4U * 1024U * 1024U;
    /// Maximum supported chunk count per group.
    inline constexpr std::uint32_t maximum_chunks_per_group = 4096U;
    /// Default bound for concurrent in-flight groups.
    inline constexpr std::uint32_t maximum_in_flight_groups = 64U;
    /// Maximum retained incomplete-bytes budget before rejecting new groups.
    inline constexpr std::uint32_t maximum_incomplete_group_bytes = 8U * 1024U * 1024U;

    /// Packetization limits that own reassembly safety boundaries.
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

    /// Result of one group prep run before chunk serialization.
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

    /// Prepared reassembly payload ownership for chunk production.
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

    /// Finalized byte sequence and chunk metadata from a complete group.
    struct CompletedByteGroup
    {
        PacketGroupId group_id{};
        std::uint32_t chunk_count{};
        std::uint32_t total_packet_bytes{};
        std::vector<Byte> bytes{};
    };

    /// Reassembly outcome for one accepted packet.
    struct ReassemblyResult
    {
        ReassemblyResultKind kind{ReassemblyResultKind::Invalid};
        PacketGroupId group_id{};
        CompletedByteGroup completed{};
        std::string error{};
    };

    /// Live reassembly accounting for diagnostics and resource budget decisions.
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

    /// One incomplete group currently tracked by reassembly.
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

    /// Full reassembly owner for one peer or stream.
    struct ReassemblyState
    {
        PacketGroupId latest_committed_group{};
        std::vector<IncompleteByteGroup> incomplete{};
        ReassemblyReport report{};
    };

    /// Throws on invalid settings. Call before first use and before each operation.
    void validate_packetization_settings(PacketizationSettings const& settings);

    /// Builds a candidate packetized representation and reports failure without
    /// mutating output when preparation bounds are violated.
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

    /// Accepts and validates one packet, updates per-peer reassembly accounting, and
    /// returns only a temporary completed result on successful reconstruction.
    [[nodiscard]] ReassemblyResult accept_group_packet(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        ByteSpan packet,
        Nanoseconds now
    );

    /// Evicts timed-out incomplete groups and updates retention counters.
    void expire_incomplete_groups(
        PacketizationSettings const& settings,
        ReassemblyState& state,
        Nanoseconds now
    ) noexcept;

    /// Marks the last completed group canonical and removes older incomplete state.
    void commit_reassembled_group(ReassemblyState& state, PacketGroupId group_id) noexcept;

    /// Clears all reassembly state and report counters.
    void clear_reassembly_state(ReassemblyState& state) noexcept;
}
