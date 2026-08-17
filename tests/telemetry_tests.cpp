#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test_temporary_directory.hpp"

import simnet.telemetry;
import simnet.snapshot;

namespace
{
    using TestTemporaryDirectory = simnet::test::TestTemporaryDirectory;

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

TEST_CASE("evidence run context preserves the shared run identity", "[telemetry][evidence]")
{
    auto const server =
        simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Server, "study-01");
    auto const client =
        simnet::make_evidence_run_context(simnet::EvidenceProcessRole::Client, "study-01");

    CHECK(server.run_id == "study-01");
    CHECK(client.run_id == server.run_id);
    CHECK(server.process_role == simnet::EvidenceProcessRole::Server);
    CHECK(client.process_role == simnet::EvidenceProcessRole::Client);
    CHECK(server.process_started_unix_ns > 0U);
    CHECK(client.process_started_unix_ns > 0U);
}

TEST_CASE("Server replication CSV has the exact ordered v4 schema", "[telemetry][csv][server]")
{
    CHECK(simnet::server_replication_csv_schema_version == 4U);
    CHECK(
        simnet::server_replication_csv_header_v4 ==
        "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
        "elapsed_since_process_start_ns,record_order,runtime_config_fingerprint,"
        "network_compatibility_fingerprint,application_wire_fingerprint,peer_id,"
        "accepted_gameplay_role,tick,sequence,baseline_sequence,acknowledged_sequence,"
        "snapshot_kind,outcome,outcome_detail,source_entity_count,selected_entity_count,"
        "upsert_count,delete_count,canonical_entity_count,canonical_fingerprint,"
        "aoi_candidate_entity_count,lod_near_population,lod_medium_population,"
        "lod_far_population,lod_near_scheduled,lod_medium_scheduled,lod_far_scheduled,"
        "lod_pending_due_count,lod_transition_count,lod_forced_immediate_count,"
        "delta_unchanged_entity_count,delta_spawned_entity_count,"
        "delta_whole_record_entity_count,delta_field_mask_entity_count,"
        "delta_classification_field_count,delta_position_field_count,"
        "delta_heading_field_count,delta_hue_field_count,"
        "delta_complete_record_equivalent_bytes,delta_encoded_record_bytes,"
        "representation_sample_count,position_error_world_units_sum,"
        "position_error_world_units_max,heading_error_degrees_sum,heading_error_degrees_max,"
        "encoded_update_bytes,compression_encoding,compression_raw_fallback,"
        "compression_input_bytes,compression_output_bytes,compression_elapsed_ns,"
        "packet_header_bytes,transport_accepted_bytes,transport_accepted_packet_count,"
        "recovery_reason,recovery_forced_upsert_count,recovery_forced_delete_count,"
        "repeated_without_ack_upsert_count,repeated_without_ack_delete_count,"
        "submissions_since_ack_progress,encode_elapsed_ns,transport_submission_elapsed_ns,"
        "total_replication_elapsed_ns"
    );
    CHECK(parse_csv_row(simnet::server_replication_csv_header_v4).size() == 68U);

    auto temporary = TestTemporaryDirectory{"simnet_telemetry"};
    auto writer = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "paired-run",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 100U,
        },
    }};
    CHECK(writer.path().filename() == "server_replication_v4_100.csv");

    auto const measurement = simnet::ServerReplicationMeasurement{
        .runtime_config_fingerprint = 8U,
        .network_compatibility_fingerprint = 9U,
        .application_wire_fingerprint = 10U,
        .peer_id = 11U,
        .accepted_gameplay_role = "player,\"alpha\"",
        .tick = 13U,
        .sequence = 14U,
        .baseline_sequence = 15U,
        .acknowledged_sequence = 16U,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ServerReplicationOutcome::Sent,
        .outcome_detail = "committed,\"ok\"",
        .source_entity_count = 20U,
        .selected_entity_count = 21U,
        .upsert_count = 22U,
        .delete_count = 23U,
        .canonical_entity_count = 24U,
        .canonical_fingerprint = 25U,
        .aoi_candidate_entity_count = 26U,
        .lod_near_population = 27U,
        .lod_medium_population = 28U,
        .lod_far_population = 29U,
        .lod_near_scheduled = 30U,
        .lod_medium_scheduled = 31U,
        .lod_far_scheduled = 32U,
        .lod_pending_due_count = 33U,
        .lod_transition_count = 34U,
        .lod_forced_immediate_count = 35U,
        .delta_unchanged_entity_count = 36U,
        .delta_spawned_entity_count = 37U,
        .delta_whole_record_entity_count = 38U,
        .delta_field_mask_entity_count = 39U,
        .delta_classification_field_count = 40U,
        .delta_position_field_count = 41U,
        .delta_heading_field_count = 42U,
        .delta_hue_field_count = 43U,
        .delta_complete_record_equivalent_bytes = 44U,
        .delta_encoded_record_bytes = 45U,
        .representation_sample_count = 46U,
        .position_error_world_units_sum = 47.0,
        .position_error_world_units_max = 48.0,
        .heading_error_degrees_sum = 49.0,
        .heading_error_degrees_max = 50.0,
        .encoded_update_bytes = 51U,
        .compression_encoding = "mixed",
        .compression_raw_fallback = true,
        .compression_input_bytes = 54U,
        .compression_output_bytes = 55U,
        .compression_elapsed_time = std::chrono::nanoseconds{56},
        .packet_header_bytes = 57U,
        .transport_accepted_bytes = 58U,
        .transport_accepted_packet_count = 59U,
        .recovery_reason = "ack_stalled",
        .recovery_forced_upsert_count = 61U,
        .recovery_forced_delete_count = 62U,
        .repeated_without_ack_upsert_count = 63U,
        .repeated_without_ack_delete_count = 64U,
        .submissions_since_ack_progress = 65U,
        .encode_elapsed_time = std::chrono::nanoseconds{66},
        .transport_submission_elapsed_time = std::chrono::nanoseconds{67},
        .total_replication_elapsed_time = std::chrono::nanoseconds{68},
    };

    REQUIRE(
        writer.submit(
            measurement,
            {.recorded_at_unix_ns = 110U, .elapsed_since_process_start_ns = 9U}
        )
    );
    REQUIRE(writer.close());
    REQUIRE(writer.close());
    CHECK(writer.healthy());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 2U);
    CHECK(lines.front() == simnet::server_replication_csv_header_v4);
    CHECK(
        lines[1] ==
        "4,paired-run,server,100,110,9,0,8,9,10,11,\"player,\"\"alpha\"\"\",13,14,15,16,"
        "patch,sent,\"committed,\"\"ok\"\"\",20,21,22,23,24,25,26,27,28,29,30,31,32,33,"
        "34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,mixed,1,54,55,56,57,"
        "58,59,ack_stalled,61,62,63,64,65,66,67,68"
    );

    auto const header = parse_csv_row(lines[0]);
    auto const row = parse_csv_row(lines[1]);
    REQUIRE(header.size() == 68U);
    REQUIRE(row.size() == header.size());

    CHECK(column_value(header, row, "schema_version") == "4");
    CHECK(column_value(header, row, "run_id") == "paired-run");
    CHECK(column_value(header, row, "process_role") == "server");
    CHECK(column_value(header, row, "process_started_unix_ns") == "100");
    CHECK(column_value(header, row, "recorded_at_unix_ns") == "110");
    CHECK(column_value(header, row, "elapsed_since_process_start_ns") == "9");
    CHECK(column_value(header, row, "record_order") == "0");

    CHECK(column_value(header, row, "peer_id") == "11");
    CHECK(column_value(header, row, "accepted_gameplay_role") == "player,\"alpha\"");
    CHECK(column_value(header, row, "sequence") == "14");
    CHECK(column_value(header, row, "baseline_sequence") == "15");
    CHECK(column_value(header, row, "outcome") == "sent");
    CHECK(column_value(header, row, "outcome_detail") == "committed,\"ok\"");

    CHECK(column_value(header, row, "encoded_update_bytes") == "51");
    CHECK(column_value(header, row, "compression_raw_fallback") == "1");
    CHECK(column_value(header, row, "compression_elapsed_ns") == "56");
    CHECK(column_value(header, row, "transport_accepted_bytes") == "58");
    CHECK(column_value(header, row, "transport_accepted_packet_count") == "59");
    CHECK(column_value(header, row, "encode_elapsed_ns") == "66");
    CHECK(column_value(header, row, "transport_submission_elapsed_ns") == "67");
    CHECK(column_value(header, row, "total_replication_elapsed_ns") == "68");
}

TEST_CASE("Server v4 preserves ordinary compression evidence outcomes", "[telemetry][csv][server]")
{
    struct CompressionEvidenceCase
    {
        std::string_view detail;
        std::string_view encoding;
        bool raw_fallback;
    };
    constexpr auto cases = std::array{
        CompressionEvidenceCase{"compression_disabled", "disabled", false},
        CompressionEvidenceCase{"whole_update_zstd", "zstd", false},
        CompressionEvidenceCase{"whole_update_raw", "raw", true},
        CompressionEvidenceCase{"per_packet_all_zstd", "zstd", false},
        CompressionEvidenceCase{"per_packet_mixed", "mixed", true},
        CompressionEvidenceCase{"per_packet_all_raw", "raw", true},
    };

    auto temporary = TestTemporaryDirectory{"simnet_server_compression_evidence"};
    auto writer = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "compression-evidence",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 200U,
        },
    }};

    for (auto index = std::size_t{}; index < cases.size(); ++index)
    {
        REQUIRE(writer.submit({
            .sequence = static_cast<std::uint32_t>(index + 1U),
            .outcome = simnet::ServerReplicationOutcome::Sent,
            .outcome_detail = cases[index].detail,
            .compression_encoding = cases[index].encoding,
            .compression_raw_fallback = cases[index].raw_fallback,
        }));
    }
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == cases.size() + 1U);
    auto const header = parse_csv_row(lines.front());
    for (auto index = std::size_t{}; index < cases.size(); ++index)
    {
        auto const row = parse_csv_row(lines[index + 1U]);
        REQUIRE(row.size() == 68U);
        CHECK(column_value(header, row, "outcome_detail") == cases[index].detail);
        CHECK(column_value(header, row, "compression_encoding") == cases[index].encoding);
        CHECK(
            column_value(header, row, "compression_raw_fallback") ==
            (cases[index].raw_fallback ? "1" : "0")
        );
    }
}

TEST_CASE("Server replication CSV drains a large evidence batch", "[telemetry][csv][server]")
{
    auto temporary = TestTemporaryDirectory{"simnet_server_evidence_batch"};
    auto writer = simnet::ServerReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "server-batch",
            .process_role = simnet::EvidenceProcessRole::Server,
            .process_started_unix_ns = 300U,
        },
    }};

    for (auto sequence = std::uint32_t{1}; sequence <= 300U; ++sequence)
    {
        auto detail = std::string{"temporary-server-detail-"} + std::to_string(sequence);
        REQUIRE(writer.submit({
            .sequence = sequence,
            .outcome = simnet::ServerReplicationOutcome::Skipped,
            .outcome_detail = detail,
        }));
    }

    REQUIRE(writer.submitted_count() == 300U);
    REQUIRE(writer.close());
    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 301U);
    auto const header = parse_csv_row(lines.front());
    auto const final_row = parse_csv_row(lines.back());
    CHECK(column_value(header, final_row, "sequence") == "300");
    CHECK(column_value(header, final_row, "outcome_detail") == "temporary-server-detail-300");
}

TEST_CASE("Client replication CSV preserves application outcome bytes and timings", "[telemetry][csv][client]")
{
    CHECK(simnet::client_replication_csv_schema_version == 3U);

    auto temporary = TestTemporaryDirectory{"simnet_telemetry"};
    auto writer = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "paired-run",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 200U,
        },
    }};

    REQUIRE(writer.set_accepted_gameplay_role("player"));

    auto const measurement = simnet::ClientReplicationMeasurement{
        .tick = 9U,
        .sequence = 5U,
        .baseline_sequence = 4U,
        .acknowledged_sequence_before = 4U,
        .received_sequence_after = 5U,
        .acknowledged_sequence_after = 5U,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ClientReplicationOutcome::Applied,
        .outcome_detail = "committed",
        .encoded_update_bytes = 88U,
        .upsert_count = 5U,
        .delete_count = 1U,
        .reconstructed_entity_count = 12U,
        .final_sink_entity_count = 12U,
        .canonical_fingerprint = 5678U,
        .packet_group_id = 5U,
        .received_outer_bytes = 113U,
        .group_chunk_count = 2U,
        .received_packet_count = 1U,
        .decompression_elapsed_time = std::chrono::nanoseconds{7},
        .decode_elapsed_time = std::chrono::nanoseconds{1},
        .baseline_resolution_elapsed_time = std::chrono::nanoseconds{2},
        .reconstruction_elapsed_time = std::chrono::nanoseconds{3},
        .sink_preparation_elapsed_time = std::chrono::nanoseconds{4},
        .sink_application_elapsed_time = std::chrono::nanoseconds{5},
        .canonical_snapshot_commit_elapsed_time = std::chrono::nanoseconds{6},
        .decode_to_applied_elapsed_time = std::chrono::nanoseconds{21},
    };

    REQUIRE(
        writer.submit(
            measurement,
            {.recorded_at_unix_ns = 220U, .elapsed_since_process_start_ns = 20U}
        )
    );
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 2U);
    CHECK(lines.front() == simnet::client_replication_csv_header_v3);

    auto const header = parse_csv_row(lines[0]);
    auto const row = parse_csv_row(lines[1]);
    REQUIRE(row.size() == header.size());

    CHECK(column_value(header, row, "schema_version") == "3");
    CHECK(column_value(header, row, "run_id") == "paired-run");
    CHECK(column_value(header, row, "process_role") == "client");
    CHECK(column_value(header, row, "process_started_unix_ns") == "200");
    CHECK(column_value(header, row, "recorded_at_unix_ns") == "220");
    CHECK(column_value(header, row, "elapsed_since_process_start_ns") == "20");
    CHECK(column_value(header, row, "record_order") == "0");

    CHECK(column_value(header, row, "accepted_gameplay_role") == "player");
    CHECK(column_value(header, row, "sequence") == "5");
    CHECK(column_value(header, row, "baseline_sequence") == "4");
    CHECK(column_value(header, row, "outcome") == "applied");

    CHECK(column_value(header, row, "encoded_update_bytes") == "88");
    CHECK(column_value(header, row, "received_outer_bytes") == "113");
    CHECK(column_value(header, row, "group_chunk_count") == "2");
    CHECK(column_value(header, row, "canonical_fingerprint") == "5678");

    CHECK(column_value(header, row, "decompression_elapsed_ns") == "7");
    CHECK(column_value(header, row, "decode_elapsed_ns") == "1");
    CHECK(column_value(header, row, "baseline_resolution_elapsed_ns") == "2");
    CHECK(column_value(header, row, "reconstruction_elapsed_ns") == "3");
    CHECK(column_value(header, row, "sink_preparation_elapsed_ns") == "4");
    CHECK(column_value(header, row, "sink_application_elapsed_ns") == "5");
    CHECK(column_value(header, row, "canonical_snapshot_commit_elapsed_ns") == "6");
    CHECK(column_value(header, row, "decode_to_applied_elapsed_ns") == "21");
}

TEST_CASE(
    "Client replication CSV owns buffered text after producer strings expire",
    "[telemetry][csv][client][regression]"
)
{
    auto temporary = TestTemporaryDirectory{"simnet_telemetry"};
    auto writer = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "buffered-text-lifetime",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 300U,
        },
    }};
    REQUIRE(writer.set_accepted_gameplay_role("stationary_observer"));

    {
        auto outcome_detail = std::string{"temporary-applied-detail"};
        auto compression_mode = std::string{"temporary-zstd-mode"};
        auto decompression_encoding = std::string{"temporary-zstd-frame"};
        auto decompression_result = std::string{"temporary-decompression-success"};
        auto const measurement = simnet::ClientReplicationMeasurement{
            .sequence = 1U,
            .outcome = simnet::ClientReplicationOutcome::Applied,
            .outcome_detail = outcome_detail,
            .compression_mode = compression_mode,
            .decompression_encoding = decompression_encoding,
            .decompression_result = decompression_result,
        };
        REQUIRE(writer.submit(measurement));
    }

    REQUIRE(writer.buffered_count() == 1U);
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 2U);
    auto const header = parse_csv_row(lines[0]);
    auto const row = parse_csv_row(lines[1]);
    REQUIRE(row.size() == header.size());
    CHECK(column_value(header, row, "outcome_detail") == "temporary-applied-detail");
    CHECK(column_value(header, row, "compression_mode") == "temporary-zstd-mode");
    CHECK(column_value(header, row, "decompression_encoding") == "temporary-zstd-frame");
    CHECK(
        column_value(header, row, "decompression_result") ==
        "temporary-decompression-success"
    );
}

TEST_CASE(
    "Client replication CSV drains during a large receive batch",
    "[telemetry][csv][client][regression]"
)
{
    auto temporary = TestTemporaryDirectory{"simnet_telemetry"};
    auto writer = simnet::ClientReplicationCsvWriter{{
        .enabled = true,
        .output_directory = temporary.path(),
        .run = {
            .run_id = "large-receive-batch",
            .process_role = simnet::EvidenceProcessRole::Client,
            .process_started_unix_ns = 400U,
        },
    }};
    REQUIRE(writer.set_accepted_gameplay_role("stationary_observer"));

    for (auto sequence = std::uint32_t{1}; sequence <= 300U; ++sequence)
    {
        auto detail = std::string{"temporary-batch-detail-"} + std::to_string(sequence);
        REQUIRE(writer.submit({
            .sequence = sequence,
            .outcome = simnet::ClientReplicationOutcome::PacketIncomplete,
            .outcome_detail = detail,
        }));
    }

    REQUIRE(writer.submitted_count() == 300U);
    REQUIRE(writer.close());
    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 301U);
    auto const header = parse_csv_row(lines.front());
    auto const final_row = parse_csv_row(lines.back());
    CHECK(column_value(header, final_row, "sequence") == "300");
    CHECK(column_value(header, final_row, "outcome_detail") == "temporary-batch-detail-300");
}
