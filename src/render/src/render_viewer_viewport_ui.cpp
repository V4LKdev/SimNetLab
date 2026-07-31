module;

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

#include <raylib.h>

module simnet.render;

import :ui;
import :viewer_impl;
import simnet.core;

namespace simnet {
namespace {
using namespace render_detail;

void draw_text(Font font, char const *value, Vector2 position, float size,
               Color color) {
  DrawTextEx(font, value, position, size, 0.75F, color);
}
} // namespace

void Viewer::Impl::draw_viewport_ui(RenderFrame const &frame,
                                    ViewerResult const &) {
  draw_orientation_gizmo();
  auto layout = viewport_ui_layout(scene_rect_);
  auto const mouse = GetMousePosition();
  auto const pause_label = frame.info.simulation_paused.value_or(false)
                               ? "\uf04b  Resume"
                               : "\uf04c  Pause";
  std::array labels{pause_label, "\uf030  Camera", "\uf03a  Overlays",
                    "\uf059  Help"};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    auto const rect = layout.toolbar_buttons[index];
    auto const hovered = CheckCollisionPointRec(mouse, rect);
    auto const disabled =
        index == 0U && !frame.info.capabilities.can_pause_simulation;
    auto const active = (index == 1U && ui_.popover == OpenPopover::Camera) ||
                        (index == 2U && ui_.popover == OpenPopover::Overlays) ||
                        (index == 3U && ui_.popover == OpenPopover::Help);
    DrawRectangleRec(rect, active                 ? palette.hover
                           : hovered && !disabled ? palette.hover
                                                  : palette.raised);
    DrawRectangleLinesEx(rect, 1.0F, active ? palette.accent : palette.border);
    draw_text(font_, labels[index], {rect.x + 11.0F, rect.y + 9.0F},
              typography.toolbar, disabled ? palette.muted : palette.primary);
  }

  char const *camera_name = "Overview orbit";
  if (mode_ == CameraMode::EntityFollow) {
    camera_name = "Entity follow";
  } else if (mode_ == CameraMode::StationaryObserver) {
    camera_name = "Stationary observer";
  } else if (mode_ == CameraMode::Game) {
    camera_name = "Game camera";
  }
  char mode[96]{};
  std::snprintf(mode, sizeof(mode), "\uf030  %s", camera_name);
  auto const backing_width =
      MeasureTextEx(font_, mode, typography.toolbar, 0.75F).x + 20.0F;
  auto const backing = Rectangle{static_cast<float>(scene_rect_.x + 14), 14.0F,
                                 backing_width, 38.0F};
  DrawRectangleRec(backing, ColorAlpha(palette.raised, 0.88F));
  draw_text(font_, mode, {backing.x + 10.0F, backing.y + 9.0F},
            typography.toolbar, palette.primary);

  if (ui_.popover == OpenPopover::Camera) {
    auto options = camera_options(frame, selected_entity_.has_value());
    auto count = std::size_t{};
    for (auto const &option : options) {
      count += option.available ? 1U : 0U;
    }
    layout.popover.height =
        96.0F + static_cast<float>(count) * overlay_row_height;
    DrawRectangleRec(layout.popover, palette.raised);
    DrawRectangleLinesEx(layout.popover, 1.0F, palette.border);
    draw_text(font_, "\uf030  CAMERA",
              {layout.popover.x + 14.0F, layout.popover.y + 16.0F},
              typography.section, palette.accent);
    auto y = layout.popover.y + 50.0F;
    for (auto const &option : options) {
      if (!option.available) {
        continue;
      }
      auto const active = mode_ == option.mode;
      char label[96]{};
      std::snprintf(label, sizeof(label), "%s  %s", active ? "\uf00c" : " ",
                    option.label);
      draw_text(font_, label, {layout.popover.x + 18.0F, y + 7.0F},
                typography.body, active ? palette.accent : palette.primary);
      y += overlay_row_height;
    }
    DrawLine(static_cast<int>(layout.popover.x + 14.0F),
             static_cast<int>(y + 3.0F),
             static_cast<int>(layout.popover.x + layout.popover.width - 14.0F),
             static_cast<int>(y + 3.0F), palette.divider);
    draw_text(font_, "\uf2f9  Reset active camera",
              {layout.popover.x + 18.0F, y + 15.0F}, typography.body,
              palette.primary);
  } else if (ui_.popover == OpenPopover::Overlays) {
    auto options =
        overlay_options(overlays_, frame, selected_entity_.has_value());
    auto count = std::size_t{};
    auto groups = std::size_t{};
    auto last_group = std::string_view{};
    for (auto const &option : options) {
      if (!option.available) {
        continue;
      }
      ++count;
      if (option.group != last_group) {
        ++groups;
        last_group = option.group;
      }
    }
    layout.popover.height = 52.0F +
                            static_cast<float>(count) * overlay_row_height +
                            static_cast<float>(groups) * 25.0F;
    DrawRectangleRec(layout.popover, palette.raised);
    DrawRectangleLinesEx(layout.popover, 1.0F, palette.border);
    draw_text(font_, "\uf03a  VISUAL OVERLAYS",
              {layout.popover.x + 14.0F, layout.popover.y + 16.0F},
              typography.section, palette.accent);
    auto y = layout.popover.y + 50.0F;
    last_group = {};
    for (auto const &option : options) {
      if (!option.available) {
        continue;
      }
      if (option.group != last_group) {
        last_group = option.group;
        draw_text(font_, option.group, {layout.popover.x + 16.0F, y + 5.0F},
                  typography.secondary, palette.muted);
        y += 25.0F;
      }
      auto const row =
          Rectangle{layout.popover.x + 12.0F, y, layout.popover.width - 24.0F,
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
      draw_text(font_, option.label, {row.x + 30.0F, row.y + 7.0F},
                typography.body, palette.primary);
      y += overlay_row_height;
    }
  }

  if (selected_entity_frame_.has_value() && ui_.page != InspectorPage::Entity) {
    auto const card = Rectangle{
        static_cast<float>(scene_rect_.x) +
            (static_cast<float>(scene_rect_.width) - 460.0F) * 0.5F,
        static_cast<float>(scene_rect_.height) - 76.0F, 460.0F, 56.0F};
    DrawRectangleRec(card, ColorAlpha(palette.raised, 0.94F));
    DrawRectangleLinesEx(card, 1.0F, palette.border);
    char value[160]{};
    if (frame.selected_details.has_value() &&
        frame.selected_details->id == selected_entity_frame_->id) {
      std::snprintf(
          value, sizeof(value),
          "Entity %u   |   Speed %.2f   |   Neighbors %u",
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
    draw_text(font_, value, {card.x + 16.0F, card.y + 17.0F},
              typography.context_card, palette.selection);
  }
}

void Viewer::Impl::draw_help_overlay(RenderFrame const &frame) const {
  if (ui_.popover != OpenPopover::Help) {
    return;
  }
  auto const rect = help_overlay_rect(scene_rect_, frame, mode_);
  DrawRectangleRec(rect, palette.raised);
  DrawRectangleLinesEx(rect, 1.0F, palette.border);
  auto y = rect.y + 17.0F;
  auto line = [&](char const *value, Color color = palette.secondary) {
    draw_text(font_, value, {rect.x + 18.0F, y}, typography.body, color);
    y += 29.0F;
  };
  line("\uf11c  VIEWER CONTROLS", palette.accent);
  line("F1 / F2 / F3 / F4   Overview / Network / Entity / Setup");
  if (mode_ == CameraMode::OverviewOrbit) {
    line("LMB                    Select entity");
    line("RMB + drag             Orbit camera");
    line("Wheel                  Zoom viewport");
  } else if (mode_ == CameraMode::EntityFollow) {
    line("[ / ]                  Previous / next entity");
    line("Backspace              Clear selection");
    line("RMB + drag             Orbit around entity");
    line("Wheel                  Follow distance");
  }
  if (mode_ == CameraMode::OverviewOrbit ||
      mode_ == CameraMode::EntityFollow) {
    line("O                      Toggle automatic orbit");
  }
  line("C / R                   Camera menu / reset");
  line("M / H                   Overlays / close help");
  line("P                       Pause / resume");
  line("F12                     Save full-window screenshot");
  if (mode_ == CameraMode::Game && frame.game_camera.has_value()) {
    line("WASD                    Steer player fish");
    line("Shift / Ctrl            Accelerate / slow");
  }
  if (mode_ == CameraMode::StationaryObserver &&
      frame.stationary_observer.has_value()) {
    line("Arrow keys              Rotate stationary observer");
  }
  line("Escape                  Quit application");
}

void Viewer::Impl::draw_orientation_gizmo() const {
  auto const forward =
      normalize_or(Vec3f{.x = camera_.target.x - camera_.position.x,
                         .y = camera_.target.y - camera_.position.y,
                         .z = camera_.target.z - camera_.position.z},
                   Vec3f{.z = 1.0F});
  auto const camera_up = normalize_or(
      Vec3f{.x = camera_.up.x, .y = camera_.up.y, .z = camera_.up.z},
      Vec3f{.y = 1.0F});
  auto const right = normalize_or(cross(forward, camera_up), Vec3f{.x = 1.0F});
  auto const up = normalize_or(cross(right, forward), Vec3f{.y = 1.0F});
  auto const origin = Vector2{static_cast<float>(scene_rect_.x + 58),
                              static_cast<float>(scene_rect_.height - 58)};
  DrawCircleV(origin, 35.0F, ColorAlpha(palette.raised, 0.82F));
  auto axis = [&](Vec3f world, Color color, char const *label) {
    auto const endpoint = Vector2{origin.x + dot(world, right) * 27.0F,
                                  origin.y - dot(world, up) * 27.0F};
    DrawLineEx(origin, endpoint, 2.5F, color);
    draw_text(font_, label, {endpoint.x + 3.0F, endpoint.y - 8.0F},
              typography.secondary, color);
  };
  axis(Vec3f{.x = 1.0F}, Color{199, 104, 104, 255}, "X");
  axis(Vec3f{.y = 1.0F}, Color{86, 168, 121, 255}, "Y");
  axis(Vec3f{.z = 1.0F}, Color{90, 158, 207, 255}, "Z");
}
} // namespace simnet
