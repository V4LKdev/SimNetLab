#pragma once
#include <cstdint>
#include <vector>
#include <hwy/ops/inside-inl.h>

#include "boid_math.hpp"

#if defined(_MSC_VER)
    #include <malloc.h>
#endif

namespace simnet::ecs {

    struct Position3D {
        Vec3 pos;
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

    struct BoidFeatures {
        bool separation = true;
        bool alignment = true;
        bool cohesion = true;
    };


    template <typename T, size_t Align = 64>
    struct AlignedAllocator {
        using value_type = T;

        AlignedAllocator() noexcept = default;

        template <typename U>
        AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

        // THIS IS THE MISSING PIECE CLANG WANTS:
        // This tells std::vector how to rebind this allocator to other internal types
        template <typename U>
        struct rebind {
            using other = AlignedAllocator<U, Align>;
        };

        T* allocate(size_t n) {
            if (n == 0) return nullptr;
#if defined(_MSC_VER)
            void* ptr = _aligned_malloc(n * sizeof(T), Align);
#else
            void* ptr = nullptr;
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

        // Boilerplate equality operators required by the allocator traits
        template <typename U, size_t A>
        bool operator==(const AlignedAllocator<U, A>&) const noexcept { return true; }

        template <typename U, size_t A>
        bool operator!=(const AlignedAllocator<U, A>&) const noexcept { return false; }
    };

}
