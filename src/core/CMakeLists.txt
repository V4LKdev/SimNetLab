add_library(core STATIC
        simulation.cpp

        # ECS pipeline
        ecs/pipeline.cpp

        # Individual systems
        ecs/systems/alignment.cpp
        ecs/systems/cohesion.cpp
        ecs/systems/separation.cpp
        ecs/systems/apply_steering.cpp
        ecs/systems/integrate_position.cpp
        ecs/systems/clear_accumulator.cpp
        ecs/systems/align_heading.cpp
        ecs/systems/flock_stats.cpp
        ecs/systems/build_neighbor_snapshot.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(flecs CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(HWY CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC unofficial::enet::enet
        PUBLIC hwy
)

if (TELEMETRY_ENABLED)
    target_link_libraries(core PUBLIC telemetry)
endif ()
