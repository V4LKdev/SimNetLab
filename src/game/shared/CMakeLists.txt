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
        systems/system_functions.hpp
)

target_include_directories(game_shared
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(game_shared
        PUBLIC core
)
