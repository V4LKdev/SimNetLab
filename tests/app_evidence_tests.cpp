#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

import simnet.app_evidence;
import simnet.core;
import simnet.game_server;
import simnet.telemetry;

static_assert(!std::is_copy_constructible_v<simnet::app::ServerBoidCsvWriter>);
static_assert(!std::is_copy_assignable_v<simnet::app::ServerBoidCsvWriter>);
static_assert(!std::is_move_constructible_v<simnet::app::ServerBoidCsvWriter>);
static_assert(!std::is_move_assignable_v<simnet::app::ServerBoidCsvWriter>);

namespace
{
    class EvidenceTemporaryDirectory
    {
    public:
        EvidenceTemporaryDirectory()
        {
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path()
                / ("simnet_boid_tel003_" + std::to_string(stamp));
            std::filesystem::create_directories(path_);
        }

        ~EvidenceTemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_{};
    };

    [[nodiscard]] std::vector<std::string> read_evidence_lines(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path};
        auto lines = std::vector<std::string>{};
        auto line = std::string{};
        while (std::getline(input, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    [[nodiscard]] simnet::ServerGameStepReport sample_report()
    {
        return {
            .entity_count = 10,
            .diagnostics = {
                .grid = {
                    .entity_count = 10,
                    .occupied_cell_count = 4,
                    .max_cell_occupancy = 5,
                    .average_occupied_cell_load = 1.25F,
                },
                .raw_candidates_mean = 2.5,
                .raw_candidates_max = 6,
                .retained_neighbors_mean = 3.5,
                .retained_neighbors_max = 7,
                .neighbor_cap_hit_count = 8,
                .separation_neighbors_mean = 4.5,
                .social_neighbors_mean = 5.5,
                .isolated_boid_count = 9,
                .nearest_neighbor_distance_mean = 6.5,
                .speed_mean = 7.5,
                .speed_min = 8.5F,
                .speed_max = 9.5F,
                .acceleration_mean = 10.5,
                .acceleration_max = 11.5F,
                .acceleration_saturation_count = 12,
                .overlap_recovery_count = 13,
                .hard_wall_guard_count = 14,
                .polarization = 0.75F,
            },
            .phases = {
                .capture_ms = 1.0,
                .grid_ms = 2.0,
                .compute_ms = 3.0,
                .validate_ms = 4.0,
                .commit_ms = 5.0,
                .progress_ms = 6.0,
            },
        };
    }
}

TEST_CASE("Server boid CSV has an exact buffered v1 schema", "[telemetry][csv][boids]")
{
    CHECK(
        simnet::app::server_boids_csv_header_v1
        == "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
           "elapsed_since_process_start_ns,record_order,tick,entity_count,worker_count,"
           "occupied_cell_count,max_cell_occupancy,average_entities_per_occupied_cell,"
           "raw_candidate_count_mean,raw_candidate_count_max,retained_neighbor_count_mean,"
           "retained_neighbor_count_max,neighbor_cap_hit_count,separation_neighbor_count_mean,"
           "social_neighbor_count_mean,isolated_boid_count,"
           "nearest_neighbor_distance_world_units_mean,speed_world_units_per_second_mean,"
           "speed_world_units_per_second_min,speed_world_units_per_second_max,"
           "acceleration_world_units_per_second_squared_mean,"
           "acceleration_world_units_per_second_squared_max,acceleration_saturation_count,"
           "overlap_recovery_count,hard_wall_guard_count,polarization_ratio,capture_duration_ms,"
           "grid_duration_ms,compute_duration_ms,validate_duration_ms,commit_duration_ms,"
           "progress_duration_ms"
    );
    auto temporary = EvidenceTemporaryDirectory{};
    auto writer = simnet::app::ServerBoidCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "boid-run",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 100,
        },
        .tick_rate_hz = 2.0,
        .worker_count = 3,
    }};
    CHECK(writer.path().filename() == "server_boids_v1_100.csv");

    auto const report = sample_report();
    REQUIRE(
        writer.sample(1, report, {.recorded_at_unix_ns = 105, .elapsed_since_process_start_ns = 4})
    );
    CHECK(writer.submitted_count() == 0);
    REQUIRE(
        writer.sample(2, report, {.recorded_at_unix_ns = 110, .elapsed_since_process_start_ns = 9})
    );
    REQUIRE(writer.sample(
        2,
        report,
        {.recorded_at_unix_ns = 120, .elapsed_since_process_start_ns = 19},
        true
    ));
    CHECK(writer.submitted_count() == 1);
    CHECK(writer.buffered_count() == 1);
    REQUIRE(writer.drain());
    CHECK(read_evidence_lines(writer.path()).size() == 2);
    REQUIRE(writer.close());

    auto const lines = read_evidence_lines(writer.path());
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == simnet::app::server_boids_csv_header_v1);
    CHECK(
        lines[1]
        == "1,boid-run,server,100,110,9,0,2,10,3,4,5,1.25,2.5,6,3.5,7,8,4.5,5.5,9,6.5,7.5,8.5,9.5,10.5,11.5,12,13,14,0.75,1,2,3,4,5,6"
    );
}

TEST_CASE(
    "enabled Server boid CSV rejects a forged run context before output",
    "[telemetry][csv][boids]"
)
{
    auto temporary = EvidenceTemporaryDirectory{};
    auto const output_directory = temporary.path() / "forged_boid";
    CHECK_THROWS(simnet::app::ServerBoidCsvWriter({
        .enabled = true,
        .output_directory = output_directory,
        .run = {
            .run_id = "../unsafe",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 150,
        },
        .tick_rate_hz = 1.0,
        .worker_count = 1,
    }));
    CHECK_FALSE(std::filesystem::exists(output_directory));
}

TEST_CASE("Server boid CSV disabling and overflow are explicit", "[telemetry][csv][boids]")
{
    auto temporary = EvidenceTemporaryDirectory{};
    auto const disabled_directory = temporary.path() / "disabled";
    auto disabled = simnet::app::ServerBoidCsvWriter{{
        .enabled = false,
        .output_directory = disabled_directory,
        .run = {
            .run_id = "disabled",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 200,
        },
        .tick_rate_hz = 1.0,
        .worker_count = 1,
    }};
    CHECK(disabled.sample(1, sample_report()));
    CHECK(disabled.close());
    CHECK(disabled.sample(2, sample_report()));
    CHECK(disabled.drain());
    CHECK(disabled.close());
    CHECK(disabled.healthy());
    CHECK_FALSE(std::filesystem::exists(disabled_directory));

    auto writer = simnet::app::ServerBoidCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "overflow",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 201,
        },
        .tick_rate_hz = 1.0,
        .worker_count = 1,
    }};
    auto const report = sample_report();
    for (auto tick = simnet::Tick{1}; tick <= simnet::app::server_boids_csv_buffer_capacity;
         ++tick) {
        REQUIRE(writer.sample(
            tick,
            report,
            {.recorded_at_unix_ns = tick, .elapsed_since_process_start_ns = tick}
        ));
    }
    CHECK_FALSE(writer.sample(simnet::app::server_boids_csv_buffer_capacity + 1U, report));
    CHECK_FALSE(writer.healthy());
    CHECK(writer.error().find("overflow") != std::string_view::npos);
    CHECK_FALSE(writer.close());
    CHECK_FALSE(writer.close());
    CHECK(writer.buffered_count() == 0);
    CHECK(
        read_evidence_lines(writer.path()).size()
        == simnet::app::server_boids_csv_buffer_capacity + 1U
    );

    auto closed_writer = simnet::app::ServerBoidCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "closed-boid",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 202,
        },
        .tick_rate_hz = 1.0,
        .worker_count = 1,
    }};
    REQUIRE(closed_writer.close());
    CHECK_FALSE(closed_writer.sample(1, report));
    CHECK_FALSE(closed_writer.healthy());
    CHECK(closed_writer.error().find("after close") != std::string_view::npos);
    CHECK_FALSE(closed_writer.close());
}
