module;

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
using render_detail::PanelModel;
using render_detail::PanelRow;
using render_detail::UiValueState;

void copy_text(auto &destination, char const *text) noexcept {
  std::snprintf(destination.data(), destination.size(), "%s", text);
}

void add_section(PanelModel &model, char const *title) noexcept {
  if (model.section_count >= model.sections.size()) {
    return;
  }
  auto &section = model.sections[model.section_count++];
  copy_text(section.title, title);
  section.first_row = model.row_count;
  section.row_count = 0;
}

template <typename... Args>
void add_row(PanelModel &model, char const *label, UiValueState state,
             char const *format, Args... args) noexcept {
  if (model.row_count >= model.rows.size() || model.section_count == 0U) {
    return;
  }
  auto &row = model.rows[model.row_count++];
  copy_text(row.label, label);
  std::snprintf(row.value.data(), row.value.size(), format, args...);
  row.state = state;
  ++model.sections[model.section_count - 1U].row_count;
}

void add_text(PanelModel &model, char const *text,
              UiValueState state = UiValueState::Muted) noexcept {
  if (model.row_count >= model.rows.size() || model.section_count == 0U) {
    return;
  }
  auto &row = model.rows[model.row_count++];
  copy_text(row.value, text);
  row.state = state;
  row.full_width = true;
  ++model.sections[model.section_count - 1U].row_count;
}

char const *yes_no(bool value) noexcept { return value ? "Yes" : "No"; }

double milliseconds(NS value) noexcept {
  return static_cast<double>(value.count()) / 1'000'000.0;
}

float magnitude(Vec3f value) noexcept {
  return std::sqrt(std::max(0.0F, length_squared(value)));
}
} // namespace

void Viewer::Impl::build_panel_model(RenderFrame const &frame,
                                     bool valid_entities) {
  using enum render_detail::InspectorPage;
  using enum UiValueState;
  panel_model_.clear();

  if (ui_.page == Overview) {
    add_section(panel_model_, "RUNTIME STATUS");
    if (frame.info.simulation_paused.has_value()) {
      add_row(panel_model_, "State",
              *frame.info.simulation_paused ? Warning : Success, "%s",
              *frame.info.simulation_paused ? "Paused" : "Running");
    } else {
      add_row(panel_model_, "State", Muted, "%s", "Unavailable");
    }
    add_row(panel_model_, "Application", Normal, "%.*s",
            static_cast<int>(frame.info.context.application.size()),
            frame.info.context.application.data());
    add_row(panel_model_, "Role", Normal, "%.*s",
            static_cast<int>(frame.info.context.role.size()),
            frame.info.context.role.data());
    if (frame.info.session_ready.has_value()) {
      add_row(panel_model_, "Session",
              *frame.info.session_ready ? Success : Warning, "%s",
              *frame.info.session_ready ? "Ready" : "Pending");
    }
    if (!frame.info.status_message.empty()) {
      add_row(panel_model_, "Status", Warning, "%.*s",
              static_cast<int>(frame.info.status_message.size()),
              frame.info.status_message.data());
    }

    add_section(panel_model_, "WORLD");
    add_row(panel_model_, "Tick", Normal, "%llu",
            static_cast<unsigned long long>(frame.info.tick));
    if (frame.info.snapshot_sequence.has_value()) {
      add_row(panel_model_, "Snapshot", Normal, "%u",
              *frame.info.snapshot_sequence);
    }
    if (valid_entities) {
      add_row(panel_model_, "Entities", Normal, "%zu", frame.entities.size());
    } else {
      add_row(panel_model_, "Entity view", Error, "%s", "Invalid");
      add_row(panel_model_, "IDs", Normal, "%zu", frame.entities.ids.size());
      add_row(panel_model_, "Positions", Normal, "%zu",
              frame.entities.positions.size());
      add_row(panel_model_, "Headings", Normal, "%zu",
              frame.entities.headings.size());
      add_row(panel_model_, "Hues", Normal, "%zu", frame.entities.hues.size());
    }
    add_row(panel_model_, "Skipped",
            completed_stats_.skipped_entity_count == 0 ? Muted : Warning, "%u",
            completed_stats_.skipped_entity_count);
    auto const bounds = frame.info.world_bounds;
    add_row(panel_model_, "Bounds min", Muted, "%.0f  %.0f  %.0f", bounds.min.x,
            bounds.min.y, bounds.min.z);
    add_row(panel_model_, "Bounds max", Muted, "%.0f  %.0f  %.0f", bounds.max.x,
            bounds.max.y, bounds.max.z);

    add_section(panel_model_, "SIMULATION");
    if (frame.info.fixed_tick_rate_hz.has_value()) {
      add_row(panel_model_, "Fixed rate", Normal, "%.1f Hz",
              *frame.info.fixed_tick_rate_hz);
    } else {
      add_row(panel_model_, "Fixed rate", Muted, "%s", "Not available");
    }
    add_row(panel_model_, "Frame delta", Normal, "%.2f ms",
            milliseconds(frame.info.frame_delta));

    if (frame.info.interpolation.has_value()) {
      auto const &value = *frame.info.interpolation;
      add_section(panel_model_, "INTERPOLATION");
      add_row(panel_model_, "State", value.interpolating ? Success : Muted,
              "%s",
              !value.enabled        ? "Off"
              : value.interpolating ? "Active"
                                    : "Holding");
      add_row(panel_model_, "Alpha", Normal, "%.3f", value.alpha);
      add_row(panel_model_, "From tick", Normal, "%llu",
              static_cast<unsigned long long>(value.from_tick));
      add_row(panel_model_, "To tick", Normal, "%llu",
              static_cast<unsigned long long>(value.to_tick));
      add_row(panel_model_, "Tick distance", Normal, "%llu",
              static_cast<unsigned long long>(value.to_tick - value.from_tick));
    }

    if (frame.spatial.has_value()) {
      auto const &value = *frame.spatial;
      add_section(panel_model_, "SPATIAL SYSTEM");
      add_row(panel_model_, "Occupied cells", Normal, "%u",
              value.occupied_cell_count);
      add_row(panel_model_, "Displayed cells",
              value.display_capped ? Warning : Normal, "%zu of %u",
              value.cells.size(), value.occupied_cell_count);
      add_row(panel_model_, "Display cap",
              value.display_capped ? Warning : Muted, "%s",
              value.display_capped ? "Reached" : "Not reached");
      add_row(panel_model_, "Max occupancy", Normal, "%u",
              value.max_cell_occupancy);
      add_row(panel_model_, "Average load", Normal, "%.2f",
              value.average_occupied_cell_load);
      if (value.query_radius.has_value()) {
        add_row(panel_model_, "Query radius", Normal, "%.1f",
                *value.query_radius);
      }
    }

    add_section(panel_model_, "RENDERING");
    add_row(panel_model_, "FPS", Normal, "%d", GetFPS());
    add_row(panel_model_, "Input CPU", Normal, "%.3f ms",
            milliseconds(completed_stats_.input_cpu_time));
    add_row(panel_model_, "Preparation CPU", Normal, "%.3f ms",
            milliseconds(completed_stats_.preparation_cpu_time));
    add_row(panel_model_, "Scene CPU", Normal, "%.3f ms",
            milliseconds(completed_stats_.scene_submit_cpu_time));
    add_row(panel_model_, "Panel CPU", Normal, "%.3f ms",
            milliseconds(completed_stats_.panel_cpu_time));
    add_row(panel_model_, "Instances", Normal, "%u",
            completed_stats_.instance_count);
    add_row(panel_model_, "Draw calls", Normal, "%u",
            completed_stats_.draw_calls);
    add_row(panel_model_, "Hue buckets", Normal, "%u",
            completed_stats_.active_hue_buckets);

    add_section(panel_model_, "CAMERA");
    char const *camera_name = "Overview orbit";
    if (mode_ == CameraMode::EntityFollow) {
      camera_name = "Entity follow";
    } else if (mode_ == CameraMode::StationaryObserver) {
      camera_name = "Stationary observer";
    } else if (mode_ == CameraMode::Game) {
      camera_name = "Game";
    }
    add_row(panel_model_, "Mode", Normal, "%s", camera_name);
    add_row(panel_model_, "Position", Normal, "%.1f  %.1f  %.1f",
            camera_.position.x, camera_.position.y, camera_.position.z);
    add_row(panel_model_, "Target", Normal, "%.1f  %.1f  %.1f",
            camera_.target.x, camera_.target.y, camera_.target.z);
    if (mode_ == CameraMode::OverviewOrbit) {
      add_row(panel_model_, "Distance", Normal, "%.1f", overview_distance_);
      add_row(panel_model_, "Yaw / pitch", Muted, "%.1f / %.1f deg",
              overview_yaw_ * 180.0F / render_detail::pi,
              overview_pitch_ * 180.0F / render_detail::pi);
    } else if (mode_ == CameraMode::EntityFollow) {
      add_row(panel_model_, "Follow distance", Normal, "%.1f",
              detail_distance_);
      add_row(panel_model_, "Yaw / pitch", Muted, "%.1f / %.1f deg",
              detail_yaw_ * 180.0F / render_detail::pi,
              detail_pitch_ * 180.0F / render_detail::pi);
    } else if (mode_ == CameraMode::StationaryObserver &&
               frame.stationary_observer.has_value()) {
      add_row(panel_model_, "FOV", Normal, "%.1f deg",
              frame.stationary_observer->vertical_fov_degrees);
      add_row(panel_model_, "Radius", Normal, "%.1f",
              frame.stationary_observer->interest_radius);
    } else if (mode_ == CameraMode::Game) {
      add_row(panel_model_, "Control", Muted, "%s", "Server authoritative");
    }
    return;
  }

  if (ui_.page == Network) {
    if (!frame.info.capabilities.has_networking) {
      add_section(panel_model_, "NETWORK");
      add_text(panel_model_, "Networking is not enabled for this viewer.");
      return;
    }
    add_section(panel_model_, "CONNECTION");
    if (frame.info.connection.has_value()) {
      add_row(panel_model_, "State", Normal, "%.*s",
              static_cast<int>(frame.info.connection->state.size()),
              frame.info.connection->state.data());
      if (frame.info.connection->peer.has_value()) {
        add_row(panel_model_, "Peer", Normal, "%u",
                *frame.info.connection->peer);
      } else {
        add_row(panel_model_, "Peer", Muted, "%s", "Waiting");
      }
    } else {
      add_row(panel_model_, "State", Warning, "%s", "Pending");
    }
    add_row(panel_model_, "Role", Normal, "%.*s",
            static_cast<int>(frame.info.context.role.size()),
            frame.info.context.role.data());
    if (frame.info.session_ready.has_value()) {
      add_row(panel_model_, "Session",
              *frame.info.session_ready ? Success : Warning, "%s",
              *frame.info.session_ready ? "Ready" : "Pending");
    }

    add_section(panel_model_, "SNAPSHOT STATE");
    auto const add_sequence = [&](char const *label,
                                  std::optional<SequenceId> value,
                                  char const *missing) {
      if (value.has_value()) {
        add_row(panel_model_, label, Normal, "%u", *value);
      } else {
        add_row(panel_model_, label, Muted, "%s", missing);
      }
    };
    add_sequence("Presentation", frame.info.snapshot_sequence, "Not available");
    if (frame.info.replication.has_value()) {
      auto const &value = *frame.info.replication;
      add_sequence("Emitted", value.latest_emitted_sequence, "Not emitted");
      add_sequence("Received", value.latest_received_sequence, "Not received");
      add_sequence("Applied", value.latest_applied_sequence, "Not applied");
      add_sequence("Baseline", value.acknowledged_baseline_sequence,
                   "Not available");
      if (value.latest_snapshot_tick.has_value()) {
        add_row(panel_model_, "Snapshot tick", Normal, "%llu",
                static_cast<unsigned long long>(*value.latest_snapshot_tick));
      }
      if (value.retained_snapshot_count.has_value()) {
        add_row(panel_model_, "Retained", Normal, "%u",
                *value.retained_snapshot_count);
      }
      add_sequence("Oldest retained", value.oldest_retained_sequence,
                   "Not available");
      add_sequence("Newest retained", value.newest_retained_sequence,
                   "Not available");
    } else {
      add_text(panel_model_, "Waiting for replication state.");
    }
    add_section(panel_model_, "AUTHORITATIVE STATE");
    add_row(panel_model_, "Presentation tick", Normal, "%llu",
            static_cast<unsigned long long>(frame.info.tick));
    if (frame.info.simulation_paused.has_value()) {
      add_row(panel_model_, "Simulation",
              *frame.info.simulation_paused ? Warning : Success, "%s",
              *frame.info.simulation_paused ? "Paused" : "Running");
    } else {
      add_row(panel_model_, "Simulation", Muted, "%s", "Not available");
    }
    return;
  }

  add_section(panel_model_, "IDENTITY & SOURCE");
  if (!selected_entity_frame_.has_value()) {
    add_text(panel_model_, "No entity selected.", Muted);
    add_text(panel_model_, "Left-click an entity in the viewport.");
    add_text(panel_model_, "Use [ and ] to cycle visible entities.");
    if (!frame.info.capabilities.has_entity_diagnostics) {
      add_text(panel_model_,
               "Detailed diagnostics require an authoritative source.");
    }
    return;
  }
  auto const &selected = *selected_entity_frame_;
  add_row(panel_model_, "Entity ID", Normal, "%u", selected.id);
  add_row(panel_model_, "Hue", Normal, "%u", selected.hue);

  auto const *details = frame.selected_details.has_value() &&
                                frame.selected_details->id == selected.id
                            ? &*frame.selected_details
                            : nullptr;
  if (details == nullptr) {
    add_row(panel_model_, "Source", Muted, "%s",
            frame.info.capabilities.has_entity_diagnostics
                ? "Waiting for diagnostics"
                : "Replicated presentation");
  } else {
    if (details->replicated.has_value()) {
      add_row(panel_model_, "Source", Normal, "%s",
              *details->replicated ? "Replicated" : "Authoritative");
    }
    if (details->last_update_tick.has_value()) {
      add_row(panel_model_, "Update tick", Normal, "%llu",
              static_cast<unsigned long long>(*details->last_update_tick));
    }
    if (details->last_update_sequence.has_value()) {
      add_row(panel_model_, "Update sequence", Normal, "%u",
              *details->last_update_sequence);
    }
    if (details->current_cell.has_value()) {
      add_row(panel_model_, "Current cell", Normal, "%d  %d  %d",
              details->current_cell->x, details->current_cell->y,
              details->current_cell->z);
    }
  }

  add_section(panel_model_, "TRANSFORM");
  add_row(panel_model_, "Position", Normal, "%.2f  %.2f  %.2f",
          selected.position.x, selected.position.y, selected.position.z);
  add_row(panel_model_, "Heading", Normal, "%.2f  %.2f  %.2f",
          selected.heading.x, selected.heading.y, selected.heading.z);
  if (details == nullptr) {
    return;
  }

  if (details->velocity.has_value() || details->acceleration.has_value() ||
      details->speed.has_value()) {
    add_section(panel_model_, "MOTION");
    if (details->velocity.has_value()) {
      auto value = *details->velocity;
      add_row(panel_model_, "Velocity", Normal, "%.2f  %.2f  %.2f", value.x,
              value.y, value.z);
    }
    if (details->acceleration.has_value()) {
      auto value = *details->acceleration;
      add_row(panel_model_, "Acceleration", Normal, "%.2f  %.2f  %.2f", value.x,
              value.y, value.z);
    }
    if (details->speed.has_value()) {
      add_row(panel_model_, "Speed", Normal, "%.2f", *details->speed);
      if (details->maximum_speed.has_value() &&
          *details->maximum_speed > 0.0F) {
        add_row(panel_model_, "Maximum speed", Muted, "%.1f%%",
                100.0F * *details->speed / *details->maximum_speed);
      }
    }
  }

  add_section(panel_model_, "NEIGHBOURHOOD");
  auto const optional_count = [&](char const *label,
                                  std::optional<std::uint32_t> value) {
    if (value.has_value()) {
      add_row(panel_model_, label, Normal, "%u", *value);
    } else {
      add_row(panel_model_, label, Muted, "%s", "-");
    }
  };
  optional_count("Raw candidates", details->raw_candidate_count);
  optional_count("Retained", details->retained_neighbor_count);
  optional_count("Maximum", details->maximum_neighbors);
  optional_count("Separation", details->separation_neighbor_count);
  optional_count("Alignment", details->alignment_neighbor_count);
  optional_count("Cohesion", details->cohesion_neighbor_count);
  optional_count("Hue", details->hue_neighbor_count);
  if (details->neighbor_cap_hit.has_value()) {
    add_row(panel_model_, "Cap reached",
            *details->neighbor_cap_hit ? Warning : Muted, "%s",
            yes_no(*details->neighbor_cap_hit));
  }

  if (details->queried_cell_count.has_value() ||
      details->query_radius.has_value()) {
    add_section(panel_model_, "SPATIAL QUERY");
    optional_count("Queried cells", details->queried_cell_count);
    optional_count("Displayed cells", details->displayed_queried_cell_count);
    if (details->query_radius.has_value()) {
      add_row(panel_model_, "Query radius", Normal, "%.1f",
              *details->query_radius);
    }
    if (details->query_visualization_capped.has_value()) {
      add_row(panel_model_, "Visualization",
              *details->query_visualization_capped ? Warning : Muted, "%s",
              *details->query_visualization_capped ? "Capped" : "Complete");
    }
  }

  add_section(panel_model_, "RULE CONFIGURATION");
  if (details->separation_radius.has_value()) {
    add_row(panel_model_, "Separation radius", Normal, "%.1f",
            *details->separation_radius);
  }
  if (details->alignment_radius.has_value()) {
    add_row(panel_model_, "Alignment radius", Normal, "%.1f",
            *details->alignment_radius);
  }
  if (details->cohesion_radius.has_value()) {
    add_row(panel_model_, "Cohesion radius", Normal, "%.1f",
            *details->cohesion_radius);
  }
  if (details->field_of_view_degrees.has_value()) {
    add_row(panel_model_, "Field of view", Normal, "%.1f deg",
            *details->field_of_view_degrees);
  }

  add_section(panel_model_, "STEERING CONTRIBUTIONS");
  auto const vector_row = [&](char const *label, std::optional<Vec3f> value) {
    if (!value.has_value()) {
      return;
    }
    add_row(panel_model_, label, Normal, "(%.2f %.2f %.2f)  | %.2f", value->x,
            value->y, value->z, magnitude(*value));
  };
  vector_row("Separation", details->separation);
  vector_row("Alignment", details->alignment);
  vector_row("Cohesion", details->cohesion);
  vector_row("Containment", details->containment);
  vector_row("Wander", details->wander);
  vector_row("Final acceleration", details->acceleration);

  if (details->current_hue.has_value() || details->hue_target.has_value() ||
      details->hue_delta.has_value()) {
    add_section(panel_model_, "HUE BEHAVIOUR");
    if (details->current_hue.has_value()) {
      add_row(panel_model_, "Current hue", Normal, "%.3f",
              *details->current_hue);
    }
    if (details->hue_target.has_value()) {
      add_row(panel_model_, "Target hue", Normal, "%.3f", *details->hue_target);
    }
    if (details->hue_delta.has_value()) {
      add_row(panel_model_, "Delta", Normal, "%.4f", *details->hue_delta);
    }
    if (details->applied_hue_step.has_value()) {
      add_row(panel_model_, "Applied step", Normal, "%.4f",
              *details->applied_hue_step);
    }
    if (details->wander_active.has_value()) {
      add_row(panel_model_, "Wander", Muted, "%s",
              yes_no(*details->wander_active));
    }
    if (details->hue_assimilation_active.has_value()) {
      add_row(panel_model_, "Assimilation", Muted, "%s",
              yes_no(*details->hue_assimilation_active));
    }
    if (details->hue_drift_active.has_value()) {
      add_row(panel_model_, "Drift", Muted, "%s",
              yes_no(*details->hue_drift_active));
    }
  }

  add_section(panel_model_, "EXCEPTIONAL STATES");
  bool const exceptional = details->neighbor_cap_hit.value_or(false) ||
                           details->overlap_recovery.value_or(false) ||
                           details->acceleration_saturated.value_or(false) ||
                           details->wall_guard.value_or(false);
  if (!exceptional) {
    add_row(panel_model_, "Status", Success, "%s", "None");
  } else {
    add_row(panel_model_, "Neighbour cap",
            details->neighbor_cap_hit.value_or(false) ? Warning : Muted, "%s",
            details->neighbor_cap_hit.value_or(false) ? "Reached" : "No");
    add_row(panel_model_, "Overlap recovery",
            details->overlap_recovery.value_or(false) ? Warning : Muted, "%s",
            details->overlap_recovery.value_or(false) ? "Active" : "No");
    add_row(panel_model_, "Acceleration",
            details->acceleration_saturated.value_or(false) ? Warning : Muted,
            "%s",
            details->acceleration_saturated.value_or(false) ? "Saturated"
                                                            : "Normal");
    add_row(panel_model_, "Wall guard",
            details->wall_guard.value_or(false) ? Warning : Muted, "%s",
            details->wall_guard.value_or(false) ? "Active" : "No");
  }
}
} // namespace simnet
