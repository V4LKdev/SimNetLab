#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

import simnet.telemetry;
import simnet.config;
import simnet.core;
import simnet.snapshot;

static_assert(std::is_trivially_move_constructible_v<simnet::ServerReplicationMeasurement>);
static_assert(std::is_trivially_move_constructible_v<simnet::ClientReplicationMeasurement>);
static_assert(std::is_same_v<
              decltype(simnet::ServerReplicationMeasurement::encode_elapsed_time),
              simnet::Nanoseconds>);
static_assert(std::is_same_v<
              decltype(simnet::ClientReplicationMeasurement::sink_application_elapsed_time),
              simnet::Nanoseconds>);
static_assert(!std::is_copy_constructible_v<simnet::EvidenceCsvFile>);
static_assert(!std::is_copy_assignable_v<simnet::EvidenceCsvFile>);
static_assert(!std::is_move_constructible_v<simnet::EvidenceCsvFile>);
static_assert(!std::is_move_assignable_v<simnet::EvidenceCsvFile>);
static_assert(std::is_nothrow_destructible_v<simnet::EvidenceCsvFile>);
static_assert(!std::is_copy_constructible_v<simnet::ServerReplicationCsvWriter>);
static_assert(!std::is_copy_assignable_v<simnet::ServerReplicationCsvWriter>);
static_assert(!std::is_move_constructible_v<simnet::ServerReplicationCsvWriter>);
static_assert(!std::is_move_assignable_v<simnet::ServerReplicationCsvWriter>);
static_assert(std::is_nothrow_destructible_v<simnet::ServerReplicationCsvWriter>);
static_assert(!std::is_copy_constructible_v<simnet::ClientReplicationCsvWriter>);
static_assert(!std::is_copy_assignable_v<simnet::ClientReplicationCsvWriter>);
static_assert(!std::is_move_constructible_v<simnet::ClientReplicationCsvWriter>);
static_assert(!std::is_move_assignable_v<simnet::ClientReplicationCsvWriter>);
static_assert(std::is_nothrow_destructible_v<simnet::ClientReplicationCsvWriter>);

namespace
{
    class TemporaryDirectory
    {
      public:
        TemporaryDirectory()
        {
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ =
                std::filesystem::temp_directory_path() / ("simnet_tel003_" + std::to_string(stamp));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
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

    [[nodiscard]] std::vector<std::string> read_lines(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path};
        auto lines = std::vector<std::string>{};
        auto line = std::string{};
        while (std::getline(input, line))
        {
            lines.push_back(line);
        }
        return lines;
    }

    [[nodiscard]] std::vector<std::string> parse_csv_row(std::string_view text)
    {
        auto fields = std::vector<std::string>{};
        auto field = std::string{};
        auto quoted = false;
        for (auto index = std::size_t{}; index < text.size(); ++index)
        {
            auto const character = text[index];
            if (character == '"')
            {
                if (quoted && index + 1U < text.size() && text[index + 1U] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else
                {
                    quoted = !quoted;
                }
            }
            else if (character == ',' && !quoted)
            {
                fields.push_back(std::move(field));
                field.clear();
            }
            else
            {
                field.push_back(character);
            }
        }
        fields.push_back(std::move(field));
        return fields;
    }

    [[nodiscard]] std::string const& column_value(
        std::vector<std::string> const& header,
        std::vector<std::string> const& row,
        std::string_view column
    )
    {
        auto const found = std::ranges::find(header, column);
        REQUIRE(found != header.end());
        auto const index = static_cast<std::size_t>(std::distance(header.begin(), found));
        REQUIRE(index < row.size());
        return row[index];
    }
}

TEST_CASE("evidence run IDs are validated or honestly process local", "[telemetry][csv]")
{
    auto const supplied =
        simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Server, "Study_01-A");
    CHECK(supplied.run_id == "Study_01-A");
    CHECK(supplied.process_started_unix_ns > 0);

    auto const generated = simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Client);
    CHECK(generated.run_id == "client-" + std::to_string(generated.process_started_unix_ns));

    for (auto const invalid :
         {"", "-bad", "=formula", "comma,value", "quote\"value", "space value", "line\nvalue"})
    {
        CHECK_THROWS(
            simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Server, invalid)
        );
    }
    CHECK_THROWS(
        simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Server, std::string(65, 'a'))
    );
}

TEST_CASE(
    "enabled replication writers reject forged run contexts before output",
    "[telemetry][csv]"
)
{
    auto temporary = TemporaryDirectory{};
    auto const server_directory = temporary.path() / "forged_server";
    CHECK_THROWS(
        simnet::ServerReplicationCsvWriter({
            .enabled = true,
            .output_directory = server_directory,
            .run = {
                .run_id = "../unsafe",
                .process_role = simnet::EvidenceProcessRole::Server,
                .process_started_unix_ns = 90,
            },
        })
    );
    CHECK_FALSE(std::filesystem::exists(server_directory));

    auto const client_directory = temporary.path() / "forged_client";
    CHECK_THROWS(
        simnet::ClientReplicationCsvWriter({
            .enabled = true,
            .output_directory = client_directory,
            .run = {
                .run_id = "unsafe,run",
                .process_role = simnet::EvidenceProcessRole::Client,
                .process_started_unix_ns = 91,
            },
        })
    );
    CHECK_FALSE(std::filesystem::exists(client_directory));
}

TEST_CASE("evidence CSV flush and post-close failures are observable", "[telemetry][csv]")
{
    auto temporary = TemporaryDirectory{};
    auto const path = temporary.path() / "checked.csv";
    auto file = simnet::EvidenceCsvFile{path, "header"};
    REQUIRE(file.write_row("row"));
    REQUIRE(file.flush());
    CHECK(read_lines(path) == std::vector<std::string>{"header", "row"});
    REQUIRE(file.close());

    CHECK_FALSE(file.write_row("late"));
    CHECK_FALSE(file.healthy());
    CHECK(file.error().find("after close") != std::string_view::npos);
    CHECK_FALSE(file.flush());
    CHECK_FALSE(file.close());
}

TEST_CASE("Server replication CSV has complete peer-attributed v3 rows", "[telemetry][csv][peer]")
{
    CHECK(simnet::server_replication_csv_schema_version == 3U);
    auto temporary = TemporaryDirectory{};
    auto writer = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "paired-run",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 100,
        },
    }};
    CHECK(writer.path().filename() == "server_replication_v3_100.csv");

    auto first = simnet::ServerReplicationMeasurement{
        .peer_id = 3U,
        .accepted_gameplay_role = "player",
        .tick = 7,
        .sequence = 3,
        .baseline_sequence = 2,
        .acknowledged_sequence = 2,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ServerReplicationOutcome::Sent,
        .outcome_detail = "committed,with \"quoted\" detail",
        .source_entity_count = 10,
        .selected_entity_count = 8,
        .upsert_count = 6,
        .delete_count = 2,
        .representation_layout = "raw",
        .complete_record_bytes = 30,
        .encoded_update_bytes = 101,
        .application_payload_bytes = 101,
        .transport_payload_bytes = 101,
        .compression_elapsed_time = std::chrono::nanoseconds{10},
        .packet_group_id = 3,
        .packet_chunk_count = 2,
        .canonical_entity_count = 8,
        .canonical_fingerprint = 1234,
        .snapshot_extraction_elapsed_time = std::chrono::nanoseconds{11},
        .baseline_resolution_elapsed_time = std::chrono::nanoseconds{12},
        .encode_elapsed_time = std::chrono::nanoseconds{13},
        .transport_send_elapsed_time = std::chrono::nanoseconds{14},
        .snapshot_retention_elapsed_time = std::chrono::nanoseconds{15},
        .total_replication_elapsed_time = std::chrono::nanoseconds{65},
    };
    auto second = first;
    second.tick = 8;
    second.sequence = 4;
    second.outcome = simnet::ServerReplicationOutcome::Skipped;
    second.outcome_detail = "cadence_skip";
    REQUIRE(
        writer.submit(first, {.recorded_at_unix_ns = 110, .elapsed_since_process_start_ns = 9})
    );
    REQUIRE(
        writer.submit(second, {.recorded_at_unix_ns = 120, .elapsed_since_process_start_ns = 19})
    );
    CHECK(writer.submitted_count() == 2);
    CHECK(writer.buffered_count() == 2);
    REQUIRE(writer.drain());
    CHECK(writer.buffered_count() == 0);
    CHECK(read_lines(writer.path()).size() == 3);
    REQUIRE(writer.close());
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == simnet::server_replication_csv_header_v3);
    CHECK(lines[0].find("_cpu_ns") == std::string::npos);
    auto const header = parse_csv_row(lines[0]);
    auto const row1 = parse_csv_row(lines[1]);
    auto const row2 = parse_csv_row(lines[2]);
    CHECK(row1.size() == header.size());
    CHECK(row2.size() == header.size());
    CHECK(column_value(header, row1, "schema_version") == "3");
    CHECK(column_value(header, row1, "peer_id") == "3");
    CHECK(column_value(header, row1, "outcome") == "sent");
    CHECK(column_value(header, row1, "outcome_detail") == "committed,with \"quoted\" detail");
    CHECK(column_value(header, row1, "complete_record_bytes") == "30");
    CHECK(column_value(header, row1, "canonical_fingerprint") == "1234");
    CHECK(column_value(header, row1, "compression_elapsed_ns") == "10");
    CHECK(column_value(header, row1, "snapshot_extraction_elapsed_ns") == "11");
    CHECK(column_value(header, row1, "baseline_resolution_elapsed_ns") == "12");
    CHECK(column_value(header, row1, "encode_elapsed_ns") == "13");
    CHECK(column_value(header, row1, "transport_send_elapsed_ns") == "14");
    CHECK(column_value(header, row1, "snapshot_retention_elapsed_ns") == "15");
    CHECK(column_value(header, row1, "total_replication_elapsed_ns") == "65");
    CHECK(column_value(header, row1, "record_order") == "0");
    CHECK(column_value(header, row2, "record_order") == "1");
    CHECK(column_value(header, row2, "outcome") == "skipped");
    CHECK(column_value(header, row2, "outcome_detail") == "cadence_skip");
    CHECK(column_value(header, row1, "recorded_at_unix_ns") == "110");
    CHECK(column_value(header, row2, "recorded_at_unix_ns") == "120");
    CHECK(column_value(header, row1, "elapsed_since_process_start_ns") == "9");
    CHECK(column_value(header, row2, "elapsed_since_process_start_ns") == "19");
}

TEST_CASE(
    "Client replication CSV requires the accepted role and remains distinct",
    "[telemetry][csv]"
)
{
    CHECK(simnet::client_replication_csv_schema_version == 2U);
    auto temporary = TemporaryDirectory{};
    auto writer = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "paired-run",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 200,
        },
    }};
    CHECK(simnet::client_replication_csv_header_v2 != simnet::server_replication_csv_header_v3);
    CHECK(writer.path().filename() == "client_replication_v2_200.csv");

    auto const measurement = simnet::ClientReplicationMeasurement{
        .tick = 9,
        .sequence = 5,
        .baseline_sequence = 4,
        .acknowledged_sequence_before = 4,
        .received_sequence_after = 5,
        .acknowledged_sequence_after = 5,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ClientReplicationOutcome::Applied,
        .outcome_detail = "committed",
        .encoded_update_bytes = 88,
        .upsert_count = 5,
        .delete_count = 1,
        .reconstructed_entity_count = 12,
        .final_sink_entity_count = 12,
        .canonical_fingerprint = 5678,
        .packet_group_id = 5,
        .received_outer_bytes = 113,
        .group_chunk_count = 2,
        .received_packet_count = 1,
        .decompression_elapsed_time = std::chrono::nanoseconds{7},
        .decode_elapsed_time = std::chrono::nanoseconds{1},
        .baseline_resolution_elapsed_time = std::chrono::nanoseconds{2},
        .reconstruction_elapsed_time = std::chrono::nanoseconds{3},
        .sink_preparation_elapsed_time = std::chrono::nanoseconds{4},
        .sink_application_elapsed_time = std::chrono::nanoseconds{5},
        .canonical_snapshot_commit_elapsed_time = std::chrono::nanoseconds{6},
        .total_receive_to_applied_elapsed_time = std::chrono::nanoseconds{21},
    };
    REQUIRE(writer.submit(
        measurement,
        {.recorded_at_unix_ns = 220, .elapsed_since_process_start_ns = 20}
    ));
    CHECK_FALSE(writer.drain());
    CHECK_FALSE(writer.healthy());
    CHECK(writer.buffered_count() == 1);
    CHECK(writer.error().find("accepted gameplay role") != std::string_view::npos);
    CHECK_FALSE(writer.close());
    CHECK_FALSE(writer.close());
    CHECK(writer.error().find("accepted gameplay role") != std::string_view::npos);
    CHECK(writer.buffered_count() == 1);
    CHECK(read_lines(writer.path()).size() == 1);

    auto second_writer = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "second-run",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 201,
        },
    }};
    REQUIRE(second_writer.set_accepted_gameplay_role("player"));
    REQUIRE(second_writer.submit(
        measurement,
        {.recorded_at_unix_ns = 220, .elapsed_since_process_start_ns = 20}
    ));
    auto next_measurement = measurement;
    next_measurement.tick = 10;
    next_measurement.sequence = 6;
    next_measurement.outcome = simnet::ClientReplicationOutcome::PacketIncomplete;
    next_measurement.outcome_detail = "group_incomplete";
    REQUIRE(second_writer.submit(
        next_measurement,
        {.recorded_at_unix_ns = 230, .elapsed_since_process_start_ns = 30}
    ));
    REQUIRE(second_writer.close());
    auto const lines = read_lines(second_writer.path());
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == simnet::client_replication_csv_header_v2);
    CHECK(lines[0].find("_cpu_ns") == std::string::npos);
    auto const header = parse_csv_row(lines[0]);
    auto const row1 = parse_csv_row(lines[1]);
    auto const row2 = parse_csv_row(lines[2]);
    CHECK(row1.size() == header.size());
    CHECK(row2.size() == header.size());
    CHECK(column_value(header, row1, "schema_version") == "2");
    CHECK(column_value(header, row1, "accepted_gameplay_role") == "player");
    CHECK(column_value(header, row1, "outcome") == "applied");
    CHECK(column_value(header, row1, "received_outer_bytes") == "113");
    CHECK(column_value(header, row1, "canonical_fingerprint") == "5678");
    CHECK(column_value(header, row1, "decompression_elapsed_ns") == "7");
    CHECK(column_value(header, row1, "decode_elapsed_ns") == "1");
    CHECK(column_value(header, row1, "baseline_resolution_elapsed_ns") == "2");
    CHECK(column_value(header, row1, "reconstruction_elapsed_ns") == "3");
    CHECK(column_value(header, row1, "sink_preparation_elapsed_ns") == "4");
    CHECK(column_value(header, row1, "sink_application_elapsed_ns") == "5");
    CHECK(column_value(header, row1, "canonical_snapshot_commit_elapsed_ns") == "6");
    CHECK(column_value(header, row1, "total_receive_to_applied_elapsed_ns") == "21");
    CHECK(column_value(header, row1, "record_order") == "0");
    CHECK(column_value(header, row2, "record_order") == "1");
    CHECK(column_value(header, row2, "outcome") == "packet_incomplete");
    CHECK(column_value(header, row2, "outcome_detail") == "group_incomplete");
    CHECK(column_value(header, row1, "recorded_at_unix_ns") == "220");
    CHECK(column_value(header, row2, "recorded_at_unix_ns") == "230");
}

TEST_CASE("replication CSV disabling and failures are explicit", "[telemetry][csv]")
{
    auto temporary = TemporaryDirectory{};
    auto const disabled_directory = temporary.path() / "disabled";
    auto disabled = simnet::ServerReplicationCsvWriter{{
        .enabled = false,
        .output_directory = disabled_directory,
        .run = {
            .run_id = "disabled",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 300,
        },
    }};
    CHECK(disabled.submit({}));
    CHECK(disabled.close());
    CHECK(disabled.submit({}));
    CHECK(disabled.drain());
    CHECK(disabled.close());
    CHECK(disabled.healthy());
    CHECK_FALSE(std::filesystem::exists(disabled_directory));

    auto writer = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "overflow",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 301,
        },
    }};
    for (auto index = std::size_t{}; index < simnet::replication_csv_buffer_capacity; ++index)
    {
        REQUIRE(writer.submit(
            {},
            {.recorded_at_unix_ns = index, .elapsed_since_process_start_ns = index}
        ));
    }
    CHECK_FALSE(writer.submit({}));
    CHECK_FALSE(writer.healthy());
    CHECK(writer.error().find("overflow") != std::string_view::npos);
    CHECK_FALSE(writer.close());
    CHECK_FALSE(writer.close());
    CHECK(writer.buffered_count() == 0);
    CHECK(read_lines(writer.path()).size() == simnet::replication_csv_buffer_capacity + 1U);

    auto exclusive = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "exclusive",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 302,
        },
    }};
    REQUIRE(exclusive.close());
    CHECK_THROWS(
        simnet::ServerReplicationCsvWriter(
            simnet::ReplicationCsvWriterConfig{
                .enabled = true,
                .output_directory = temporary.path(),
                .run = {
                    .run_id = "collision",
                    .process_role = simnet::EvidenceProcessRole::Server,
                    .process_started_unix_ns = 302,
                },
            }
        )
    );
}

TEST_CASE(
    "replication writer terminal failures preserve state and truthful rows",
    "[telemetry][csv]"
)
{
    auto temporary = TemporaryDirectory{};
    auto changed_role = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "role-change",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 310,
        },
    }};
    REQUIRE(changed_role.set_accepted_gameplay_role("player"));
    REQUIRE(
        changed_role.submit({}, {.recorded_at_unix_ns = 311, .elapsed_since_process_start_ns = 1})
    );
    CHECK_FALSE(changed_role.set_accepted_gameplay_role("stationary_observer"));
    CHECK_FALSE(changed_role.healthy());
    CHECK_FALSE(changed_role.close());
    CHECK(changed_role.buffered_count() == 0);
    auto const role_lines = read_lines(changed_role.path());
    REQUIRE(role_lines.size() == 2);
    CHECK(role_lines[1].find(",player,") != std::string::npos);
    CHECK_FALSE(changed_role.close());

    auto submit_after_close = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "closed-submit",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 312,
        },
    }};
    REQUIRE(submit_after_close.close());
    CHECK_FALSE(submit_after_close.submit({}));
    CHECK_FALSE(submit_after_close.healthy());
    CHECK(submit_after_close.error().find("after close") != std::string_view::npos);
    CHECK_FALSE(submit_after_close.close());

    auto drain_after_close = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "closed-drain",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 313,
        },
    }};
    REQUIRE(drain_after_close.close());
    CHECK_FALSE(drain_after_close.drain());
    CHECK_FALSE(drain_after_close.healthy());
    CHECK(drain_after_close.error().find("after close") != std::string_view::npos);
    CHECK_FALSE(drain_after_close.close());
}

TEST_CASE("telemetry logging has explicit sink ownership", "[telemetry][logging]")
{
    simnet::shutdown_telemetry();
    CHECK_FALSE(simnet::log_enabled(simnet::LogLevel::Info));

    auto config = simnet::TelemetryConfig{
        .console_log_enabled = true,
        .file_log_enabled = false,
        .min_level = "warn",
    };
    simnet::initialize_telemetry(config);
    CHECK_FALSE(simnet::log_enabled(simnet::LogLevel::Info));
    CHECK(simnet::log_enabled(simnet::LogLevel::Warn));
    simnet::shutdown_telemetry();

    config.console_log_enabled = false;
    simnet::initialize_telemetry(config);
    CHECK_FALSE(simnet::log_enabled(simnet::LogLevel::Critical));
    CHECK_NOTHROW(
        simnet::log(
            simnet::LogCategory::Telemetry,
            simnet::LogLevel::Critical,
            "zero-sink logging is disabled"
        )
    );
    simnet::shutdown_telemetry();
    simnet::shutdown_telemetry();
    CHECK_FALSE(simnet::log_enabled(simnet::LogLevel::Critical));
}

TEST_CASE("Server replication measurements preserve byte ownership", "[telemetry][server]")
{
    auto measurements = simnet::ServerReplicationMeasurements{};
    auto const skipped = simnet::ServerReplicationMeasurement{
        .peer_id = 4U,
        .accepted_gameplay_role = "stationary_observer",
        .tick = 7,
        .outcome = simnet::ServerReplicationOutcome::Skipped,
        .source_entity_count = 4,
    };
    measurements.observe(skipped);

    CHECK(measurements.attempt_count == 1);
    CHECK(measurements.sent_count == 0);
    CHECK(measurements.latest_attempt->tick == 7);
    CHECK(measurements.latest_attempt->peer_id == 4U);
    CHECK(measurements.latest_attempt->accepted_gameplay_role == "stationary_observer");
    CHECK_FALSE(measurements.latest_sent.has_value());

    auto const sent = simnet::ServerReplicationMeasurement{
        .peer_id = 9U,
        .accepted_gameplay_role = "player",
        .tick = 8,
        .sequence = 3,
        .outcome = simnet::ServerReplicationOutcome::Sent,
        .source_entity_count = 4,
        .selected_entity_count = 3,
        .upsert_count = 3,
        .encoded_update_bytes = 96,
        .application_payload_bytes = 96,
        .transport_payload_bytes = 96,
    };
    measurements.observe(sent);

    REQUIRE(measurements.latest_sent.has_value());
    CHECK(measurements.attempt_count == 2);
    CHECK(measurements.sent_count == 1);
    CHECK(measurements.latest_sent->sequence == 3);
    CHECK(measurements.latest_sent->peer_id == 9U);
    CHECK(measurements.latest_sent->accepted_gameplay_role == "player");
    CHECK(measurements.latest_sent->selected_entity_count == 3);
    CHECK(measurements.latest_sent->encoded_update_bytes == 96);
    CHECK(measurements.latest_sent->application_payload_bytes == 96);
    CHECK(measurements.latest_sent->transport_payload_bytes == 96);
    CHECK(measurements.latest_sent->total_replication_elapsed_time == simnet::Nanoseconds{});
}

TEST_CASE("Client applied measurements exclude failed attempts", "[telemetry][client]")
{
    auto measurements = simnet::ClientReplicationMeasurements{};
    auto const failed = simnet::ClientReplicationMeasurement{
        .tick = 10,
        .sequence = 5,
        .outcome = simnet::ClientReplicationOutcome::SinkApplicationFailed,
        .encoded_update_bytes = 128,
        .upsert_count = 4,
        .reconstructed_entity_count = 4,
        .final_sink_entity_count = 0,
        .sink_application_elapsed_time = std::chrono::nanoseconds{2},
    };
    measurements.observe(failed);

    CHECK(measurements.attempt_count == 1);
    CHECK(measurements.applied_count == 0);
    CHECK_FALSE(measurements.latest_applied.has_value());
    CHECK(
        measurements.latest_attempt->total_receive_to_applied_elapsed_time == simnet::Nanoseconds{}
    );

    auto const applied = simnet::ClientReplicationMeasurement{
        .tick = 11,
        .sequence = 6,
        .baseline_sequence = 5,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ClientReplicationOutcome::Applied,
        .encoded_update_bytes = 160,
        .upsert_count = 3,
        .delete_count = 1,
        .reconstructed_entity_count = 6,
        .final_sink_entity_count = 6,
        .total_receive_to_applied_elapsed_time = std::chrono::nanoseconds{9},
    };
    measurements.observe(applied);

    REQUIRE(measurements.latest_applied.has_value());
    CHECK(measurements.attempt_count == 2);
    CHECK(measurements.applied_count == 1);
    CHECK(measurements.latest_applied->sequence == 6);
    CHECK(measurements.latest_applied->baseline_sequence == 5);
    CHECK(measurements.latest_applied->snapshot_kind == simnet::SnapshotKind::Patch);
    CHECK(measurements.latest_applied->upsert_count == 3);
    CHECK(measurements.latest_applied->delete_count == 1);
    CHECK(measurements.latest_applied->reconstructed_entity_count == 6);
    CHECK(measurements.latest_applied->final_sink_entity_count == 6);
    CHECK(measurements.latest_applied->total_receive_to_applied_elapsed_time.count() == 9);
}
