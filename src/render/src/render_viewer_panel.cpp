module;

#include <algorithm>
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

import simnet.core;

#include "../private/render_viewer_impl.hpp"

namespace simnet {
namespace {
using namespace render_detail;

constexpr float panel_content_top = 126.0F;
constexpr float panel_footer_height = 50.0F;
constexpr float section_header_height = 56.0F;
constexpr float panel_row_height = 29.0F;

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

Rectangle inspector_tab(std::size_t index, float panel_width) noexcept {
  auto constexpr margin = 12.0F;
  auto constexpr gap = 3.0F;
  auto const width = (panel_width - margin * 2.0F - gap * 3.0F) / 4.0F;
  return {margin + static_cast<float>(index) * (width + gap), 78.0F, width,
          36.0F};
}

void draw_text(Font font, char const *value, Vector2 position, float size,
               Color color) {
  DrawTextEx(font, value, position, size, 0.75F, color);
}

float panel_content_extent(PanelModel const &model) noexcept {
  auto height = 12.0F;
  for (std::size_t index = 0; index < model.section_count; ++index) {
    auto const &section = model.sections[index];
    height += section_header_height;
    if (section.expanded) {
      height += panel_row_height * section.row_count;
    }
  }
  return height;
}

void toggle_popover(UiState &ui, OpenPopover requested) noexcept {
  ui.popover = ui.popover == requested ? OpenPopover::None : requested;
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
  } else if (IsKeyPressed(KEY_F4)) {
    ui_.page = InspectorPage::Setup;
  }
  if (IsKeyPressed(KEY_H)) {
    toggle_popover(ui_, OpenPopover::Help);
  }
  if (IsKeyPressed(KEY_M)) {
    toggle_popover(ui_, OpenPopover::Overlays);
  }
  if (IsKeyPressed(KEY_C)) {
    toggle_popover(ui_, OpenPopover::Camera);
  }
  if (IsKeyPressed(KEY_R)) {
    reset_active_camera(frame);
  }
  if (IsKeyPressed(KEY_P) && frame.info.capabilities.can_pause_simulation) {
    result.toggle_simulation_pause_requested = true;
  }
  if (IsKeyPressed(KEY_BACKSPACE) && selected_entity_.has_value()) {
    clear_selection(result);
  }

  auto const mouse = GetMousePosition();
  auto const panel_width = static_cast<float>(scene_rect_.x);
  if (mouse.x < panel_width) {
    ui_.pointer_captured = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      for (std::size_t index = 0; index < 4U; ++index) {
        if (CheckCollisionPointRec(mouse, inspector_tab(index, panel_width))) {
          ui_.page = static_cast<InspectorPage>(index);
        }
      }

      auto const content_bottom =
          static_cast<float>(config_.window_height) - panel_footer_height;
      if (mouse.y >= panel_content_top && mouse.y < content_bottom) {
        auto const &model = panel_model_;
        auto const scroll = ui_.page_scroll[page_index(ui_.page)];
        auto y = panel_content_top - scroll + 12.0F;
        for (std::size_t index = 0; index < model.section_count; ++index) {
          auto const &section = model.sections[index];
          auto const header =
              Rectangle{0.0F, y, panel_width, section_header_height};
          if (section.collapsible && CheckCollisionPointRec(mouse, header)) {
            ui_.expanded_sections[page_index(ui_.page)] ^= (1U << index);
            break;
          }
          y += section_header_height;
          if (section.expanded) {
            y += panel_row_height * section.row_count;
          }
        }
      }
    }
    auto const wheel = GetMouseWheelMove();
    if (wheel != 0.0F && mouse.y >= panel_content_top &&
        mouse.y <
            static_cast<float>(config_.window_height) - panel_footer_height) {
      auto &scroll = ui_.page_scroll[page_index(ui_.page)];
      scroll = std::max(0.0F, scroll - wheel * 52.0F);
    }
  }

  auto layout = viewport_ui_layout(scene_rect_);
  auto const overlays =
      overlay_options(overlays_, frame, selected_entity_.has_value());
  auto const cameras = camera_options(frame, selected_entity_.has_value());
  if (ui_.popover == OpenPopover::Overlays) {
    auto count = std::size_t{};
    auto groups = std::size_t{};
    auto last_group = std::string_view{};
    for (auto const &option : overlays) {
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
  } else if (ui_.popover == OpenPopover::Camera) {
    auto count = std::size_t{};
    for (auto const &option : cameras) {
      count += option.available ? 1U : 0U;
    }
    layout.popover.height =
        96.0F + static_cast<float>(count) * overlay_row_height;
  }

  for (auto const rect : layout.toolbar_buttons) {
    if (CheckCollisionPointRec(mouse, rect)) {
      ui_.pointer_captured = true;
    }
  }
  if ((ui_.popover == OpenPopover::Camera ||
       ui_.popover == OpenPopover::Overlays) &&
      CheckCollisionPointRec(mouse, layout.popover)) {
    ui_.pointer_captured = true;
  }
  if (ui_.popover == OpenPopover::Help &&
      CheckCollisionPointRec(mouse, help_overlay_rect(scene_rect_, frame))) {
    ui_.pointer_captured = true;
  }

  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return;
  }
  if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[0]) &&
      frame.info.capabilities.can_pause_simulation) {
    result.toggle_simulation_pause_requested = true;
    return;
  }
  if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[1])) {
    toggle_popover(ui_, OpenPopover::Camera);
    return;
  }
  if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[2])) {
    toggle_popover(ui_, OpenPopover::Overlays);
    return;
  }
  if (CheckCollisionPointRec(mouse, layout.toolbar_buttons[3])) {
    toggle_popover(ui_, OpenPopover::Help);
    return;
  }

  if (ui_.popover == OpenPopover::Camera &&
      CheckCollisionPointRec(mouse, layout.popover)) {
    auto y = layout.popover.y + 50.0F;
    for (auto const &option : cameras) {
      if (!option.available) {
        continue;
      }
      auto const row =
          Rectangle{layout.popover.x + 12.0F, y, layout.popover.width - 24.0F,
                    overlay_row_height - 2.0F};
      if (CheckCollisionPointRec(mouse, row)) {
        mode_ = option.mode;
        ui_.popover = OpenPopover::None;
        return;
      }
      y += overlay_row_height;
    }
    auto const reset =
        Rectangle{layout.popover.x + 12.0F, y + 8.0F,
                  layout.popover.width - 24.0F, overlay_row_height - 2.0F};
    if (CheckCollisionPointRec(mouse, reset)) {
      reset_active_camera(frame);
      ui_.popover = OpenPopover::None;
    }
    return;
  }

  if (ui_.popover == OpenPopover::Overlays &&
      CheckCollisionPointRec(mouse, layout.popover)) {
    auto y = layout.popover.y + 50.0F;
    auto last_group = std::string_view{};
    for (auto &option : overlays) {
      if (!option.available) {
        continue;
      }
      if (option.group != last_group) {
        y += 25.0F;
        last_group = option.group;
      }
      auto const row =
          Rectangle{layout.popover.x + 12.0F, y, layout.popover.width - 24.0F,
                    overlay_row_height - 2.0F};
      if (CheckCollisionPointRec(mouse, row)) {
        *option.value = !*option.value;
        return;
      }
      y += overlay_row_height;
    }
    return;
  }

  if (ui_.popover != OpenPopover::None) {
    ui_.popover = OpenPopover::None;
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
            typography.application_title, palette.primary);
  char context[96]{};
  constexpr std::array page_titles{"Overview", "Network", "Entity", "Setup"};
  if (frame.info.context.kind == ViewerKind::Server) {
    auto const state =
        frame.info.simulation_paused.value_or(false) ? "Paused" : "Running";
    std::snprintf(context, sizeof(context), "%s  |  %s",
                  page_titles[page_index(ui_.page)], state);
  } else {
    std::snprintf(context, sizeof(context), "%s  |  %.*s",
                  page_titles[page_index(ui_.page)],
                  static_cast<int>(frame.info.context.client_role.size()),
                  frame.info.context.client_role.data());
  }
  draw_text(font_, context, {16.0F, 46.0F}, typography.context,
            palette.secondary);

  constexpr std::array page_names{"\uf201 Overview", "\uf1eb Network",
                                  "\uf1b2 Entity", "\uf013 Setup"};
  for (std::size_t index = 0; index < page_names.size(); ++index) {
    auto const rect = inspector_tab(index, width);
    auto const active = page_index(ui_.page) == index;
    if (active) {
      DrawRectangleRec(rect, palette.raised);
      DrawRectangle(static_cast<int>(rect.x), static_cast<int>(rect.y + 34.0F),
                    static_cast<int>(rect.width), 2, palette.accent);
    }
    draw_text(font_, page_names[index], {rect.x + 5.0F, rect.y + 9.0F},
              typography.page, active ? palette.primary : palette.muted);
  }

  auto const content_top = static_cast<int>(panel_content_top);
  auto const content_bottom = static_cast<int>(
      static_cast<float>(config_.window_height) - panel_footer_height);
  auto const content_height = content_bottom - content_top;
  auto &scroll = ui_.page_scroll[page_index(ui_.page)];
  auto const total_height = panel_content_extent(panel_model_);
  auto const max_scroll = std::max(0.0F, total_height - content_height);
  scroll = std::clamp(scroll, 0.0F, max_scroll);
  auto content_y = static_cast<float>(content_top) - scroll + 12.0F;

  BeginScissorMode(0, content_top, scene_rect_.x - 2, content_height);
  for (std::size_t section_index = 0;
       section_index < panel_model_.section_count; ++section_index) {
    auto const &section = panel_model_.sections[section_index];
    DrawLine(16, static_cast<int>(content_y + 3.0F), scene_rect_.x - 16,
             static_cast<int>(content_y + 3.0F), palette.divider);
    char title[48]{};
    if (section.collapsible) {
      std::snprintf(title, sizeof(title), "%s  %s",
                    section.expanded ? "\uf078" : "\uf054",
                    section.title.data());
    } else {
      std::snprintf(title, sizeof(title), "%s", section.title.data());
    }
    draw_text(font_, title, {16.0F, content_y + 16.0F}, typography.section,
              palette.accent);
    content_y += section_header_height;
    if (!section.expanded) {
      continue;
    }
    for (std::size_t row_index = section.first_row;
         row_index < section.first_row + section.row_count; ++row_index) {
      auto const &row = panel_model_.rows[row_index];
      if (row.full_width) {
        draw_text(font_, row.value.data(), {16.0F, content_y}, typography.body,
                  value_color(row.state));
      } else {
        draw_text(font_, row.label.data(), {16.0F, content_y}, typography.body,
                  palette.secondary);
        auto const value_width =
            MeasureTextEx(font_, row.value.data(), typography.body, 0.75F).x;
        draw_text(font_, row.value.data(),
                  {std::max(174.0F, width - 16.0F - value_width), content_y},
                  typography.body, value_color(row.state));
      }
      content_y += panel_row_height;
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
  char footer[160]{};
  if (frame.info.context.kind == ViewerKind::Server) {
    auto const connected = frame.info.connection
                               .and_then([](auto const &value) {
                                 return value.connected_peer_count;
                               })
                               .value_or(0U);
    if (connected == 0U) {
      std::snprintf(footer, sizeof(footer), "No clients connected");
    } else {
      std::snprintf(footer, sizeof(footer), "%u client connected", connected);
    }
  } else if (frame.info.connection.has_value()) {
    std::snprintf(footer, sizeof(footer), "%.*s",
                  static_cast<int>(frame.info.connection->state.size()),
                  frame.info.connection->state.data());
  } else {
    std::snprintf(footer, sizeof(footer), "Disconnected");
  }
  char decorated[180]{};
  std::snprintf(decorated, sizeof(decorated), "\uf1eb  %s", footer);
  draw_text(font_, decorated, {16.0F, static_cast<float>(content_bottom + 16)},
            typography.secondary, palette.secondary);
}

} // namespace simnet
