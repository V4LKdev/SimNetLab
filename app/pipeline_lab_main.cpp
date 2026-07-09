#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;
import simnet.synthetic;
import simnet.telemetry;

namespace
{
    struct LabScenario
    {
        std::string name;
        simnet::PipelineDefinition pipeline;
        simnet::SyntheticSnapshotSettings synthetic;
        simnet::Tick ticks {};
    };

    struct LabTickReport
    {
        std::string scenario;
        simnet::Tick tick {};
        simnet::SequenceId baseline_sequence {};
        bool emitted {};
        bool delta {};
        simnet::EncodeSkipReason skip_reason { simnet::EncodeSkipReason::None };
        simnet::SnapshotKind kind { simnet::SnapshotKind::Patch };
        std::uint32_t input_entities {};
        std::uint32_t selected_entities {};
        std::uint32_t upserts {};
        std::uint32_t deletes {};
        std::uint32_t packet_bytes {};
        bool budget_exceeded {};
        bool decode_valid {};
        std::string error;
    };

    struct LabScenarioSummary
    {
        std::string scenario;
        simnet::Tick ticks {};
        std::uint32_t packets {};
        std::uint32_t skips {};
        std::uint32_t input_entities {};
        std::uint64_t total_packet_bytes {};
        std::uint32_t average_packet_bytes {};
        std::uint32_t max_packet_bytes {};
        std::uint32_t budget_exceeded_count {};
        std::uint32_t decode_failures {};
    };

    [[nodiscard]] std::string snapshot_kind_name(simnet::SnapshotKind kind)
    {
        return kind == simnet::SnapshotKind::FullReplace ? "FullReplace" : "Patch";
    }

    [[nodiscard]] std::string skip_reason_name(simnet::EncodeSkipReason reason)
    {
        return reason == simnet::EncodeSkipReason::SendInterval ? "SendInterval" : "None";
    }

    void add_field(simnet::MetricRecord& record, std::string name, simnet::MetricValue value)
    {
        record.fields.push_back({ .name = std::move(name), .value = std::move(value) });
    }

    [[nodiscard]] simnet::MetricRecord make_tick_record(LabTickReport const& report)
    {
        auto record = simnet::MetricRecord {
            .stream = "pipeline_lab_tick",
            .tick = report.tick,
            .fields = {},
        };
        add_field(record, "scenario", report.scenario);
        add_field(record, "result", report.emitted ? std::string { "packet" } : std::string { "skipped" });
        add_field(record, "baseline_sequence", static_cast<std::uint64_t>(report.baseline_sequence));
        add_field(record, "delta", report.delta);
        add_field(record, "skip_reason", skip_reason_name(report.skip_reason));
        add_field(record, "kind", report.emitted ? snapshot_kind_name(report.kind) : std::string { "None" });
        add_field(record, "input_entities", static_cast<std::uint64_t>(report.input_entities));
        add_field(record, "selected_entities", static_cast<std::uint64_t>(report.selected_entities));
        add_field(record, "upserts", static_cast<std::uint64_t>(report.upserts));
        add_field(record, "deletes", static_cast<std::uint64_t>(report.deletes));
        add_field(record, "packet_bytes", static_cast<std::uint64_t>(report.packet_bytes));
        add_field(record, "budget_exceeded", report.budget_exceeded);
        add_field(record, "decode_valid", report.decode_valid);
        if (!report.error.empty()) {
            add_field(record, "error", report.error);
        }
        return record;
    }

    [[nodiscard]] simnet::MetricRecord make_summary_record(LabScenarioSummary const& summary)
    {
        auto record = simnet::MetricRecord {
            .stream = "pipeline_lab_summary",
            .tick = 0,
            .fields = {},
        };
        add_field(record, "scenario", summary.scenario);
        add_field(record, "ticks", static_cast<std::uint64_t>(summary.ticks));
        add_field(record, "packets", static_cast<std::uint64_t>(summary.packets));
        add_field(record, "skips", static_cast<std::uint64_t>(summary.skips));
        add_field(record, "input_entities", static_cast<std::uint64_t>(summary.input_entities));
        add_field(record, "total_packet_bytes", summary.total_packet_bytes);
        add_field(record, "average_packet_bytes", static_cast<std::uint64_t>(summary.average_packet_bytes));
        add_field(record, "max_packet_bytes", static_cast<std::uint64_t>(summary.max_packet_bytes));
        add_field(record, "budget_exceeded_count", static_cast<std::uint64_t>(summary.budget_exceeded_count));
        add_field(record, "decode_failures", static_cast<std::uint64_t>(summary.decode_failures));
        return record;
    }

    void print_metric_record(simnet::MetricRecord const& record)
    {
        std::cout << simnet::format_metric_record_key_value(record) << '\n';
    }

    void publish_metric_record(simnet::MetricRecord record)
    {
        simnet::submit_metric_record(record);
        print_metric_record(record);
    }

    [[nodiscard]] LabScenarioSummary run_scenario(LabScenario const& scenario)
    {
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline lab scenario start: " + scenario.name);

        auto encode_state = simnet::ClientReplicationState {};
        auto decode_state = simnet::ClientReplicationState {};
        auto scratch = simnet::PipelineScratch {};
        auto summary = LabScenarioSummary {
            .scenario = scenario.name,
            .ticks = scenario.ticks,
            .input_entities = scenario.synthetic.entity_count,
        };

        for (simnet::Tick tick = 0; tick < scenario.ticks; ++tick) {
            auto const snapshot = simnet::make_synthetic_world_snapshot(scenario.synthetic, tick);
            auto encode_output = simnet::encode_snapshot(
                scenario.pipeline,
                encode_state,
                scratch,
                { .snapshot = &snapshot }
            );

            auto report = LabTickReport {
                .scenario = scenario.name,
                .tick = tick,
                .baseline_sequence = encode_output.report.baseline_sequence,
                .emitted = encode_output.kind == simnet::EncodeResultKind::Packet,
                .delta = encode_output.report.delta,
                .skip_reason = encode_output.skip_reason,
                .input_entities = encode_output.report.input_entities,
                .selected_entities = encode_output.report.selected_entities,
                .upserts = encode_output.report.upsert_count,
                .deletes = encode_output.report.delete_count,
                .packet_bytes = encode_output.report.packet_bytes,
                .budget_exceeded = encode_output.report.budget_exceeded,
                .error = {},
            };

            if (!report.emitted) {
                ++summary.skips;
                publish_metric_record(make_tick_record(report));
                continue;
            }

            ++summary.packets;
            summary.total_packet_bytes += report.packet_bytes;
            summary.max_packet_bytes = std::max(summary.max_packet_bytes, report.packet_bytes);
            if (report.budget_exceeded) {
                ++summary.budget_exceeded_count;
            }

            auto decode_output = simnet::decode_packet(
                scenario.pipeline,
                decode_state,
                scratch,
                { .bytes = encode_output.packet.bytes }
            );
            report.decode_valid = decode_output.report.valid;
            if (decode_output.report.valid) {
                report.kind = decode_output.patch.kind;
                report.upserts = static_cast<std::uint32_t>(decode_output.patch.upserts.size());
                report.deletes = static_cast<std::uint32_t>(decode_output.patch.deletes.size());
                auto const validation = simnet::validate_client_snapshot_patch(decode_output.patch);
                if (!validation.valid) {
                    report.decode_valid = false;
                    report.error = validation.message;
                }
            } else {
                report.error = decode_output.report.error;
            }

            if (!report.decode_valid) {
                ++summary.decode_failures;
                simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Error,
                    "pipeline lab decode failure: " + scenario.name + " tick " + std::to_string(tick)
                        + " " + report.error);
            }

            publish_metric_record(make_tick_record(report));
        }

        if (summary.packets != 0U) {
            summary.average_packet_bytes = static_cast<std::uint32_t>(
                summary.total_packet_bytes / summary.packets
            );
        }

        publish_metric_record(make_summary_record(summary));
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline lab scenario complete: " + scenario.name);
        return summary;
    }

    [[nodiscard]] LabScenario make_quantized_bitpacked_scenario(simnet::SharedConfig const& config)
    {
        auto pipeline = simnet::make_raw_snapshot_pipeline();
        pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval
            | simnet::PipelineTechniqueFlags::Quantization
            | simnet::PipelineTechniqueFlags::OctHeading
            | simnet::PipelineTechniqueFlags::BitPacking;
        pipeline.send_interval.interval_ticks = 2;
        pipeline.quantization.position_bounds = simnet::make_centered_bounds(config.simulation.world_half);

        return {
            .name = "quantized_bitpacked",
            .pipeline = pipeline,
            .synthetic = {
                .seed = config.run.seed,
                .entity_count = 1000,
                .bounds = simnet::make_centered_bounds(config.simulation.world_half),
                .pattern = simnet::SyntheticPattern::RandomUniform,
            },
            .ticks = 3,
        };
    }

    [[nodiscard]] LabScenario make_incremental_scenario(simnet::SharedConfig const& config)
    {
        auto pipeline = simnet::make_raw_snapshot_pipeline();
        pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval
            | simnet::PipelineTechniqueFlags::Incremental;
        pipeline.send_interval.interval_ticks = 2;
        pipeline.incremental.max_entities_per_packet = 4;

        return {
            .name = "incremental",
            .pipeline = pipeline,
            .synthetic = {
                .seed = config.run.seed,
                .entity_count = 10,
                .bounds = simnet::make_centered_bounds(config.simulation.world_half),
                .pattern = simnet::SyntheticPattern::RandomUniform,
            },
            .ticks = 5,
        };
    }

    [[nodiscard]] simnet::WorldSnapshot make_delta_snapshot(
        simnet::Tick tick,
        std::vector<simnet::BoidState> const& boids
    )
    {
        auto snapshot = simnet::WorldSnapshot {};
        snapshot.tick = tick;
        snapshot.reserve(boids.size());
        for (auto const& boid : boids) {
            snapshot.ids.push_back(boid.id);
            snapshot.positions.push_back(boid.position);
            snapshot.headings.push_back(boid.heading);
            snapshot.hues.push_back(boid.hue);
        }
        return snapshot;
    }

    [[nodiscard]] bool run_delta_scenario()
    {
        auto pipeline = simnet::make_raw_snapshot_pipeline();
        pipeline.techniques |= simnet::PipelineTechniqueFlags::Delta;

        auto const baseline = make_delta_snapshot(0, {
            { .id = 1, .position = { 1.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 10 },
            { .id = 2, .position = { 2.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 20 },
            { .id = 3, .position = { 3.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 30 },
        });
        auto const current = make_delta_snapshot(1, {
            { .id = 1, .position = { 1.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 10 },
            { .id = 2, .position = { 20.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 20 },
            { .id = 4, .position = { 4.0F, 0.0F, 0.0F }, .heading = { 1.0F, 0.0F, 0.0F }, .hue = 40 },
        });

        auto encode_state = simnet::ClientReplicationState {};
        auto decode_state = simnet::ClientReplicationState {};
        auto scratch = simnet::PipelineScratch {};
        auto full = simnet::encode_snapshot(
            pipeline,
            encode_state,
            scratch,
            { .snapshot = &baseline }
        );
        auto full_decoded = simnet::decode_packet(
            pipeline,
            decode_state,
            scratch,
            { .bytes = full.packet.bytes }
        );

        auto delta = simnet::encode_snapshot(
            pipeline,
            encode_state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &baseline,
                .baseline_sequence = full.packet.sequence,
            }
        );
        auto const sequence_before_mismatch = decode_state.latest_remote_sequence;
        auto wrong_baseline = simnet::decode_packet(
            pipeline,
            decode_state,
            scratch,
            {
                .bytes = delta.packet.bytes,
                .applied_baseline_sequence = 0,
            }
        );
        auto const wrong_baseline_preserved_state =
            decode_state.latest_remote_sequence == sequence_before_mismatch
            && wrong_baseline.patch.upserts.empty()
            && wrong_baseline.patch.deletes.empty();
        auto delta_decoded = simnet::decode_packet(
            pipeline,
            decode_state,
            scratch,
            {
                .bytes = delta.packet.bytes,
                .applied_baseline_sequence = full.packet.sequence,
            }
        );

        auto bitpacked_pipeline = pipeline;
        bitpacked_pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization
            | simnet::PipelineTechniqueFlags::OctHeading
            | simnet::PipelineTechniqueFlags::BitPacking;
        bitpacked_pipeline.quantization.position_bounds = simnet::make_centered_bounds(100.0F);
        auto bitpacked_encode_state = simnet::ClientReplicationState {};
        auto bitpacked_decode_state = simnet::ClientReplicationState {};
        auto bitpacked_full = simnet::encode_snapshot(
            bitpacked_pipeline,
            bitpacked_encode_state,
            scratch,
            { .snapshot = &baseline }
        );
        auto bitpacked_full_decoded = simnet::decode_packet(
            bitpacked_pipeline,
            bitpacked_decode_state,
            scratch,
            { .bytes = bitpacked_full.packet.bytes }
        );
        auto bitpacked_delta = simnet::encode_snapshot(
            bitpacked_pipeline,
            bitpacked_encode_state,
            scratch,
            {
                .snapshot = &current,
                .baseline_snapshot = &baseline,
                .baseline_sequence = bitpacked_full.packet.sequence,
            }
        );
        auto bitpacked_delta_decoded = simnet::decode_packet(
            bitpacked_pipeline,
            bitpacked_decode_state,
            scratch,
            {
                .bytes = bitpacked_delta.packet.bytes,
                .applied_baseline_sequence = bitpacked_full.packet.sequence,
            }
        );
        auto const bitpacked_delta_valid = bitpacked_full_decoded.report.valid
            && bitpacked_delta_decoded.report.valid
            && bitpacked_delta.report.upsert_count == 2
            && bitpacked_delta.report.delete_count == 1
            && bitpacked_delta_decoded.patch.deletes == std::vector<simnet::EntityNetId> { 3 };

        auto const signature_changes = simnet::pipeline_decode_signature(pipeline)
            != simnet::pipeline_decode_signature(simnet::make_raw_snapshot_pipeline());
        auto const valid = signature_changes
            && bitpacked_delta_valid
            && full.kind == simnet::EncodeResultKind::Packet
            && full.report.snapshot_kind == simnet::SnapshotKind::FullReplace
            && full.report.baseline_sequence == 0
            && full_decoded.report.valid
            && delta.kind == simnet::EncodeResultKind::Packet
            && delta.report.delta
            && delta.report.snapshot_kind == simnet::SnapshotKind::Patch
            && delta.report.baseline_sequence == full.packet.sequence
            && delta.report.upsert_count == 2
            && delta.report.delete_count == 1
            && !wrong_baseline.report.valid
            && wrong_baseline_preserved_state
            && decode_state.latest_remote_sequence == delta.packet.sequence
            && sequence_before_mismatch == full.packet.sequence
            && delta_decoded.report.valid
            && delta_decoded.patch.upserts.size() == 2
            && delta_decoded.patch.upserts[0].id == 2
            && delta_decoded.patch.upserts[1].id == 4
            && delta_decoded.patch.deletes == std::vector<simnet::EntityNetId> { 3 };

        publish_metric_record(make_tick_record({
            .scenario = "delta",
            .tick = baseline.tick,
            .baseline_sequence = full.report.baseline_sequence,
            .emitted = true,
            .delta = full.report.delta,
            .kind = full.report.snapshot_kind,
            .input_entities = full.report.input_entities,
            .selected_entities = full.report.selected_entities,
            .upserts = full.report.upsert_count,
            .deletes = full.report.delete_count,
            .packet_bytes = full.report.packet_bytes,
            .budget_exceeded = full.report.budget_exceeded,
            .decode_valid = full_decoded.report.valid,
            .error = {},
        }));
        publish_metric_record(make_tick_record({
            .scenario = "delta",
            .tick = current.tick,
            .baseline_sequence = delta.report.baseline_sequence,
            .emitted = true,
            .delta = delta.report.delta,
            .kind = delta.report.snapshot_kind,
            .input_entities = delta.report.input_entities,
            .selected_entities = delta.report.selected_entities,
            .upserts = delta.report.upsert_count,
            .deletes = delta.report.delete_count,
            .packet_bytes = delta.report.packet_bytes,
            .budget_exceeded = delta.report.budget_exceeded,
            .decode_valid = delta_decoded.report.valid,
            .error = valid ? std::string {} : std::string { "delta scenario invariant failed" },
        }));
        return valid;
    }
}

int main()
{
    bool telemetry_started = false;
    try {
        SIMNET_TRACE_SCOPE("PipelineLab main");

        auto const shared_config = simnet::load_shared_config(simnet::default_shared_config_path());
        auto telemetry_config = simnet::default_server_config().telemetry;
        telemetry_config.console_log_enabled = false;

        simnet::initialize_telemetry(telemetry_config);
        telemetry_started = true;
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info, "pipeline lab start");

        auto const scenarios = std::vector<LabScenario> {
            make_quantized_bitpacked_scenario(shared_config),
            make_incremental_scenario(shared_config),
        };

        auto failed = false;
        for (auto const& scenario : scenarios) {
            auto const summary = run_scenario(scenario);
            failed = failed || summary.decode_failures != 0U;
        }
        failed = failed || !run_delta_scenario();

        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            failed ? "pipeline lab failed" : "pipeline lab complete");
        SIMNET_TRACE_FRAME("pipeline_lab");
        simnet::flush_telemetry();
        simnet::shutdown_telemetry();
        return failed ? 1 : 0;
    } catch (std::exception const& error) {
        std::cerr << "PipelineLab failed: " << error.what() << '\n';
        if (telemetry_started) {
            simnet::shutdown_telemetry();
        }
        return 1;
    }
}
