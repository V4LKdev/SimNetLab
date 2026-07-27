module;

#include <cstdint>
#include <flecs.h>
#include <memory>
#include <optional>
#include <string>

/// @brief Authoritative boid simulation runtime.
export module simnet.game_server:simulation;

import simnet.core;

export namespace simnet
{
    struct BoidSimulationSettings
    {
        float world_half { 400.0F };
        float cell_size { 20.0F };
        std::uint32_t max_neighbors { 64 };
        float min_speed { 6.0F };
        float cruise_speed { 8.0F };
        float max_speed { 10.0F };
        float max_acceleration { 12.0F };
        float perception_radius { 18.0F };
        float separation_radius { 3.6F };
        float field_of_view_degrees { 240.0F };
        float containment_prediction_seconds { 0.75F };
        float containment_margin { 22.5F };
        float separation_acceleration { 10.0F };
        float containment_acceleration { 9.0F };
        float alignment_acceleration { 3.0F };
        float cohesion_acceleration { 2.0F };
    };

    struct SelectedBoidDebug
    {
        EntityNetId id {};
        Vec3f velocity {};
        Vec3f acceleration {};
        float speed {};
        std::uint32_t candidate_count {};
        std::uint32_t separation_neighbor_count {};
        std::uint32_t alignment_neighbor_count {};
        std::uint32_t cohesion_neighbor_count {};
        Vec3f separation {};
        Vec3f alignment {};
        Vec3f cohesion {};
        Vec3f containment {};
    };

    struct ServerGameStepReport
    {
        bool valid { true };
        std::string error {};
        std::uint32_t entity_count {};
        std::uint32_t overlap_recovery_count {};
        std::uint32_t hard_wall_guard_count {};
        std::uint32_t neighbor_cap_hit_count {};
    };

    /// Owns non-replicated, preallocated data used by authoritative boid systems.
    ///
    /// The runtime must outlive the Flecs world to which it is registered.
    class ServerGameRuntime
    {
    public:
        struct Impl;

        explicit ServerGameRuntime(BoidSimulationSettings settings);
        ~ServerGameRuntime();

        ServerGameRuntime(ServerGameRuntime const&) = delete;
        ServerGameRuntime& operator=(ServerGameRuntime const&) = delete;
        ServerGameRuntime(ServerGameRuntime&&) = delete;
        ServerGameRuntime& operator=(ServerGameRuntime&&) = delete;

        void select_boid(std::optional<EntityNetId> id) noexcept;
        [[nodiscard]] std::optional<SelectedBoidDebug> selected_boid_debug() const noexcept;
        [[nodiscard]] ServerGameStepReport const& last_step_report() const noexcept;

    private:
        std::unique_ptr<Impl> impl_;

        friend void register_server_game(flecs::world&, ServerGameRuntime&);
        friend bool prepare_server_game_runtime(flecs::world&, ServerGameRuntime&);
    };

    /// Sizes all external buffers before world.progress() starts worker execution.
    [[nodiscard]] bool prepare_server_game_runtime(
        flecs::world& world,
        ServerGameRuntime& runtime
    );
}
