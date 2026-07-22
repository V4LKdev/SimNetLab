module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

/// @brief Generic visualization data contracts.
export module simnet.render:types;

import simnet.core;

export namespace simnet
{
    enum class ViewMode : std::uint8_t
    {
        Overview,
        EntityDetail,
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
    };

    struct RenderFrame
    {
        RenderEntityView entities {};
        RenderFrameInfo info {};
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
        std::string title { "SimNet" };
    };
}
