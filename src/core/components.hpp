#pragma once
#include <cstdint>
#include <vector>

#include "boid_math.hpp"

#if defined(_MSC_VER)
    #include <malloc.h>
#endif

namespace simnet::ecs {

    struct Position3D {
        simnet::Vec3 pos;
    };

    struct Velocity3D {
        Vec3 vel;
    };

    struct DesiredVelocity3D {
        Vec3 desired;
    };

    struct BoidConfig {
        float max_speed;
        float max_force;
        float mass;
        float separation_weight;
        float alignment_weight;
        float cohesion_weight;
    };


    struct BoidPerception {
        float separation_radius;
        float cohesion_radius;
        float alignment_radius;
        float fov_cos;
    };

    struct Boid {
        uint32_t scratch_index;
    };

    struct AoSNeighbour {
        Vec3 pos;
        Vec3 vel;
    };

    struct BoidScratchpadAoS {
        // Can use a pointer here since Vec3 is alignas(16)
        const AoSNeighbour* neighbours;
        uint32_t count;
    };

    // TODO: Replace with null weights
    struct BoidFeatures {
        bool separation = true;
        bool alignment = true;
        bool cohesion = true;
    };


    // 64-byte aligned allocator for SIMD buffers
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

}
