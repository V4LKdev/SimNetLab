/// \file   config_loader.cpp
/// \brief  Strict sectioned JSON loading / saving for SimConfig.

module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

module simnet.utils;

namespace simnet::utils {
    namespace {
        template<std::size_t N>
        void reject_unknown_keys(const nlohmann::json &object,
                                 const char *scope,
                                 const std::array<std::string_view, N> &allowed)
        {
            for (const auto &[key, value]: object.items()) {
                (void) value;
                if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
                    throw std::runtime_error(std::string("unknown config key ") + scope + "." + key);
                }
            }
        }

        [[nodiscard]] const nlohmann::json &section(const nlohmann::json &root, const char *name)
        {
            static const nlohmann::json empty = nlohmann::json::object();
            if (!root.contains(name)) {
                return empty;
            }
            if (!root.at(name).is_object()) {
                throw std::runtime_error(std::string("config section must be an object: ") + name);
            }
            return root.at(name);
        }

        [[nodiscard]] int read_int(const nlohmann::json &object, const char *key, int current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_number_integer()) {
                throw std::runtime_error(std::string("expected integer for ") + key);
            }
            const auto parsed = value.get<long long>();
            if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
                throw std::runtime_error(std::string("integer out of range for ") + key);
            }
            return static_cast<int>(parsed);
        }

        [[nodiscard]] std::uint32_t read_u32(const nlohmann::json &object, const char *key, std::uint32_t current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_number_unsigned()) {
                throw std::runtime_error(std::string("expected unsigned integer for ") + key);
            }
            const auto parsed = value.get<unsigned long long>();
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(std::string("unsigned integer out of range for ") + key);
            }
            return static_cast<std::uint32_t>(parsed);
        }

        [[nodiscard]] std::uint64_t read_u64(const nlohmann::json &object, const char *key, std::uint64_t current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_number_unsigned()) {
                throw std::runtime_error(std::string("expected unsigned integer for ") + key);
            }
            return value.get<std::uint64_t>();
        }

        [[nodiscard]] float read_float(const nlohmann::json &object, const char *key, float current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_number()) {
                throw std::runtime_error(std::string("expected number for ") + key);
            }
            return value.get<float>();
        }

        [[nodiscard]] bool read_bool(const nlohmann::json &object, const char *key, bool current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_boolean()) {
                throw std::runtime_error(std::string("expected boolean for ") + key);
            }
            return value.get<bool>();
        }

        [[nodiscard]] std::string read_string(const nlohmann::json &object,
                                              const char *key,
                                              const std::string &current)
        {
            if (!object.contains(key)) {
                return current;
            }
            const auto &value = object.at(key);
            if (!value.is_string()) {
                throw std::runtime_error(std::string("expected string for ") + key);
            }
            return value.get<std::string>();
        }

        [[nodiscard]] bool finite_nonnegative(float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        [[nodiscard]] bool finite_positive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] std::string validation_error(const char *field, const char *requirement)
        {
            std::ostringstream out;
            out << field << " " << requirement;
            return out.str();
        }

        [[nodiscard]] bool require_nonnegative(float value,
                                               const char *field,
                                               std::string &message)
        {
            if (finite_nonnegative(value)) {
                return true;
            }
            message = validation_error(field, "must be finite and greater than or equal to zero");
            return false;
        }

        [[nodiscard]] bool require_positive(float value,
                                            const char *field,
                                            std::string &message)
        {
            if (finite_positive(value)) {
                return true;
            }
            message = validation_error(field, "must be finite and greater than zero");
            return false;
        }

        void parse_config(const nlohmann::json &j, SimConfig &cfg)
        {
            if (!j.is_object()) {
                throw std::runtime_error("config root must be a JSON object");
            }

            reject_unknown_keys(j, "root", std::array<std::string_view, 6>{
                                    "run", "simulation", "population", "server", "techniques", "telemetry"
                                });

            const auto &run = section(j, "run");
            const auto &simulation = section(j, "simulation");
            const auto &population = section(j, "population");
            const auto &server = section(j, "server");
            const auto &techniques = section(j, "techniques");
            const auto &telemetry = section(j, "telemetry");

            reject_unknown_keys(run, "run", std::array<std::string_view, 1>{
                                    "seed"
                                });
            reject_unknown_keys(simulation, "simulation", std::array<std::string_view, 23>{
                                    "world_half",
                                    "body_radius",
                                    "max_speed",
                                    "max_accel_frac",
                                    "separation_strength",
                                    "local_alignment_strength",
                                    "aggregate_alignment_strength",
                                    "aggregate_cohesion_strength",
                                    "target_exact_neighbors",
                                    "separation_radius",
                                    "local_alignment_radius",
                                    "cohesion_radius",
                                    "hue_radius",
                                    "exact_cell_size",
                                    "aggregate_cell_size",
                                    "fov_cos",
                                    "min_distance",
                                    "enable_alignment",
                                    "enable_cohesion",
                                    "enable_separation",
                                    "enable_hue_adaptation",
                                    "hue_adaptation_rate",
                                    "aggregate_weight_cap"
                                });
            reject_unknown_keys(population, "population", std::array<std::string_view, 1>{
                                    "target_boid_count"
                                });
            reject_unknown_keys(server, "server", std::array<std::string_view, 4>{
                                    "sim_hz", "max_sim_steps", "multithreaded_ecs", "ecs_thread_count"
                                });
            reject_unknown_keys(techniques, "techniques", std::array<std::string_view, 4>{
                                    "enable_aoi", "enable_delta", "enable_quantization", "enable_compression"
                                });
            reject_unknown_keys(telemetry, "telemetry", std::array<std::string_view, 2>{
                                    "statistics_interval_ticks", "log_level"
                                });

            cfg.seed = read_u64(run, "seed", cfg.seed);

            cfg.world_half = read_float(simulation, "world_half", cfg.world_half);
            cfg.body_radius = read_float(simulation, "body_radius", cfg.body_radius);
            cfg.max_speed = read_float(simulation, "max_speed", cfg.max_speed);
            cfg.max_accel_frac = read_float(simulation, "max_accel_frac", cfg.max_accel_frac);
            cfg.separation_strength = read_float(simulation, "separation_strength", cfg.separation_strength);
            cfg.local_alignment_strength = read_float(simulation, "local_alignment_strength",
                                                      cfg.local_alignment_strength);
            cfg.aggregate_alignment_strength = read_float(simulation, "aggregate_alignment_strength",
                                                          cfg.aggregate_alignment_strength);
            cfg.aggregate_cohesion_strength = read_float(simulation, "aggregate_cohesion_strength",
                                                         cfg.aggregate_cohesion_strength);
            cfg.target_exact_neighbors = read_u32(simulation, "target_exact_neighbors", cfg.target_exact_neighbors);
            cfg.separation_radius = read_float(simulation, "separation_radius", cfg.separation_radius);
            cfg.local_alignment_radius = read_float(simulation, "local_alignment_radius", cfg.local_alignment_radius);
            cfg.cohesion_radius = read_float(simulation, "cohesion_radius", cfg.cohesion_radius);
            cfg.hue_radius = read_float(simulation, "hue_radius", cfg.hue_radius);
            cfg.exact_cell_size = read_float(simulation, "exact_cell_size", cfg.exact_cell_size);
            cfg.aggregate_cell_size = read_float(simulation, "aggregate_cell_size", cfg.aggregate_cell_size);
            cfg.fov_cos = read_float(simulation, "fov_cos", cfg.fov_cos);
            cfg.min_distance = read_float(simulation, "min_distance", cfg.min_distance);
            cfg.enable_alignment = read_bool(simulation, "enable_alignment", cfg.enable_alignment);
            cfg.enable_cohesion = read_bool(simulation, "enable_cohesion", cfg.enable_cohesion);
            cfg.enable_separation = read_bool(simulation, "enable_separation", cfg.enable_separation);
            cfg.enable_hue_adaptation = read_bool(simulation, "enable_hue_adaptation", cfg.enable_hue_adaptation);
            cfg.hue_adaptation_rate = read_float(simulation, "hue_adaptation_rate", cfg.hue_adaptation_rate);
            cfg.aggregate_weight_cap = read_u32(simulation, "aggregate_weight_cap", cfg.aggregate_weight_cap);

            cfg.max_boids = read_int(population, "target_boid_count", cfg.max_boids);

            cfg.sim_hz = read_int(server, "sim_hz", cfg.sim_hz);
            cfg.max_sim_steps = read_int(server, "max_sim_steps", cfg.max_sim_steps);
            cfg.multithreaded_ecs = read_bool(server, "multithreaded_ecs", cfg.multithreaded_ecs);
            cfg.ecs_thread_count = read_int(server, "ecs_thread_count", cfg.ecs_thread_count);

            cfg.enable_aoi = read_bool(techniques, "enable_aoi", cfg.enable_aoi);
            cfg.enable_delta = read_bool(techniques, "enable_delta", cfg.enable_delta);
            cfg.enable_quantization = read_bool(techniques, "enable_quantization", cfg.enable_quantization);
            cfg.enable_compression = read_bool(techniques, "enable_compression", cfg.enable_compression);

            cfg.statistics_interval_ticks = read_int(telemetry, "statistics_interval_ticks",
                                                     cfg.statistics_interval_ticks);
            cfg.log_level = read_string(telemetry, "log_level", cfg.log_level);
        }
    }

    bool validate_config(const SimConfig &cfg, std::string &message)
    {
        if (cfg.sim_hz <= 0) {
            message = validation_error("server.sim_hz", "must be greater than zero");
            return false;
        }
        if (cfg.max_sim_steps <= 0) {
            message = validation_error("server.max_sim_steps", "must be greater than zero");
            return false;
        }
        if (cfg.max_boids < 0) {
            message = validation_error("population.target_boid_count", "must be greater than or equal to zero");
            return false;
        }
        if (!require_positive(cfg.world_half, "simulation.world_half", message)) return false;
        if (!require_positive(cfg.body_radius, "simulation.body_radius", message)) return false;
        if (!require_nonnegative(cfg.max_speed, "simulation.max_speed", message)) return false;
        if (!require_positive(cfg.max_accel_frac, "simulation.max_accel_frac", message)) return false;
        if (!require_nonnegative(cfg.separation_strength, "simulation.separation_strength", message)) return false;
        if (!require_nonnegative(cfg.local_alignment_strength, "simulation.local_alignment_strength", message)) return
                false;
        if (!require_nonnegative(cfg.aggregate_alignment_strength, "simulation.aggregate_alignment_strength", message))
            return false;
        if (!require_nonnegative(cfg.aggregate_cohesion_strength, "simulation.aggregate_cohesion_strength", message))
            return false;
        if (!require_nonnegative(cfg.separation_radius, "simulation.separation_radius", message)) return false;
        if (!require_nonnegative(cfg.local_alignment_radius, "simulation.local_alignment_radius", message)) return
                false;
        if (!require_nonnegative(cfg.cohesion_radius, "simulation.cohesion_radius", message)) return false;
        if (!require_nonnegative(cfg.hue_radius, "simulation.hue_radius", message)) return false;
        if (!require_nonnegative(cfg.exact_cell_size, "simulation.exact_cell_size", message)) return false;
        if (!require_nonnegative(cfg.aggregate_cell_size, "simulation.aggregate_cell_size", message)) return false;
        if (!std::isfinite(cfg.fov_cos) || cfg.fov_cos < -1.0f || cfg.fov_cos > 1.0f) {
            message = validation_error("simulation.fov_cos", "must be finite and in [-1, 1]");
            return false;
        }
        if (!require_nonnegative(cfg.min_distance, "simulation.min_distance", message)) return false;
        if (!require_nonnegative(cfg.hue_adaptation_rate, "simulation.hue_adaptation_rate", message)) return false;
        if (cfg.target_exact_neighbors == 0) {
            message = validation_error("simulation.target_exact_neighbors", "must be greater than zero");
            return false;
        }
        if (cfg.aggregate_weight_cap == 0) {
            message = validation_error("simulation.aggregate_weight_cap", "must be greater than zero");
            return false;
        }
        if (cfg.ecs_thread_count < 0) {
            message = validation_error("server.ecs_thread_count", "must be greater than or equal to zero");
            return false;
        }
        if (cfg.statistics_interval_ticks <= 0) {
            message = validation_error("telemetry.statistics_interval_ticks", "must be greater than zero");
            return false;
        }
        if (cfg.log_level != "trace" &&
            cfg.log_level != "debug" &&
            cfg.log_level != "info" &&
            cfg.log_level != "warn" &&
            cfg.log_level != "error" &&
            cfg.log_level != "critical" &&
            cfg.log_level != "off") {
            message = validation_error("telemetry.log_level",
                                       "must be one of trace, debug, info, warn, error, critical, off");
            return false;
        }

        message.clear();
        return true;
    }

    ConfigLoadResult load_config_json(const std::string &path, const SimConfig &defaults)
    {
        ConfigLoadResult result;
        result.config = defaults;

        std::ifstream in(path);
        if (!in.is_open()) {
            result.status = ConfigLoadStatus::missing;
            result.message = "config file missing: " + path;
            return result;
        }

        SimConfig parsed = defaults;
        try {
            nlohmann::json j;
            in >> j;
            parse_config(j, parsed);
        } catch (const std::exception &e) {
            result.status = ConfigLoadStatus::parse_error;
            result.message = std::string("config parse_error: ") + e.what();
            return result;
        }

        std::string validation_message;
        if (!validate_config(parsed, validation_message)) {
            result.status = ConfigLoadStatus::validation_error;
            result.message = "config validation_error: " + validation_message;
            return result;
        }

        result.status = ConfigLoadStatus::loaded;
        result.config = parsed;
        result.config_fingerprint = fingerprint(parsed);
        result.message = "config loaded: " + path;
        return result;
    }

    void save_json(const std::string &path, const SimConfig &cfg)
    {
        nlohmann::json j;
        j["run"] = {
            {"seed", cfg.seed}
        };
        j["simulation"] = {
            {"world_half", cfg.world_half},
            {"body_radius", cfg.body_radius},
            {"max_speed", cfg.max_speed},
            {"max_accel_frac", cfg.max_accel_frac},
            {"separation_strength", cfg.separation_strength},
            {"local_alignment_strength", cfg.local_alignment_strength},
            {"aggregate_alignment_strength", cfg.aggregate_alignment_strength},
            {"aggregate_cohesion_strength", cfg.aggregate_cohesion_strength},
            {"target_exact_neighbors", cfg.target_exact_neighbors},
            {"separation_radius", cfg.separation_radius},
            {"local_alignment_radius", cfg.local_alignment_radius},
            {"cohesion_radius", cfg.cohesion_radius},
            {"hue_radius", cfg.hue_radius},
            {"exact_cell_size", cfg.exact_cell_size},
            {"aggregate_cell_size", cfg.aggregate_cell_size},
            {"fov_cos", cfg.fov_cos},
            {"min_distance", cfg.min_distance},
            {"enable_alignment", cfg.enable_alignment},
            {"enable_cohesion", cfg.enable_cohesion},
            {"enable_separation", cfg.enable_separation},
            {"enable_hue_adaptation", cfg.enable_hue_adaptation},
            {"hue_adaptation_rate", cfg.hue_adaptation_rate},
            {"aggregate_weight_cap", cfg.aggregate_weight_cap}
        };
        j["population"] = {
            {"target_boid_count", cfg.max_boids}
        };
        j["server"] = {
            {"sim_hz", cfg.sim_hz},
            {"max_sim_steps", cfg.max_sim_steps},
            {"multithreaded_ecs", cfg.multithreaded_ecs},
            {"ecs_thread_count", cfg.ecs_thread_count}
        };
        j["techniques"] = {
            {"enable_aoi", cfg.enable_aoi},
            {"enable_delta", cfg.enable_delta},
            {"enable_quantization", cfg.enable_quantization},
            {"enable_compression", cfg.enable_compression}
        };
        j["telemetry"] = {
            {"statistics_interval_ticks", cfg.statistics_interval_ticks},
            {"log_level", cfg.log_level}
        };

        std::ofstream out(path);
        out << std::setw(2) << j << '\n';
    }
}
