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
        bool emitted {};
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
                .emitted = encode_output.kind == simnet::EncodeResultKind::Packet,
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
        pipeline.codec = simnet::CodecKind::BitPacked;
        pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval
            | simnet::PipelineTechniqueFlags::Quantization
            | simnet::PipelineTechniqueFlags::OctHeading;
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
