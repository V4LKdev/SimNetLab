module;
#include <fstream>
#include <string_view>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <spdlog/spdlog.h>

module simnet.telemetry;

namespace telemetry::detail {
    void write_csv(std::string_view filepath,
                   std::span<const TickRecord> records,
                   std::string_view run_tag,
                   std::string_view node_id,
                   std::string_view run_id,
                   const std::vector<std::pair<std::string, std::string> > &parameters)
    {
        std::ofstream out{std::string(filepath)};
        if (!out) {
            spdlog::error("telemetry: cannot open CSV file '{}'", filepath);
            return;
        }

        // metadata
        if (!node_id.empty()) out << "# node_id " << node_id << '\n';
        if (!run_id.empty()) out << "# run_id " << run_id << '\n';
        if (!run_tag.empty()) out << "# run_tag " << run_tag << '\n';
        for (const auto &[k, v]: parameters)
            out << "# param " << k << '=' << v << '\n';

        // header
        out << "tick,frame_wall_us,entity_count,sim_time_us,serialization_us,"
                "compression_us,net_send_us,net_recv_us,"
                "bytes_sent,bytes_recv,packets_sent,packets_lost,"
                "snapshots_sent,snapshots_acked,snapshots_lost,"
                "uncompressed_bytes,compressed_bytes,"
                "pipeline_input_rows,pipeline_selected_rows,pipeline_output_rows,"
                "pipeline_canonical_bytes,pipeline_encoded_bytes,"
                "pipeline_stage_failures,pipeline_decode_failures,"
                "pipeline_baseline_hits,pipeline_baseline_misses,pipeline_stale_baselines,"
                "state_hash,pos_drift,vel_drift,"
                "anisotropy_x,anisotropy_y,anisotropy_z,"
                "neighbor_cache_edges,neighbor_cache_checks,"
                "spatial_grid_cells,spatial_grid_occupied_cells,"
                "flock_avg_speed,flock_avg_steer,flock_polarization\n";

        for (const auto &r: records) {
            out << r.tick << ','
                    << r.frame_wall_us << ','
                    << r.entity_count << ','
                    << r.sim_time_us << ','
                    << r.serialization_us << ','
                    << r.compression_us << ','
                    << r.net_send_us << ','
                    << r.net_recv_us << ','
                    << r.bytes_sent << ','
                    << r.bytes_recv << ','
                    << r.packets_sent << ','
                    << r.packets_lost << ','
                    << r.snapshots_sent << ','
                    << r.snapshots_acked << ','
                    << r.snapshots_lost << ','
                    << r.uncompressed_bytes << ','
                    << r.compressed_bytes << ','
                    << r.pipeline_input_rows << ','
                    << r.pipeline_selected_rows << ','
                    << r.pipeline_output_rows << ','
                    << r.pipeline_canonical_bytes << ','
                    << r.pipeline_encoded_bytes << ','
                    << r.pipeline_stage_failures << ','
                    << r.pipeline_decode_failures << ','
                    << r.pipeline_baseline_hits << ','
                    << r.pipeline_baseline_misses << ','
                    << r.pipeline_stale_baselines << ','
                    << r.state_hash << ','
                    << r.pos_drift << ','
                    << r.vel_drift << ','
                    << r.anisotropy_x << ','
                    << r.anisotropy_y << ','
                    << r.anisotropy_z << ','
                    << r.neighbor_cache_edges << ','
                    << r.neighbor_cache_checks << ','
                    << r.spatial_grid_cells << ','
                    << r.spatial_grid_occupied_cells << ','
                    << r.flock_avg_speed << ','
                    << r.flock_avg_steer << ','
                    << r.flock_polarization << '\n';
        }
    }
}
