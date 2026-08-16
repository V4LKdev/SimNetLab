#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("Server replication CSV preserves peer attribution byte ownership and timings", "[telemetry][csv][server]")
{
    CHECK(simnet::server_replication_csv_schema_version == 3U);

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

    auto const measurement = simnet::ServerReplicationMeasurement{
        .peer_id = 3U,
        .accepted_gameplay_role = "player",
        .tick = 7U,
        .sequence = 3U,
        .baseline_sequence = 2U,
        .acknowledged_sequence = 2U,
        .snapshot_kind = simnet::SnapshotKind::Patch,
        .outcome = simnet::ServerReplicationOutcome::Sent,
        .outcome_detail = "committed",
        .source_entity_count = 10U,
        .selected_entity_count = 8U,
        .upsert_count = 6U,
        .delete_count = 2U,
        .representation_layout = "raw",
        .complete_record_bytes = 30U,
        .encoded_update_bytes = 101U,
        .application_payload_bytes = 89U,
        .transport_payload_bytes = 113U,
        .compression_elapsed_time = std::chrono::nanoseconds{10},
        .packet_group_id = 3U,
        .packet_chunk_count = 2U,
        .canonical_entity_count = 8U,
        .canonical_fingerprint = 1234U,
        .snapshot_extraction_elapsed_time = std::chrono::nanoseconds{11},
        .baseline_resolution_elapsed_time = std::chrono::nanoseconds{12},
        .encode_elapsed_time = std::chrono::nanoseconds{13},
        .transport_send_elapsed_time = std::chrono::nanoseconds{14},
        .snapshot_retention_elapsed_time = std::chrono::nanoseconds{15},
        .total_replication_elapsed_time = std::chrono::nanoseconds{65},
    };

    REQUIRE(
        writer.submit(
            measurement,
            {.recorded_at_unix_ns = 110U, .elapsed_since_process_start_ns = 9U}
        )
    );
    REQUIRE(writer.close());

    auto const lines = read_lines(writer.path());
    REQUIRE(lines.size() == 2U);
    CHECK(lines.front() == simnet::server_replication_csv_header_v3);

    auto const header = parse_csv_row(lines[0]);
    auto const row = parse_csv_row(lines[1]);
    REQUIRE(row.size() == header.size());

    CHECK(column_value(header, row, "schema_version") == "3");
    CHECK(column_value(header, row, "run_id") == "paired-run");
    CHECK(column_value(header, row, "process_role") == "server");
    CHECK(column_value(header, row, "process_started_unix_ns") == "100");
    CHECK(column_value(header, row, "recorded_at_unix_ns") == "110");
    CHECK(column_value(header, row, "elapsed_since_process_start_ns") == "9");
    CHECK(column_value(header, row, "record_order") == "0");

    CHECK(column_value(header, row, "peer_id") == "3");
    CHECK(column_value(header, row, "accepted_gameplay_role") == "player");
    CHECK(column_value(header, row, "sequence") == "3");
    CHECK(column_value(header, row, "baseline_sequence") == "2");
    CHECK(column_value(header, row, "outcome") == "sent");

    CHECK(column_value(header, row, "encoded_update_bytes") == "101");
    CHECK(column_value(header, row, "application_payload_bytes") == "89");
    CHECK(column_value(header, row, "transport_payload_bytes") == "113");
    CHECK(column_value(header, row, "packet_chunk_count") == "2");
    CHECK(column_value(header, row, "canonical_fingerprint") == "1234");

    CHECK(column_value(header, row, "compression_elapsed_ns") == "10");
    CHECK(column_value(header, row, "snapshot_extraction_elapsed_ns") == "11");
    CHECK(column_value(header, row, "baseline_resolution_elapsed_ns") == "12");
    CHECK(column_value(header, row, "encode_elapsed_ns") == "13");
    CHECK(column_value(header, row, "transport_send_elapsed_ns") == "14");
    CHECK(column_value(header, row, "snapshot_retention_elapsed_ns") == "15");
    CHECK(column_value(header, row, "total_replication_elapsed_ns") == "65");
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
