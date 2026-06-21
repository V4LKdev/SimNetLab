# game/shared
add_library(game_shared STATIC
        components.hpp
        flocking_systems.cpp
        telemetry_systems.cpp
        systems/boid/align_heading.cpp
        systems/boid/alignment.cpp
        systems/boid/apply_steering.cpp
        systems/boid/build_neighbor_snapshot.cpp
        systems/boid/cohesion.cpp
        systems/boid/flock_stats.cpp
        systems/boid/integrate_position.cpp
        systems/boid/separation.cpp
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
