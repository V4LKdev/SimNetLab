module;

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module simnet.app_visual_setup;

namespace
{
    using namespace simnet;

    [[nodiscard]] std::string format_float(double value, int precision = 2)
    {
        auto buffer = std::array<char, 64>{};
        std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
        return buffer.data();
    }

    [[nodiscard]] std::string format_float(float value, int precision = 2)
    {
        return format_float(static_cast<double>(value), precision);
    }

    [[nodiscard]] std::string format_unsigned_integer(std::uint64_t value)
    {
        auto buffer = std::array<char, 64>{};
        std::snprintf(buffer.data(), buffer.size(), "%llu", static_cast<unsigned long long>(value));
        return buffer.data();
    }

    [[nodiscard]] std::string format_hexadecimal(std::uint64_t value)
    {
        auto buffer = std::array<char, 32>{};
        std::snprintf(
            buffer.data(),
            buffer.size(),
            "0x%016llX",
            static_cast<unsigned long long>(value)
        );
        return buffer.data();
    }

    [[nodiscard]] std::string enabled_label(bool value)
    {
        return value ? "Enabled" : "Disabled";
    }

    [[nodiscard]] bool
    has_technique(PipelineDefinition const& pipeline, PipelineTechniqueFlags flag)
    {
        return has_all_flags(pipeline.techniques, flag);
    }

    [[nodiscard]] char const* heading_encoding_name(PipelineDefinition const& pipeline)
    {
        if (has_technique(pipeline, PipelineTechniqueFlags::OctHeading))
        {
            return "Octahedral uint16 x 2";
        }
        if (has_technique(pipeline, PipelineTechniqueFlags::Quantization))
        {
            return "Quantized int16 x 3";
        }
        return "Float32 x 3";
    }
}

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
        section.rows =
            std::span{rows_}.subspan(active_section_row_, row_count_ - active_section_row_);
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
            has_technique(pipeline, PipelineTechniqueFlags::SendInterval)
                ? "Every " + format_unsigned_integer(pipeline.send_interval.interval_ticks) +
                      " ticks"
                : "Every tick"
        );
        add_row(
            "Update scheduling",
            has_technique(pipeline, PipelineTechniqueFlags::Incremental) ? "Incremental round-robin"
                                                                         : "All entities"
        );
        add_row(
            "Position encoding",
            has_technique(pipeline, PipelineTechniqueFlags::Quantization) ? "Quantized uint16 x 3"
                                                                          : "Float32 x 3"
        );
        add_row("Heading encoding", heading_encoding_name(pipeline));
        add_row(
            "Bit packing",
            enabled_label(has_technique(pipeline, PipelineTechniqueFlags::BitPacking))
        );
        add_row(
            "Delta baseline",
            enabled_label(has_technique(pipeline, PipelineTechniqueFlags::Delta))
        );
        auto area_of_interest = std::string{"None"};
        if (pipeline.area_of_interest.mode == AreaOfInterestMode::Radius)
        {
            area_of_interest = "Radius " + format_float(pipeline.area_of_interest.radius);
        }
        else if (pipeline.area_of_interest.mode == AreaOfInterestMode::Fov)
        {
            area_of_interest = "3D cone " + format_float(pipeline.area_of_interest.radius) +
                               " radius, " +
                               format_float(pipeline.area_of_interest.fov_degrees, 1) + " deg";
        }
        add_row("Area of interest", std::move(area_of_interest));
        auto level_of_detail = std::string{"None"};
        if (pipeline.level_of_detail.mode == LevelOfDetailMode::DistanceBands)
        {
            level_of_detail =
                "Distance bands " + format_float(pipeline.level_of_detail.near_distance) + " / " +
                format_float(pipeline.level_of_detail.medium_distance) + ", intervals 1 / " +
                format_unsigned_integer(pipeline.level_of_detail.medium_interval_ticks) + " / " +
                format_unsigned_integer(pipeline.level_of_detail.far_interval_ticks);
        }
        add_row("Level of detail", std::move(level_of_detail));
        auto compression = std::string{shared.compression.mode};
        if (shared.compression.mode != "none")
        {
            compression +=
                ", level " +
                format_unsigned_integer(static_cast<std::uint64_t>(shared.compression.level));
        }
        add_row("Compression", std::move(compression));
        add_row(
            "Packetization",
            shared.packetization.enabled
                ? "Enabled, " + format_unsigned_integer(shared.packetization.max_payload_bytes) +
                      " bytes"
                : "Disabled"
        );
        end_section();
    }

    void RunSetupStorage::add_workload(SharedConfig const& shared)
    {
        begin_section("WORKLOAD", false);
        add_row("Tick rate", format_float(shared.simulation.tick_rate_hz, 1) + " Hz");
        add_row("Initial boids", format_unsigned_integer(shared.simulation.initial_boid_count));
        add_row(
            "World size",
            format_float(shared.simulation.world_half * 2.0F, 0) + " x " +
                format_float(shared.simulation.world_half * 2.0F, 0) + " x " +
                format_float(shared.simulation.world_half * 2.0F, 0)
        );
        add_row("Seed", format_unsigned_integer(shared.run.seed));
        add_row("Spatial cell size", format_float(shared.spatial.cell_size));
        add_row("Maximum neighbors", format_unsigned_integer(shared.spatial.max_neighbors));
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
        add_row("Runtime fingerprint", format_hexadecimal(revision_));
        add_row(
            "Network fingerprint",
            format_hexadecimal(fingerprint_network_compatibility(shared).value)
        );
#if defined(SIMNET_BUILD_CONFIG)
        add_row("Build", SIMNET_BUILD_CONFIG);
#endif
        add_row("Flecs workers", format_unsigned_integer(local.flecs.thread_count));
        end_section();
        add_techniques(pipeline, shared);
        add_workload(shared);

        begin_section("TRANSPORT", false);
        add_row("Backend", "ENet");
        add_row("Delivery", shared.snapshot_delivery.mode);
        add_row(
            "Maximum payload",
            format_unsigned_integer(local.transport.max_payload_bytes) + " bytes"
        );
        add_row("Configured client limit", format_unsigned_integer(local.transport.max_clients));
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
        add_row("Runtime fingerprint", format_hexadecimal(revision_));
        add_row(
            "Network fingerprint",
            format_hexadecimal(fingerprint_network_compatibility(shared).value)
        );
#if defined(SIMNET_BUILD_CONFIG)
        add_row("Build", SIMNET_BUILD_CONFIG);
#endif
        add_row("Client role", local.gameplay.role);
        end_section();
        add_techniques(pipeline, shared);
        add_workload(shared);

        begin_section("TRANSPORT", false);
        add_row("Backend", "ENet");
        add_row("Delivery", shared.snapshot_delivery.mode);
        add_row(
            "Maximum payload",
            format_unsigned_integer(local.transport.max_payload_bytes) + " bytes"
        );
        end_section();
    }

    RunSetupView RunSetupStorage::view() const noexcept
    {
        return {
            .revision = revision_,
            .sections = std::span{sections_}.first(section_count_),
        };
    }
}
