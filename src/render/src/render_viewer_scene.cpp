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
#include <raymath.h>
#include <rlgl.h>

#include <simnet/telemetry_trace.hpp>

module simnet.render;

import :ui;
import :viewer_impl;
import simnet.core;
import simnet.telemetry;

namespace simnet {
using namespace render_detail;

void Viewer::Impl::clear_instances() {
  SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.clear_buckets",
                              simnet::LogCategory::Render);
  for (auto &bucket : transform_buckets_) {
    bucket.clear();
  }
}

void Viewer::Impl::prepare_instances(RenderEntityView const &entities,
                                     RenderStats &stats) {
  clear_instances();
  {
    SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.capacity_growth",
                                simnet::LogCategory::Render);
    auto const per_bucket = entities.size() / hue_bucket_count + 1U;
    for (auto &bucket : transform_buckets_) {
      if (bucket.capacity() < per_bucket) {
        bucket.reserve(per_bucket);
      }
    }
  }
  {
    SIMNET_TRACE_SCOPE_CATEGORY("render.prepare.transforms",
                                simnet::LogCategory::Render);
    for (std::size_t index = 0; index < entities.size(); ++index) {
      auto const position = entities.positions[index];
      auto const heading = entities.headings[index];
      if (!finite(position) || !finite(heading)) {
        ++stats.skipped_entity_count;
        continue;
      }
      transform_buckets_[hue_bucket(entities.hues[index])].push_back(
          entity_transform(position, heading, config_.entity_scale));
      ++stats.instance_count;
    }
  }
}

void Viewer::Impl::draw_stationary_observer(
    StationaryObserverView const &observer, RenderStats &stats) {
  SIMNET_TRACE_SCOPE_CATEGORY("render.stationary_observer_geometry",
                              simnet::LogCategory::Render);
  auto const position = to_raylib(observer.position);
  auto const forward = normalized_or_forward(observer.forward);
  auto const direction_end =
      to_raylib(observer.position + forward * observer.interest_radius);
  DrawSphere(position, std::max(config_.entity_scale, 1.0F) * 1.5F,
             Color{247, 184, 74, 255});
  DrawLine3D(position, direction_end, Color{247, 184, 74, 255});
  if (overlays_.observer_radius) {
    DrawSphereWires(position, observer.interest_radius, 20, 20,
                    Color{247, 184, 74, 110});
  }
  if (overlays_.observer_frustum) {
    auto const basis = world_up_basis(forward);
    auto const aspect = static_cast<float>(scene_rect_.width) /
                        static_cast<float>(scene_rect_.height);
    auto const vertical = observer.vertical_fov_degrees * DEG2RAD;
    auto const vertical_half =
        std::tan(vertical * 0.5F) * observer.interest_radius;
    auto const horizontal_half = vertical_half * aspect;
    auto const center = observer.position + forward * observer.interest_radius;
    auto const corner = [&](float horizontal, float vertical_offset) {
      return to_raylib(center + basis.right * horizontal +
                       basis.up * vertical_offset);
    };
    auto const top_left = corner(-horizontal_half, vertical_half);
    auto const top_right = corner(horizontal_half, vertical_half);
    auto const bottom_left = corner(-horizontal_half, -vertical_half);
    auto const bottom_right = corner(horizontal_half, -vertical_half);
    auto const color = Color{247, 184, 74, 150};
    DrawLine3D(position, top_left, color);
    DrawLine3D(position, top_right, color);
    DrawLine3D(position, bottom_left, color);
    DrawLine3D(position, bottom_right, color);
    DrawLine3D(top_left, top_right, color);
    DrawLine3D(top_right, bottom_right, color);
    DrawLine3D(bottom_right, bottom_left, color);
    DrawLine3D(bottom_left, top_left, color);
  }
  ++stats.draw_calls;
}

void Viewer::Impl::draw_spatial_cells(SpatialDebugView const &spatial,
                                      RenderStats &stats) {
  SIMNET_TRACE_SCOPE_CATEGORY("render.spatial_geometry",
                              simnet::LogCategory::Render);
  for (auto const &cell : spatial.cells) {
    auto const intensity = static_cast<unsigned char>(
        std::min(220U, 55U + cell.entity_count * 12U));
    DrawBoundingBox(
        {
            .min = to_raylib(cell.bounds.min),
            .max = to_raylib(cell.bounds.max),
        },
        Color{85, 179, 226, intensity});
  }
  if (!spatial.cells.empty()) {
    ++stats.draw_calls;
  }
}

void Viewer::Impl::draw_debug_primitives(DebugPrimitiveView const &debug,
                                         RenderStats &stats) {
  SIMNET_TRACE_SCOPE_CATEGORY("render.selected_debug_geometry",
                              simnet::LogCategory::Render);
  if (overlays_.rule_radii) {
    for (auto const &sphere : debug.spheres) {
      if (sphere.radius <= 0.0F || !std::isfinite(sphere.radius) ||
          !finite(sphere.center)) {
        continue;
      }
      DrawSphereWires(to_raylib(sphere.center), sphere.radius, 24, 12,
                      to_raylib(sphere.color));
      ++stats.draw_calls;
    }
  }
  if (overlays_.steering_vectors) {
    for (auto const &vector : debug.vectors) {
      if (!finite(vector.origin) || !finite(vector.vector) ||
          simnet::length_squared(vector.vector) <= 0.000001F) {
        continue;
      }
      DrawLine3D(to_raylib(vector.origin),
                 to_raylib(vector.origin + vector.vector),
                 to_raylib(vector.color));
      ++stats.draw_calls;
    }
  }
  if (overlays_.queried_cells) {
    for (auto const &box : debug.boxes) {
      if (!finite(box.bounds.min) || !finite(box.bounds.max)) {
        continue;
      }
      DrawBoundingBox(
          {.min = to_raylib(box.bounds.min), .max = to_raylib(box.bounds.max)},
          to_raylib(box.color));
      ++stats.draw_calls;
    }
  }
  if (overlays_.field_of_view) {
    for (auto const &cone : debug.cones) {
      if (!finite(cone.apex) || !finite(cone.direction) ||
          cone.length <= 0.0F || !std::isfinite(cone.length) ||
          cone.half_angle_degrees <= 0.0F ||
          cone.half_angle_degrees >= 180.0F ||
          !std::isfinite(cone.half_angle_degrees)) {
        continue;
      }
      auto const forward = normalized_or_forward(cone.direction);
      auto right = cross(forward, {0.0F, 1.0F, 0.0F});
      if (simnet::length_squared(right) <= 0.000001F) {
        right = cross(forward, {1.0F, 0.0F, 0.0F});
      }
      right = normalized_or_forward(right);
      auto const up = normalized_or_forward(cross(right, forward));
      auto const angle = cone.half_angle_degrees * pi / 180.0F;
      auto const forward_scale = std::cos(angle);
      auto const lateral_scale = std::sin(angle);
      auto previous = simnet::Vec3f{};
      auto constexpr segments = 12;
      for (auto index = 0; index <= segments; ++index) {
        auto const around = 2.0F * pi * static_cast<float>(index) /
                            static_cast<float>(segments);
        auto const lateral = right * std::cos(around) + up * std::sin(around);
        auto const direction = normalized_or_forward(forward * forward_scale +
                                                     lateral * lateral_scale);
        auto const point = cone.apex + direction * cone.length;
        DrawLine3D(to_raylib(cone.apex), to_raylib(point),
                   to_raylib(cone.color));
        if (index != 0) {
          DrawLine3D(to_raylib(previous), to_raylib(point),
                     to_raylib(cone.color));
        }
        previous = point;
      }
      ++stats.draw_calls;
    }
  }
}

void Viewer::Impl::draw_debug_labels(DebugPrimitiveView const &debug) const {
  if (!overlays_.debug_labels) {
    return;
  }
  auto drawn = std::size_t{};
  auto constexpr label_limit = std::size_t{12};
  auto label = [&](Vec3f position, std::string_view value, DebugColor color) {
    if (value.empty() || drawn >= label_limit || !finite(position)) {
      return;
    }
    auto const screen = GetWorldToScreenEx(
        to_raylib(position), camera_, scene_rect_.width, scene_rect_.height);
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*s", static_cast<int>(value.size()),
                  value.data());
    DrawTextEx(font_, text, {screen.x + 6.0F, screen.y - 8.0F},
               typography.debug_label, 0.75F, to_raylib(color));
    ++drawn;
  };
  for (auto const &sphere : debug.spheres) {
    label(sphere.center, sphere.label, sphere.color);
  }
  for (auto const &vector : debug.vectors) {
    label(vector.origin + vector.vector, vector.label, vector.color);
  }
  for (auto const &box : debug.boxes) {
    label((box.bounds.min + box.bounds.max) * 0.5F, box.label, box.color);
  }
  for (auto const &cone : debug.cones) {
    label(cone.apex + normalized_or_forward(cone.direction) * cone.length,
          cone.label, cone.color);
  }
}

void Viewer::Impl::draw_selected_trail(RenderStats &stats) const {
  if (!overlays_.selected_trail || selected_trail_.size() < 2U) {
    return;
  }
  rlBegin(RL_LINES);
  for (std::size_t index = 1; index < selected_trail_.size(); ++index) {
    auto const alpha = static_cast<unsigned char>(
        55.0F + 190.0F * static_cast<float>(index) /
                    static_cast<float>(selected_trail_.size() - 1U));
    auto const start = selected_trail_[index - 1U];
    auto const end = selected_trail_[index];
    rlColor4ub(255U, 205U, 90U, alpha);
    rlVertex3f(start.x, start.y, start.z);
    rlVertex3f(end.x, end.y, end.z);
  }
  rlEnd();
  ++stats.draw_calls;
}

void Viewer::Impl::draw_scene(RenderFrame const &frame, RenderStats &stats) {
  BeginTextureMode(scene_);
  ClearBackground(Color{10, 13, 18, 255});
  BeginMode3D(camera_);
  auto const aspect = static_cast<double>(scene_rect_.width) /
                      static_cast<double>(scene_rect_.height);
  rlSetMatrixProjection(
      MatrixPerspective(camera_.fovy * DEG2RAD, aspect, 0.01, 10000.0));
  rlSetMatrixModelview(
      MatrixLookAt(camera_.position, camera_.target, camera_.up));

  if (overlays_.world_bounds) {
    auto const bounds = frame.info.world_bounds;
    auto const center = Vector3{
        (bounds.min.x + bounds.max.x) * 0.5F,
        (bounds.min.y + bounds.max.y) * 0.5F,
        (bounds.min.z + bounds.max.z) * 0.5F,
    };
    DrawCubeWires(center, bounds.max.x - bounds.min.x,
                  bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z,
                  Color{95, 112, 136, 72});
  }
  if (overlays_.origin_axes) {
    DrawLine3D({0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}, RED);
    DrawLine3D({0.0F, 0.0F, 0.0F}, {0.0F, 10.0F, 0.0F}, GREEN);
    DrawLine3D({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 10.0F}, BLUE);
  }
  if (frame.stationary_observer.has_value() &&
      mode_ != CameraMode::StationaryObserver && overlays_.observer_marker) {
    draw_stationary_observer(*frame.stationary_observer, stats);
  }
  if (frame.spatial.has_value() && overlays_.spatial_cells) {
    draw_spatial_cells(*frame.spatial, stats);
  }
  if (!frame.debug_primitives.empty()) {
    draw_debug_primitives(frame.debug_primitives, stats);
  }
  draw_selected_trail(stats);

  if (instancing_available_) {
    for (std::size_t index = 0; index < transform_buckets_.size(); ++index) {
      auto const &bucket = transform_buckets_[index];
      if (bucket.empty()) {
        continue;
      }
      auto const color =
          hue_color(static_cast<std::uint8_t>(index * 256U / hue_bucket_count));
      for (int mesh_index = 0; mesh_index < model_.meshCount; ++mesh_index) {
        auto const material_index = model_.meshMaterial[mesh_index];
        if (material_index < 0 || material_index >= model_.materialCount) {
          continue;
        }
        auto &material = model_.materials[material_index];
        material.maps[MATERIAL_MAP_DIFFUSE].color = color;
        DrawMeshInstanced(model_.meshes[mesh_index], material, bucket.data(),
                          static_cast<int>(bucket.size()));
        ++stats.draw_calls;
      }
      ++stats.active_hue_buckets;
    }
  }
  if (selected_entity_frame_.has_value() && overlays_.selected_marker) {
    auto const radius =
        std::max(config_.picking_radius, config_.entity_scale * 1.5F);
    DrawSphereWires(to_raylib(selected_entity_frame_->position), radius, 8, 12,
                    render_detail::palette.selection);
    ++stats.draw_calls;
  }
  EndMode3D();
  draw_debug_labels(frame.debug_primitives);
  EndTextureMode();
}
} // namespace simnet
