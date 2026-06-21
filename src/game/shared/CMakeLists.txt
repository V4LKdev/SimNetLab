# game/shared
add_library(game_shared STATIC
        components.hpp
        flocking_systems.cpp
        flocking_systems.hpp
        telemetry_systems.cpp
        telemetry_systems.hpp
        systems/alignment.cpp
        systems/cohesion.cpp
        systems/separation.cpp
        systems/apply_steering.cpp
        systems/integrate_position.cpp
        systems/clear_accumulator.cpp
        systems/align_heading.cpp
        systems/flock_stats.cpp
        systems/build_neighbor_snapshot.cpp
)

target_include_directories(game_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_include_directories(game_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(game_shared PUBLIC core)

# Make telemetry available to all downstream game code
if (BUILD_TELEMETRY)
    target_link_libraries(game_shared PUBLIC telemetry)
endif ()
