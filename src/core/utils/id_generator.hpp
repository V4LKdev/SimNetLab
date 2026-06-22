#pragma once

#include <atomic>
#include <cstdint>

namespace simnet::core::utils {
    namespace detail {
        inline std::atomic<std::uint32_t> &next_network_id() noexcept
        {
            static std::atomic<std::uint32_t> val{1};
            return val;
        }
    }

    /// Generates a unique, monotonically increasing network ID
    inline std::uint32_t generate_network_id() noexcept
    {
        return detail::next_network_id().fetch_add(1, std::memory_order_relaxed);
    }

    /// Returns current id without incrementing
    inline std::uint32_t get_current_id() noexcept
    {
        return detail::next_network_id().load(std::memory_order_relaxed);
    }

    /// Resets the generator to a given starting value
    inline void reset_network_id(std::uint32_t start = 1) noexcept
    {
        detail::next_network_id().store(start, std::memory_order_relaxed);
    }
}
