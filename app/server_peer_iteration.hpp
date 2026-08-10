#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

namespace simnet::app::detail
{
    enum class PeerAdmission
    {
        Accept,
        Duplicate,
        Full,
    };

    template <typename PeerState, typename PeerId, typename Projection>
    [[nodiscard]] PeerAdmission peer_admission(
        std::vector<PeerState> const& peers,
        PeerId peer_id,
        std::size_t capacity,
        Projection projection
    )
    {
        auto const found = std::ranges::lower_bound(peers, peer_id, {}, projection);
        if (found != peers.end() && std::invoke(projection, *found) == peer_id)
        {
            return PeerAdmission::Duplicate;
        }
        return peers.size() >= capacity ? PeerAdmission::Full : PeerAdmission::Accept;
    }

    // Erasure keeps the next sorted peer at the current index.
    template <typename PeerState, typename IsJoined, typename Process, typename Remove>
    void process_sorted_peer_states(
        std::vector<PeerState>& peers,
        IsJoined is_joined,
        Process process,
        Remove remove
    )
    {
        auto index = std::size_t{};
        while (index < peers.size())
        {
            if (!is_joined(peers[index]) || process(peers[index]))
            {
                ++index;
                continue;
            }
            remove(peers[index]);
            peers.erase(
                peers.begin() + static_cast<typename std::vector<PeerState>::difference_type>(index)
            );
        }
    }
}
