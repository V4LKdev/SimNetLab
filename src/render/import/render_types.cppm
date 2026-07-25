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
        Observer,
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
        ViewerCapabilities capabilities {};
        std::optional<RenderConnectionInfo> connection {};
        std::optional<RenderReplicationInfo> replication {};
        std::string_view status_message {};
    };

    /// Optional producer-owned facts for the currently selected entity.
    struct SelectedEntityDetails
    {
        EntityNetId id {};
        std::optional<Vec3f> velocity {};
        std::optional<Vec3f> acceleration {};
        std::optional<float> speed {};
        std::optional<Tick> last_update_tick {};
        std::optional<SequenceId> last_update_sequence {};
        std::optional<bool> replicated {};
    };

    /// Local debug observer supplied by the application for this draw call.
    struct ObserverView
    {
        Vec3f position {};
        Vec3f forward { .z = 1.0F };
        float interest_radius {};
        float vertical_fov_degrees { 60.0F };
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

    struct RenderFrame
    {
        RenderEntityView entities {};
        RenderFrameInfo info {};
        std::optional<SelectedEntityDetails> selected_details {};
        std::optional<ObserverView> observer {};
        std::optional<SpatialDebugView> spatial {};
    };

    struct PlayerViewInput
    {
        float move_forward {};
        float move_right {};
        float move_up {};
        float look_yaw {};
        float look_pitch {};
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
        float debug_observer_yaw_axis {};
        float debug_observer_pitch_axis {};
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
        float debug_observer_interest_radius { 150.0F };
        float debug_observer_vertical_fov_degrees { 60.0F };
        std::uint32_t max_visible_spatial_cells { 2048 };
        std::string entity_mesh_path {};
        std::string title { "SimNet" };
    };
}
