#pragma once

#include <cstdint>

namespace simnet::core::utils {
    namespace detail {
        inline std::uint32_t &next_network_id() noexcept
        {
            static std::uint32_t val = 1;
            return val;
        }
    }

    /// Generates a unique, monotonically increasing network ID.
    inline std::uint32_t generate_network_id() noexcept
    {
        return detail::next_network_id()++;
    }

    /// Resets the generator to a given starting value.
    inline void reset_network_id(std::uint32_t start = 1) noexcept
    {
        detail::next_network_id() = start;
    }
}
