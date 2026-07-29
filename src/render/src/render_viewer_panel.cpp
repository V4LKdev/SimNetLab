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
#include <rlgl.h>

#include <simnet/telemetry_trace.hpp>

module simnet.render;

import simnet.core;
import simnet.telemetry;

#include "../private/render_viewer_impl.hpp"

namespace simnet {
using namespace render_detail;

void Viewer::Impl::update_panel_input() noexcept {
  if (IsKeyPressed(KEY_F1)) {
    page_ = PanelPage::Overview;
  } else if (IsKeyPressed(KEY_F2)) {
    page_ = PanelPage::Network;
  } else if (IsKeyPressed(KEY_F3)) {
    page_ = PanelPage::Entity;
  }
  if (IsKeyPressed(KEY_F12)) {
    show_help_ = !show_help_;
  }
}

void Viewer::Impl::update_controls(RenderFrame const &frame,
                                   ViewerResult &result) {
  auto const mouse = GetMousePosition();
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
      mouse.x >= static_cast<float>(config_.panel_width)) {
    return;
  }
  auto constexpr button_height = 26.0F;
  if (page_ == PanelPage::Network) {
    return;
  }
  auto const button_count =
      page_ == PanelPage::Entity
          ? (selected_entity_.has_value() ? 3.0F : 0.0F)
          : 3.0F + (frame.stationary_observer.has_value() ? 3.0F : 0.0F) +
                (frame.spatial.has_value() ? 1.0F : 0.0F) +
                (frame.info.capabilities.can_pause_simulation ? 1.0F : 0.0F);
  if (button_count == 0.0F) {
    return;
  }
  auto const button_y = static_cast<float>(config_.window_height) -
                        button_count * (button_height + 8.0F) - 18.0F;
  auto const button_at = [&](int index) {
    return Rectangle{
        16.0F, button_y + static_cast<float>(index) * (button_height + 8.0F),
        static_cast<float>(config_.panel_width) - 32.0F, button_height};
  };
  if (page_ == PanelPage::Entity) {
    if (selected_entity_.has_value() &&
        CheckCollisionPointRec(mouse, button_at(0))) {
      show_selected_debug_ = !show_selected_debug_;
    } else if (selected_entity_.has_value() &&
               CheckCollisionPointRec(mouse, button_at(1))) {
      show_selected_trail_ = !show_selected_trail_;
    } else if (selected_entity_.has_value() &&
               CheckCollisionPointRec(mouse, button_at(2))) {
      clear_selection(result);
    }
    return;
  }
  if (CheckCollisionPointRec(mouse, button_at(0))) {
    show_bounds_ = !show_bounds_;
  } else if (CheckCollisionPointRec(mouse, button_at(1))) {
    show_axes_ = !show_axes_;
  } else if (CheckCollisionPointRec(mouse, button_at(2))) {
    if (mode_ == ViewMode::EntityDetail && selected_entity_frame_.has_value()) {
      reset_detail_camera(max_distance_ * 0.25F);
    } else {
      auto const center = Vec3f{target_.x, target_.y, target_.z};
      reset_overview_camera(center);
    }
  } else if (frame.stationary_observer.has_value() &&
             CheckCollisionPointRec(mouse, button_at(3))) {
    show_stationary_observer_ = !show_stationary_observer_;
  } else if (frame.stationary_observer.has_value() &&
             CheckCollisionPointRec(mouse, button_at(4))) {
    show_stationary_observer_radius_ = !show_stationary_observer_radius_;
  } else if (frame.stationary_observer.has_value() &&
             CheckCollisionPointRec(mouse, button_at(5))) {
    show_stationary_observer_frustum_ = !show_stationary_observer_frustum_;
  } else if (frame.spatial.has_value() &&
             CheckCollisionPointRec(
                 mouse,
                 button_at(3 +
                           (frame.stationary_observer.has_value() ? 3 : 0)))) {
    show_spatial_cells_ = !show_spatial_cells_;
  } else if (frame.info.capabilities.can_pause_simulation &&
             CheckCollisionPointRec(
                 mouse,
                 button_at(3 + (frame.stationary_observer.has_value() ? 3 : 0) +
                           (frame.spatial.has_value() ? 1 : 0)))) {
    result.toggle_simulation_pause_requested = true;
  }
}

void Viewer::Impl::draw_panel(RenderFrame const &frame, bool valid_entities,
                              RenderStats const &stats,
                              ViewerResult const &result) {
  auto const width = static_cast<float>(config_.panel_width);
  DrawRectangle(0, 0, static_cast<int>(config_.panel_width),
                static_cast<int>(config_.window_height),
                Color{23, 28, 36, 255});
  DrawLine(static_cast<int>(config_.panel_width) - 1, 0,
           static_cast<int>(config_.panel_width) - 1,
           static_cast<int>(config_.window_height), Color{75, 88, 108, 255});
  auto y = 18.0F;
  auto text = [&](char const *value, int size = 16, Color color = RAYWHITE) {
    DrawTextEx(font_, value, Vector2{16.0F, y}, static_cast<float>(size), 1.0F,
               color);
    y += static_cast<float>(size + 8);
  };
  auto section = [&](char const *value) {
    y += 8.0F;
    DrawLine(16, static_cast<int>(y), static_cast<int>(width) - 16,
             static_cast<int>(y), Color{68, 82, 102, 255});
    y += 10.0F;
    text(value, 17, Color{133, 186, 235, 255});
  };
  char line[192]{};
  text(config_.title.c_str(), 23);
  auto const mode_name = [](ViewMode mode) {
    switch (mode) {
    case ViewMode::Overview:
      return "Overview";
    case ViewMode::EntityDetail:
      return "Entity detail";
    case ViewMode::StationaryObserver:
      return "Stationary observer";
    case ViewMode::Game:
      return "Game";
    }
    return "Unknown";
  };
  auto const page_name = [](PanelPage page) {
    switch (page) {
    case PanelPage::Overview:
      return "F1 Overview";
    case PanelPage::Network:
      return "F2 Network";
    case PanelPage::Entity:
      return "F3 Entity";
    }
    return "Unknown";
  };
  text(page_name(page_), 17, Color{133, 186, 235, 255});
  auto button = [&](float &button_y, char const *label, bool active) {
    auto constexpr button_height = 26.0F;
    auto const rect = Rectangle{16.0F, button_y, width - 32.0F, button_height};
    DrawRectangleRec(rect, active ? Color{51, 102, 145, 255}
                                  : Color{45, 54, 68, 255});
    DrawRectangleLinesEx(rect, 1.0F, Color{91, 113, 140, 255});
    DrawTextEx(font_, label, Vector2{24.0F, button_y + 5.0F}, 16.0F, 1.0F,
               RAYWHITE);
    button_y += button_height + 8.0F;
  };
  if (page_ == PanelPage::Overview) {
    section("Application");
    std::snprintf(line, sizeof(line), "mode %s", mode_name(mode_));
    text(line);
    if (frame.info.simulation_paused.has_value()) {
      text(*frame.info.simulation_paused ? "simulation paused"
                                         : "simulation running");
    } else {
      text("simulation state unavailable");
    }
    if (!frame.info.status_message.empty()) {
      std::snprintf(line, sizeof(line), "%.*s",
                    static_cast<int>(frame.info.status_message.size()),
                    frame.info.status_message.data());
      text(line, 15, Color{247, 184, 74, 255});
    }
    section("World");
    std::snprintf(line, sizeof(line), "tick %llu",
                  static_cast<unsigned long long>(frame.info.tick));
    text(line);
    std::snprintf(line, sizeof(line), "entities %zu",
                  valid_entities ? frame.entities.size() : 0U);
    text(line);
    std::snprintf(line, sizeof(line), "skipped %u", stats.skipped_entity_count);
    text(line);
    if (frame.info.fixed_tick_rate_hz.has_value()) {
      std::snprintf(line, sizeof(line), "fixed rate %.1f Hz",
                    *frame.info.fixed_tick_rate_hz);
      text(line);
    }
    if (!valid_entities) {
      text("invalid entity view", 15, ORANGE);
    }
    if (frame.spatial.has_value()) {
      auto const &spatial = *frame.spatial;
      section("Spatial");
      std::snprintf(line, sizeof(line), "occupied cells %u",
                    spatial.occupied_cell_count);
      text(line);
      std::snprintf(line, sizeof(line), "displayed cells %zu",
                    spatial.cells.size());
      text(line);
      text(spatial.display_capped ? "display capped yes" : "display capped no");
      std::snprintf(line, sizeof(line), "max occupancy %u",
                    spatial.max_cell_occupancy);
      text(line);
      std::snprintf(line, sizeof(line), "average occupancy %.2f",
                    spatial.average_occupied_cell_load);
      text(line);
    }
    section("Rendering");
    std::snprintf(line, sizeof(line), "FPS %d", GetFPS());
    text(line);
    std::snprintf(line, sizeof(line), "frame %.2f ms",
                  static_cast<double>(frame.info.frame_delta.count()) /
                      1'000'000.0);
    text(line);
    if (frame.info.interpolation.has_value()) {
      auto const &interpolation = *frame.info.interpolation;
      std::snprintf(line, sizeof(line), "interpolation %s alpha %.2f",
                    interpolation.enabled
                        ? (interpolation.interpolating ? "active" : "holding")
                        : "off",
                    interpolation.alpha);
      text(line);
      std::snprintf(line, sizeof(line), "display ticks %llu -> %llu",
                    static_cast<unsigned long long>(interpolation.from_tick),
                    static_cast<unsigned long long>(interpolation.to_tick));
      text(line);
    }
    std::snprintf(line, sizeof(line), "input %.2f ms",
                  static_cast<double>(stats.input_cpu_time.count()) /
                      1'000'000.0);
    text(line);
    std::snprintf(line, sizeof(line), "prepare %.2f ms",
                  static_cast<double>(stats.preparation_cpu_time.count()) /
                      1'000'000.0);
    text(line);
    std::snprintf(line, sizeof(line), "scene %.2f ms",
                  static_cast<double>(stats.scene_submit_cpu_time.count()) /
                      1'000'000.0);
    text(line);
    std::snprintf(line, sizeof(line), "panel %.2f ms",
                  static_cast<double>(stats.panel_cpu_time.count()) /
                      1'000'000.0);
    text(line);
    std::snprintf(line, sizeof(line), "instances %u calls %u",
                  stats.instance_count, stats.draw_calls);
    text(line);
    std::snprintf(line, sizeof(line), "hue buckets %u",
                  stats.active_hue_buckets);
    text(line);
    section("Camera");
    std::snprintf(line, sizeof(line), "position %.1f %.1f %.1f",
                  camera_.position.x, camera_.position.y, camera_.position.z);
    text(line);
    std::snprintf(line, sizeof(line), "target %.1f %.1f %.1f", camera_.target.x,
                  camera_.target.y, camera_.target.z);
    text(line);
    auto const active_distance =
        mode_ == ViewMode::EntityDetail ? detail_distance_ : overview_distance_;
    std::snprintf(line, sizeof(line), "distance %.1f", active_distance);
    text(line);

    auto constexpr button_height = 26.0F;
    auto const button_count =
        3.0F + (frame.stationary_observer.has_value() ? 3.0F : 0.0F) +
        (frame.spatial.has_value() ? 1.0F : 0.0F) +
        (frame.info.capabilities.can_pause_simulation ? 1.0F : 0.0F);
    auto button_y = static_cast<float>(config_.window_height) -
                    button_count * (button_height + 8.0F) - 18.0F;
    button(button_y, show_bounds_ ? "Hide bounds" : "Show bounds",
           show_bounds_);
    button(button_y, show_axes_ ? "Hide axes" : "Show axes", show_axes_);
    button(button_y, "Reset camera", false);
    if (frame.stationary_observer.has_value()) {
      button(button_y,
             show_stationary_observer_ ? "Hide stationary observer"
                                       : "Show stationary observer",
             show_stationary_observer_);
      button(button_y,
             show_stationary_observer_radius_ ? "Hide observer radius"
                                              : "Show observer radius",
             show_stationary_observer_radius_);
      button(button_y,
             show_stationary_observer_frustum_ ? "Hide observer frustum"
                                               : "Show observer frustum",
             show_stationary_observer_frustum_);
    }
    if (frame.spatial.has_value()) {
      button(button_y,
             show_spatial_cells_ ? "Hide spatial cells" : "Show spatial cells",
             show_spatial_cells_);
    }
    if (frame.info.capabilities.can_pause_simulation) {
      button(button_y,
             frame.info.simulation_paused.value_or(false) ? "Resume simulation"
                                                          : "Pause simulation",
             false);
    }
  } else if (page_ == PanelPage::Network) {
    section("Connection");
    if (frame.info.connection.has_value()) {
      std::snprintf(line, sizeof(line), "state %.*s",
                    static_cast<int>(frame.info.connection->state.size()),
                    frame.info.connection->state.data());
      text(line);
      if (frame.info.connection->peer.has_value()) {
        std::snprintf(line, sizeof(line), "peer %u",
                      *frame.info.connection->peer);
        text(line);
      }
    } else {
      text("connection unavailable");
    }
    if (frame.info.session_ready.has_value()) {
      text(*frame.info.session_ready ? "session ready" : "session not ready");
    }
    section("Replication");
    if (frame.info.replication.has_value()) {
      auto const &replication = *frame.info.replication;
      auto sequence = [&](char const *label, std::optional<SequenceId> value) {
        if (value.has_value()) {
          std::snprintf(line, sizeof(line), "%s %u", label, *value);
          text(line);
        }
      };
      sequence("emitted", replication.latest_emitted_sequence);
      sequence("received", replication.latest_received_sequence);
      sequence("applied", replication.latest_applied_sequence);
      sequence("baseline", replication.acknowledged_baseline_sequence);
      if (replication.latest_snapshot_tick.has_value()) {
        std::snprintf(
            line, sizeof(line), "snapshot tick %llu",
            static_cast<unsigned long long>(*replication.latest_snapshot_tick));
        text(line);
      }
      if (replication.retained_snapshot_count.has_value()) {
        std::snprintf(line, sizeof(line), "retained %u",
                      *replication.retained_snapshot_count);
        text(line);
      }
      sequence("oldest", replication.oldest_retained_sequence);
      sequence("newest", replication.newest_retained_sequence);
    } else {
      text("replication unavailable");
    }
    section("Simulation");
    if (frame.info.simulation_paused.has_value()) {
      text(*frame.info.simulation_paused ? "authoritative pause"
                                         : "authoritative running");
    } else {
      text("simulation state unavailable");
    }
  } else {
    section("Selected Entity");
    if (!selected_entity_frame_.has_value()) {
      text("Left click an entity to select it");
      text("Use [ and ] to cycle visible IDs");
    } else {
      auto const &selected = *selected_entity_frame_;
      std::snprintf(line, sizeof(line), "id %u", selected.id);
      text(line);
      std::snprintf(line, sizeof(line), "position %.2f %.2f %.2f",
                    selected.position.x, selected.position.y,
                    selected.position.z);
      text(line);
      std::snprintf(line, sizeof(line), "heading %.2f %.2f %.2f",
                    selected.heading.x, selected.heading.y, selected.heading.z);
      text(line);
      std::snprintf(line, sizeof(line), "hue %u", selected.hue);
      text(line);
      if (frame.selected_details.has_value() &&
          frame.selected_details->id == selected.id) {
        auto const &details = *frame.selected_details;
        if (details.velocity.has_value()) {
          std::snprintf(line, sizeof(line), "velocity %.2f %.2f %.2f",
                        details.velocity->x, details.velocity->y,
                        details.velocity->z);
          text(line);
        }
        if (details.acceleration.has_value()) {
          std::snprintf(line, sizeof(line), "acceleration %.2f %.2f %.2f",
                        details.acceleration->x, details.acceleration->y,
                        details.acceleration->z);
          text(line);
        }
        if (details.speed.has_value()) {
          std::snprintf(line, sizeof(line), "speed %.2f", *details.speed);
          text(line);
        }
        if (details.raw_candidate_count.has_value()) {
          std::snprintf(line, sizeof(line), "neighbors raw %u kept %u/%u",
                        *details.raw_candidate_count,
                        details.retained_neighbor_count.value_or(0U),
                        details.maximum_neighbors.value_or(0U));
          text(line);
          std::snprintf(line, sizeof(line), "accepted sep %u align %u coh %u",
                        details.separation_neighbor_count.value_or(0U),
                        details.alignment_neighbor_count.value_or(0U),
                        details.cohesion_neighbor_count.value_or(0U));
          text(line);
          std::snprintf(line, sizeof(line), "hue neighbors %u",
                        details.hue_neighbor_count.value_or(0U));
          text(line);
        }
        if (details.current_cell.has_value()) {
          std::snprintf(line, sizeof(line), "cell %d %d %d queried %u",
                        details.current_cell->x, details.current_cell->y,
                        details.current_cell->z,
                        details.queried_cell_count.value_or(0U));
          text(line);
        }
        if (details.query_radius.has_value()) {
          std::snprintf(line, sizeof(line), "radii query %.1f separation %.1f",
                        *details.query_radius,
                        details.separation_radius.value_or(0.0F));
          text(line);
          std::snprintf(line, sizeof(line), "alignment %.1f cohesion %.1f",
                        details.alignment_radius.value_or(0.0F),
                        details.cohesion_radius.value_or(0.0F));
          text(line);
          std::snprintf(line, sizeof(line), "FOV %.1f deg",
                        details.field_of_view_degrees.value_or(0.0F));
          text(line);
        }
        if (details.neighbor_cap_hit.value_or(false) ||
            details.overlap_recovery.value_or(false) ||
            details.acceleration_saturated.value_or(false) ||
            details.wall_guard.value_or(false)) {
          std::snprintf(line, sizeof(line),
                        "flags cap %s overlap %s accel-cap %s wall %s",
                        details.neighbor_cap_hit.value_or(false) ? "yes" : "no",
                        details.overlap_recovery.value_or(false) ? "yes" : "no",
                        details.acceleration_saturated.value_or(false) ? "yes"
                                                                       : "no",
                        details.wall_guard.value_or(false) ? "yes" : "no");
          text(line);
        }
        auto vector = [&](char const *label,
                          std::optional<Vec3f> const &value) {
          if (!value.has_value()) {
            return;
          }
          std::snprintf(line, sizeof(line), "%s %.2f %.2f %.2f", label,
                        value->x, value->y, value->z);
          text(line);
        };
        vector("separation", details.separation);
        vector("alignment", details.alignment);
        vector("cohesion", details.cohesion);
        vector("containment", details.containment);
        vector("wander", details.wander);
        if (details.current_hue.has_value()) {
          std::snprintf(line, sizeof(line), "hue %.3f target %.3f",
                        *details.current_hue,
                        details.hue_target.value_or(*details.current_hue));
          text(line);
          std::snprintf(line, sizeof(line), "hue delta %.4f step %.4f",
                        details.hue_delta.value_or(0.0F),
                        details.applied_hue_step.value_or(0.0F));
          text(line);
          std::snprintf(
              line, sizeof(line), "active wander %s hue-assim %s hue-drift %s",
              details.wander_active.value_or(false) ? "yes" : "no",
              details.hue_assimilation_active.value_or(false) ? "yes" : "no",
              details.hue_drift_active.value_or(false) ? "yes" : "no");
          text(line);
        }
        if (details.last_update_tick.has_value()) {
          std::snprintf(
              line, sizeof(line), "update tick %llu",
              static_cast<unsigned long long>(*details.last_update_tick));
          text(line);
        }
        if (details.last_update_sequence.has_value()) {
          std::snprintf(line, sizeof(line), "update sequence %u",
                        *details.last_update_sequence);
          text(line);
        }
        if (details.replicated.has_value()) {
          text(*details.replicated ? "replicated" : "authoritative");
        }
      }
    }
    if (selected_entity_.has_value()) {
      auto constexpr button_height = 26.0F;
      auto button_y = static_cast<float>(config_.window_height) -
                      3.0F * (button_height + 8.0F) - 18.0F;
      button(button_y,
             show_selected_debug_ ? "Hide selected debug"
                                  : "Show selected debug",
             show_selected_debug_);
      button(button_y,
             show_selected_trail_ ? "Hide selected trail"
                                  : "Show selected trail",
             show_selected_trail_);
      button(button_y, "Return to overview", false);
    }
  }
  static_cast<void>(result);
}

void Viewer::Impl::draw_help_overlay(RenderFrame const &frame) const {
  auto const hint_position = Vector2{
      static_cast<float>(scene_rect_.x + scene_rect_.width - 150),
      18.0F,
  };
  DrawTextEx(font_, "F12 Help", hint_position, 15.0F, 1.0F,
             Color{180, 198, 220, 255});
  if (!show_help_) {
    return;
  }
  auto const rect = Rectangle{
      static_cast<float>(scene_rect_.x + 36),
      48.0F,
      420.0F,
      frame.game_camera.has_value() ? 370.0F : 300.0F,
  };
  DrawRectangleRec(rect, Color{20, 25, 33, 245});
  DrawRectangleLinesEx(rect, 1.0F, Color{91, 113, 140, 255});
  auto y = rect.y + 18.0F;
  auto line = [&](char const *value, int size = 15, Color color = RAYWHITE) {
    DrawTextEx(font_, value, Vector2{rect.x + 16.0F, y},
               static_cast<float>(size), 1.0F, color);
    y += static_cast<float>(size + 7);
  };
  line("Viewer controls", 18, Color{133, 186, 235, 255});
  line("F1       Overview panel");
  line("F2       Network panel");
  line("F3       Entity panel");
  line("Left click entity  Select");
  line("[ / ]    Previous or next entity");
  line("Right drag  Orbit");
  line("Wheel     Zoom");
  line(frame.game_camera.has_value() ? "F4        Toggle Game / Overview"
       : frame.stationary_observer.has_value()
           ? "F4        Toggle stationary observer"
           : "F4        No special view");
  if (frame.game_camera.has_value()) {
    line("W / S     Pitch fish");
    line("A / D     Yaw fish");
    line("Shift     Accelerate");
    line("Ctrl      Slow down");
  }
  line("F5        Reset camera");
  if (frame.stationary_observer.has_value()) {
    line("Arrows    Rotate stationary observer");
  }
  line("Backspace Clear selection and overview");
  line("F12       Close help");
}
} // namespace simnet
