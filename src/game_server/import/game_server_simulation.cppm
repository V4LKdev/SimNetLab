module;

#include <cstdint>
#include <flecs.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// @brief Authoritative boid simulation runtime.
export module simnet.game_server:simulation;

import simnet.core;
import simnet.spatial;

export namespace simnet
{
    struct BoidSimulationSettings
    {
        std::uint64_t seed { 12345 };
        float world_half { 400.0F };
        float cell_size { 20.0F };
        std::uint32_t max_neighbors { 64 };
        bool enable_separation { true };
        bool enable_alignment { true };
        bool enable_cohesion { true };
        bool enable_containment { true };
        bool enable_wander { true };
        bool enable_hue_assimilation { true };
        bool enable_hue_drift { true };
        float min_speed { 6.0F };
        float cruise_speed { 8.0F };
        float max_speed { 10.0F };
        float max_acceleration { 12.0F };
        float separation_radius { 3.6F };
        float alignment_radius { 18.0F };
        float cohesion_radius { 18.0F };
        float field_of_view_degrees { 240.0F };
        float containment_prediction_seconds { 0.75F };
        float containment_margin { 22.5F };
        float separation_acceleration { 10.0F };
        float containment_acceleration { 9.0F };
        float alignment_acceleration { 3.0F };
        float cohesion_acceleration { 2.0F };
        float wander_acceleration { 0.35F };
        float wander_frequency_hz { 0.35F };
        float hue_assimilation_rate { 0.25F };
        float hue_drift_rate { 0.02F };
    };

    struct PlayerMovementSettings
    {
        float world_half { 400.0F };
        float cruise_speed { 8.0F };
        float boost_speed { 14.0F };
        float slow_speed { 3.0F };
        float speed_change_rate { 12.0F };
        float yaw_rate_degrees { 90.0F };
        float pitch_rate_degrees { 75.0F };
        float pitch_limit_degrees { 80.0F };
    };

    struct SelectedBoidDebug
    {
        EntityNetId id {};
        Vec3f velocity {};
        Vec3f acceleration {};
        float speed {};
        std::uint32_t raw_candidate_count {};
        std::uint32_t retained_neighbor_count {};
        std::uint32_t separation_neighbor_count {};
        std::uint32_t alignment_neighbor_count {};
        std::uint32_t cohesion_neighbor_count {};
        std::uint32_t hue_neighbor_count {};
        CellCoord current_cell {};
        std::vector<Aabb3f> queried_cell_bounds {};
        float separation_radius {};
        float alignment_radius {};
        float cohesion_radius {};
        float query_radius {};
        float field_of_view_degrees {};
        std::uint32_t maximum_neighbors {};
        bool neighbor_cap_hit {};
        bool overlap_recovery {};
        bool acceleration_saturated {};
        bool wall_guard {};
        bool wander_active {};
        bool hue_assimilation_active {};
        bool hue_drift_active {};
        Vec3f separation {};
        Vec3f alignment {};
        Vec3f cohesion {};
        Vec3f containment {};
        Vec3f wander {};
        float current_hue {};
        float hue_target {};
        float hue_delta {};
        float applied_hue_step {};
    };

    struct ServerGameStepReport
    {
        struct PhaseTimings
        {
            double capture_ms {};
            double grid_ms {};
            double compute_ms {};
            double validate_ms {};
            double commit_ms {};
            double progress_ms {};
        };

        struct Diagnostics
        {
            SpatialGridStats grid {};
            double raw_candidates_mean {};
            std::uint32_t raw_candidates_max {};
            double retained_neighbors_mean {};
            std::uint32_t retained_neighbors_max {};
            std::uint32_t neighbor_cap_hit_count {};
            double separation_neighbors_mean {};
            double social_neighbors_mean {};
            std::uint32_t isolated_boid_count {};
            double nearest_neighbor_distance_mean {};
            double speed_mean {};
            float speed_min {};
            float speed_max {};
            double acceleration_mean {};
            float acceleration_max {};
            std::uint32_t acceleration_saturation_count {};
            std::uint32_t overlap_recovery_count {};
            std::uint32_t hard_wall_guard_count {};
            float polarization {};
        };

        bool valid { true };
        std::string error {};
        std::uint32_t entity_count {};
        std::uint32_t overlap_recovery_count {};
        std::uint32_t hard_wall_guard_count {};
        std::uint32_t neighbor_cap_hit_count {};
        Diagnostics diagnostics {};
        PhaseTimings phases {};
    };

    /// Owns non-replicated, preallocated data used by authoritative boid systems.
    ///
    /// The runtime must outlive the Flecs world to which it is registered.
    class ServerGameRuntime
    {
    public:
        struct Impl;

        explicit ServerGameRuntime(
            BoidSimulationSettings settings,
            PlayerMovementSettings player_settings = {}
        );
        ~ServerGameRuntime();

        ServerGameRuntime(ServerGameRuntime const&) = delete;
        ServerGameRuntime& operator=(ServerGameRuntime const&) = delete;
        ServerGameRuntime(ServerGameRuntime&&) = delete;
        ServerGameRuntime& operator=(ServerGameRuntime&&) = delete;

        void select_boid(std::optional<EntityNetId> id);
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
