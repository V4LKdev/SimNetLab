module;
#include <fstream>
#include <string_view>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <spdlog/spdlog.h>

module telemetry;

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
                "state_hash,pos_drift,vel_drift,"
                "anisotropy_x,anisotropy_y,anisotropy_z\n";

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
                    << r.state_hash << ','
                    << r.pos_drift << ','
                    << r.vel_drift << ','
                    << r.anisotropy_x << ','
                    << r.anisotropy_y << ','
                    << r.anisotropy_z << '\n';
        }
    }
} // namespace telemetry::detail
