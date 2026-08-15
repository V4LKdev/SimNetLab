#ifndef SIMNET_APP_SERVER_PEER_ITERATION_HPP_INCLUDED
#define SIMNET_APP_SERVER_PEER_ITERATION_HPP_INCLUDED

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

// Header-defined templates keep production and focused peer-iteration tests on one contract.
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
        auto peer_index = std::size_t{};
        while (peer_index < peers.size())
        {
            if (!is_joined(peers[peer_index]) || process(peers[peer_index]))
            {
                ++peer_index;
                continue;
            }
            remove(peers[peer_index]);
            peers.erase(
                peers.begin() +
                static_cast<typename std::vector<PeerState>::difference_type>(peer_index)
            );
        }
    }
}

#endif // SIMNET_APP_SERVER_PEER_ITERATION_HPP_INCLUDED
