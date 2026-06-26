module;
#include <fstream>
#include <string_view>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

module telemetry;

namespace telemetry::detail {
    static void add_tick_record(nlohmann::json &j, const TickRecord &r)
    {
        j = {
            {"tick", r.tick},
            {"frame_wall_us", r.frame_wall_us},
            {"entity_count", r.entity_count},
            {"sim_time_us", r.sim_time_us},
            {"serialization_us", r.serialization_us},
            {"compression_us", r.compression_us},
            {"net_send_us", r.net_send_us},
            {"net_recv_us", r.net_recv_us},
            {"bytes_sent", r.bytes_sent},
            {"bytes_recv", r.bytes_recv},
            {"packets_sent", r.packets_sent},
            {"packets_lost", r.packets_lost},
            {"snapshots_sent", r.snapshots_sent},
            {"snapshots_acked", r.snapshots_acked},
            {"snapshots_lost", r.snapshots_lost},
            {"uncompressed_bytes", r.uncompressed_bytes},
            {"compressed_bytes", r.compressed_bytes},
            {"state_hash", r.state_hash},
            {"pos_drift", r.pos_drift},
            {"vel_drift", r.vel_drift},
            {"anisotropy_x", r.anisotropy_x},
            {"anisotropy_y", r.anisotropy_y},
            {"anisotropy_z", r.anisotropy_z}
        };
    }

    void write_json(std::string_view filepath,
                    std::span<const TickRecord> records,
                    std::string_view run_tag,
                    std::string_view node_id,
                    std::string_view run_id,
                    const std::vector<std::pair<std::string, std::string> > &parameters)
    {
        nlohmann::json root;
        root["node_id"] = node_id;
        root["run_id"] = run_id;
        root["run_tag"] = run_tag;

        if (!parameters.empty()) {
            nlohmann::json params = nlohmann::json::object();
            for (const auto &[k, v]: parameters)
                params[k] = v;
            root["parameters"] = std::move(params);
        }

        nlohmann::json ticks = nlohmann::json::array();
        for (const auto &r: records) {
            nlohmann::json entry;
            add_tick_record(entry, r);
            ticks.push_back(std::move(entry));
        }
        root["ticks"] = std::move(ticks);

        std::ofstream out{std::string(filepath)};
        if (!out) {
            spdlog::error("telemetry: cannot open JSON file '{}'", filepath);
            return;
        }
        out << root.dump(2);
    }
} // namespace telemetry::detail
