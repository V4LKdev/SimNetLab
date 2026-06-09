#pragma once
#include <cstdint>
#include <vector>

#include "boid_math.hpp"

#if defined(_MSC_VER)
    #include <malloc.h>
#endif

namespace simnet::ecs {

    // --------------------------------------------------------
    //  Core ECS components
    // --------------------------------------------------------

    struct Position3D {
        Vec3 pos;
    };

    struct Velocity3D {
        Vec3 vel;
    };

    struct DesiredVelocity3D {
        Vec3 desired;
    };

    struct Boid {
        uint32_t scratch_index = 0;
    };


    // --------------------------------------------------------
    //  Boid parameters (singletons)
    // --------------------------------------------------------

    struct BoidConfig {
        float max_speed        = 150.0f;
        float max_force        = 8.0f;
        float mass             = 1.0f;
        float separation_weight = 2.5f;
        float alignment_weight  = 1.2f;
        float cohesion_weight   = 0.5f;
    };

    struct BoidPerception {
        float separation_radius = 25.0f;
        float cohesion_radius   = 50.0f;
        float alignment_radius  = 40.0f;
        float fov_cos           = -1.0f;
    };

    // --------------------------------------------------------
    //  AoS neighbour representation
    // --------------------------------------------------------

    struct AoSNeighbour {
        Vec3 pos;
        Vec3 vel;
    };

    // Global neighbour list - built once per frame
    struct NeighbourList {
        std::vector<AoSNeighbour> neighbours;
    };


    // --------------------------------------------------------
    //  Per‑boid scratchpad - AoS (used by scalar path)
    // --------------------------------------------------------

    struct BoidScratchpadAoS {
        const AoSNeighbour* neighbours;     // pointer into NeighbourList
        uint32_t            count;
    };


    // --------------------------------------------------------
    //  Per‑boid scratchpad - SoA (used by SIMD path)
    // --------------------------------------------------------

    template <typename T, size_t Align = 64>
    struct AlignedAllocator {
        using value_type = T;

        AlignedAllocator() noexcept = default;

        template <typename U>
        AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

        template <typename U>
        struct rebind {
            using other = AlignedAllocator<U, Align>;
        };

        T* allocate(size_t n) {
            if (n == 0) return nullptr;
            void* ptr = nullptr;
#if defined(_MSC_VER)
            ptr = _aligned_malloc(n * sizeof(T), Align);
#else
            if (posix_memalign(&ptr, Align, n * sizeof(T)) != 0) ptr = nullptr;
#endif
            if (!ptr) throw std::bad_alloc();
            return static_cast<T*>(ptr);
        }

        void deallocate(T* p, size_t) noexcept {
#if defined(_MSC_VER)
            _aligned_free(p);
#else
            free(p);
#endif
        }

        template <typename U, size_t A>
        bool operator==(const AlignedAllocator<U, A>&) const noexcept { return true; }

        template <typename U, size_t A>
        bool operator!=(const AlignedAllocator<U, A>&) const noexcept { return false; }
    };

    struct BoidScratchpadSoA {
        // --- Boid state ---
        std::vector<float, AlignedAllocator<float>> pos_x;
        std::vector<float, AlignedAllocator<float>> pos_y;
        std::vector<float, AlignedAllocator<float>> pos_z;
        std::vector<float, AlignedAllocator<float>> vel_x;
        std::vector<float, AlignedAllocator<float>> vel_y;
        std::vector<float, AlignedAllocator<float>> vel_z;

        // --- Per‑boid weights & radii (populated from singletons) ---
        std::vector<float, AlignedAllocator<float>> config_sep_weight;
        std::vector<float, AlignedAllocator<float>> config_ali_weight;
        std::vector<float, AlignedAllocator<float>> config_coh_weight;
        std::vector<float, AlignedAllocator<float>> perception_sep_radius_sq;
        std::vector<float, AlignedAllocator<float>> perception_ali_radius_sq;
        std::vector<float, AlignedAllocator<float>> perception_coh_radius_sq;
        std::vector<float, AlignedAllocator<float>> perception_fov_cos;

        // --- Self‑indices (0..count-1) ---
        std::vector<int32_t> boid_indices;

        size_t count = 0;
    };

} // namespace simnet::ecs
