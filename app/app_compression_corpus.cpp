module;

#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

module simnet.app_compression_corpus;

import simnet.core;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;

namespace
{
    using namespace simnet;
    using namespace simnet::app;

    constexpr auto sha256_initial = std::array<std::uint32_t, 8>{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    constexpr auto sha256_round = std::array<std::uint32_t, 64>{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U,
    };

    [[nodiscard]] constexpr std::uint32_t
    choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return (x & y) ^ (~x & z);
    }

    [[nodiscard]] constexpr std::uint32_t
    majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    void sha256_block(std::array<std::uint32_t, 8>& state, std::uint8_t const* block) noexcept
    {
        auto words = std::array<std::uint32_t, 64>{};
        for (auto index = std::size_t{}; index < 16U; ++index)
        {
            auto const offset = index * 4U;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                           (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                           static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (auto index = std::size_t{16U}; index < words.size(); ++index)
        {
            auto const s0 = std::rotr(words[index - 15U], 7) ^ std::rotr(words[index - 15U], 18) ^
                            (words[index - 15U] >> 3U);
            auto const s1 = std::rotr(words[index - 2U], 17) ^ std::rotr(words[index - 2U], 19) ^
                            (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (auto index = std::size_t{}; index < words.size(); ++index)
        {
            auto const s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            auto const first = h + s1 + choose(e, f, g) + sha256_round[index] + words[index];
            auto const s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            auto const second = s0 + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    [[nodiscard]] std::string sha256(ByteSpan bytes)
    {
        auto state = sha256_initial;
        auto offset = std::size_t{};
        while (bytes.size() - offset >= 64U)
        {
            sha256_block(state, reinterpret_cast<std::uint8_t const*>(bytes.data() + offset));
            offset += 64U;
        }

        auto tail = std::array<std::uint8_t, 128>{};
        auto const remaining = bytes.size() - offset;
        for (auto index = std::size_t{}; index < remaining; ++index)
        {
            tail[index] = static_cast<std::uint8_t>(bytes[offset + index]);
        }
        tail[remaining] = 0x80U;
        auto const padded_bytes = remaining < 56U ? 64U : 128U;
        auto const bit_count = static_cast<std::uint64_t>(bytes.size()) * 8U;
        for (auto index = std::size_t{}; index < 8U; ++index)
        {
            tail[padded_bytes - 1U - index] = static_cast<std::uint8_t>(bit_count >> (index * 8U));
        }
        sha256_block(state, tail.data());
        if (padded_bytes == 128U)
        {
            sha256_block(state, tail.data() + 64U);
        }

        constexpr auto digits = std::string_view{"0123456789abcdef"};
        auto result = std::string{};
        result.reserve(64U);
        for (auto const word : state)
        {
            for (auto shift = 28; shift >= 0; shift -= 4)
            {
                result.push_back(digits[(word >> static_cast<unsigned>(shift)) & 0x0fU]);
            }
        }
        return result;
    }

    template <std::integral Value> void append_integer(std::string& output, Value value)
    {
        char buffer[32]{};
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error("failed to format compression corpus manifest integer");
        }
        output.append(buffer, result.ptr);
    }

    void append_field(std::string& output, std::string_view value)
    {
        if (!output.empty())
        {
            output.push_back(',');
        }
        output.append(value);
    }

    template <std::integral Value> void append_field(std::string& output, Value value)
    {
        if (!output.empty())
        {
            output.push_back(',');
        }
        append_integer(output, value);
    }

    void append_field(std::string& output, bool value)
    {
        append_field(output, value ? 1U : 0U);
    }

    [[nodiscard]] std::string_view snapshot_kind_name(SnapshotKind kind) noexcept
    {
        return kind == SnapshotKind::Patch ? "patch" : "full_replace";
    }

    [[nodiscard]] std::string_view representation_name(EntityRecordLayout layout) noexcept
    {
        switch (layout)
        {
            case EntityRecordLayout::Raw:
                return "raw";
            case EntityRecordLayout::Quantized:
                return "quantized";
            case EntityRecordLayout::QuantizedOctHeading:
                return "quantized_oct_heading";
            case EntityRecordLayout::BitPackedQuantizedOctHeading:
                return "bit_packed_quantized_oct_heading";
        }
        return "unknown";
    }

    [[nodiscard]] std::string_view area_of_interest_name(AreaOfInterestMode mode) noexcept
    {
        switch (mode)
        {
            case AreaOfInterestMode::None:
                return "none";
            case AreaOfInterestMode::Radius:
                return "radius";
            case AreaOfInterestMode::Fov:
                return "fov";
        }
        return "unknown";
    }

    [[nodiscard]] std::string_view level_of_detail_name(LevelOfDetailMode mode) noexcept
    {
        return mode == LevelOfDetailMode::DistanceBands ? "distance_bands" : "none";
    }

    [[nodiscard]] std::string sample_filename(PeerId peer, EncodeReport const& report)
    {
        return "sample_peer_" + std::to_string(peer) + "_sequence_" +
               std::to_string(report.sequence) + "_tick_" + std::to_string(report.tick) + ".bin";
    }

    [[nodiscard]] bool
    write_sample(std::filesystem::path const& path, ByteSpan bytes, std::string& error)
    {
        if (bytes.size() > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = "compression corpus sample exceeds stream size: " + path.string();
            return false;
        }
        auto output = std::ofstream{path, std::ios::binary | std::ios::out | std::ios::noreplace};
        if (!output)
        {
            error = "failed to exclusively create compression corpus sample: " + path.string();
            return false;
        }
        output.write(
            reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        output.flush();
        if (!output)
        {
            error = "failed to write or flush compression corpus sample: " + path.string();
        }
        output.close();
        if (output.fail() && error.empty())
        {
            error = "failed to close compression corpus sample: " + path.string();
        }
        if (error.empty())
        {
            return true;
        }
        auto cleanup_error = std::error_code{};
        std::filesystem::remove(path, cleanup_error);
        if (cleanup_error)
        {
            error += ". Failed to remove incomplete sample: " + cleanup_error.message();
        }
        return false;
    }
}

namespace simnet::app
{
    struct CompressionCorpusWriter::Impl
    {
        explicit Impl(CompressionCorpusWriterConfig writer_config)
            : config(std::move(writer_config))
        {
            if (!config.output_directory.has_value())
            {
                return;
            }
            validate_evidence_run_context(config.run);
            if (config.run.process_role != EvidenceProcessRole::Server)
            {
                throw std::invalid_argument("compression corpus process role must be Server");
            }
            if (config.output_directory->empty())
            {
                throw std::invalid_argument(
                    "compression corpus output directory must not be empty"
                );
            }

            auto filesystem_error = std::error_code{};
            auto const exists = std::filesystem::exists(*config.output_directory, filesystem_error);
            if (filesystem_error)
            {
                throw std::runtime_error(
                    "failed to inspect compression corpus output directory: " +
                    filesystem_error.message()
                );
            }
            if (exists)
            {
                auto const directory =
                    std::filesystem::is_directory(*config.output_directory, filesystem_error);
                if (filesystem_error || !directory)
                {
                    throw std::runtime_error(
                        "compression corpus destination is not a directory: " +
                        config.output_directory->string()
                    );
                }
                auto const empty =
                    std::filesystem::is_empty(*config.output_directory, filesystem_error);
                if (filesystem_error || !empty)
                {
                    throw std::runtime_error(
                        "compression corpus output directory must be empty: " +
                        config.output_directory->string()
                    );
                }
            }
            else
            {
                auto const created =
                    std::filesystem::create_directories(*config.output_directory, filesystem_error);
                if (!created || filesystem_error)
                {
                    auto message =
                        std::string{"failed to exclusively create compression corpus directory: "} +
                        config.output_directory->string();
                    if (filesystem_error)
                    {
                        message += ". " + filesystem_error.message();
                    }
                    throw std::runtime_error(message);
                }
            }

            manifest_path = *config.output_directory / "manifest.csv";
            manifest.emplace(manifest_path, compression_corpus_manifest_header_v1);
        }

        void reject(std::string message)
        {
            if (failure.empty())
            {
                failure = std::move(message);
            }
        }

        CompressionCorpusWriterConfig config{};
        std::optional<EvidenceCsvFile> manifest{};
        std::filesystem::path manifest_path{};
        std::string failure{};
        std::uint64_t samples{};
        bool closed{};
    };

    CompressionCorpusWriter::CompressionCorpusWriter(CompressionCorpusWriterConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    CompressionCorpusWriter::~CompressionCorpusWriter()
    {
        if (impl_)
        {
            static_cast<void>(close());
        }
    }

    bool CompressionCorpusWriter::capture(
        PeerId peer,
        PipelineDefinition const& pipeline,
        std::uint32_t source_entity_count,
        EncodeOutput const& encoded
    )
    {
        if (!enabled())
        {
            return true;
        }
        if (impl_->closed)
        {
            impl_->reject("compression corpus capture attempted after close");
            return false;
        }
        if (!impl_->failure.empty())
        {
            return false;
        }
        if (peer == 0U || encoded.kind != EncodeResultKind::Update ||
            encoded.update.sequence == 0U || encoded.update.sequence != encoded.report.sequence ||
            encoded.update.bytes.empty() ||
            encoded.update.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
            encoded.resulting_snapshot.size() > std::numeric_limits<std::uint32_t>::max())
        {
            impl_->reject("compression corpus sample metadata or byte count is invalid");
            return false;
        }

        auto const filename = sample_filename(peer, encoded.report);
        auto const sample_path = *impl_->config.output_directory / filename;
        auto sample_error = std::string{};
        if (!write_sample(sample_path, encoded.update.bytes, sample_error))
        {
            impl_->reject(std::move(sample_error));
            return false;
        }

        auto row = std::string{};
        row.reserve(512U);
        append_field(row, compression_corpus_manifest_schema_version);
        append_field(row, impl_->config.run.run_id);
        append_field(row, peer);
        append_field(row, encoded.report.tick);
        append_field(row, encoded.report.sequence);
        append_field(row, encoded.report.baseline_sequence);
        append_field(row, snapshot_kind_name(encoded.report.snapshot_kind));
        append_field(row, representation_name(encoded.report.representation.layout));
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::SendInterval));
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Incremental));
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization));
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading));
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta));
        append_field(
            row,
            has_all_flags(pipeline.techniques, PipelineTechniqueFlags::DeltaFieldMask)
        );
        append_field(row, has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking));
        append_field(row, area_of_interest_name(pipeline.area_of_interest.mode));
        append_field(row, level_of_detail_name(pipeline.level_of_detail.mode));
        append_field(row, impl_->config.seed);
        append_field(row, static_cast<std::uint32_t>(encoded.resulting_snapshot.size()));
        append_field(row, source_entity_count);
        append_field(row, static_cast<std::uint32_t>(encoded.update.bytes.size()));
        append_field(row, sha256(encoded.update.bytes));
        append_field(row, filename);

        if (!impl_->manifest->write_row(row) || !impl_->manifest->flush())
        {
            auto cleanup_error = std::error_code{};
            std::filesystem::remove(sample_path, cleanup_error);
            auto error = std::string{"failed to write or flush compression corpus manifest: "} +
                         std::string{impl_->manifest->error()};
            if (cleanup_error)
            {
                error += ". Failed to remove uncommitted sample: " + cleanup_error.message();
            }
            impl_->reject(std::move(error));
            return false;
        }
        ++impl_->samples;
        return true;
    }

    bool CompressionCorpusWriter::close()
    {
        if (!enabled())
        {
            return true;
        }
        if (impl_->closed)
        {
            return impl_->failure.empty();
        }
        if (!impl_->manifest->close())
        {
            impl_->reject(
                "failed to close compression corpus manifest: " +
                std::string{impl_->manifest->error()}
            );
        }
        impl_->closed = true;
        return impl_->failure.empty();
    }

    bool CompressionCorpusWriter::enabled() const noexcept
    {
        return impl_ != nullptr && impl_->config.output_directory.has_value();
    }

    bool CompressionCorpusWriter::healthy() const noexcept
    {
        return impl_ != nullptr && impl_->failure.empty();
    }

    std::uint64_t CompressionCorpusWriter::sample_count() const noexcept
    {
        return impl_->samples;
    }

    std::string_view CompressionCorpusWriter::error() const noexcept
    {
        return impl_->failure;
    }

    std::filesystem::path const& CompressionCorpusWriter::manifest_path() const noexcept
    {
        return impl_->manifest_path;
    }
}
