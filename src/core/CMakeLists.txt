add_library(core STATIC
        simulation.cpp
        ecs/pipeline.cpp
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

target_include_directories(core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC nlohmann_json::nlohmann_json
)

# telemetry & simd are always available (interface or static)
target_link_libraries(core PUBLIC telemetry simd)