#pragma once

#include "render_ui.hpp"

namespace simnet::render_detail {
inline constexpr std::size_t hue_bucket_count = 32;
inline constexpr std::size_t selected_trail_max_points = 2400;
inline constexpr float pi = 3.14159265358979323846F;
inline constexpr float min_pitch = -pi * 0.48F;
inline constexpr float max_pitch = pi * 0.48F;
inline constexpr float minimum_distance = 2.0F;

[[nodiscard]] inline Vector3 to_raylib(Vec3f value) noexcept {
  return {value.x, value.y, value.z};
}

[[nodiscard]] inline Color to_raylib(DebugColor value) noexcept {
  return {value.red, value.green, value.blue, value.alpha};
}

[[nodiscard]] inline bool finite(Vec3f value) noexcept {
  return is_finite(value);
}

[[nodiscard]] inline Vec3f normalized_or_forward(Vec3f heading) noexcept {
  auto const magnitude_squared = length_squared(heading);
  if (!std::isfinite(magnitude_squared) || magnitude_squared <= 0.000001F) {
    return {0.0F, 0.0F, 1.0F};
  }
  return heading / std::sqrt(magnitude_squared);
}

[[nodiscard]] inline Vec3f cross(Vec3f lhs, Vec3f rhs) noexcept {
  return {
      .x = lhs.y * rhs.z - lhs.z * rhs.y,
      .y = lhs.z * rhs.x - lhs.x * rhs.z,
      .z = lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

struct WorldUpBasis {
  Vec3f right{.x = 1.0F};
  Vec3f up{.y = 1.0F};
};

[[nodiscard]] inline WorldUpBasis world_up_basis(Vec3f forward) noexcept {
  auto const horizontal =
      normalize_or(Vec3f{.x = forward.x, .z = forward.z}, Vec3f{.z = 1.0F});
  auto const right =
      normalize_or(cross(Vec3f{.y = 1.0F}, horizontal), Vec3f{.x = 1.0F});
  return {
      .right = right,
      .up = normalize_or(cross(forward, right), Vec3f{.y = 1.0F}),
  };
}

[[nodiscard]] inline Matrix entity_transform(Vec3f position, Vec3f heading,
                                             float scale) noexcept {
  auto const forward = normalized_or_forward(heading);
  auto const sign = std::copysign(1.0F, forward.z);
  auto const a = -1.0F / (sign + forward.z);
  auto const b = forward.x * forward.y * a;
  auto const right = Vec3f{
      .x = 1.0F + sign * forward.x * forward.x * a,
      .y = sign * b,
      .z = -sign * forward.x,
  };
  auto const up = Vec3f{
      .x = b,
      .y = sign + forward.y * forward.y * a,
      .z = -forward.y,
  };
  return {
      .m0 = right.x * scale,
      .m4 = up.x * scale,
      .m8 = forward.x * scale,
      .m12 = position.x,
      .m1 = right.y * scale,
      .m5 = up.y * scale,
      .m9 = forward.y * scale,
      .m13 = position.y,
      .m2 = right.z * scale,
      .m6 = up.z * scale,
      .m10 = forward.z * scale,
      .m14 = position.z,
      .m3 = 0.0F,
      .m7 = 0.0F,
      .m11 = 0.0F,
      .m15 = 1.0F,
  };
}

[[nodiscard]] inline Color hue_color(std::uint8_t hue) noexcept {
  return ColorFromHSV(static_cast<float>(hue) * (360.0F / 256.0F), 0.72F,
                      0.96F);
}

[[nodiscard]] inline std::size_t hue_bucket(std::uint8_t hue) noexcept {
  return static_cast<std::size_t>(hue) * hue_bucket_count / 256U;
}
} // namespace simnet::render_detail

namespace simnet {
class Viewer::Impl {
public:
  explicit Impl(ViewerConfig config);
  ~Impl();

  [[nodiscard]] ViewerResult draw(RenderFrame const &frame);
  void set_camera_mode(CameraMode mode) noexcept;

private:
  struct SelectedEntity {
    EntityNetId id{};
    Vec3f position{};
    Vec3f heading{};
    std::uint8_t hue{};
  };

  void load_entity_model();
  [[nodiscard]] std::optional<SelectedEntity>
  find_selected_entity(RenderEntityView const &entities) const;
  void clear_selection(ViewerResult &result,
                       bool preserve_navigation_anchor = false);
  void update_camera(RenderFrame const &frame, ViewerResult &result);
  void reset_active_camera(RenderFrame const &frame) noexcept;
  void update_camera_position(float yaw, float pitch, float distance) noexcept;
  void reset_overview_camera(Vec3f center) noexcept;
  void reset_detail_camera(float world_extent) noexcept;
  [[nodiscard]] bool mouse_in_scene(Vector2 mouse) const noexcept;
  [[nodiscard]] static std::optional<float>
  ray_sphere_hit_distance(Ray ray, Vector3 center, float radius) noexcept;
  void update_selection(RenderEntityView const &entities, ViewerResult &result);
  void select_entity(SelectedEntity selected, ViewerResult &result);
  void update_selected_trail();
  void select_adjacent_entity(RenderEntityView const &entities, int direction,
                              ViewerResult &result);

  void update_panel_input(RenderFrame const &frame, ViewerResult &result);
  void build_panel_model(RenderFrame const &frame, bool valid_entities);
  void draw_panel(RenderFrame const &frame, bool valid_entities,
                  RenderStats const &stats, ViewerResult const &result);
  void draw_help_overlay(RenderFrame const &frame) const;
  void draw_viewport_ui(RenderFrame const &frame, ViewerResult const &result);
  void draw_orientation_gizmo() const;

  void clear_instances();
  void prepare_instances(RenderEntityView const &entities, RenderStats &stats);
  void draw_stationary_observer(StationaryObserverView const &observer,
                                RenderStats &stats);
  void draw_spatial_cells(SpatialDebugView const &spatial, RenderStats &stats);
  void draw_debug_primitives(DebugPrimitiveView const &debug,
                             RenderStats &stats);
  void draw_debug_labels(DebugPrimitiveView const &debug) const;
  void draw_selected_trail(RenderStats &stats) const;
  void draw_scene(RenderFrame const &frame, RenderStats &stats);

  ViewerConfig config_;
  render_detail::SceneRect scene_rect_{};
  RenderTexture2D scene_{};
  Font font_{};
  Mesh mesh_{};
  Model model_{};
  Shader shader_{};
  Camera3D camera_{};
  Vector3 target_{};
  CameraMode mode_{CameraMode::OverviewOrbit};
  bool instancing_available_{};
  bool camera_initialized_{};
  render_detail::OverlayState overlays_{};
  render_detail::UiState ui_{};
  render_detail::PanelModel panel_model_{};
  render_detail::PanelModel setup_panel_model_{};
  std::optional<std::uint64_t> setup_revision_{};
  RenderStats completed_stats_{};
  float overview_yaw_{render_detail::pi * 0.25F};
  float overview_pitch_{render_detail::pi / 6.0F};
  float overview_distance_{10.0F};
  float detail_yaw_{render_detail::pi * 0.25F};
  float detail_pitch_{render_detail::pi / 6.0F};
  float detail_distance_{};
  float min_distance_{render_detail::minimum_distance};
  float max_distance_{100.0F};
  float detail_min_distance_{render_detail::minimum_distance};
  float detail_max_distance_{100.0F};
  std::optional<EntityNetId> selected_entity_{};
  std::optional<EntityNetId> navigation_anchor_{};
  std::optional<SelectedEntity> selected_entity_frame_{};
  std::deque<Vec3f> selected_trail_{};
  std::array<std::vector<Matrix>, render_detail::hue_bucket_count>
      transform_buckets_;
};
} // namespace simnet
