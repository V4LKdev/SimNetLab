module;

#include <array>
#include <cstddef>
#include <cstdint>

#include <raylib.h>

module simnet.render:ui;

import :types;

namespace simnet::render_detail
{
    struct SceneRect
    {
        int x{};
        int y{};
        int width{};
        int height{};
    };

    enum class InspectorPage : std::uint8_t
    {
        Overview,
        Network,
        Entity,
        Setup
    };
    enum class OpenPopover : std::uint8_t
    {
        None,
        Camera,
        Overlays,
        Help
    };
    enum class UiValueState : std::uint8_t
    {
        Normal,
        Muted,
        Success,
        Warning,
        Error
    };

    struct PanelRow
    {
        std::array<char, 36> label{};
        std::array<char, 112> value{};
        UiValueState state{UiValueState::Normal};
        bool full_width{};
    };

    struct PanelSection
    {
        std::array<char, 36> title{};
        std::uint16_t first_row{};
        std::uint16_t row_count{};
        bool collapsible{};
        bool expanded{true};
    };

    struct PanelModel
    {
        static constexpr std::size_t max_sections = 16;
        static constexpr std::size_t max_rows = 128;

        std::array<PanelSection, max_sections> sections{};
        std::array<PanelRow, max_rows> rows{};
        std::uint16_t section_count{};
        std::uint16_t row_count{};

        inline void clear() noexcept
        {
            section_count = 0;
            row_count = 0;
        }
    };

    struct OverlayState
    {
        bool world_bounds{true};
        bool origin_axes{};
        bool spatial_cells{};
        bool stationary_observer_marker{true};
        bool stationary_observer_radius{};
        bool stationary_observer_frustum{};
        bool selected_marker{true};
        bool rule_radii{};
        bool steering_vectors{true};
        bool queried_cells{};
        bool field_of_view{};
        bool selected_trail{true};
        bool debug_labels{};
    };

    inline constexpr float overlay_row_height = 36.0F;

    struct OverlayOption
    {
        char const* group;
        char const* label;
        bool* value;
        bool available;
    };

    [[nodiscard]] inline auto
    overlay_options(OverlayState& state, RenderFrame const& frame, bool has_selection) noexcept
    {
        return std::array{
            OverlayOption{"GENERAL", "World bounds", &state.world_bounds, true},
            OverlayOption{"GENERAL", "World origin axes", &state.origin_axes, true},
            OverlayOption{
                "GENERAL",
                "Spatial cells",
                &state.spatial_cells,
                frame.info.capabilities.has_spatial_visualization
            },
            OverlayOption{
                "STATIONARY OBSERVER",
                "Stationary observer marker",
                &state.stationary_observer_marker,
                frame.info.capabilities.has_stationary_observer
            },
            OverlayOption{
                "STATIONARY OBSERVER",
                "Stationary observer radius",
                &state.stationary_observer_radius,
                frame.info.capabilities.has_stationary_observer
            },
            OverlayOption{
                "STATIONARY OBSERVER",
                "Stationary observer frustum",
                &state.stationary_observer_frustum,
                frame.info.capabilities.has_stationary_observer
            },
            OverlayOption{"SELECTION", "Selected marker", &state.selected_marker, has_selection},
            OverlayOption{
                "SELECTION",
                "Rule radii",
                &state.rule_radii,
                has_selection && frame.info.capabilities.has_entity_diagnostics
            },
            OverlayOption{
                "SELECTION",
                "Steering vectors",
                &state.steering_vectors,
                has_selection && frame.info.capabilities.has_entity_diagnostics
            },
            OverlayOption{
                "SELECTION",
                "Queried cells",
                &state.queried_cells,
                has_selection && frame.info.capabilities.has_entity_diagnostics
            },
            OverlayOption{
                "SELECTION",
                "Field of view",
                &state.field_of_view,
                has_selection && frame.info.capabilities.has_entity_diagnostics
            },
            OverlayOption{
                "SELECTION",
                "Selected trail",
                &state.selected_trail,
                has_selection && frame.info.capabilities.has_selected_trail
            },
            OverlayOption{
                "SELECTION",
                "Debug labels",
                &state.debug_labels,
                has_selection && frame.info.capabilities.has_entity_diagnostics
            },
        };
    }

    struct CameraOption
    {
        char const* label;
        CameraMode mode;
        bool available;
    };

    [[nodiscard]] inline auto camera_options(RenderFrame const& frame, bool has_selection) noexcept
    {
        return std::array{
            CameraOption{"Overview orbit", CameraMode::OverviewOrbit, true},
            CameraOption{"Follow selected entity", CameraMode::EntityFollow, has_selection},
            CameraOption{
                "Stationary observer",
                CameraMode::StationaryObserver,
                frame.stationary_observer.has_value()
            },
            CameraOption{"Game camera", CameraMode::Game, frame.game_camera.has_value()},
        };
    }

    struct UiState
    {
        InspectorPage page{InspectorPage::Overview};
        std::array<float, 4> page_scroll{};
        OpenPopover popover{OpenPopover::None};
        std::array<std::uint32_t, 4> expanded_sections{0U, 0U, 0U, 3U};
        bool pointer_captured{};
    };

    struct UiPalette
    {
        Color window{13, 16, 20, 255};
        Color panel{20, 24, 30, 255};
        Color raised{26, 32, 40, 255};
        Color hover{32, 40, 51, 255};
        Color border{43, 52, 63, 255};
        Color divider{54, 65, 77, 255};
        Color primary{230, 233, 237, 255};
        Color secondary{164, 173, 184, 255};
        Color muted{111, 121, 133, 255};
        Color accent{90, 158, 207, 255};
        Color success{86, 168, 121, 255};
        Color warning{200, 157, 82, 255};
        Color error{199, 104, 104, 255};
        Color selection{216, 183, 90, 255};
    };

    inline constexpr UiPalette palette{};

    struct UiTypography
    {
        float application_title{24.0F};
        float context{15.0F};
        float page{16.0F};
        float section{15.0F};
        float body{16.0F};
        float secondary{14.0F};
        float toolbar{15.0F};
        float context_card{16.0F};
        float debug_label{14.0F};
    };

    inline constexpr UiTypography typography{};

    inline void draw_text(Font font, char const* value, Vector2 position, float size, Color color)
    {
        DrawTextEx(font, value, position, size, 0.75F, color);
    }

    struct ViewportUiLayout
    {
        std::array<Rectangle, 4> toolbar_buttons{};
        Rectangle popover{};
    };

    [[nodiscard]] inline Rectangle
    help_overlay_rect(SceneRect scene, RenderFrame const& frame, CameraMode mode) noexcept
    {
        auto lines = 7.0F;
        if (mode == CameraMode::OverviewOrbit)
        {
            lines += 4.0F;
        }
        else if (mode == CameraMode::EntityFollow)
        {
            lines += 5.0F;
        }
        else if (mode == CameraMode::Game && frame.game_camera.has_value())
        {
            lines += 2.0F;
        }
        else if (mode == CameraMode::StationaryObserver && frame.stationary_observer.has_value())
        {
            lines += 1.0F;
        }
        return {static_cast<float>(scene.x + 32), 70.0F, 540.0F, 56.0F + lines * 29.0F};
    }

    [[nodiscard]] inline ViewportUiLayout viewport_ui_layout(SceneRect scene) noexcept
    {
        auto constexpr button_width = 108.0F;
        auto constexpr button_height = 38.0F;
        auto constexpr gap = 6.0F;
        auto const right = static_cast<float>(scene.x + scene.width) - 14.0F;
        auto layout = ViewportUiLayout{};
        for (std::size_t index = 0; index < layout.toolbar_buttons.size(); ++index)
        {
            layout.toolbar_buttons[index] = {
                right - (button_width + gap) *
                            static_cast<float>(layout.toolbar_buttons.size() - index),
                14.0F,
                button_width,
                button_height,
            };
        }
        layout.popover = {right - 320.0F, 60.0F, 320.0F, 430.0F};
        return layout;
    }
} // namespace simnet::render_detail
