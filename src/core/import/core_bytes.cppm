module;

#include <cstddef>
#include <cstdint>
#include <span>

/// @brief Core byte and network identifier types.
export module simnet.core:bytes;

export namespace simnet
{
    /// Network-visible entity identifier.
    using EntityNetId = std::uint32_t;

    /// Transport peer identifier.
    using PeerId = std::uint16_t;

    /// Packet or message sequence identifier.
    using SequenceId = std::uint32_t;

    /// Raw byte value.
    using Byte = std::byte;

    /// Immutable contiguous byte view.
    using ByteSpan = std::span<const Byte>;

    /// Mutable contiguous byte view.
    using MutableByteSpan = std::span<Byte>;
}
