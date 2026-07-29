#pragma once

namespace simnet::render_detail {
struct SceneRect {
  int x{};
  int y{};
  int width{};
  int height{};
};

enum class InspectorPage : std::uint8_t { Overview, Network, Entity };
enum class UiValueState : std::uint8_t {
  Normal,
  Muted,
  Success,
  Warning,
  Error
};

struct PanelRow {
  std::array<char, 36> label{};
  std::array<char, 112> value{};
  UiValueState state{UiValueState::Normal};
  bool full_width{};
};

struct PanelSection {
  std::array<char, 36> title{};
  std::uint16_t first_row{};
  std::uint16_t row_count{};
};

struct PanelModel {
  static constexpr std::size_t max_sections = 16;
  static constexpr std::size_t max_rows = 128;

  std::array<PanelSection, max_sections> sections{};
  std::array<PanelRow, max_rows> rows{};
  std::uint16_t section_count{};
  std::uint16_t row_count{};

  inline void clear() noexcept {
    section_count = 0;
    row_count = 0;
  }
};

struct OverlayState {
  bool world_bounds{true};
  bool origin_axes{true};
  bool spatial_cells{};
  bool observer_marker{true};
  bool observer_radius{};
  bool observer_frustum{};
  bool selected_marker{true};
  bool rule_radii{true};
  bool steering_vectors{true};
  bool queried_cells{};
  bool field_of_view{};
  bool selected_trail{true};
  bool debug_labels{};
};

struct UiState {
  InspectorPage page{InspectorPage::Overview};
  std::array<float, 3> page_scroll{};
  bool overlay_menu_open{};
  bool help_open{};
  bool pointer_captured{};
};

struct UiPalette {
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

struct UiTypography {
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

namespace icon {
inline constexpr char play[] = "\uf04b";
inline constexpr char pause[] = "\uf04c";
inline constexpr char camera[] = "\uf030";
inline constexpr char overlays[] = "\uf03a";
inline constexpr char help[] = "\uf059";
inline constexpr char overview[] = "\uf201";
inline constexpr char network[] = "\uf1eb";
inline constexpr char entity[] = "\uf1b2";
inline constexpr char view[] = "\uf06e";
inline constexpr char controls[] = "\uf11c";
} // namespace icon

struct ViewportUiLayout {
  std::array<Rectangle, 4> toolbar_buttons{};
  Rectangle popover{};
};

[[nodiscard]] inline ViewportUiLayout
viewport_ui_layout(SceneRect scene) noexcept {
  auto constexpr button_width = 108.0F;
  auto constexpr button_height = 38.0F;
  auto constexpr gap = 6.0F;
  auto const right = static_cast<float>(scene.x + scene.width) - 14.0F;
  auto layout = ViewportUiLayout{};
  for (std::size_t index = 0; index < layout.toolbar_buttons.size(); ++index) {
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
