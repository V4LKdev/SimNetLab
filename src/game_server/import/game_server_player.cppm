module;

#include <flecs.h>

/// @brief Authoritative session-owned player lifecycle and semantic input.
export module simnet.game_server:player;

import simnet.core;

export namespace simnet
{
    struct PlayerControlState
    {
        bool pitch_up{};
        bool yaw_left{};
        bool pitch_down{};
        bool yaw_right{};
        bool accelerate{};
        bool decelerate{};
        bool left_mouse{};
        bool right_mouse{};
    };

    /// Spawns an authoritative player. Returns zero on failure.
    [[nodiscard]] EntityNetId spawn_authoritative_player(flecs::world& world);

    /// Replaces the latest-state input owned by the authoritative player.
    [[nodiscard]] bool
    set_authoritative_player_input(flecs::world& world, EntityNetId id, PlayerControlState input);

    /// Deletes the session-owned player and its replication index entry.
    [[nodiscard]] bool delete_authoritative_player(flecs::world& world, EntityNetId id);
}
