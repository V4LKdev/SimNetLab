#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test_temporary_directory.hpp"

import simnet.app_compression_corpus;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;

namespace
{
    using TestTemporaryDirectory = simnet::test::TestTemporaryDirectory;

    [[nodiscard]] simnet::EvidenceRunContext corpus_run()
    {
        return {
            .run_id = "corpus-run",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 100U,
            .monotonic_start = std::chrono::steady_clock::now(),
        };
    }

    [[nodiscard]] std::vector<simnet::Byte> bytes_from(std::string_view text)
    {
        auto bytes = std::vector<simnet::Byte>{};
        bytes.reserve(text.size());
        for (auto const value : text)
        {
            bytes.push_back(static_cast<simnet::Byte>(static_cast<unsigned char>(value)));
        }
        return bytes;
    }

    [[nodiscard]] std::vector<simnet::Byte> read_bytes(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
        REQUIRE(input);

        auto const end = input.tellg();
        REQUIRE(end > 0);

        auto bytes = std::vector<simnet::Byte>(static_cast<std::size_t>(end));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(end));
        REQUIRE(input);
        return bytes;
    }

    [[nodiscard]] std::vector<std::string> read_lines(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path};
        REQUIRE(input);

        auto lines = std::vector<std::string>{};
        auto line = std::string{};
        while (std::getline(input, line))
        {
            lines.push_back(line);
        }
        return lines;
    }

    [[nodiscard]] simnet::WorldSnapshot make_snapshot(simnet::Tick tick)
    {
        auto snapshot = simnet::WorldSnapshot{};
        snapshot.tick = tick;
        snapshot.ids = {1U, 2U};
        snapshot.classifications = {
            simnet::EntityClassification{1U},
            simnet::EntityClassification{1U},
        };
        snapshot.positions = {{1.0F, 2.0F, 3.0F}, {-4.0F, 5.0F, -6.0F}};
        snapshot.headings = {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
        snapshot.hues = {7U, 8U};
        return snapshot;
    }

    [[nodiscard]] simnet::PipelineDefinition manifest_pipeline()
    {
        return {
            .techniques = simnet::PipelineTechniqueFlags::SendInterval |
                          simnet::PipelineTechniqueFlags::Incremental |
                          simnet::PipelineTechniqueFlags::Quantization |
                          simnet::PipelineTechniqueFlags::OctHeading |
                          simnet::PipelineTechniqueFlags::Delta |
                          simnet::PipelineTechniqueFlags::DeltaFieldMask |
                          simnet::PipelineTechniqueFlags::BitPacking,
            .area_of_interest = {.mode = simnet::AreaOfInterestMode::Fov},
            .level_of_detail = {.mode = simnet::LevelOfDetailMode::DistanceBands},
        };
    }

    [[nodiscard]] simnet::EncodeOutput manifest_output()
    {
        return {
            .kind = simnet::EncodeResultKind::Update,
            .update = {.sequence = 4U, .bytes = bytes_from("abc")},
            .report =
                {
                    .tick = 9U,
                    .sequence = 4U,
                    .baseline_sequence = 3U,
                    .snapshot_kind = simnet::SnapshotKind::Patch,
                    .representation =
                        {
                            .layout = simnet::EntityRecordLayout::QuantizedOctHeading,
                        },
                },
            .resulting_snapshot = make_snapshot(9U),
        };
    }
}

TEST_CASE("compression corpus stores exact production encoded bytes", "[compression][corpus]")
{
    auto temporary = TestTemporaryDirectory{"simnet_compression_corpus"};
    auto const output_directory = temporary.path() / "capture";

    auto pipeline = simnet::PipelineDefinition{
        .techniques = simnet::PipelineTechniqueFlags::Quantization |
                      simnet::PipelineTechniqueFlags::OctHeading |
                      simnet::PipelineTechniqueFlags::BitPacking,
    };
    auto state = simnet::ClientReplicationState{};
    auto scratch = simnet::PipelineScratch{};
    auto const snapshot = make_snapshot(17U);
    auto const encoded = simnet::encode_snapshot(pipeline, state, scratch, {.snapshot = &snapshot});
    REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);

    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = output_directory,
        .run = corpus_run(),
        .seed = 41002U,
    }};
    REQUIRE(writer.capture(7U, pipeline, 2U, encoded));
    REQUIRE(writer.close());

    auto const sample_path = output_directory / "sample_peer_7_sequence_1_tick_17.bin";
    CHECK(read_bytes(sample_path) == encoded.update.bytes);
    CHECK(writer.sample_count() == 1U);
}

TEST_CASE("compression corpus manifest preserves treatment identity and sample hash", "[compression][corpus]")
{
    auto temporary = TestTemporaryDirectory{"simnet_compression_corpus"};
    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = temporary.path() / "capture",
        .run = corpus_run(),
        .seed = 41001U,
    }};

    REQUIRE(writer.capture(3U, manifest_pipeline(), 5U, manifest_output()));
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.manifest_path());
    REQUIRE(lines.size() == 2U);
    CHECK(lines[0] == simnet::app::compression_corpus_manifest_header_v1);
    CHECK(
        lines[1] == "1,corpus-run,3,9,4,3,patch,quantized_oct_heading,1,1,1,1,1,1,1,fov,"
                    "distance_bands,41001,2,5,3,"
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad,"
                    "sample_peer_3_sequence_4_tick_9.bin"
    );
}

TEST_CASE("compression corpus refuses to overwrite existing output", "[compression][corpus]")
{
    auto temporary = TestTemporaryDirectory{"simnet_compression_corpus"};
    auto const output_directory = temporary.path() / "capture";
    std::filesystem::create_directories(output_directory);

    auto const sentinel_path = output_directory / "keep.bin";
    {
        auto sentinel = std::ofstream{sentinel_path, std::ios::binary};
        REQUIRE(sentinel);
        sentinel << "keep";
    }

    CHECK_THROWS(
        simnet::app::CompressionCorpusWriter({
            .output_directory = output_directory,
            .run = corpus_run(),
            .seed = 41001U,
        })
    );
    CHECK(read_bytes(sentinel_path) == bytes_from("keep"));
}

TEST_CASE("compression corpus rejects inconsistent capture metadata", "[compression][corpus]")
{
    auto temporary = TestTemporaryDirectory{"simnet_compression_corpus"};
    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = temporary.path() / "capture",
        .run = corpus_run(),
        .seed = 41001U,
    }};

    auto encoded = manifest_output();
    encoded.update.sequence = 5U;

    CHECK_FALSE(writer.capture(3U, manifest_pipeline(), 5U, encoded));
    CHECK_FALSE(writer.healthy());
    CHECK(writer.sample_count() == 0U);
}