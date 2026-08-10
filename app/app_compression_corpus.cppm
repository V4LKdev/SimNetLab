module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module simnet.app_compression_corpus;

import simnet.core;
import simnet.pipeline;
import simnet.telemetry;

export namespace simnet::app
{
    inline constexpr std::uint32_t compression_corpus_manifest_schema_version = 1U;
    inline constexpr std::string_view compression_corpus_manifest_header_v1 =
        "schema_version,run_id,peer_id,tick,sequence,baseline_sequence,snapshot_kind,"
        "representation,send_interval,incremental,quantization,oct_heading,delta,"
        "delta_field_mask,bit_packing,area_of_interest,level_of_detail,seed,entity_count,"
        "source_entity_count,byte_count,sha256,sample_file";

    struct CompressionCorpusWriterConfig
    {
        std::optional<std::filesystem::path> output_directory{};
        EvidenceRunContext run{};
        std::uint64_t seed{};
    };

    /// Writes exact production EncodedUpdate samples to one exclusive developer-owned directory.
    class CompressionCorpusWriter
    {
      public:
        explicit CompressionCorpusWriter(CompressionCorpusWriterConfig config);
        ~CompressionCorpusWriter();

        CompressionCorpusWriter(CompressionCorpusWriter const&) = delete;
        CompressionCorpusWriter& operator=(CompressionCorpusWriter const&) = delete;
        CompressionCorpusWriter(CompressionCorpusWriter&&) = delete;
        CompressionCorpusWriter& operator=(CompressionCorpusWriter&&) = delete;

        [[nodiscard]] bool capture(
            PeerId peer,
            PipelineDefinition const& pipeline,
            std::uint32_t source_entity_count,
            EncodeOutput const& encoded
        );
        [[nodiscard]] bool close();
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] bool healthy() const noexcept;
        [[nodiscard]] std::uint64_t sample_count() const noexcept;
        [[nodiscard]] std::string_view error() const noexcept;
        [[nodiscard]] std::filesystem::path const& manifest_path() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
