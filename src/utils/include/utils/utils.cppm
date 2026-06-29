/// \file   utils.cppm
/// \brief  Primary module interface for simnet utilities.
///
/// \details
/// Provides Vec3 math, SimConfig, timing, identifiers, and path helpers.

module;
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <limits>
#include <filesystem>

export module simnet.utils;

export namespace simnet::utils {
    /// Shared scalar 3D vector for boundary math
    struct Vec3 {
    public:
        float data[3] = {0.0f, 0.0f, 0.0f};

        /// Returns the x component.
        [[nodiscard]] constexpr float x() const noexcept { return data[0]; }
        /// Returns the mutable x component.
        constexpr float &x() noexcept { return data[0]; }
        /// Returns the y component.
        [[nodiscard]] constexpr float y() const noexcept { return data[1]; }
        /// Returns the mutable y component.
        constexpr float &y() noexcept { return data[1]; }
        /// Returns the z component.
        [[nodiscard]] constexpr float z() const noexcept { return data[2]; }
        /// Returns the mutable z component.
        constexpr float &z() noexcept { return data[2]; }

        /// Creates a zero vector.
        constexpr Vec3() noexcept = default;

        /// Creates a vector from components.
        constexpr Vec3(float x, float y, float z) noexcept : data{x, y, z}
        {
        }

        /// Returns the zero vector.
        static constexpr Vec3 zero() noexcept { return {}; }
        /// Returns the default forward direction used by boids.
        static constexpr Vec3 forward() noexcept { return {1.0f, 0.0f, 0.0f}; }

        /// Returns a component by index without bounds checks.
        constexpr float operator[](int i) const noexcept { return data[i]; }
        /// Returns a mutable component by index without bounds checks.
        constexpr float &operator[](int i) noexcept { return data[i]; }

        constexpr Vec3 operator+(const Vec3 &o) const noexcept { return {x() + o.x(), y() + o.y(), z() + o.z()}; }
        constexpr Vec3 operator-(const Vec3 &o) const noexcept { return {x() - o.x(), y() - o.y(), z() - o.z()}; }
        constexpr Vec3 operator*(float s) const noexcept { return {x() * s, y() * s, z() * s}; }
        constexpr Vec3 operator/(float s) const noexcept { return {x() / s, y() / s, z() / s}; }
        constexpr Vec3 operator-() const noexcept { return {-x(), -y(), -z()}; }
        friend constexpr Vec3 operator*(float s, const Vec3 &v) noexcept { return v * s; }

        constexpr Vec3 &operator+=(const Vec3 &o) noexcept
        {
            data[0] += o.data[0];
            data[1] += o.data[1];
            data[2] += o.data[2];
            return *this;
        }

        constexpr Vec3 &operator-=(const Vec3 &o) noexcept
        {
            data[0] -= o.data[0];
            data[1] -= o.data[1];
            data[2] -= o.data[2];
            return *this;
        }

        constexpr Vec3 &operator*=(float s) noexcept
        {
            data[0] *= s;
            data[1] *= s;
            data[2] *= s;
            return *this;
        }

        constexpr Vec3 &operator/=(float s) noexcept
        {
            data[0] /= s;
            data[1] /= s;
            data[2] /= s;
            return *this;
        }

        /// Returns squared vector length.
        [[nodiscard]] constexpr float length_sq() const noexcept { return x() * x() + y() * y() + z() * z(); }

        /// Returns vector length.
        [[nodiscard]] float length() const noexcept { return std::sqrt(length_sq()); }

        /// Returns distance to another vector.
        [[nodiscard]] float dist(const Vec3 &v) const noexcept { return (*this - v).length(); }

        /// Returns squared distance to another vector.
        [[nodiscard]] constexpr float dist_sq(const Vec3 &v) const noexcept
        {
            const float dx = v.x() - x(), dy = v.y() - y(), dz = v.z() - z();
            return dx * dx + dy * dy + dz * dz;
        }

        /// Returns a unit vector, or zero if this vector has no usable length.
        [[nodiscard]] Vec3 normalized() const noexcept
        {
            const float l2 = length_sq();
            if (l2 == 1.0f) return *this;
            const float l = std::sqrt(l2);
            return (l > 0.0f) ? *this / l : Vec3{};
        }

        /// Returns dot product with another vector.
        [[nodiscard]] constexpr float dot(const Vec3 &v) const noexcept
        {
            return x() * v.x() + y() * v.y() + z() * v.z();
        }

        /// Returns cross product with another vector.
        [[nodiscard]] constexpr Vec3 cross(const Vec3 &v) const noexcept
        {
            return {
                y() * v.z() - z() * v.y(),
                z() * v.x() - x() * v.z(),
                x() * v.y() - y() * v.x()
            };
        }

        constexpr bool operator==(const Vec3 &o) const noexcept = default;
    };

    static_assert(sizeof(Vec3) == 12);
    static_assert(alignof(Vec3) == 4);

    // --- Config ---

    /// Complete deterministic run contract loaded from sectioned JSON.
    struct SimConfig {
    public:
        /// Returns the built-in development defaults.
        static SimConfig default_config() noexcept { return {}; }

        /// Fixed RNG seed for deterministic spawn state and repeatable benchmark runs.
        uint64_t seed = 12345;

        /// Fixed simulation rate in authoritative ticks per second.
        int sim_hz = 30;
        /// Maximum fixed ticks processed per outer frame when the app falls behind.
        int max_sim_steps = 5;

        /// Enables Flecs worker threads and matching flock work shards.
        bool multithreaded_ecs = false;
        /// Requested worker/shard count. zero means derive from hardware.
        int ecs_thread_count = 4;

        /// Enables exact local alignment rule.
        bool enable_alignment = true;
        /// Enables aggregate-grid cohesion rule.
        bool enable_cohesion = true;
        /// Enables exact local separation rule.
        bool enable_separation = true;
        /// Enables aggregate-grid hue adaptation.
        bool enable_hue_adaptation = true;

        /// Half extent of the cubic simulation world.
        float world_half = 200.0f;
        /// Target active boid count; JSON field is under population.
        int max_boids = 1000;
        /// Physical boid radius used by containment and derived neighbor ranges.
        float body_radius = 0.15f;

        /// Maximum boid speed in world units per second.
        float max_speed = 12.0f;
        /// Acceleration limit as a fraction of max_speed.
        float max_accel_frac = 2.0f;

        /// Steering weight for exact local separation.
        float separation_strength = 1.5f;
        /// Steering weight for exact local alignment.
        float local_alignment_strength = 0.7f;
        /// Steering weight for aggregate-grid alignment.
        float aggregate_alignment_strength = 0.35f;
        /// Steering weight for aggregate-grid cohesion.
        float aggregate_cohesion_strength = 0.25f;
        /// Target expected neighbor count used to derive exact radius when radii are zero.
        std::uint32_t target_exact_neighbors = 16;

        /// Separation radius. zero derives from body radius and exact radius.
        float separation_radius = 0.0f;
        /// Local alignment radius. zero derives from exact radius.
        float local_alignment_radius = 0.0f;
        /// Aggregate cohesion radius. zero derives from exact radius.
        float cohesion_radius = 0.0f;
        /// Aggregate hue radius. zero derives from exact radius.
        float hue_radius = 0.0f;

        /// Exact neighbor grid cell size. zero derives from exact radius.
        float exact_cell_size = 0.0f;
        /// Aggregate grid cell size. zero derives from exact/cohesion radii.
        float aggregate_cell_size = 0.0f;

        /// Cosine field-of-view threshold for local alignment.
        float fov_cos = -0.25f;
        /// Squared-distance guard threshold source for exact local rules.
        float min_distance = 1e-4f;

        /// Per-tick hue blend factor multiplier.
        float hue_adaptation_rate = 0.65f;
        /// Maximum aggregate-cell boid count contribution for one sampled cell.
        std::uint32_t aggregate_weight_cap = 16;

        /// Future
        bool enable_aoi = false;
        bool enable_delta = false;
        bool enable_quantization = false;
        bool enable_compression = false;

        /// Interval for simulation statistics snapshots.
        int statistics_interval_ticks = 30;
        /// Minimum spdlog level: trace, debug, info, warn, error, critical, or off.
        std::string log_level = "info";

        /// Returns maximum timestep accumulation window in seconds.
        [[nodiscard]] double max_accum_sec() const noexcept { return static_cast<double>(max_sim_steps) / sim_hz; }
        /// Returns fixed simulation dt in seconds.
        [[nodiscard]] float dt_seconds() const noexcept { return 1.0f / static_cast<float>(sim_hz); }
        /// Returns fixed simulation dt in nanoseconds.
        [[nodiscard]] int64_t dt_ns() const noexcept { return 1'000'000'000 / sim_hz; }
        /// Returns maximum timestep accumulation window in nanoseconds.
        [[nodiscard]] int64_t max_accum_ns() const noexcept { return max_sim_steps * dt_ns(); }
        /// Returns derived acceleration limit.
        [[nodiscard]] float max_accel_value() const noexcept { return max_speed * max_accel_frac; }
    };

    /// Result for loading a config file.
    enum class ConfigLoadStatus {
        loaded,
        missing,
        parse_error,
        validation_error
    };

    /// Config load result with either a validated config or an unchanged default config.
    struct ConfigLoadResult {
    public:
        ConfigLoadStatus status = ConfigLoadStatus::missing;
        SimConfig config = SimConfig::default_config();
        std::string message;
        uint64_t config_fingerprint = 0;
    };

    /// Computes deterministic FNV-1a fingerprint for the validated config contract.
    uint64_t fingerprint(const SimConfig &cfg);

    /// Validates numeric config invariants.
    bool validate_config(const SimConfig &cfg, std::string &message);

    /// Loads strict sectioned JSON into a temporary config and commits only on success.
    ConfigLoadResult load_config_json(const std::string &path, const SimConfig &defaults = SimConfig::default_config());

    /// Writes config JSON using the strict sectioned schema.
    void save_json(const std::string &path, const SimConfig &cfg);

    /// Monotonic app clock.
    using Clock = std::chrono::steady_clock;
    /// Monotonic app timestamp.
    using TimePoint = Clock::time_point;
    /// Monotonic app duration.
    using Duration = Clock::duration;
    /// Nanosecond duration.
    using Nanoseconds = std::chrono::nanoseconds;
    /// Microsecond duration.
    using Microseconds = std::chrono::microseconds;
    /// Millisecond duration.
    using Milliseconds = std::chrono::milliseconds;
    /// Second duration.
    using Seconds = std::chrono::seconds;

    /// Converts a duration to int milliseconds with saturation.
    template<typename Rep, typename Period>
    constexpr int to_milliseconds_saturating(const std::chrono::duration<Rep, Period> &d)
    {
        const auto ms = std::chrono::duration_cast<Milliseconds>(d);
        if (ms.count() > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
        if (ms.count() < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
        return static_cast<int>(ms.count());
    }

    /// Minimal fixed-step timing settings derived from SimConfig.
    struct TimestepSettings {
    public:
        int sim_hz = 30;
        int max_sim_steps = 5;

        [[nodiscard]] int normalized_sim_hz() const noexcept { return sim_hz > 0 ? sim_hz : 30; }
        [[nodiscard]] int normalized_max_sim_steps() const noexcept { return max_sim_steps > 0 ? max_sim_steps : 1; }
        [[nodiscard]] int64_t dt_ns() const noexcept { return 1'000'000'000 / normalized_sim_hz(); }
        [[nodiscard]] int64_t max_accum_ns() const noexcept { return normalized_max_sim_steps() * dt_ns(); }
    };

    /// Owns wall-clock accumulation for fixed-step app loops.
    struct TimestepController {
    public:
        /// Creates a controller from timing settings.
        explicit TimestepController(TimestepSettings settings);

        /// Adds elapsed wall time in nanoseconds.
        void advance(int64_t elapsed_ns);

        /// Adds elapsed wall time.
        void advance(Nanoseconds elapsed);

        /// Consumes one fixed step if enough accumulated time is available.
        bool try_step();

        /// Returns milliseconds until the next fixed step can run.
        [[nodiscard]] int ms_until_next_tick() const noexcept;

        /// Returns accumulator fraction toward the next fixed step.
        [[nodiscard]] double interpolation_alpha() const noexcept;

        /// Returns total simulated fixed-step time.
        [[nodiscard]] Nanoseconds sim_time() const noexcept { return sim_time_; }
        /// Returns total simulated fixed-step time in nanoseconds.
        [[nodiscard]] int64_t sim_time_ns() const noexcept { return sim_time_.count(); }
        /// Returns fixed steps consumed since the last advance call.
        [[nodiscard]] int steps_this_frame() const noexcept { return steps_this_frame_; }

    private:
        Nanoseconds dt_;
        Nanoseconds max_accum_;
        int max_steps_;
        Nanoseconds accumulator_{};
        Nanoseconds sim_time_{};
        int steps_this_frame_ = 0;
    };

    /// Returns the executable directory if discoverable, otherwise current path.
    std::filesystem::path get_executable_dir();

    /// Resolves a runtime path relative to source tree in dev builds or executable in packaged builds.
    std::string resolve_runtime_path(const std::filesystem::path &relative_path);
}
