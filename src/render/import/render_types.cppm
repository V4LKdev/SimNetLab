module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// @brief Generic visualization data contracts.
export module simnet.render:types;

import simnet.core;

export namespace simnet
{
    enum class ViewMode : std::uint8_t
    {
        Overview,
        EntityDetail,
        StationaryObserver,
        Game
    };

    struct RenderEntityView
    {
        std::span<const EntityNetId> ids {};
        std::span<const Vec3f> positions {};
        std::span<const Vec3f> headings {};
        std::span<const std::uint8_t> hues {};

        [[nodiscard]] bool valid() const noexcept
        {
            return ids.size() == positions.size()
                && ids.size() == headings.size()
                && ids.size() == hues.size();
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return ids.size();
        }
    };

    struct ViewerCapabilities
    {
        bool can_pause_simulation {};
    };

    /// Presentation-only interpolation facts supplied by the application.
    struct RenderInterpolationInfo
    {
        bool enabled {};
        bool interpolating {};
        Tick from_tick {};
        Tick to_tick {};
        double alpha { 1.0 };
    };

    /// Non-owning application connection facts valid for the duration of Viewer::draw().
    struct RenderConnectionInfo
    {
        std::string_view state {};
        std::optional<PeerId> peer {};
    };

    /// Optional replication facts supplied by an application that owns them.
    struct RenderReplicationInfo
    {
        std::optional<SequenceId> latest_emitted_sequence {};
        std::optional<SequenceId> latest_received_sequence {};
        std::optional<SequenceId> latest_applied_sequence {};
        std::optional<SequenceId> acknowledged_baseline_sequence {};
        std::optional<Tick> latest_snapshot_tick {};
        std::optional<std::uint32_t> retained_snapshot_count {};
        std::optional<SequenceId> oldest_retained_sequence {};
        std::optional<SequenceId> newest_retained_sequence {};
    };

    struct RenderFrameInfo
    {
        Tick tick {};
        std::optional<SequenceId> snapshot_sequence {};
        std::optional<bool> session_ready {};
        Aabb3f world_bounds {};
        NS frame_delta {};
        std::optional<double> fixed_tick_rate_hz {};
        std::optional<bool> simulation_paused {};
        std::optional<RenderInterpolationInfo> interpolation {};
        ViewerCapabilities capabilities {};
        std::optional<RenderConnectionInfo> connection {};
        std::optional<RenderReplicationInfo> replication {};
        std::string_view status_message {};
    };

    /// Producer-provided integer cell coordinate for selected-entity diagnostics.
    struct SelectedCellCoord
    {
        std::int32_t x {};
        std::int32_t y {};
        std::int32_t z {};
    };

    /// Optional producer-owned facts for the currently selected entity.
    struct SelectedEntityDetails
    {
        EntityNetId id {};
        std::optional<Vec3f> velocity {};
        std::optional<Vec3f> acceleration {};
        std::optional<float> speed {};
        std::optional<std::uint32_t> raw_candidate_count {};
        std::optional<std::uint32_t> retained_neighbor_count {};
        std::optional<std::uint32_t> separation_neighbor_count {};
        std::optional<std::uint32_t> alignment_neighbor_count {};
        std::optional<std::uint32_t> cohesion_neighbor_count {};
        std::optional<std::uint32_t> hue_neighbor_count {};
        std::optional<SelectedCellCoord> current_cell {};
        std::optional<std::uint32_t> queried_cell_count {};
        std::optional<float> separation_radius {};
        std::optional<float> alignment_radius {};
        std::optional<float> cohesion_radius {};
        std::optional<float> query_radius {};
        std::optional<float> field_of_view_degrees {};
        std::optional<std::uint32_t> maximum_neighbors {};
        std::optional<bool> neighbor_cap_hit {};
        std::optional<bool> overlap_recovery {};
        std::optional<bool> acceleration_saturated {};
        std::optional<bool> wall_guard {};
        std::optional<bool> wander_active {};
        std::optional<bool> hue_assimilation_active {};
        std::optional<bool> hue_drift_active {};
        std::optional<Vec3f> separation {};
        std::optional<Vec3f> alignment {};
        std::optional<Vec3f> cohesion {};
        std::optional<Vec3f> containment {};
        std::optional<Vec3f> wander {};
        std::optional<float> current_hue {};
        std::optional<float> hue_target {};
        std::optional<float> hue_delta {};
        std::optional<float> applied_hue_step {};
        std::optional<Tick> last_update_tick {};
        std::optional<SequenceId> last_update_sequence {};
        std::optional<bool> replicated {};
    };

    /// Local stationary interest/debug view supplied by the application.
    struct StationaryObserverView
    {
        Vec3f position {};
        Vec3f forward { .z = 1.0F };
        float interest_radius {};
        float vertical_fov_degrees { 60.0F };
    };

    /// Application-resolved camera pose. The viewer does not infer player semantics.
    struct GameCameraView
    {
        Vec3f position {};
        Vec3f target { .z = 1.0F };
        Vec3f up { .y = 1.0F };
        float vertical_fov_degrees { 70.0F };
    };

    /// One non-owning occupied-cell view supplied by an application.
    struct SpatialCellView
    {
        Aabb3f bounds {};
        std::uint32_t entity_count {};
    };

    /// Bounded spatial debug data valid for the duration of Viewer::draw().
    struct SpatialDebugView
    {
        std::span<const SpatialCellView> cells {};
        std::uint32_t occupied_cell_count {};
        std::uint32_t max_cell_occupancy {};
        float average_occupied_cell_load {};
        bool display_capped {};
    };

    struct DebugColor
    {
        std::uint8_t red { 255U };
        std::uint8_t green { 255U };
        std::uint8_t blue { 255U };
        std::uint8_t alpha { 255U };
    };

    struct DebugSphereView
    {
        Vec3f center {};
        float radius {};
        DebugColor color {};
        std::string_view label {};
    };

    struct DebugVectorView
    {
        Vec3f origin {};
        Vec3f vector {};
        DebugColor color {};
        std::string_view label {};
    };

    struct DebugBoxView
    {
        Aabb3f bounds {};
        DebugColor color {};
        std::string_view label {};
    };

    struct DebugConeView
    {
        Vec3f apex {};
        Vec3f direction { .z = 1.0F };
        float length {};
        float half_angle_degrees {};
        DebugColor color {};
        std::string_view label {};
    };

    /// Producer-resolved debug geometry valid for the duration of Viewer::draw().
    struct DebugPrimitiveView
    {
        std::span<const DebugSphereView> spheres {};
        std::span<const DebugVectorView> vectors {};
        std::span<const DebugBoxView> boxes {};
        std::span<const DebugConeView> cones {};

        [[nodiscard]] bool empty() const noexcept
        {
            return spheres.empty() && vectors.empty() && boxes.empty() && cones.empty();
        }
    };

    struct RenderFrame
    {
        RenderEntityView entities {};
        RenderFrameInfo info {};
        std::optional<SelectedEntityDetails> selected_details {};
        std::optional<StationaryObserverView> stationary_observer {};
        std::optional<GameCameraView> game_camera {};
        std::optional<SpatialDebugView> spatial {};
        DebugPrimitiveView debug_primitives {};
    };

    struct PlayerViewInput
    {
        bool pitch_up {};
        bool yaw_left {};
        bool pitch_down {};
        bool yaw_right {};
        bool accelerate {};
        bool decelerate {};
        bool left_mouse {};
        bool right_mouse {};
    };

    struct RenderStats
    {
        NS input_cpu_time {};
        NS preparation_cpu_time {};
        NS scene_submit_cpu_time {};
        NS panel_cpu_time {};
        std::uint32_t instance_count {};
        std::uint32_t skipped_entity_count {};
        std::uint32_t draw_calls {};
        std::uint32_t active_hue_buckets {};
    };

    struct ViewerResult
    {
        bool close_requested {};
        bool toggle_simulation_pause_requested {};
        ViewMode view_mode { ViewMode::Overview };
        std::optional<EntityNetId> selected_entity {};
        bool selected_entity_changed {};
        float stationary_observer_yaw_axis {};
        float stationary_observer_pitch_axis {};
        PlayerViewInput player_input {};
        RenderStats stats {};
    };

    struct ViewerConfig
    {
        std::uint32_t window_width { 1800 };
        std::uint32_t window_height { 1080 };
        std::uint32_t panel_width { 360 };
        std::uint32_t target_frame_rate { 60 };
        float entity_scale { 1.0F };
        float picking_radius { 1.0F };
        float stationary_observer_interest_radius { 150.0F };
        float stationary_observer_vertical_fov_degrees { 60.0F };
        std::uint32_t max_visible_spatial_cells { 2048 };
        std::string entity_mesh_path {};
        std::string title { "SimNet" };
    };
}
