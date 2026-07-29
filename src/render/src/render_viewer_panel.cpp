module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <vector>

#include <raylib.h>

module simnet.render;

import simnet.core;

#include "../private/render_viewer_impl.hpp"

namespace simnet {
namespace {
using namespace render_detail;

constexpr float panel_content_top = 126.0F;
constexpr float panel_footer_height = 50.0F;
constexpr float overlay_row_height = 36.0F;

Color value_color(UiValueState state) noexcept {
  switch (state) {
  case UiValueState::Muted:
    return palette.muted;
  case UiValueState::Success:
    return palette.success;
  case UiValueState::Warning:
    return palette.warning;
  case UiValueState::Error:
    return palette.error;
  case UiValueState::Normal:
    return palette.primary;
  }
  return palette.primary;
}

std::size_t page_index(InspectorPage page) noexcept {
  return static_cast<std::size_t>(page);
}

struct OverlayOption {
  char const *label;
  bool *value;
  bool visible;
};

auto overlay_options(OverlayState &state, RenderFrame const &frame,
                     bool has_selection) noexcept {
  return std::array{
      OverlayOption{"World bounds", &state.world_bounds, true},
      OverlayOption{"Origin axes", &state.origin_axes, true},
      OverlayOption{"Spatial cells", &state.spatial_cells,
                    frame.info.capabilities.has_spatial_visualization},
      OverlayOption{"Observer marker", &state.observer_marker,
                    frame.info.capabilities.has_stationary_observer},
      OverlayOption{"Observer radius", &state.observer_radius,
                    frame.info.capabilities.has_stationary_observer},
      OverlayOption{"Observer frustum", &state.observer_frustum,
                    frame.info.capabilities.has_stationary_observer},
      OverlayOption{"Selected marker", &state.selected_marker,
                    has_selection &&
                        frame.info.capabilities.has_selected_trail},
      OverlayOption{"Rule radii", &state.rule_radii,
                    has_selection &&
                        frame.info.capabilities.has_entity_diagnostics},
      OverlayOption{"Steering vectors", &state.steering_vectors,
                    has_selection &&
                        frame.info.capabilities.has_entity_diagnostics},
      OverlayOption{"Queried cells", &state.queried_cells,
                    has_selection &&
                        frame.info.capabilities.has_entity_diagnostics},
      OverlayOption{"Field of view", &state.field_of_view,
                    has_selection &&
                        frame.info.capabilities.has_entity_diagnostics},
      OverlayOption{"Selected trail", &state.selected_trail,
                    has_selection &&
                        frame.info.capabilities.has_selected_trail},
      OverlayOption{"Debug labels", &state.debug_labels,
                    has_selection &&
                        frame.info.capabilities.has_entity_diagnostics},
  };
}

template <typename Options>
void fit_popover(Rectangle &popover, Options const &options) noexcept {
  auto visible_count = std::size_t{};
  for (auto const &option : options) {
    visible_count += option.visible ? 1U : 0U;
  }
  popover.height = 68.0F +
                   static_cast<float>(visible_count) * overlay_row_height;
}

Rectangle inspector_tab(std::size_t index, float panel_width) noexcept {
  auto constexpr margin = 12.0F;
  auto constexpr gap = 4.0F;
  auto const width = (panel_width - margin * 2.0F - gap * 2.0F) / 3.0F;
  return {margin + static_cast<float>(index) * (width + gap), 78.0F, width,
          36.0F};
}

void draw_text(Font font, char const *value, Vector2 position, float size,
               Color color) {
  DrawTextEx(font, value, position, size, 0.75F, color);
}
} // namespace

void Viewer::Impl::update_panel_input(RenderFrame const &frame,
                                      ViewerResult &result) {
  ui_.pointer_captured = false;
  if (IsKeyPressed(KEY_F1)) {
    ui_.page = InspectorPage::Overview;
  } else if (IsKeyPressed(KEY_F2)) {
    ui_.page = InspectorPage::Network;
  } else if (IsKeyPressed(KEY_F3)) {
    ui_.page = InspectorPage::Entity;
  }
  if (IsKeyPressed(KEY_F12)) {
    ui_.help_open = !ui_.help_open;
  }
  if (IsKeyPressed(KEY_M)) {
    ui_.overlay_menu_open = !ui_.overlay_menu_open;
  }
  if (IsKeyPressed(KEY_P) && frame.info.capabilities.can_pause_simulation) {
    result.toggle_simulation_pause_requested = true;
  }
  if (IsKeyPressed(KEY_ESCAPE)) {
    if (ui_.overlay_menu_open) {
      ui_.overlay_menu_open = false;
    } else if (ui_.help_open) {
      ui_.help_open = false;
    } else if (selected_entity_.has_value()) {
      clear_selection(result);
      ui_.page = InspectorPage::Overview;
    }
  }

  auto const mouse = GetMousePosition();
  if (mouse.x < static_cast<float>(scene_rect_.x)) {
    ui_.pointer_captured = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      for (std::size_t index = 0; index < 3U; ++index) {
        if (CheckCollisionPointRec(
                mouse,
                inspector_tab(index, static_cast<float>(scene_rect_.x)))) {
          ui_.page = static_cast<InspectorPage>(index);
        }
      }
    }
    auto const wheel = GetMouseWheelMove();
    if (wheel != 0.0F && mouse.y >= panel_content_top &&
        mouse.y < static_cast<float>(config_.window_height) -
                      panel_footer_height) {
      auto &scroll = ui_.page_scroll[page_index(ui_.page)];
      scroll = std::max(0.0F, scroll - wheel * 52.0F);
    }
  }

  auto layout = viewport_ui_layout(scene_rect_);
  auto options =
      overlay_options(overlays_, frame, selected_entity_.has_value());
  fit_popover(layout.popover, options);
  for (auto const rect : layout.toolbar_buttons) {
    if (CheckCollisionPointRec(mouse, rect)) {
      ui_.pointer_captured = true;
    }
  }
  if (ui_.overlay_menu_open && CheckCollisionPointRec(mouse, layout.popover)) {
    ui_.pointer_captured = true;
  }
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return;
  }
  if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[0]) &&
      frame.info.capabilities.can_pause_simulation) {
    result.toggle_simulation_pause_requested = true;
  } else if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[1])) {
    cycle_camera(frame);
  } else if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[2])) {
    ui_.overlay_menu_open = !ui_.overlay_menu_open;
  } else if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[3])) {
    ui_.help_open = !ui_.help_open;
  } else if (ui_.overlay_menu_open &&
             CheckCollisionPointRec(mouse, layout.popover)) {
    auto row_y = layout.popover.y + 56.0F;
    for (auto &option : options) {
      if (!option.visible) {
        continue;
      }
      auto const rect = Rectangle{layout.popover.x + 12.0F, row_y,
                                  layout.popover.width - 24.0F,
                                  overlay_row_height - 2.0F};
      if (CheckCollisionPointRec(mouse, rect)) {
        *option.value = !*option.value;
        break;
      }
      row_y += overlay_row_height;
    }
  } else if (ui_.overlay_menu_open) {
    ui_.overlay_menu_open = false;
  }
}

void Viewer::Impl::draw_panel(RenderFrame const &frame, bool valid_entities,
                              RenderStats const &, ViewerResult const &) {
  build_panel_model(frame, valid_entities);
  auto const width = static_cast<float>(scene_rect_.x);
  DrawRectangle(0, 0, scene_rect_.x, static_cast<int>(config_.window_height),
                palette.panel);
  DrawLine(scene_rect_.x - 1, 0, scene_rect_.x - 1,
           static_cast<int>(config_.window_height), palette.border);

  draw_text(font_, config_.title.c_str(), {16.0F, 12.0F},
            typography.application_title,
            palette.primary);
  char role[96]{};
  std::snprintf(role, sizeof(role), "%.*s  |  %.*s",
                static_cast<int>(frame.info.context.application.size()),
                frame.info.context.application.data(),
                static_cast<int>(frame.info.context.role.size()),
                frame.info.context.role.data());
  draw_text(font_, role, {16.0F, 46.0F}, typography.context,
            palette.secondary);

  constexpr std::array page_names{"\uf201  Overview", "\uf1eb  Network",
                                  "\uf1b2  Entity"};
  for (std::size_t index = 0; index < page_names.size(); ++index) {
    auto const rect = inspector_tab(index, width);
    auto const active = page_index(ui_.page) == index;
    if (active) {
      DrawRectangleRec(rect, palette.raised);
      DrawRectangle(static_cast<int>(rect.x), static_cast<int>(rect.y + 34.0F),
                    static_cast<int>(rect.width), 2, palette.accent);
    }
    draw_text(font_, page_names[index], {rect.x + 6.0F, rect.y + 9.0F},
              typography.page, active ? palette.primary : palette.muted);
  }

  auto const content_top = static_cast<int>(panel_content_top);
  auto const content_bottom = static_cast<int>(
      static_cast<float>(config_.window_height) - panel_footer_height);
  auto const content_height = content_bottom - content_top;
  auto &scroll = ui_.page_scroll[page_index(ui_.page)];
  auto content_y = static_cast<float>(content_top) - scroll;
  auto constexpr section_height = 42.0F;
  auto constexpr row_height = 29.0F;
  auto total_height = 12.0F;
  for (std::size_t index = 0; index < panel_model_.section_count; ++index) {
    total_height += section_height +
                    row_height * panel_model_.sections[index].row_count + 10.0F;
  }
  auto const max_scroll = std::max(0.0F, total_height - content_height);
  scroll = std::clamp(scroll, 0.0F, max_scroll);
  content_y = static_cast<float>(content_top) - scroll;

  BeginScissorMode(0, content_top, scene_rect_.x - 2, content_height);
  for (std::size_t section_index = 0;
       section_index < panel_model_.section_count; ++section_index) {
    auto const &section = panel_model_.sections[section_index];
    content_y += 15.0F;
    DrawLine(16, static_cast<int>(content_y), scene_rect_.x - 16,
             static_cast<int>(content_y), palette.divider);
    content_y += 12.0F;
    draw_text(font_, section.title.data(), {16.0F, content_y},
              typography.section,
              palette.accent);
    content_y += 29.0F;
    for (std::size_t row_index = section.first_row;
         row_index < section.first_row + section.row_count; ++row_index) {
      auto const &row = panel_model_.rows[row_index];
      if (row.full_width) {
        draw_text(font_, row.value.data(), {16.0F, content_y}, typography.body,
                  value_color(row.state));
      } else {
        draw_text(font_, row.label.data(), {16.0F, content_y},
                  typography.body,
                  palette.secondary);
        auto const value_width =
            MeasureTextEx(font_, row.value.data(), typography.body, 0.75F).x;
        draw_text(font_, row.value.data(),
                  {std::max(174.0F, width - 16.0F - value_width), content_y},
                  typography.body, value_color(row.state));
      }
      content_y += row_height;
    }
  }
  EndScissorMode();

  if (max_scroll > 0.0F) {
    auto const track = Rectangle{width - 7.0F, static_cast<float>(content_top),
                                 3.0F, static_cast<float>(content_height)};
    DrawRectangleRec(track, palette.border);
    auto const thumb_height =
        std::max(28.0F, track.height * track.height / total_height);
    auto const thumb_y =
        track.y + (track.height - thumb_height) * scroll / max_scroll;
    DrawRectangleRec({track.x, thumb_y, track.width, thumb_height},
                     palette.accent);
  }

  DrawRectangle(0, content_bottom, scene_rect_.x,
                static_cast<int>(config_.window_height) - content_bottom,
                palette.raised);
  char footer[128]{};
  auto const session =
      frame.info.session_ready.has_value()
          ? (*frame.info.session_ready ? "Session ready" : "Session pending")
      : frame.info.connection.has_value() ? frame.info.connection->state
                                          : "Local viewer";
  if (frame.info.connection.has_value() &&
      frame.info.connection->peer.has_value()) {
    std::snprintf(footer, sizeof(footer), "%.*s  |  Peer %u",
                  static_cast<int>(session.size()), session.data(),
                  *frame.info.connection->peer);
  } else {
    std::snprintf(footer, sizeof(footer), "%.*s",
                  static_cast<int>(session.size()), session.data());
  }
  char decorated_footer[160]{};
  std::snprintf(decorated_footer, sizeof(decorated_footer), "\uf1eb  %s",
                footer);
  draw_text(font_, decorated_footer,
            {16.0F, static_cast<float>(content_bottom + 16)},
            typography.secondary, palette.secondary);
}

void Viewer::Impl::draw_viewport_ui(RenderFrame const &frame,
                                    ViewerResult const &) {
  auto layout = viewport_ui_layout(scene_rect_);
  auto options =
      overlay_options(overlays_, frame, selected_entity_.has_value());
  fit_popover(layout.popover, options);
  auto const pause_label = frame.info.simulation_paused.value_or(false)
                               ? "\uf04b  Resume"
                               : "\uf04c  Pause";
  std::array labels{pause_label, "\uf030  Camera", "\uf03a  Overlays",
                    "\uf059  Help"};
  auto const mouse = GetMousePosition();
  for (std::size_t index = 0; index < labels.size(); ++index) {
    auto const rect = layout.toolbar_buttons[index];
    auto const hovered = CheckCollisionPointRec(mouse, rect);
    auto disabled =
        index == 0U && !frame.info.capabilities.can_pause_simulation;
    DrawRectangleRec(rect,
                     hovered && !disabled ? palette.hover : palette.raised);
    DrawRectangleLinesEx(rect, 1.0F, palette.border);
    draw_text(font_, labels[index], {rect.x + 11.0F, rect.y + 10.0F},
              typography.toolbar,
              disabled ? palette.muted : palette.primary);
  }

  char mode[96]{};
  char const *name = "Overview orbit";
  if (mode_ == CameraMode::EntityFollow) {
    name = "Entity follow";
  } else if (mode_ == CameraMode::StationaryObserver) {
    name = "Stationary observer";
  } else if (mode_ == CameraMode::Game) {
    name = "Game";
  }
  std::snprintf(mode, sizeof(mode), "\uf030  %s", name);
  draw_text(font_, mode, {static_cast<float>(scene_rect_.x + 18), 22.0F},
            typography.toolbar, palette.secondary);

  if (ui_.overlay_menu_open) {
    DrawRectangleRec(layout.popover, palette.raised);
    DrawRectangleLinesEx(layout.popover, 1.0F, palette.border);
    draw_text(font_, "\uf03a  VISUAL OVERLAYS",
              {layout.popover.x + 14.0F, layout.popover.y + 18.0F},
              typography.section, palette.accent);
    auto y = layout.popover.y + 56.0F;
    for (auto const &option : options) {
      if (!option.visible) {
        continue;
      }
      auto const row = Rectangle{layout.popover.x + 12.0F, y,
                                 layout.popover.width - 24.0F,
                                 overlay_row_height - 2.0F};
      if (CheckCollisionPointRec(mouse, row)) {
        DrawRectangleRec(row, palette.hover);
      }
      DrawRectangleLinesEx({row.x + 4.0F, row.y + 8.0F, 16.0F, 16.0F}, 1.0F,
                           *option.value ? palette.accent : palette.border);
      if (*option.value) {
        DrawRectangle(static_cast<int>(row.x + 8.0F),
                      static_cast<int>(row.y + 12.0F), 8, 8, palette.accent);
      }
      draw_text(font_, option.label, {row.x + 30.0F, row.y + 8.0F},
                typography.body, palette.primary);
      y += overlay_row_height;
    }
  }

  if (selected_entity_frame_.has_value() && ui_.page != InspectorPage::Entity) {
    auto const card = Rectangle{
        static_cast<float>(scene_rect_.x) +
            (static_cast<float>(scene_rect_.width) - 460.0F) * 0.5F,
        static_cast<float>(scene_rect_.height) - 76.0F, 460.0F, 56.0F};
    DrawRectangleRec(card, palette.raised);
    DrawRectangleLinesEx(card, 1.0F, palette.border);
    char value[160]{};
    if (frame.selected_details.has_value() &&
        frame.selected_details->id == selected_entity_frame_->id) {
      std::snprintf(
          value, sizeof(value),
          "Entity %u   |   Speed %.2f   |   Neighbours %u",
          selected_entity_frame_->id,
          frame.selected_details->speed.value_or(0.0F),
          frame.selected_details->retained_neighbor_count.value_or(0U));
    } else {
      std::snprintf(value, sizeof(value), "Entity %u   |   %.1f  %.1f  %.1f",
                    selected_entity_frame_->id,
                    selected_entity_frame_->position.x,
                    selected_entity_frame_->position.y,
                    selected_entity_frame_->position.z);
    }
    draw_text(font_, value, {card.x + 16.0F, card.y + 18.0F},
              typography.context_card, palette.selection);
  }
}

void Viewer::Impl::draw_help_overlay(RenderFrame const &frame) const {
  draw_text(font_, "\uf059  F12 Help",
            {static_cast<float>(scene_rect_.x + scene_rect_.width - 128),
             static_cast<float>(scene_rect_.height - 34)},
            typography.secondary, palette.muted);
  if (!ui_.help_open) {
    return;
  }
  auto const rect =
      Rectangle{static_cast<float>(scene_rect_.x + 32), 70.0F, 510.0F,
                frame.game_camera.has_value() ? 440.0F : 376.0F};
  DrawRectangleRec(rect, palette.raised);
  DrawRectangleLinesEx(rect, 1.0F, palette.border);
  auto y = rect.y + 18.0F;
  auto line = [&](char const *value, Color color = palette.secondary) {
    draw_text(font_, value, {rect.x + 18.0F, y}, typography.body, color);
    y += 28.0F;
  };
  line("\uf11c  VIEWER CONTROLS", palette.accent);
  line("F1 / F2 / F3   Inspector pages");
  line("Left click      Select entity");
  line("[ / ]           Previous / next entity");
  line("Right drag      Orbit camera");
  line("Wheel           Zoom or scroll panel");
  line("F4              Cycle available cameras");
  line("F5              Reset active camera");
  line("P               Pause / resume");
  line("M               Visual overlays");
  if (frame.game_camera.has_value()) {
    line("WASD            Steer player fish");
    line("Shift / Ctrl    Accelerate / slow");
  }
  if (frame.stationary_observer.has_value()) {
    line("Arrow keys      Rotate observer");
  }
  line("Escape          Close UI / clear selection");
  line("F12             Close help");
}
} // namespace simnet
