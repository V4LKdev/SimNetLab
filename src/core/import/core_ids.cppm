module;

#include <cstdint>

/// @brief Core network identifier types.
export module simnet.core:ids;

export namespace simnet
{
    /// Network-visible entity identifier.
    using EntityNetId = std::uint32_t;

    /// Transport peer identifier.
    using PeerId = std::uint16_t;

    /// Packet or message sequence identifier.
    using SequenceId = std::uint32_t;
}
