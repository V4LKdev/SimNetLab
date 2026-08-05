module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

export module simnet.app_visual_setup;

import simnet.config;
import simnet.pipeline;
import simnet.render;

export namespace simnet::app
{
    /// Owns the immutable strings and views used by the read-only Setup inspector.
    class RunSetupStorage
    {
    public:
        RunSetupStorage(
            SharedConfig const& shared,
            ServerConfig const& local,
            PipelineDefinition const& pipeline,
            std::filesystem::path const& shared_source,
            std::filesystem::path const& local_source
        );

        RunSetupStorage(
            SharedConfig const& shared,
            ClientConfig const& local,
            PipelineDefinition const& pipeline,
            std::filesystem::path const& shared_source,
            std::filesystem::path const& local_source
        );

        [[nodiscard]] RunSetupView view() const noexcept;

        RunSetupStorage(RunSetupStorage const&) = delete;
        RunSetupStorage& operator=(RunSetupStorage const&) = delete;
        RunSetupStorage(RunSetupStorage&&) = delete;
        RunSetupStorage& operator=(RunSetupStorage&&) = delete;

    private:
        void begin_section(std::string_view title, bool expanded);
        void end_section();
        void add_row(std::string_view label, std::string value);
        void add_shared_sections(SharedConfig const& shared);
        void add_techniques(PipelineDefinition const& pipeline, SharedConfig const& shared);

        std::array<std::string, 128> values_{};
        std::array<SetupRowView, 128> rows_{};
        std::array<SetupSectionView, 8> sections_{};
        std::size_t value_count_{};
        std::size_t row_count_{};
        std::size_t section_count_{};
        std::size_t active_section_row_{};
        std::uint64_t revision_{};
    };
} // namespace simnet::app

namespace
{
    using namespace simnet;

    [[nodiscard]] std::string format_float(double value, int precision = 2)
    {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return buffer;
    }

    [[nodiscard]] std::string format_u64(std::uint64_t value)
    {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        return buffer;
    }

    [[nodiscard]] std::string format_hex(std::uint64_t value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%016llX", static_cast<unsigned long long>(value));
        return buffer;
    }

    [[nodiscard]] std::string enabled(bool value)
    {
        return value ? "Enabled" : "Disabled";
    }

    [[nodiscard]] bool has(PipelineDefinition const& pipeline, PipelineTechniqueFlags flag)
    {
        return has_all_flags(pipeline.techniques, flag);
    }
} // namespace

namespace simnet::app
{
    void RunSetupStorage::begin_section(std::string_view title, bool expanded)
    {
        active_section_row_ = row_count_;
        sections_[section_count_] = {
            .title = title,
            .initially_expanded = expanded,
        };
    }

    void RunSetupStorage::end_section()
    {
        auto& section = sections_[section_count_++];
        section.rows
            = std::span{rows_}.subspan(active_section_row_, row_count_ - active_section_row_);
    }

    void RunSetupStorage::add_row(std::string_view label, std::string value)
    {
        values_[value_count_] = std::move(value);
        rows_[row_count_++] = {
            .label = label,
            .value = values_[value_count_++],
        };
    }

    void
    RunSetupStorage::add_techniques(PipelineDefinition const& pipeline, SharedConfig const& shared)
    {
        begin_section("ACTIVE TECHNIQUES", true);
        add_row(
            "Send cadence",
            has(pipeline, PipelineTechniqueFlags::SendInterval)
                ? "Every " + format_u64(pipeline.send_interval.interval_ticks) + " ticks"
                : "Every tick"
        );
        add_row(
            "Update scheduling",
            has(pipeline, PipelineTechniqueFlags::Incremental) ? "Incremental round-robin"
                                                               : "All entities"
        );
        add_row(
            "Position encoding",
            has(pipeline, PipelineTechniqueFlags::Quantization) ? "Quantized uint16 x 3"
                                                                : "Float32 x 3"
        );
        add_row(
            "Heading encoding",
            has(pipeline, PipelineTechniqueFlags::OctHeading)         ? "Octahedral uint16 x 2"
                : has(pipeline, PipelineTechniqueFlags::Quantization) ? "Quantized int16 x 3"
                                                                      : "Float32 x 3"
        );
        add_row("Bit packing", enabled(has(pipeline, PipelineTechniqueFlags::BitPacking)));
        add_row("Delta baseline", enabled(has(pipeline, PipelineTechniqueFlags::Delta)));
        auto area_of_interest = std::string{"None"};
        if (pipeline.area_of_interest.mode == AreaOfInterestMode::Radius) {
            area_of_interest = "Radius " + format_float(pipeline.area_of_interest.radius);
        } else if (pipeline.area_of_interest.mode == AreaOfInterestMode::Fov) {
            area_of_interest = "3D cone " + format_float(pipeline.area_of_interest.radius)
                + " radius, " + format_float(pipeline.area_of_interest.fov_degrees, 1) + " deg";
        }
        add_row("Area of interest", std::move(area_of_interest));
        auto compression = std::string{shared.compression.mode};
        if (shared.compression.mode != "none") {
            compression
                += ", level " + format_u64(static_cast<std::uint64_t>(shared.compression.level));
        }
        add_row("Compression", std::move(compression));
        add_row(
            "Packetization",
            shared.packetization.enabled
                ? "Enabled, " + format_u64(shared.packetization.max_payload_bytes) + " bytes"
                : "Disabled"
        );
        end_section();
    }

    void RunSetupStorage::add_shared_sections(SharedConfig const& shared)
    {
        begin_section("SIMULATION", false);
        add_row("Tick rate", format_float(shared.simulation.tick_rate_hz, 1) + " Hz");
        add_row("Fixed timestep", format_float(1000.0 / shared.simulation.tick_rate_hz, 3) + " ms");
        add_row("Initial boids", format_u64(shared.simulation.initial_boid_count));
        add_row(
            "World size",
            format_float(shared.simulation.world_half * 2.0F, 0) + " x "
                + format_float(shared.simulation.world_half * 2.0F, 0) + " x "
                + format_float(shared.simulation.world_half * 2.0F, 0)
        );
        add_row("Seed", format_u64(shared.run.seed));
        add_row("Deterministic", "Yes");
        end_section();

        begin_section("BOID RULES", false);
        add_row(
            "Speed min / cruise / max",
            format_float(shared.boids.min_speed) + " / " + format_float(shared.boids.cruise_speed)
                + " / " + format_float(shared.boids.max_speed)
        );
        add_row("Maximum acceleration", format_float(shared.boids.max_acceleration));
        add_row("Separation radius", format_float(shared.boids.separation_radius));
        add_row("Alignment radius", format_float(shared.boids.alignment_radius));
        add_row("Cohesion radius", format_float(shared.boids.cohesion_radius));
        add_row("Field of view", format_float(shared.boids.field_of_view_degrees, 1) + " deg");
        add_row("Separation", enabled(shared.boids.enable_separation));
        add_row("Alignment", enabled(shared.boids.enable_alignment));
        add_row("Cohesion", enabled(shared.boids.enable_cohesion));
        add_row("Containment", enabled(shared.boids.enable_containment));
        add_row("Wander", enabled(shared.boids.enable_wander));
        add_row("Hue assimilation", enabled(shared.boids.enable_hue_assimilation));
        add_row("Hue drift", enabled(shared.boids.enable_hue_drift));
        end_section();

        begin_section("PLAYER MOVEMENT", false);
        add_row(
            "Speed cruise / boost / slow",
            format_float(shared.player.cruise_speed) + " / "
                + format_float(shared.player.boost_speed) + " / "
                + format_float(shared.player.slow_speed)
        );
        add_row("Speed change rate", format_float(shared.player.speed_change_rate));
        add_row(
            "Yaw acceleration",
            format_float(shared.player.yaw_acceleration_degrees, 1) + " deg/s2"
        );
        add_row(
            "Pitch acceleration",
            format_float(shared.player.pitch_acceleration_degrees, 1) + " deg/s2"
        );
        add_row("Maximum yaw rate", format_float(shared.player.max_yaw_rate_degrees, 1) + " deg/s");
        add_row(
            "Maximum pitch rate",
            format_float(shared.player.max_pitch_rate_degrees, 1) + " deg/s"
        );
        add_row("Pitch limit", format_float(shared.player.pitch_limit_degrees, 1) + " deg");
        end_section();

        begin_section("SPATIAL PARTITION", false);
        add_row("Implementation", "Deterministic uniform grid");
        add_row("Cell size", format_float(shared.spatial.cell_size));
        add_row("Maximum neighbors", format_u64(shared.spatial.max_neighbors));
        add_row(
            "Query radius",
            format_float(
                std::max({
                    shared.boids.separation_radius,
                    shared.boids.alignment_radius,
                    shared.boids.cohesion_radius,
                })
            )
        );
        end_section();
    }

    RunSetupStorage::RunSetupStorage(
        SharedConfig const& shared,
        ServerConfig const& local,
        PipelineDefinition const& pipeline,
        std::filesystem::path const& shared_source,
        std::filesystem::path const& local_source
    )
    {
        revision_ = fingerprint_runtime_config(shared, local).value;
        begin_section("RUN IDENTITY", true);
        add_row("Viewer", "Server");
        add_row("Shared profile", shared_source.stem().string());
        add_row("Server profile", local_source.stem().string());
        add_row("Shared source", shared_source.string());
        add_row("Server source", local_source.string());
        add_row("Runtime fingerprint", format_hex(revision_));
        add_row("Network fingerprint", format_hex(fingerprint_network_compatibility(shared).value));
#if defined(SIMNET_BUILD_CONFIG)
        add_row("Build", SIMNET_BUILD_CONFIG);
#endif
        add_row("Flecs workers", format_u64(local.flecs.thread_count));
        end_section();
        add_techniques(pipeline, shared);
        add_shared_sections(shared);

        begin_section("PRESENTATION", false);
        add_row("Interpolation", enabled(local.visualization.interpolation_enabled));
        add_row("Target frame rate", format_u64(local.visualization.target_fps) + " FPS");
        end_section();

        begin_section("TRANSPORT", false);
        add_row("Backend", "ENet");
        add_row("Delivery", local.transport.snapshot_delivery);
        add_row("Maximum payload", format_u64(local.transport.max_payload_bytes) + " bytes");
        add_row("Configured client limit", format_u64(local.transport.max_clients));
        end_section();
    }

    RunSetupStorage::RunSetupStorage(
        SharedConfig const& shared,
        ClientConfig const& local,
        PipelineDefinition const& pipeline,
        std::filesystem::path const& shared_source,
        std::filesystem::path const& local_source
    )
    {
        revision_ = fingerprint_runtime_config(shared, local).value;
        begin_section("RUN IDENTITY", true);
        add_row("Viewer", "Client");
        add_row("Shared profile", shared_source.stem().string());
        add_row("Client profile", local_source.stem().string());
        add_row("Shared source", shared_source.string());
        add_row("Client source", local_source.string());
        add_row("Runtime fingerprint", format_hex(revision_));
        add_row("Network fingerprint", format_hex(fingerprint_network_compatibility(shared).value));
#if defined(SIMNET_BUILD_CONFIG)
        add_row("Build", SIMNET_BUILD_CONFIG);
#endif
        add_row("Client role", local.gameplay.role);
        end_section();
        add_techniques(pipeline, shared);
        add_shared_sections(shared);

        begin_section("PRESENTATION", false);
        add_row("Interpolation", enabled(local.visualization.interpolation_enabled));
        add_row("Target frame rate", format_u64(local.visualization.target_fps) + " FPS");
        end_section();

        begin_section("TRANSPORT", false);
        add_row("Backend", "ENet");
        add_row("Delivery", local.transport.snapshot_delivery);
        add_row("Maximum payload", format_u64(local.transport.max_payload_bytes) + " bytes");
        end_section();
    }

    RunSetupView RunSetupStorage::view() const noexcept
    {
        return {
            .revision = revision_,
            .sections = std::span{sections_}.first(section_count_),
        };
    }
} // namespace simnet::app
