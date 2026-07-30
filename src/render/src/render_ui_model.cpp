module;

#include <algorithm>
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
using render_detail::PanelModel;
using render_detail::UiValueState;

void copy_text(auto &destination, std::string_view text) noexcept {
  std::snprintf(destination.data(), destination.size(), "%.*s",
                static_cast<int>(text.size()), text.data());
}

void add_section(PanelModel &model, std::string_view title,
                 bool collapsible = false, bool expanded = true) noexcept {
  if (model.section_count >= model.sections.size()) {
    return;
  }
  auto &section = model.sections[model.section_count++];
  copy_text(section.title, title);
  section.first_row = model.row_count;
  section.row_count = 0;
  section.collapsible = collapsible;
  section.expanded = expanded;
}

template <typename... Args>
void add_row(PanelModel &model, std::string_view label, UiValueState state,
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

void add_text(PanelModel &model, std::string_view text,
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
    add_section(panel_model_, "RUNTIME");
    if (frame.info.simulation_paused.has_value()) {
      add_row(panel_model_, "State",
              *frame.info.simulation_paused ? Warning : Success, "%s",
              *frame.info.simulation_paused ? "Paused" : "Running");
    } else if (frame.info.context.kind == ViewerKind::Client &&
               frame.info.connection.has_value()) {
      add_row(panel_model_, "State", Normal, "%.*s",
              static_cast<int>(frame.info.connection->state.size()),
              frame.info.connection->state.data());
    } else {
      add_row(panel_model_, "State", Muted, "%s", "Unavailable");
    }
    if (!frame.info.status_message.empty()) {
      add_row(panel_model_, "Status", Warning, "%.*s",
              static_cast<int>(frame.info.status_message.size()),
              frame.info.status_message.data());
    }

    add_section(panel_model_, "WORLD");
    add_row(panel_model_, "Tick", Normal, "%llu",
            static_cast<unsigned long long>(frame.info.tick));
    if (valid_entities) {
      add_row(panel_model_, "Entities", Normal, "%zu", frame.entities.size());
    } else {
      add_row(panel_model_, "Entity view", Error, "%s", "Invalid");
      add_row(panel_model_, "IDs / positions", Warning, "%zu / %zu",
              frame.entities.ids.size(), frame.entities.positions.size());
      add_row(panel_model_, "Headings / hues", Warning, "%zu / %zu",
              frame.entities.headings.size(), frame.entities.hues.size());
    }
    auto const extent =
        frame.info.world_bounds.max - frame.info.world_bounds.min;
    add_row(panel_model_, "World size", Muted, "%.0f x %.0f x %.0f", extent.x,
            extent.y, extent.z);
    if (completed_stats_.skipped_entity_count != 0U) {
      add_row(panel_model_, "Invalid entities", Warning, "%u",
              completed_stats_.skipped_entity_count);
    }

    if (frame.info.fixed_tick_rate_hz.has_value()) {
      add_section(panel_model_, "SIMULATION");
      add_row(panel_model_, "Tick rate", Normal, "%.1f Hz",
              *frame.info.fixed_tick_rate_hz);
    }

    if (frame.info.interpolation.has_value()) {
      auto const &value = *frame.info.interpolation;
      add_section(panel_model_, "PRESENTATION");
      add_row(panel_model_, "Interpolation",
              value.interpolating ? Success : Muted, "%s",
              !value.enabled        ? "Off"
              : value.interpolating ? "Active"
                                    : "Holding");
      add_row(panel_model_, "Ticks", Normal, "%llu -> %llu",
              static_cast<unsigned long long>(value.from_tick),
              static_cast<unsigned long long>(value.to_tick));
      add_row(panel_model_, "Alpha", Normal, "%.3f", value.alpha);
      auto const distance = value.to_tick - value.from_tick;
      if (distance > 1U) {
        add_row(panel_model_, "Snapshot gap", Warning, "%llu ticks",
                static_cast<unsigned long long>(distance));
      }
    }

    if (frame.spatial.has_value()) {
      auto const &value = *frame.spatial;
      add_section(panel_model_, "SPATIAL");
      add_row(panel_model_, "Occupied cells", Normal, "%u",
              value.occupied_cell_count);
      add_row(panel_model_, "Maximum occupancy", Normal, "%u",
              value.max_cell_occupancy);
      add_row(panel_model_, "Average occupancy", Normal, "%.2f",
              value.average_occupied_cell_load);
      if (value.display_capped) {
        add_row(panel_model_, "Debug visualization", Warning,
                "Limited to %zu cells", value.cells.size());
      }
    }

    add_section(panel_model_, "RENDERING");
    add_row(panel_model_, "Frame", Normal, "%.1f ms / %d FPS",
            milliseconds(frame.info.frame_delta), GetFPS());
    add_row(panel_model_, "Viewer CPU", Normal, "%.3f ms",
            milliseconds(completed_stats_.viewer_cpu_time));
    if (valid_entities &&
        completed_stats_.instance_count != frame.entities.size()) {
      add_row(panel_model_, "Rendered", Warning, "%u / %zu",
              completed_stats_.instance_count, frame.entities.size());
    }
    add_row(panel_model_, "Draw calls", Normal, "%u",
            completed_stats_.draw_calls);
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
      auto const &connection = *frame.info.connection;
      if (frame.info.context.kind == ViewerKind::Server &&
          connection.connected_peer_count.value_or(0U) == 0U) {
        add_text(panel_model_, "Listening for client.", Success);
      } else {
        add_row(panel_model_, "State", Success, "%.*s",
                static_cast<int>(connection.state.size()),
                connection.state.data());
      }
      if (connection.peer.has_value()) {
        add_row(panel_model_, "Peer", Normal, "%u", *connection.peer);
      }
    } else {
      add_text(panel_model_, "Connection state is pending.");
    }

    if (!frame.info.replication.has_value()) {
      add_section(panel_model_, "REPLICATION");
      add_text(panel_model_, "No replication data.");
      add_text(panel_model_,
               "Snapshot state appears after a session is established.");
      return;
    }

    auto const &value = *frame.info.replication;
    add_section(panel_model_, "REPLICATION");
    auto sequence = [&](std::string_view label,
                        std::optional<SequenceId> current) {
      if (current.has_value()) {
        add_row(panel_model_, label, Normal, "%u", *current);
      }
    };
    sequence("Latest sent", value.latest_emitted_sequence);
    sequence("Latest received", value.latest_received_sequence);
    sequence("Latest applied", value.latest_applied_sequence);
    sequence("ACK baseline", value.acknowledged_baseline_sequence);
    if (value.latest_snapshot_tick.has_value()) {
      add_row(panel_model_, "Latest snapshot tick", Normal, "%llu",
              static_cast<unsigned long long>(*value.latest_snapshot_tick));
    }
    if (frame.info.context.kind == ViewerKind::Client &&
        value.latest_snapshot_tick.has_value()) {
      add_row(panel_model_, "Rendered tick", Normal, "%llu",
              static_cast<unsigned long long>(frame.info.tick));
      auto const delay = *value.latest_snapshot_tick > frame.info.tick
                             ? *value.latest_snapshot_tick - frame.info.tick
                             : 0U;
      add_row(panel_model_, "Presentation delay", delay > 1U ? Warning : Normal,
              "%llu tick%s", static_cast<unsigned long long>(delay),
              delay == 1U ? "" : "s");
    }

    if (value.retained_snapshot_count.has_value()) {
      auto const index = panel_model_.section_count;
      auto const expanded =
          (ui_.expanded_sections[static_cast<std::size_t>(Network)] &
           (1U << index)) != 0U;
      add_section(panel_model_, "ADVANCED HISTORY", true, expanded);
      add_row(panel_model_, "Retained snapshots", Normal, "%u",
              *value.retained_snapshot_count);
      sequence("Oldest retained", value.oldest_retained_sequence);
      sequence("Newest retained", value.newest_retained_sequence);
    }
    return;
  }

  if (ui_.page == Setup) {
    if (!frame.setup.has_value()) {
      add_section(panel_model_, "EXPERIMENT SETUP");
      add_text(panel_model_, "Effective setup information is unavailable.");
      return;
    }
    if (setup_revision_ != frame.setup->revision) {
      panel_model_.clear();
      for (auto const &source : frame.setup->sections) {
        add_section(panel_model_, source.title, true,
                    source.initially_expanded);
        for (auto const &row : source.rows) {
          add_row(panel_model_, row.label, Normal, "%.*s",
                  static_cast<int>(row.value.size()), row.value.data());
        }
      }
      setup_panel_model_ = panel_model_;
      setup_revision_ = frame.setup->revision;
    } else {
      panel_model_ = setup_panel_model_;
    }
    for (std::size_t index = 0; index < panel_model_.section_count; ++index) {
      auto const expanded =
          (ui_.expanded_sections[static_cast<std::size_t>(Setup)] &
           (1U << index)) != 0U;
      panel_model_.sections[index].expanded = expanded;
    }
    return;
  }

  add_section(panel_model_, "ENTITY");
  if (!selected_entity_frame_.has_value()) {
    add_text(panel_model_, "No entity selected.");
    add_text(panel_model_, "Left-click an entity in the viewport.");
    if (!frame.info.capabilities.has_entity_diagnostics) {
      add_text(panel_model_,
               "Detailed diagnostics require an authoritative source.");
    }
    return;
  }

  auto const &selected = *selected_entity_frame_;
  add_row(panel_model_, "Entity ID", Normal, "%u", selected.id);
  add_row(panel_model_, "Position", Normal, "%.2f  %.2f  %.2f",
          selected.position.x, selected.position.y, selected.position.z);
  auto const *details = frame.selected_details.has_value() &&
                                frame.selected_details->id == selected.id
                            ? &*frame.selected_details
                            : nullptr;
  if (details == nullptr) {
    add_text(
        panel_model_,
        frame.info.context.kind == ViewerKind::Client
            ? "Replicated presentation; authoritative diagnostics unavailable."
            : "Waiting for selected diagnostics.");
    return;
  }

  add_section(panel_model_, "MOTION");
  if (details->speed.has_value()) {
    if (details->maximum_speed.has_value()) {
      add_row(panel_model_, "Speed", Normal, "%.2f / %.2f", *details->speed,
              *details->maximum_speed);
    } else {
      add_row(panel_model_, "Speed", Normal, "%.2f", *details->speed);
    }
  }
  if (details->acceleration.has_value()) {
    add_row(panel_model_, "Acceleration", Normal, "%.2f",
            magnitude(*details->acceleration));
  }

  add_section(panel_model_, "NEIGHBOUR QUERY");
  if (details->raw_candidate_count.has_value()) {
    add_row(panel_model_, "Candidates", Normal, "%u",
            *details->raw_candidate_count);
  }
  if (details->retained_neighbor_count.has_value()) {
    add_row(panel_model_, "Retained", Normal, "%u",
            *details->retained_neighbor_count);
  }

  add_section(panel_model_, "RULE PARTICIPATION");
  auto count = [&](std::string_view label,
                   std::optional<std::uint32_t> current) {
    if (current.has_value()) {
      add_row(panel_model_, label, Normal, "%u", *current);
    }
  };
  count("Separation", details->separation_neighbor_count);
  count("Alignment", details->alignment_neighbor_count);
  count("Cohesion", details->cohesion_neighbor_count);
  count("Hue", details->hue_neighbor_count);

  add_section(panel_model_, "STEERING");
  auto contribution = [&](std::string_view label,
                          std::optional<Vec3f> current) {
    if (current.has_value()) {
      add_row(panel_model_, label, Normal, "%.2f", magnitude(*current));
    }
  };
  contribution("Separation", details->separation);
  contribution("Alignment", details->alignment);
  contribution("Cohesion", details->cohesion);
  contribution("Containment", details->containment);
  contribution("Wander", details->wander);
  contribution("Final acceleration", details->acceleration);

  add_section(panel_model_, "EXCEPTIONAL STATES");
  bool const exceptional = details->neighbor_cap_hit.value_or(false) ||
                           details->overlap_recovery.value_or(false) ||
                           details->acceleration_saturated.value_or(false) ||
                           details->wall_guard.value_or(false);
  if (!exceptional) {
    add_text(panel_model_, "None", Success);
  } else {
    if (details->neighbor_cap_hit.value_or(false)) {
      add_text(panel_model_, "Neighbour limit reached", Warning);
    }
    if (details->overlap_recovery.value_or(false)) {
      add_text(panel_model_, "Overlap recovery active", Warning);
    }
    if (details->acceleration_saturated.value_or(false)) {
      add_text(panel_model_, "Acceleration saturated", Warning);
    }
    if (details->wall_guard.value_or(false)) {
      add_text(panel_model_, "Wall guard active", Warning);
    }
  }

  auto const advanced_index = panel_model_.section_count;
  auto const advanced_expanded =
      (ui_.expanded_sections[static_cast<std::size_t>(Entity)] &
       (1U << advanced_index)) != 0U;
  add_section(panel_model_, "ADVANCED DIAGNOSTICS", true, advanced_expanded);
  if (details->velocity.has_value()) {
    auto const value = *details->velocity;
    add_row(panel_model_, "Velocity", Normal, "%.2f  %.2f  %.2f", value.x,
            value.y, value.z);
  }
  add_row(panel_model_, "Heading", Normal, "%.2f  %.2f  %.2f",
          selected.heading.x, selected.heading.y, selected.heading.z);
  if (details->acceleration.has_value()) {
    auto const value = *details->acceleration;
    add_row(panel_model_, "Acceleration vector", Normal, "%.2f  %.2f  %.2f",
            value.x, value.y, value.z);
  }
  if (details->current_cell.has_value()) {
    add_row(panel_model_, "Current cell", Normal, "%d  %d  %d",
            details->current_cell->x, details->current_cell->y,
            details->current_cell->z);
  }
  count("Cells visited", details->queried_cell_count);
  if (details->last_update_tick.has_value()) {
    add_row(panel_model_, "Update tick", Normal, "%llu",
            static_cast<unsigned long long>(*details->last_update_tick));
  }
  if (details->last_update_sequence.has_value()) {
    add_row(panel_model_, "Update sequence", Normal, "%u",
            *details->last_update_sequence);
  }
  if (details->current_hue.has_value()) {
    add_row(panel_model_, "Current hue", Normal, "%.3f", *details->current_hue);
  }
  if (details->hue_target.has_value()) {
    add_row(panel_model_, "Hue target", Normal, "%.3f", *details->hue_target);
  }
  if (details->hue_delta.has_value()) {
    add_row(panel_model_, "Hue delta", Normal, "%.4f", *details->hue_delta);
  }
  if (details->applied_hue_step.has_value()) {
    add_row(panel_model_, "Hue step", Normal, "%.4f",
            *details->applied_hue_step);
  }
  if (details->hue_assimilation_active.value_or(false)) {
    add_row(panel_model_, "Hue update", Normal, "%s", "Assimilation");
  } else if (details->hue_drift_active.value_or(false)) {
    add_row(panel_model_, "Hue update", Normal, "%s", "Isolated drift");
  }
}
} // namespace simnet
