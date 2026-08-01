module;

#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// @brief Authoritative Flecs world snapshot extraction.
export module simnet.game_server:snapshot;

import simnet.core;
import simnet.game_shared;
import simnet.snapshot;
import :simulation;

export namespace simnet
{
    /// Raw facts reported by authoritative world snapshot extraction.
    struct ServerSnapshotExtractionReport
    {
        Tick tick{};
        std::uint32_t entity_count{};
        bool valid{true};
        std::string error{};
    };

    enum class AuthoritativeSpawnError : std::uint8_t
    {
        None,
        CountOutOfRange,
        ZeroId,
        NonAscendingIds,
        ExistingIdOverlap,
        InvalidBoidState,
        InvalidIndexState,
        FlecsBulkInsertFailed
    };

    struct AuthoritativeSpawnReport
    {
        std::size_t requested_count{};
        std::size_t spawned_count{};
        AuthoritativeSpawnError error{AuthoritativeSpawnError::None};
        std::optional<std::size_t> failing_index{};

        [[nodiscard]] bool success() const noexcept
        {
            return error == AuthoritativeSpawnError::None;
        }
    };

    [[nodiscard]] std::string_view
    authoritative_spawn_error_name(AuthoritativeSpawnError error) noexcept;

    /// Registers authoritative components, queries, and simulation systems.
    void register_server_game(flecs::world& world, ServerGameRuntime& runtime);

    /// Creates or updates one authoritative boid entity by EntityNetId.
    [[nodiscard]] flecs::entity
    upsert_authoritative_boid(flecs::world& world, EntityState const& boid);

    /// Appends a validated ascending batch of newly generated authoritative boids.
    /// Call on the owning thread outside Flecs iteration and deferred mutation contexts.
    /// Active observers must not create or delete boids during this operation.
    [[nodiscard]] AuthoritativeSpawnReport
    append_authoritative_boids(flecs::world& world, std::span<const EntityState> boids);

    /// Deletes one authoritative boid entity by EntityNetId.
    [[nodiscard]] bool delete_authoritative_boid(flecs::world& world, EntityNetId id);

    /// Returns the indexed authoritative boid count without scanning the world.
    [[nodiscard]] std::size_t authoritative_boid_count(flecs::world const& world) noexcept;

    /// Extracts a sorted validated WorldSnapshot without mutating the Flecs world.
    /// On failure, out_snapshot is cleared and its tick is set to the requested tick.
    [[nodiscard]] ServerSnapshotExtractionReport
    extract_world_snapshot(flecs::world const& world, Tick tick, WorldSnapshot& out_snapshot);
}
