module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

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
        void add_workload(SharedConfig const& shared);
        void add_techniques(PipelineDefinition const& pipeline, SharedConfig const& shared);

        std::array<std::string, 32> values_{};
        std::array<SetupRowView, 32> rows_{};
        std::array<SetupSectionView, 4> sections_{};
        std::size_t value_count_{};
        std::size_t row_count_{};
        std::size_t section_count_{};
        std::size_t active_section_row_{};
        std::uint64_t revision_{};
    };
}
