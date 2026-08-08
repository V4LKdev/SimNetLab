#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

import simnet.app_compression_corpus;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;

static_assert(!std::is_copy_constructible_v<simnet::app::CompressionCorpusWriter>);
static_assert(!std::is_copy_assignable_v<simnet::app::CompressionCorpusWriter>);
static_assert(!std::is_move_constructible_v<simnet::app::CompressionCorpusWriter>);
static_assert(!std::is_move_assignable_v<simnet::app::CompressionCorpusWriter>);

namespace
{
    class CorpusTemporaryDirectory
    {
    public:
        CorpusTemporaryDirectory()
        {
            static auto next_id = std::atomic_uint64_t{};
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path()
                / ("simnet_compression_corpus_" + std::to_string(stamp) + "_"
                   + std::to_string(next_id.fetch_add(1U)));
            std::filesystem::create_directories(path_);
        }

        ~CorpusTemporaryDirectory()
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_{};
    };

    [[nodiscard]] simnet::EvidenceRunContext corpus_run(std::string run_id = "corpus-run")
    {
        return {
            .run_id = std::move(run_id),
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 100U,
            .monotonic_start = std::chrono::steady_clock::now(),
        };
    }

    [[nodiscard]] std::vector<simnet::Byte> bytes_from(std::string_view text)
    {
        auto bytes = std::vector<simnet::Byte>{};
        bytes.reserve(text.size());
        for (auto const value : text) {
            bytes.push_back(static_cast<simnet::Byte>(static_cast<unsigned char>(value)));
        }
        return bytes;
    }

    [[nodiscard]] std::vector<simnet::Byte> read_bytes(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
        if (!input) {
            return {};
        }
        auto const end = input.tellg();
        if (end <= 0) {
            return {};
        }
        auto bytes = std::vector<simnet::Byte>(static_cast<std::size_t>(end));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(end));
        return input ? bytes : std::vector<simnet::Byte>{};
    }

    [[nodiscard]] std::vector<std::string> read_lines(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path};
        auto lines = std::vector<std::string>{};
        auto line = std::string{};
        while (std::getline(input, line)) {
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
            .techniques = simnet::PipelineTechniqueFlags::SendInterval
                | simnet::PipelineTechniqueFlags::Incremental
                | simnet::PipelineTechniqueFlags::Quantization
                | simnet::PipelineTechniqueFlags::OctHeading | simnet::PipelineTechniqueFlags::Delta
                | simnet::PipelineTechniqueFlags::DeltaFieldMask
                | simnet::PipelineTechniqueFlags::BitPacking,
            .area_of_interest = {.mode = simnet::AreaOfInterestMode::Fov},
            .level_of_detail = {.mode = simnet::LevelOfDetailMode::DistanceBands},
        };
    }

    [[nodiscard]] simnet::EncodeOutput manifest_output()
    {
        return {
            .kind = simnet::EncodeResultKind::Update,
            .update = {.sequence = 4U, .bytes = bytes_from("abc")},
            .report = {
                .tick = 9U,
                .sequence = 4U,
                .baseline_sequence = 3U,
                .snapshot_kind = simnet::SnapshotKind::Patch,
                .representation = {
                    .layout = simnet::EntityRecordLayout::QuantizedOctHeading,
                },
            },
            .resulting_snapshot = make_snapshot(9U),
        };
    }
}

TEST_CASE(
    "disabled compression corpus capture performs no filesystem work",
    "[compression][corpus]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto writer = simnet::app::CompressionCorpusWriter{{
        .run = corpus_run("../ignored-while-disabled"),
        .seed = 41001U,
    }};

    CHECK_FALSE(writer.enabled());
    REQUIRE(writer.capture(3U, manifest_pipeline(), 5U, manifest_output()));
    REQUIRE(writer.close());
    CHECK(std::filesystem::is_empty(temporary.path()));
}

TEST_CASE(
    "compression corpus preserves exact production EncodedUpdate bytes",
    "[compression][corpus][pipeline]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto const output_directory = temporary.path() / "capture";
    auto pipeline = simnet::PipelineDefinition{
        .techniques = simnet::PipelineTechniqueFlags::Quantization
            | simnet::PipelineTechniqueFlags::OctHeading
            | simnet::PipelineTechniqueFlags::BitPacking,
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

TEST_CASE(
    "compression corpus manifest records deterministic identity and standard SHA-256",
    "[compression][corpus]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = temporary.path() / "capture",
        .run = corpus_run(),
        .seed = 41001U,
    }};
    REQUIRE(writer.capture(3U, manifest_pipeline(), 5U, manifest_output()));
    auto block_output = manifest_output();
    block_output.update.sequence = 5U;
    block_output.update.bytes = bytes_from(std::string(64U, 'a'));
    block_output.report.tick = 10U;
    block_output.report.sequence = 5U;
    block_output.report.baseline_sequence = 4U;
    REQUIRE(writer.capture(3U, manifest_pipeline(), 5U, block_output));
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.manifest_path());
    REQUIRE(lines.size() == 3U);
    CHECK(lines[0] == simnet::app::compression_corpus_manifest_header_v1);
    CHECK(
        lines[1]
        == "1,corpus-run,3,9,4,3,patch,quantized_oct_heading,1,1,1,1,1,1,1,fov,"
           "distance_bands,41001,2,5,3,"
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad,"
           "sample_peer_3_sequence_4_tick_9.bin"
    );
    CHECK(
        lines[2].find("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb")
        != std::string::npos
    );
}

TEST_CASE(
    "compression corpus refuses a nonempty destination before output",
    "[compression][corpus][failure]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto const output_directory = temporary.path() / "capture";
    std::filesystem::create_directories(output_directory);
    auto const sentinel_path = output_directory / "keep.bin";
    {
        auto sentinel = std::ofstream{sentinel_path, std::ios::binary};
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
    CHECK(
        std::filesystem::directory_iterator{output_directory}
        != std::filesystem::directory_iterator{}
    );
}

TEST_CASE(
    "compression corpus sample collision is fatal and never overwrites",
    "[compression][corpus][failure]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto const output_directory = temporary.path() / "capture";
    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = output_directory,
        .run = corpus_run(),
        .seed = 41001U,
    }};
    auto const sample_path = output_directory / "sample_peer_3_sequence_4_tick_9.bin";
    {
        auto sentinel = std::ofstream{sample_path, std::ios::binary};
        sentinel << "keep";
    }

    CHECK_FALSE(writer.capture(3U, manifest_pipeline(), 5U, manifest_output()));
    CHECK_FALSE(writer.healthy());
    CHECK(writer.error().find("exclusively create") != std::string_view::npos);
    CHECK(writer.sample_count() == 0U);
    CHECK(read_bytes(sample_path) == bytes_from("keep"));
    CHECK_FALSE(writer.close());
    CHECK(read_lines(writer.manifest_path()).size() == 1U);
}

TEST_CASE(
    "compression corpus rejects invalid metadata without a sample",
    "[compression][corpus][failure]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto const output_directory = temporary.path() / "capture";
    auto writer = simnet::app::CompressionCorpusWriter{{
        .output_directory = output_directory,
        .run = corpus_run(),
        .seed = 41001U,
    }};
    auto encoded = manifest_output();
    encoded.update.sequence = 5U;

    CHECK_FALSE(writer.capture(3U, manifest_pipeline(), 5U, encoded));
    CHECK_FALSE(writer.healthy());
    CHECK(writer.error().find("metadata") != std::string_view::npos);
    CHECK(writer.sample_count() == 0U);
    CHECK_FALSE(writer.close());
    CHECK(read_lines(writer.manifest_path()).size() == 1U);
}

TEST_CASE(
    "compression corpus rejects a forged run before creating output",
    "[compression][corpus][failure]"
)
{
    auto temporary = CorpusTemporaryDirectory{};
    auto const output_directory = temporary.path() / "capture";

    CHECK_THROWS(
        simnet::app::CompressionCorpusWriter({
            .output_directory = output_directory,
            .run = corpus_run("../unsafe"),
            .seed = 41001U,
        })
    );
    CHECK_FALSE(std::filesystem::exists(output_directory));
}
