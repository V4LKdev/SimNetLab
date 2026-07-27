module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <flecs.h>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <simnet/telemetry_trace.hpp>

module simnet.game_server;

import :snapshot;
import simnet.game_shared;
import simnet.snapshot;
import simnet.spatial;
import simnet.telemetry;

namespace
{
    struct Velocity
    {
        simnet::Vec3f value {};
    };

    struct FlockRow
    {
        std::uint32_t value {};
    };

    struct AuthoritativeExtractionEntry
    {
        simnet::EntityNetId id {};
        flecs::entity_t entity {};
        simnet::Vec3f position {};
        simnet::Vec3f heading {};
        std::uint8_t hue {};
    };

    struct AuthoritativeReplicationIndex
    {
        std::vector<simnet::EntityNetId> ids {};
        std::vector<flecs::entity_t> entities {};
        mutable flecs::query<
            const simnet::NetIdentity,
            const simnet::Position,
            const simnet::Heading,
            const simnet::Hue> snapshot_query {};
        mutable std::vector<AuthoritativeExtractionEntry> extraction_scratch {};
        float cruise_speed { 8.0F };

        void reserve(std::size_t count)
        {
            ids.reserve(count);
            entities.reserve(count);
            extraction_scratch.reserve(count);
        }
    };

    [[nodiscard]] bool valid_index(AuthoritativeReplicationIndex const& index) noexcept
    {
        return index.ids.size() == index.entities.size();
    }

    [[nodiscard]] bool valid_boid_state(simnet::BoidState const& boid) noexcept
    {
        return boid.id != 0U
            && simnet::is_finite(boid.position)
            && simnet::is_finite(boid.heading)
            && std::abs(simnet::length(boid.heading) - 1.0F) <= simnet::heading_normalization_tolerance;
    }

    [[nodiscard]] auto find_index(AuthoritativeReplicationIndex const& index, simnet::EntityNetId id)
    {
        return std::lower_bound(index.ids.begin(), index.ids.end(), id);
    }

    void set_authoritative_boid_components(flecs::entity entity, simnet::BoidState const& boid)
    {
        entity.set<simnet::NetIdentity>({ .id = boid.id });
        entity.set<simnet::Position>({ .value = boid.position });
        entity.set<simnet::Heading>({ .value = boid.heading });
        entity.set<simnet::Hue>({ .value = boid.hue });
    }

    void set_authoritative_simulation_components(
        flecs::entity entity,
        simnet::BoidState const& boid,
        std::uint32_t row,
        float cruise_speed
    )
    {
        set_authoritative_boid_components(entity, boid);
        entity.set<Velocity>({ .value = boid.heading * cruise_speed });
        entity.set<FlockRow>({ .value = row });
    }

    void remap_rows(
        flecs::world& world,
        AuthoritativeReplicationIndex const& index,
        std::size_t first
    )
    {
        for (auto row = first; row < index.entities.size(); ++row) {
            flecs::entity { world, index.entities[row] }.set<FlockRow>({
                .value = static_cast<std::uint32_t>(row),
            });
        }
    }

    void reset_failed_snapshot(simnet::WorldSnapshot& snapshot, simnet::Tick tick)
    {
        snapshot.clear();
        snapshot.tick = tick;
    }

}

namespace simnet
{
    struct ServerGameRuntime::Impl
    {
        struct State
        {
            std::vector<EntityNetId> ids {};
            std::vector<Vec3f> positions {};
            std::vector<Vec3f> velocities {};
            std::vector<Vec3f> headings {};

            void resize(std::size_t count)
            {
                ids.resize(count);
                positions.resize(count);
                velocities.resize(count);
                headings.resize(count);
            }
        };

        struct NextState
        {
            std::vector<Vec3f> positions {};
            std::vector<Vec3f> velocities {};
            std::vector<Vec3f> headings {};
            std::vector<std::uint8_t> written {};

            void resize(std::size_t count)
            {
                positions.resize(count);
                velocities.resize(count);
                headings.resize(count);
                written.resize(count);
            }
        };

        struct Neighbor
        {
            std::uint32_t row {};
            EntityNetId id {};
            float distance_squared {};
        };

        struct WorkerScratch
        {
            std::vector<Neighbor> neighbors {};
            std::optional<SelectedBoidDebug> selected {};
            std::uint64_t raw_candidate_total {};
            std::uint64_t retained_neighbor_total {};
            std::uint64_t separation_neighbor_total {};
            std::uint64_t social_neighbor_total {};
            double nearest_neighbor_distance_total {};
            double speed_total {};
            double acceleration_total {};
            Vec3f heading_total {};
            float speed_min { std::numeric_limits<float>::max() };
            float speed_max {};
            float acceleration_max {};
            std::uint32_t processed_boids {};
            std::uint32_t raw_candidate_max {};
            std::uint32_t retained_neighbor_max {};
            std::uint32_t nearest_neighbor_samples {};
            std::uint32_t isolated_boids {};
            std::uint32_t acceleration_saturations {};
            std::uint32_t overlap_recoveries {};
            std::uint32_t wall_guards {};
            std::uint32_t cap_hits {};
        };

        explicit Impl(BoidSimulationSettings value)
            : settings(value)
        {
        }

        BoidSimulationSettings settings {};
        State current {};
        NextState next {};
        SpatialGrid grid {};
        SpatialGridScratch grid_scratch {};
        std::vector<WorkerScratch> workers {};
        WorkerScratch inspection {};
        std::vector<std::uint8_t> capture_seen {};
        flecs::query<
            const NetIdentity,
            const Position,
            const Heading,
            const Velocity,
            const FlockRow> capture_query {};
        std::optional<EntityNetId> selected_id {};
        std::optional<SelectedBoidDebug> selected_debug {};
        ServerGameStepReport report {};
        std::chrono::steady_clock::time_point progress_started {};
        std::chrono::steady_clock::time_point compute_started {};
        std::chrono::steady_clock::time_point commit_started {};
        std::size_t prepared_entity_count {};
        float last_delta_time {};
        bool prepared {};
        bool phase_valid {};
    };
}

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float vector_epsilon_squared = 1.0e-12F;
    constexpr float overlap_epsilon_squared = 1.0e-8F;

    struct BoidEvaluation
    {
        simnet::Vec3f next_position {};
        simnet::Vec3f next_velocity {};
        simnet::Vec3f next_heading {};
        simnet::Vec3f acceleration {};
        simnet::Vec3f separation {};
        simnet::Vec3f alignment {};
        simnet::Vec3f cohesion {};
        simnet::Vec3f containment {};
        std::uint32_t raw_candidate_count {};
        std::uint32_t retained_neighbor_count {};
        std::uint32_t queried_cell_count {};
        std::uint32_t separation_neighbor_count {};
        std::uint32_t alignment_neighbor_count {};
        std::uint32_t cohesion_neighbor_count {};
        bool neighbor_cap_hit {};
        bool overlap_recovery {};
        bool acceleration_saturated {};
        bool wall_guard {};
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start) noexcept
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    void reset_worker_diagnostics(simnet::ServerGameRuntime::Impl::WorkerScratch& worker) noexcept
    {
        worker.raw_candidate_total = 0U;
        worker.retained_neighbor_total = 0U;
        worker.separation_neighbor_total = 0U;
        worker.social_neighbor_total = 0U;
        worker.nearest_neighbor_distance_total = 0.0;
        worker.speed_total = 0.0;
        worker.acceleration_total = 0.0;
        worker.heading_total = {};
        worker.speed_min = std::numeric_limits<float>::max();
        worker.speed_max = 0.0F;
        worker.acceleration_max = 0.0F;
        worker.processed_boids = 0U;
        worker.raw_candidate_max = 0U;
        worker.retained_neighbor_max = 0U;
        worker.nearest_neighbor_samples = 0U;
        worker.isolated_boids = 0U;
        worker.acceleration_saturations = 0U;
        worker.overlap_recoveries = 0U;
        worker.wall_guards = 0U;
        worker.cap_hits = 0U;
    }

    [[nodiscard]] simnet::Vec3f clamp_length(simnet::Vec3f value, float maximum) noexcept
    {
        auto const squared = simnet::length_squared(value);
        if (maximum <= 0.0F || squared <= vector_epsilon_squared) {
            return maximum <= 0.0F ? simnet::Vec3f {} : value;
        }
        auto const maximum_squared = maximum * maximum;
        if (squared <= maximum_squared) {
            return value;
        }
        return value * (maximum / std::sqrt(squared));
    }

    [[nodiscard]] simnet::Vec3f clamp_speed(
        simnet::Vec3f velocity,
        simnet::Vec3f fallback_heading,
        float minimum,
        float maximum
    ) noexcept
    {
        auto const speed_squared = simnet::length_squared(velocity);
        if (speed_squared <= vector_epsilon_squared) {
            return minimum > 0.0F ? fallback_heading * minimum : simnet::Vec3f {};
        }
        auto const speed = std::sqrt(speed_squared);
        if (speed > maximum) {
            return velocity * (maximum / speed);
        }
        if (speed < minimum) {
            return velocity * (minimum / speed);
        }
        return velocity;
    }

    [[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] float signed_hash_component(std::uint64_t value) noexcept
    {
        auto const bits = static_cast<std::uint32_t>(mix64(value) >> 40U);
        return static_cast<float>(bits) / static_cast<float>(0xFFFFFFU) * 2.0F - 1.0F;
    }

    [[nodiscard]] simnet::Vec3f overlap_direction(
        simnet::EntityNetId lhs,
        simnet::EntityNetId rhs
    ) noexcept
    {
        auto const low = std::min(lhs, rhs);
        auto const high = std::max(lhs, rhs);
        auto const key = (static_cast<std::uint64_t>(low) << 32U) | high;
        auto const base = simnet::normalize_or(
            simnet::Vec3f {
                signed_hash_component(key),
                signed_hash_component(key ^ 0x6a09e667f3bcc909ULL),
                signed_hash_component(key ^ 0xbb67ae8584caa73bULL),
            },
            simnet::Vec3f { 1.0F, 0.0F, 0.0F }
        );
        return lhs < rhs ? base : base * -1.0F;
    }

    [[nodiscard]] bool neighbor_better(
        simnet::ServerGameRuntime::Impl::Neighbor const& lhs,
        simnet::ServerGameRuntime::Impl::Neighbor const& rhs
    ) noexcept
    {
        if (lhs.distance_squared != rhs.distance_squared) {
            return lhs.distance_squared < rhs.distance_squared;
        }
        return lhs.id < rhs.id;
    }

    void keep_neighbor(
        simnet::ServerGameRuntime::Impl::WorkerScratch& scratch,
        simnet::ServerGameRuntime::Impl::Neighbor candidate,
        std::uint32_t maximum
    )
    {
        if (scratch.neighbors.size() < maximum) {
            scratch.neighbors.push_back(candidate);
            std::push_heap(scratch.neighbors.begin(), scratch.neighbors.end(), neighbor_better);
            return;
        }
        if (!neighbor_better(candidate, scratch.neighbors.front())) {
            return;
        }
        std::pop_heap(scratch.neighbors.begin(), scratch.neighbors.end(), neighbor_better);
        scratch.neighbors.back() = candidate;
        std::push_heap(scratch.neighbors.begin(), scratch.neighbors.end(), neighbor_better);
    }

    [[nodiscard]] float containment_axis(
        float predicted,
        float minimum,
        float maximum,
        float margin
    ) noexcept
    {
        auto result = 0.0F;
        auto const lower_distance = predicted - minimum;
        if (lower_distance < margin) {
            auto const factor = std::clamp((margin - lower_distance) / margin, 0.0F, 1.0F);
            result += factor * factor;
        }
        auto const upper_distance = maximum - predicted;
        if (upper_distance < margin) {
            auto const factor = std::clamp((margin - upper_distance) / margin, 0.0F, 1.0F);
            result -= factor * factor;
        }
        return result;
    }

    [[nodiscard]] simnet::Vec3f containment_acceleration(
        simnet::ServerGameRuntime::Impl const& runtime,
        simnet::Vec3f position,
        simnet::Vec3f velocity
    ) noexcept
    {
        auto const& settings = runtime.settings;
        auto const predicted = position + velocity * settings.containment_prediction_seconds;
        auto const half = settings.world_half;
        return clamp_length(
            simnet::Vec3f {
                containment_axis(predicted.x, -half, half, settings.containment_margin),
                containment_axis(predicted.y, -half, half, settings.containment_margin),
                containment_axis(predicted.z, -half, half, settings.containment_margin),
            } * settings.containment_acceleration,
            settings.containment_acceleration
        );
    }

    [[nodiscard]] bool hard_guard_axis(
        float half,
        float& position,
        float& velocity
    ) noexcept
    {
        if (position < -half) {
            position = -half;
            if (velocity < 0.0F) {
                velocity = 0.0F;
            }
            return true;
        }
        if (position > half) {
            position = half;
            if (velocity > 0.0F) {
                velocity = 0.0F;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] BoidEvaluation evaluate_boid_row(
        simnet::ServerGameRuntime::Impl const& runtime,
        simnet::ServerGameRuntime::Impl::WorkerScratch& scratch,
        std::uint32_t row,
        float delta_time
    )
    {
        auto const& settings = runtime.settings;
        auto const& current = runtime.current;
        auto const position = current.positions[row];
        auto const velocity = current.velocities[row];
        auto const heading = current.headings[row];
        auto const id = current.ids[row];

        scratch.neighbors.clear();
        auto raw_candidate_count = std::uint32_t {};
        auto const queried_cell_count = simnet::query_radius(
            runtime.grid,
            current.positions,
            position,
            settings.perception_radius,
            [&](std::uint32_t candidate_row) {
                if (candidate_row == row) {
                    return;
                }
                ++raw_candidate_count;
                auto const distance_squared = simnet::length_squared(
                    current.positions[candidate_row] - position
                );
                keep_neighbor(
                    scratch,
                    {
                        .row = candidate_row,
                        .id = current.ids[candidate_row],
                        .distance_squared = distance_squared,
                    },
                    settings.max_neighbors
                );
            }
        );
        std::ranges::sort(scratch.neighbors, neighbor_better);

        auto separation_sum = simnet::Vec3f {};
        auto alignment_velocity = simnet::Vec3f {};
        auto cohesion_position = simnet::Vec3f {};
        auto separation_count = std::uint32_t {};
        auto alignment_count = std::uint32_t {};
        auto cohesion_count = std::uint32_t {};
        auto overlap_recovery = false;
        auto const separation_radius_squared =
            settings.separation_radius * settings.separation_radius;
        auto const half_fov_radians =
            settings.field_of_view_degrees * std::numbers::pi_v<float> / 360.0F;
        auto const fov_cosine = std::cos(half_fov_radians);

        for (auto const& neighbor : scratch.neighbors) {
            auto const offset = current.positions[neighbor.row] - position;
            if (neighbor.distance_squared <= separation_radius_squared) {
                if (neighbor.distance_squared <= overlap_epsilon_squared) {
                    separation_sum = separation_sum + overlap_direction(id, neighbor.id);
                    ++scratch.overlap_recoveries;
                    overlap_recovery = true;
                } else {
                    separation_sum = separation_sum
                        - offset / std::max(neighbor.distance_squared, overlap_epsilon_squared);
                }
                ++separation_count;
            }

            auto const accepted_by_fov =
                settings.field_of_view_degrees >= 360.0F
                || simnet::dot(heading, offset)
                    >= fov_cosine * std::sqrt(neighbor.distance_squared);
            if (accepted_by_fov) {
                alignment_velocity = alignment_velocity + current.velocities[neighbor.row];
                cohesion_position = cohesion_position + current.positions[neighbor.row];
                ++alignment_count;
                ++cohesion_count;
            }
        }

        auto separation = simnet::Vec3f {};
        if (separation_count != 0U && simnet::length_squared(separation_sum) > vector_epsilon_squared) {
            auto const desired = simnet::normalize_or(separation_sum, heading)
                * settings.max_speed;
            separation = clamp_length(
                desired - velocity,
                settings.separation_acceleration
            );
        }

        auto alignment = simnet::Vec3f {};
        if (alignment_count != 0U) {
            auto const average = alignment_velocity / static_cast<float>(alignment_count);
            alignment = clamp_length(
                average - velocity,
                settings.alignment_acceleration
            );
        }

        auto cohesion = simnet::Vec3f {};
        if (cohesion_count != 0U) {
            auto const centroid = cohesion_position / static_cast<float>(cohesion_count);
            auto const desired = simnet::normalize_or(centroid - position, heading)
                * settings.cruise_speed;
            cohesion = clamp_length(
                desired - velocity,
                settings.cohesion_acceleration
            );
        }

        auto const containment = containment_acceleration(runtime, position, velocity);
        auto const safety = clamp_length(
            separation + containment,
            settings.max_acceleration
        );
        auto const remaining = std::max(
            0.0F,
            settings.max_acceleration - simnet::length(safety)
        );
        auto const social = clamp_length(alignment + cohesion, remaining);
        auto const acceleration = safety + social;

        auto next_velocity = clamp_speed(
            velocity + acceleration * delta_time,
            heading,
            settings.min_speed,
            settings.max_speed
        );
        auto next_position = position + next_velocity * delta_time;
        auto guarded = false;
        guarded = hard_guard_axis(settings.world_half, next_position.x, next_velocity.x) || guarded;
        guarded = hard_guard_axis(settings.world_half, next_position.y, next_velocity.y) || guarded;
        guarded = hard_guard_axis(settings.world_half, next_position.z, next_velocity.z) || guarded;
        if (guarded) {
            ++scratch.wall_guards;
        }
        auto const next_heading = simnet::normalize_or(next_velocity, heading);

        auto const acceleration_squared = simnet::length_squared(acceleration);
        auto const maximum_acceleration_squared =
            settings.max_acceleration * settings.max_acceleration;
        return {
            .next_position = next_position,
            .next_velocity = next_velocity,
            .next_heading = next_heading,
            .acceleration = acceleration,
            .separation = separation,
            .alignment = alignment,
            .cohesion = cohesion,
            .containment = containment,
            .raw_candidate_count = raw_candidate_count,
            .retained_neighbor_count = static_cast<std::uint32_t>(scratch.neighbors.size()),
            .queried_cell_count = queried_cell_count,
            .separation_neighbor_count = separation_count,
            .alignment_neighbor_count = alignment_count,
            .cohesion_neighbor_count = cohesion_count,
            .neighbor_cap_hit = raw_candidate_count > settings.max_neighbors,
            .overlap_recovery = overlap_recovery,
            .acceleration_saturated =
                acceleration_squared >= maximum_acceleration_squared * (1.0F - 1.0e-5F),
            .wall_guard = guarded,
        };
    }

    [[nodiscard]] simnet::SelectedBoidDebug make_selected_debug(
        simnet::ServerGameRuntime::Impl const& runtime,
        std::uint32_t row,
        BoidEvaluation const& evaluation
    )
    {
        auto queried_cell_bounds = std::vector<simnet::Aabb3f> {};
        queried_cell_bounds.reserve(evaluation.queried_cell_count);
        simnet::for_each_radius_cell(
            runtime.grid,
            runtime.current.positions[row],
            runtime.settings.perception_radius,
            [&](simnet::CellCoord coord) {
                queried_cell_bounds.push_back(simnet::cell_bounds(runtime.grid, coord));
            }
        );
        return {
            .id = runtime.current.ids[row],
            .velocity = evaluation.next_velocity,
            .acceleration = evaluation.acceleration,
            .speed = simnet::length(evaluation.next_velocity),
            .raw_candidate_count = evaluation.raw_candidate_count,
            .retained_neighbor_count = evaluation.retained_neighbor_count,
            .separation_neighbor_count = evaluation.separation_neighbor_count,
            .alignment_neighbor_count = evaluation.alignment_neighbor_count,
            .cohesion_neighbor_count = evaluation.cohesion_neighbor_count,
            .current_cell = simnet::cell_coord_for_position(
                runtime.grid,
                runtime.current.positions[row]
            ),
            .queried_cell_bounds = std::move(queried_cell_bounds),
            .perception_radius = runtime.settings.perception_radius,
            .separation_radius = runtime.settings.separation_radius,
            .field_of_view_degrees = runtime.settings.field_of_view_degrees,
            .maximum_neighbors = runtime.settings.max_neighbors,
            .neighbor_cap_hit = evaluation.neighbor_cap_hit,
            .overlap_recovery = evaluation.overlap_recovery,
            .acceleration_saturated = evaluation.acceleration_saturated,
            .wall_guard = evaluation.wall_guard,
            .separation = evaluation.separation,
            .alignment = evaluation.alignment,
            .cohesion = evaluation.cohesion,
            .containment = evaluation.containment,
        };
    }

    void compute_boid_row(
        simnet::ServerGameRuntime::Impl& runtime,
        simnet::ServerGameRuntime::Impl::WorkerScratch& scratch,
        std::uint32_t row,
        float delta_time
    )
    {
        auto const evaluation = evaluate_boid_row(runtime, scratch, row, delta_time);
        auto const speed = simnet::length(evaluation.next_velocity);
        auto const acceleration = simnet::length(evaluation.acceleration);
        ++scratch.processed_boids;
        scratch.raw_candidate_total += evaluation.raw_candidate_count;
        scratch.retained_neighbor_total += evaluation.retained_neighbor_count;
        scratch.separation_neighbor_total += evaluation.separation_neighbor_count;
        scratch.social_neighbor_total += evaluation.alignment_neighbor_count;
        scratch.raw_candidate_max = std::max(
            scratch.raw_candidate_max,
            evaluation.raw_candidate_count
        );
        scratch.retained_neighbor_max = std::max(
            scratch.retained_neighbor_max,
            evaluation.retained_neighbor_count
        );
        if (!scratch.neighbors.empty()) {
            scratch.nearest_neighbor_distance_total += std::sqrt(
                scratch.neighbors.front().distance_squared
            );
            ++scratch.nearest_neighbor_samples;
        }
        scratch.speed_total += speed;
        scratch.acceleration_total += acceleration;
        scratch.heading_total = scratch.heading_total + evaluation.next_heading;
        scratch.speed_min = std::min(scratch.speed_min, speed);
        scratch.speed_max = std::max(scratch.speed_max, speed);
        scratch.acceleration_max = std::max(scratch.acceleration_max, acceleration);
        scratch.isolated_boids += evaluation.alignment_neighbor_count == 0U ? 1U : 0U;
        scratch.acceleration_saturations += evaluation.acceleration_saturated ? 1U : 0U;
        scratch.cap_hits += evaluation.neighbor_cap_hit ? 1U : 0U;

        runtime.next.positions[row] = evaluation.next_position;
        runtime.next.velocities[row] = evaluation.next_velocity;
        runtime.next.headings[row] = evaluation.next_heading;
        runtime.next.written[row] = 1U;

        if (runtime.selected_id == runtime.current.ids[row]) {
            scratch.selected = make_selected_debug(runtime, row, evaluation);
        }
    }

    [[nodiscard]] bool valid_settings(simnet::BoidSimulationSettings const& settings) noexcept
    {
        return std::isfinite(settings.world_half) && settings.world_half > 0.0F
            && std::isfinite(settings.cell_size) && settings.cell_size > 0.0F
            && settings.max_neighbors > 0U
            && std::isfinite(settings.min_speed) && settings.min_speed >= 0.0F
            && std::isfinite(settings.cruise_speed)
            && std::isfinite(settings.max_speed) && settings.max_speed > 0.0F
            && settings.min_speed <= settings.cruise_speed
            && settings.cruise_speed <= settings.max_speed
            && std::isfinite(settings.max_acceleration) && settings.max_acceleration > 0.0F
            && std::isfinite(settings.perception_radius) && settings.perception_radius > 0.0F
            && std::isfinite(settings.separation_radius) && settings.separation_radius > 0.0F
            && settings.separation_radius <= settings.perception_radius
            && std::isfinite(settings.field_of_view_degrees)
            && settings.field_of_view_degrees > 0.0F
            && settings.field_of_view_degrees <= 360.0F
            && std::isfinite(settings.containment_prediction_seconds)
            && settings.containment_prediction_seconds > 0.0F
            && std::isfinite(settings.containment_margin) && settings.containment_margin > 0.0F
            && std::isfinite(settings.separation_acceleration)
            && settings.separation_acceleration >= 0.0F
            && std::isfinite(settings.containment_acceleration)
            && settings.containment_acceleration >= 0.0F
            && std::isfinite(settings.alignment_acceleration)
            && settings.alignment_acceleration >= 0.0F
            && std::isfinite(settings.cohesion_acceleration)
            && settings.cohesion_acceleration >= 0.0F;
    }
}

namespace simnet
{
    std::string_view authoritative_spawn_error_name(AuthoritativeSpawnError error) noexcept
    {
        switch (error) {
        case AuthoritativeSpawnError::None: return "none";
        case AuthoritativeSpawnError::CountOutOfRange: return "count_out_of_range";
        case AuthoritativeSpawnError::ZeroId: return "zero_id";
        case AuthoritativeSpawnError::NonAscendingIds: return "non_ascending_ids";
        case AuthoritativeSpawnError::ExistingIdOverlap: return "existing_id_overlap";
        case AuthoritativeSpawnError::InvalidBoidState: return "invalid_boid_state";
        case AuthoritativeSpawnError::InvalidIndexState: return "invalid_index_state";
        case AuthoritativeSpawnError::FlecsBulkInsertFailed: return "flecs_bulk_insert_failed";
        }
        return "unknown";
    }

    ServerGameRuntime::ServerGameRuntime(BoidSimulationSettings settings)
        : impl_(std::make_unique<Impl>(settings))
    {
        if (!valid_settings(settings)) {
            throw std::invalid_argument("invalid authoritative boid simulation settings");
        }
        resize_spatial_grid(
            impl_->grid,
            make_spatial_grid_settings(make_centered_bounds(settings.world_half), settings.cell_size)
        );
    }

    ServerGameRuntime::~ServerGameRuntime() = default;

    void ServerGameRuntime::select_boid(std::optional<EntityNetId> id)
    {
        if (impl_->selected_id == id && impl_->selected_debug.has_value()) {
            return;
        }
        impl_->selected_id = id;
        impl_->selected_debug.reset();
        if (!id.has_value() || !impl_->phase_valid || impl_->last_delta_time <= 0.0F) {
            return;
        }

        auto const found = std::ranges::lower_bound(impl_->current.ids, *id);
        if (found == impl_->current.ids.end() || *found != *id) {
            return;
        }
        auto const row = static_cast<std::uint32_t>(
            std::distance(impl_->current.ids.begin(), found)
        );
        impl_->inspection.neighbors.clear();
        impl_->inspection.overlap_recoveries = 0U;
        impl_->inspection.wall_guards = 0U;
        impl_->inspection.cap_hits = 0U;
        auto const evaluation = evaluate_boid_row(
            *impl_,
            impl_->inspection,
            row,
            impl_->last_delta_time
        );
        impl_->selected_debug = make_selected_debug(*impl_, row, evaluation);
    }

    std::optional<SelectedBoidDebug> ServerGameRuntime::selected_boid_debug() const noexcept
    {
        return impl_->selected_debug;
    }

    ServerGameStepReport const& ServerGameRuntime::last_step_report() const noexcept
    {
        return impl_->report;
    }

    bool prepare_server_game_runtime(flecs::world& world, ServerGameRuntime& runtime)
    {
        auto& impl = *runtime.impl_;
        auto const& index = world.get<AuthoritativeReplicationIndex>();
        impl.report = {};
        impl.prepared = false;
        if (!valid_index(index)) {
            impl.report.valid = false;
            impl.report.error = "authoritative replication index is invalid";
            return false;
        }
        if (index.ids.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            impl.report.valid = false;
            impl.report.error = "authoritative entity count exceeds simulation row range";
            return false;
        }

        auto const entity_count = index.ids.size();
        auto const worker_count = static_cast<std::size_t>(
            std::max(1, world.get_stage_count())
        );
        impl.current.resize(entity_count);
        impl.next.resize(entity_count);
        impl.capture_seen.resize(entity_count);
        impl.workers.resize(worker_count);
        for (auto& worker : impl.workers) {
            worker.neighbors.reserve(impl.settings.max_neighbors);
        }
        impl.inspection.neighbors.reserve(impl.settings.max_neighbors);
        prepare_spatial_grid_scratch(impl.grid_scratch, entity_count, 1U);
        auto const dense_cell_count =
            static_cast<std::uint64_t>(impl.grid.dim_x)
            * static_cast<std::uint64_t>(impl.grid.dim_y)
            * static_cast<std::uint64_t>(impl.grid.dim_z);
        if (dense_cell_count <= 262144U) {
            auto const cell_count = static_cast<std::size_t>(dense_cell_count);
            impl.grid_scratch.dense_cell_keys.resize(entity_count);
            impl.grid_scratch.dense_cell_counts.resize(cell_count);
            impl.grid_scratch.dense_cell_offsets.resize(cell_count + 1U);
            impl.grid_scratch.dense_cell_write_offsets.resize(cell_count);
        }
        impl.prepared_entity_count = entity_count;
        impl.prepared = true;
        return true;
    }

    void register_server_game(flecs::world& world, ServerGameRuntime& runtime)
    {
        auto* impl = runtime.impl_.get();
        register_game_components(world);
        world.component<Velocity>("simnet::detail::Velocity");
        world.component<FlockRow>("simnet::detail::FlockRow");
        world.component<AuthoritativeReplicationIndex>("simnet::detail::AuthoritativeReplicationIndex");
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        index.cruise_speed = impl->settings.cruise_speed;
        index.snapshot_query = world.query_builder<
            const NetIdentity,
            const Position,
            const Heading,
            const Hue>()
            .cache_kind(flecs::QueryCacheAll)
            .build();
        impl->capture_query = world.query_builder<
            const NetIdentity,
            const Position,
            const Heading,
            const Velocity,
            const FlockRow>()
            .cache_kind(flecs::QueryCacheAll)
            .build();

        auto const capture_phase = world.entity("simnet::FlockCapturePhase")
            .add(flecs::Phase)
            .depends_on(flecs::OnUpdate);
        auto const grid_phase = world.entity("simnet::FlockGridPhase")
            .add(flecs::Phase)
            .depends_on(capture_phase);
        auto const compute_phase = world.entity("simnet::FlockComputePhase")
            .add(flecs::Phase)
            .depends_on(grid_phase);
        auto const validate_phase = world.entity("simnet::FlockValidatePhase")
            .add(flecs::Phase)
            .depends_on(compute_phase);
        auto const commit_phase = world.entity("simnet::FlockCommitPhase")
            .add(flecs::Phase)
            .depends_on(validate_phase);
        auto const report_phase = world.entity("simnet::FlockReportPhase")
            .add(flecs::Phase)
            .depends_on(commit_phase);

        world.system<>("simnet::FlockCapture")
            .kind(capture_phase)
            .run([impl](flecs::iter& system_iterator) {
                while (system_iterator.next()) {
                    SIMNET_TRACE_SCOPE_CATEGORY("game_server.flock.capture", LogCategory::Simulation);
                    impl->progress_started = Clock::now();
                    auto const phase_started = Clock::now();
                    impl->last_delta_time = system_iterator.delta_time();
                    impl->report = {};
                    impl->report.entity_count =
                        static_cast<std::uint32_t>(impl->prepared_entity_count);
                    impl->phase_valid = impl->prepared;
                    impl->selected_debug.reset();
                    std::ranges::fill(impl->next.written, std::uint8_t {});
                    for (auto& worker : impl->workers) {
                        worker.neighbors.clear();
                        worker.selected.reset();
                        reset_worker_diagnostics(worker);
                    }
                    if (!impl->phase_valid) {
                        impl->report.valid = false;
                        impl->report.error = "Server game runtime was not prepared before progress";
                        impl->report.phases.capture_ms = elapsed_ms(phase_started);
                        continue;
                    }

                    std::ranges::fill(impl->capture_seen, std::uint8_t {});
                    impl->capture_query.run([&](flecs::iter& query_iterator) {
                        while (query_iterator.next() && impl->phase_valid) {
                            auto const identities = query_iterator.field<const NetIdentity>(0);
                            auto const positions = query_iterator.field<const Position>(1);
                            auto const headings = query_iterator.field<const Heading>(2);
                            auto const velocities = query_iterator.field<const Velocity>(3);
                            auto const rows = query_iterator.field<const FlockRow>(4);
                            for (auto query_row : query_iterator) {
                                auto const row = rows[query_row].value;
                                if (row >= impl->prepared_entity_count || impl->capture_seen[row] != 0U) {
                                    impl->phase_valid = false;
                                    impl->report.error = "invalid or duplicate authoritative flock row";
                                    break;
                                }
                                auto const id = identities[query_row].id;
                                auto const position = positions[query_row].value;
                                auto const heading = headings[query_row].value;
                                auto const velocity = velocities[query_row].value;
                                if (id == 0U || !is_finite(position) || !is_finite(heading)
                                    || !is_normalized_heading(heading) || !is_finite(velocity)) {
                                    impl->phase_valid = false;
                                    impl->report.error = "authoritative boid capture contains invalid state";
                                    break;
                                }
                                impl->capture_seen[row] = 1U;
                                impl->current.ids[row] = id;
                                impl->current.positions[row] = position;
                                impl->current.headings[row] = heading;
                                impl->current.velocities[row] = velocity;
                            }
                        }
                    });
                    if (impl->phase_valid
                        && (!std::ranges::all_of(impl->capture_seen, [](std::uint8_t value) { return value != 0U; })
                            || !std::ranges::is_sorted(impl->current.ids))) {
                        impl->phase_valid = false;
                        impl->report.error = "authoritative capture does not match ascending runtime rows";
                    }
                    impl->report.valid = impl->phase_valid;
                    impl->report.phases.capture_ms = elapsed_ms(phase_started);
                }
            });

        world.system<>("simnet::FlockGridBuild")
            .kind(grid_phase)
            .run([impl](flecs::iter& system_iterator) {
                while (system_iterator.next()) {
                    if (!impl->phase_valid) {
                        impl->compute_started = Clock::now();
                        continue;
                    }
                    SIMNET_TRACE_SCOPE_CATEGORY("game_server.flock.grid", LogCategory::Spatial);
                    auto const phase_started = Clock::now();
                    try {
                        build_spatial_grid_serial(
                            impl->grid,
                            impl->grid_scratch,
                            impl->current.positions,
                            impl->current.ids
                        );
                        SIMNET_TRACE_PLOT(
                            "boids.grid.occupied_cells",
                            static_cast<double>(impl->grid.stats.occupied_cell_count)
                        );
                        SIMNET_TRACE_PLOT(
                            "boids.grid.max_occupancy",
                            static_cast<double>(impl->grid.stats.max_cell_occupancy)
                        );
                        SIMNET_TRACE_PLOT(
                            "boids.grid.average_load",
                            static_cast<double>(impl->grid.stats.average_occupied_cell_load)
                        );
                    } catch (std::exception const& error) {
                        impl->phase_valid = false;
                        impl->report.valid = false;
                        impl->report.error = error.what();
                    }
                    impl->report.phases.grid_ms = elapsed_ms(phase_started);
                    impl->compute_started = Clock::now();
                }
            });

        world.system<const FlockRow>("simnet::FlockCompute")
            .kind(compute_phase)
            .multi_threaded()
            .each([impl](flecs::iter& iterator, std::size_t, FlockRow const& flock_row) {
                if (!impl->phase_valid) {
                    return;
                }
                auto const stage = iterator.world().get_stage_id();
                if (stage < 0 || static_cast<std::size_t>(stage) >= impl->workers.size()
                    || flock_row.value >= impl->prepared_entity_count) {
                    return;
                }
                compute_boid_row(
                    *impl,
                    impl->workers[static_cast<std::size_t>(stage)],
                    flock_row.value,
                    iterator.delta_time()
                );
            });

        world.system<>("simnet::FlockValidate")
            .kind(validate_phase)
            .run([impl](flecs::iter& system_iterator) {
                while (system_iterator.next()) {
                    SIMNET_TRACE_SCOPE_CATEGORY("game_server.flock.validate", LogCategory::Simulation);
                    impl->report.phases.compute_ms = elapsed_ms(impl->compute_started);
                    auto const phase_started = Clock::now();
                    if (impl->phase_valid) {
                        for (std::size_t row = 0; row < impl->prepared_entity_count; ++row) {
                            if (impl->next.written[row] == 0U
                                || !is_finite(impl->next.positions[row])
                                || !is_finite(impl->next.velocities[row])
                                || !is_finite(impl->next.headings[row])
                                || !is_normalized_heading(impl->next.headings[row])) {
                                impl->phase_valid = false;
                                impl->report.error = "computed boid state failed validation";
                                break;
                            }
                        }
                    }

                    auto processed_boids = std::uint64_t {};
                    auto nearest_neighbor_samples = std::uint64_t {};
                    auto heading_total = Vec3f {};
                    impl->report.diagnostics.grid = impl->grid.stats;
                    for (auto const& worker : impl->workers) {
                        processed_boids += worker.processed_boids;
                        nearest_neighbor_samples += worker.nearest_neighbor_samples;
                        heading_total = heading_total + worker.heading_total;
                        impl->report.diagnostics.raw_candidates_mean +=
                            static_cast<double>(worker.raw_candidate_total);
                        impl->report.diagnostics.raw_candidates_max = std::max(
                            impl->report.diagnostics.raw_candidates_max,
                            worker.raw_candidate_max
                        );
                        impl->report.diagnostics.retained_neighbors_mean +=
                            static_cast<double>(worker.retained_neighbor_total);
                        impl->report.diagnostics.retained_neighbors_max = std::max(
                            impl->report.diagnostics.retained_neighbors_max,
                            worker.retained_neighbor_max
                        );
                        impl->report.diagnostics.separation_neighbors_mean +=
                            static_cast<double>(worker.separation_neighbor_total);
                        impl->report.diagnostics.social_neighbors_mean +=
                            static_cast<double>(worker.social_neighbor_total);
                        impl->report.diagnostics.nearest_neighbor_distance_mean +=
                            worker.nearest_neighbor_distance_total;
                        impl->report.diagnostics.speed_mean += worker.speed_total;
                        impl->report.diagnostics.acceleration_mean += worker.acceleration_total;
                        impl->report.diagnostics.speed_min = std::min(
                            impl->report.diagnostics.speed_min == 0.0F
                                ? std::numeric_limits<float>::max()
                                : impl->report.diagnostics.speed_min,
                            worker.speed_min
                        );
                        impl->report.diagnostics.speed_max = std::max(
                            impl->report.diagnostics.speed_max,
                            worker.speed_max
                        );
                        impl->report.diagnostics.acceleration_max = std::max(
                            impl->report.diagnostics.acceleration_max,
                            worker.acceleration_max
                        );
                        impl->report.diagnostics.neighbor_cap_hit_count += worker.cap_hits;
                        impl->report.diagnostics.isolated_boid_count += worker.isolated_boids;
                        impl->report.diagnostics.acceleration_saturation_count +=
                            worker.acceleration_saturations;
                        impl->report.diagnostics.overlap_recovery_count +=
                            worker.overlap_recoveries;
                        impl->report.diagnostics.hard_wall_guard_count += worker.wall_guards;
                        impl->report.overlap_recovery_count += worker.overlap_recoveries;
                        impl->report.hard_wall_guard_count += worker.wall_guards;
                        impl->report.neighbor_cap_hit_count += worker.cap_hits;
                        if (worker.selected.has_value()) {
                            impl->selected_debug = worker.selected;
                        }
                    }
                    if (processed_boids != 0U) {
                        auto const denominator = static_cast<double>(processed_boids);
                        impl->report.diagnostics.raw_candidates_mean /= denominator;
                        impl->report.diagnostics.retained_neighbors_mean /= denominator;
                        impl->report.diagnostics.separation_neighbors_mean /= denominator;
                        impl->report.diagnostics.social_neighbors_mean /= denominator;
                        impl->report.diagnostics.speed_mean /= denominator;
                        impl->report.diagnostics.acceleration_mean /= denominator;
                        impl->report.diagnostics.polarization = length(
                            heading_total / static_cast<float>(processed_boids)
                        );
                    } else {
                        impl->report.diagnostics.speed_min = 0.0F;
                    }
                    if (nearest_neighbor_samples != 0U) {
                        impl->report.diagnostics.nearest_neighbor_distance_mean /=
                            static_cast<double>(nearest_neighbor_samples);
                    }
                    impl->report.valid = impl->phase_valid;
                    impl->report.phases.validate_ms = elapsed_ms(phase_started);
                    impl->commit_started = Clock::now();
                }
            });

        world.system<const FlockRow, Position, Heading, Velocity>("simnet::FlockCommit")
            .kind(commit_phase)
            .multi_threaded()
            .each([impl](
                flecs::iter&,
                std::size_t,
                FlockRow const& flock_row,
                Position& position,
                Heading& heading,
                Velocity& velocity
            ) {
                if (!impl->phase_valid || flock_row.value >= impl->prepared_entity_count) {
                    return;
                }
                auto const row = flock_row.value;
                position.value = impl->next.positions[row];
                heading.value = impl->next.headings[row];
                velocity.value = impl->next.velocities[row];
            });

        world.system<>("simnet::FlockReport")
            .kind(report_phase)
            .run([impl](flecs::iter& system_iterator) {
                while (system_iterator.next()) {
                    impl->report.phases.commit_ms = elapsed_ms(impl->commit_started);
                    impl->report.phases.progress_ms = elapsed_ms(impl->progress_started);
                    auto const& diagnostics = impl->report.diagnostics;
                    auto const& phases = impl->report.phases;
                    SIMNET_TRACE_PLOT("boids.neighbors.raw_mean", diagnostics.raw_candidates_mean);
                    SIMNET_TRACE_PLOT(
                        "boids.neighbors.raw_max",
                        static_cast<double>(diagnostics.raw_candidates_max)
                    );
                    SIMNET_TRACE_PLOT(
                        "boids.neighbors.retained_mean",
                        diagnostics.retained_neighbors_mean
                    );
                    SIMNET_TRACE_PLOT(
                        "boids.neighbors.cap_hit_boids",
                        static_cast<double>(diagnostics.neighbor_cap_hit_count)
                    );
                    SIMNET_TRACE_PLOT(
                        "boids.neighbors.separation_mean",
                        diagnostics.separation_neighbors_mean
                    );
                    SIMNET_TRACE_PLOT(
                        "boids.neighbors.social_mean",
                        diagnostics.social_neighbors_mean
                    );
                    SIMNET_TRACE_PLOT("boids.motion.speed_mean", diagnostics.speed_mean);
                    SIMNET_TRACE_PLOT(
                        "boids.motion.acceleration_mean",
                        diagnostics.acceleration_mean
                    );
                    SIMNET_TRACE_PLOT(
                        "boids.motion.acceleration_saturations",
                        static_cast<double>(diagnostics.acceleration_saturation_count)
                    );
                    SIMNET_TRACE_PLOT("boids.motion.polarization", diagnostics.polarization);
                    SIMNET_TRACE_PLOT("boids.phase.capture_ms", phases.capture_ms);
                    SIMNET_TRACE_PLOT("boids.phase.grid_ms", phases.grid_ms);
                    SIMNET_TRACE_PLOT("boids.phase.compute_ms", phases.compute_ms);
                    SIMNET_TRACE_PLOT("boids.phase.validate_ms", phases.validate_ms);
                    SIMNET_TRACE_PLOT("boids.phase.commit_ms", phases.commit_ms);
                    SIMNET_TRACE_PLOT("boids.phase.progress_ms", phases.progress_ms);
                }
            });
    }

    flecs::entity upsert_authoritative_boid(flecs::world& world, BoidState const& boid)
    {
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index) || !valid_boid_state(boid)) {
            return {};
        }

        auto position = find_index(index, boid.id);
        auto offset = static_cast<std::size_t>(position - index.ids.begin());
        if (position != index.ids.end() && *position == boid.id) {
            auto entity = flecs::entity { world, index.entities[offset] };
            if (!entity.is_alive()) {
                return {};
            }
            set_authoritative_simulation_components(
                entity,
                boid,
                static_cast<std::uint32_t>(offset),
                index.cruise_speed
            );
            return entity;
        }
        if (index.ids.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return {};
        }

        index.reserve(index.ids.size() + 1U);
        position = find_index(index, boid.id);
        offset = static_cast<std::size_t>(position - index.ids.begin());
        auto entity = world.entity();
        set_authoritative_simulation_components(
            entity,
            boid,
            static_cast<std::uint32_t>(offset),
            index.cruise_speed
        );
        index.ids.insert(position, boid.id);
        index.entities.insert(index.entities.begin() + static_cast<std::ptrdiff_t>(offset), entity.id());
        remap_rows(world, index, offset + 1U);
        world.modified<AuthoritativeReplicationIndex>();
        return entity;
    }

    AuthoritativeSpawnReport append_authoritative_boids(
        flecs::world& world,
        std::span<const BoidState> boids
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn", LogCategory::Simulation);
        auto report = AuthoritativeSpawnReport { .requested_count = boids.size() };
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index)) {
            report.error = AuthoritativeSpawnError::InvalidIndexState;
            return report;
        }
        if (boids.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            report.error = AuthoritativeSpawnError::CountOutOfRange;
            return report;
        }
        if (boids.size()
            > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())
                - index.ids.size()) {
            report.error = AuthoritativeSpawnError::CountOutOfRange;
            return report;
        }
        if (boids.empty()) {
            return report;
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_validation", LogCategory::Simulation);
            auto const current_maximum = index.ids.empty() ? EntityNetId {} : index.ids.back();
            for (std::size_t offset = 0; offset < boids.size(); ++offset) {
                auto const& boid = boids[offset];
                if (boid.id == 0U) {
                    report.error = AuthoritativeSpawnError::ZeroId;
                    report.failing_index = offset;
                    return report;
                }
                if (offset != 0U && boid.id <= boids[offset - 1U].id) {
                    report.error = AuthoritativeSpawnError::NonAscendingIds;
                    report.failing_index = offset;
                    return report;
                }
                if (boid.id <= current_maximum) {
                    report.error = AuthoritativeSpawnError::ExistingIdOverlap;
                    report.failing_index = offset;
                    return report;
                }
                if (!valid_boid_state(boid)) {
                    report.error = AuthoritativeSpawnError::InvalidBoidState;
                    report.failing_index = offset;
                    return report;
                }
            }
        }

        auto identities = std::vector<NetIdentity> {};
        auto positions = std::vector<Position> {};
        auto headings = std::vector<Heading> {};
        auto hues = std::vector<Hue> {};
        auto velocities = std::vector<Velocity> {};
        auto rows = std::vector<FlockRow> {};
        auto entities = std::vector<flecs::entity_t> {};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_component_arrays", LogCategory::Simulation);
            identities.reserve(boids.size());
            positions.reserve(boids.size());
            headings.reserve(boids.size());
            hues.reserve(boids.size());
            velocities.reserve(boids.size());
            rows.reserve(boids.size());
            entities.resize(boids.size());
            index.reserve(index.ids.size() + boids.size());
            for (std::size_t offset = 0; offset < boids.size(); ++offset) {
                auto const& boid = boids[offset];
                identities.push_back({ .id = boid.id });
                positions.push_back({ .value = boid.position });
                headings.push_back({ .value = boid.heading });
                hues.push_back({ .value = boid.hue });
                velocities.push_back({ .value = boid.heading * index.cruise_speed });
                rows.push_back({
                    .value = static_cast<std::uint32_t>(index.ids.size() + offset),
                });
            }
        }

        auto ids = std::array<ecs_id_t, 6> {
            world.id<NetIdentity>(),
            world.id<Position>(),
            world.id<Heading>(),
            world.id<Hue>(),
            world.id<Velocity>(),
            world.id<FlockRow>(),
        };
        auto data = std::array<void*, 6> {
            identities.data(),
            positions.data(),
            headings.data(),
            hues.data(),
            velocities.data(),
            rows.data(),
        };
        auto populate = ecs_bulk_desc_t {};
        populate.count = static_cast<std::int32_t>(boids.size());
        std::copy(ids.begin(), ids.end(), populate.ids);
        populate.data = data.data();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_flecs", LogCategory::Simulation);
            auto const* created = ecs_bulk_init(world.c_ptr(), &populate);
            if (created == nullptr) {
                report.error = AuthoritativeSpawnError::FlecsBulkInsertFailed;
                return report;
            }
            std::copy_n(created, boids.size(), entities.begin());
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.bulk_spawn_index_append", LogCategory::Simulation);
            for (std::size_t offset = 0; offset < boids.size(); ++offset) {
                index.ids.push_back(boids[offset].id);
                index.entities.push_back(entities[offset]);
            }
            world.modified<AuthoritativeReplicationIndex>();
        }
        report.spawned_count = boids.size();
        SIMNET_TRACE_PLOT("server.authoritative_index_size", static_cast<double>(index.ids.size()));
        return report;
    }

    bool delete_authoritative_boid(flecs::world& world, EntityNetId id)
    {
        auto& index = world.ensure<AuthoritativeReplicationIndex>();
        if (!valid_index(index)) {
            return false;
        }
        auto const position = find_index(index, id);
        if (position == index.ids.end() || *position != id) {
            return false;
        }
        auto const offset = static_cast<std::size_t>(position - index.ids.begin());
        auto const entity = index.entities[offset];
        if (!ecs_is_alive(world.c_ptr(), entity)) {
            return false;
        }
        ecs_delete(world.c_ptr(), entity);
        index.ids.erase(position);
        index.entities.erase(index.entities.begin() + static_cast<std::ptrdiff_t>(offset));
        remap_rows(world, index, offset);
        world.modified<AuthoritativeReplicationIndex>();
        return true;
    }

    std::size_t authoritative_boid_count(flecs::world const& world) noexcept
    {
        auto const& index = world.get<AuthoritativeReplicationIndex>();
        return valid_index(index) ? index.ids.size() : 0U;
    }

    ServerSnapshotExtractionReport extract_world_snapshot(
        flecs::world const& world,
        Tick tick,
        WorldSnapshot& out_snapshot
    )
    {
        auto report = ServerSnapshotExtractionReport { .tick = tick };
        auto const& index = world.get<AuthoritativeReplicationIndex>();
        if (!valid_index(index) || !index.snapshot_query) {
            reset_failed_snapshot(out_snapshot, tick);
            report.valid = false;
            report.error = "authoritative replication extraction state is invalid";
            return report;
        }

        auto& scratch = index.extraction_scratch;
        scratch.clear();
        scratch.reserve(index.ids.size());
        auto globally_ascending = true;
        auto previous_id = EntityNetId {};
        auto have_previous_id = false;
        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.snapshot.query_iteration", LogCategory::Snapshot);
            index.snapshot_query.run([&](flecs::iter& iterator) {
                while (iterator.next()) {
                    auto const identities = iterator.field<const NetIdentity>(0);
                    auto const positions = iterator.field<const Position>(1);
                    auto const headings = iterator.field<const Heading>(2);
                    auto const hues = iterator.field<const Hue>(3);
                    auto const entities = iterator.entities();

                    for (auto row : iterator) {
                        auto const id = identities[row].id;
                        auto const position = positions[row].value;
                        auto const heading = headings[row].value;
                        if (id == 0U) {
                            report.valid = false;
                            report.error = "world snapshot identity is zero";
                            return;
                        }
                        if (!is_finite(position)) {
                            report.valid = false;
                            report.error = "world snapshot position contains a non-finite component";
                            return;
                        }
                        if (!is_finite(heading)) {
                            report.valid = false;
                            report.error = "world snapshot heading contains a non-finite component";
                            return;
                        }
                        if (!is_normalized_heading(heading)) {
                            report.valid = false;
                            report.error = "world snapshot heading is not normalized";
                            return;
                        }
                        if (have_previous_id && id <= previous_id) {
                            globally_ascending = false;
                        }
                        previous_id = id;
                        have_previous_id = true;
                        scratch.push_back({
                            .id = id,
                            .entity = entities[row],
                            .position = position,
                            .heading = heading,
                            .hue = hues[row].value,
                        });
                    }
                }
            });
        }

        if (!report.valid) {
            reset_failed_snapshot(out_snapshot, tick);
            return report;
        }
        if (scratch.size() != index.ids.size()) {
            reset_failed_snapshot(out_snapshot, tick);
            report.valid = false;
            report.error = "authoritative query does not match indexed entity count";
            return report;
        }
        if (!globally_ascending) {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.snapshot.scratch_sort", LogCategory::Snapshot);
            std::ranges::sort(scratch, {}, &AuthoritativeExtractionEntry::id);
        }
        SIMNET_TRACE_PLOT("game_server.snapshot.sorted_fast_path", globally_ascending ? 1.0 : 0.0);
        SIMNET_TRACE_PLOT("game_server.snapshot.scratch_capacity", static_cast<double>(scratch.capacity()));

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.snapshot.index_verification", LogCategory::Snapshot);
            for (std::size_t position = 0; position < scratch.size(); ++position) {
                auto const& entry = scratch[position];
                if (entry.id != index.ids[position] || entry.entity != index.entities[position]
                    || (position != 0U && entry.id <= scratch[position - 1U].id)) {
                    reset_failed_snapshot(out_snapshot, tick);
                    report.valid = false;
                    report.error = "authoritative query does not match indexed entity ownership";
                    return report;
                }
            }
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("game_server.snapshot.soa_commit", LogCategory::Snapshot);
            out_snapshot.clear();
            out_snapshot.tick = tick;
            out_snapshot.reserve(scratch.size());
            out_snapshot.ids.resize(scratch.size());
            out_snapshot.positions.resize(scratch.size());
            out_snapshot.headings.resize(scratch.size());
            out_snapshot.hues.resize(scratch.size());
            for (std::size_t position = 0; position < scratch.size(); ++position) {
                auto const& entry = scratch[position];
                out_snapshot.ids[position] = entry.id;
                out_snapshot.positions[position] = entry.position;
                out_snapshot.headings[position] = entry.heading;
                out_snapshot.hues[position] = entry.hue;
            }
        }
        report.entity_count = static_cast<std::uint32_t>(out_snapshot.size());
        SIMNET_TRACE_PLOT("game_server.snapshot.entities", static_cast<double>(out_snapshot.size()));
        SIMNET_TRACE_PLOT("game_server.snapshot.ids_capacity", static_cast<double>(out_snapshot.ids.capacity()));
        SIMNET_TRACE_PLOT("game_server.snapshot.positions_capacity", static_cast<double>(out_snapshot.positions.capacity()));
        return report;
    }
}
