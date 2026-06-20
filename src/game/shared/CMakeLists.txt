# game/shared
add_library(game_shared STATIC
        flocking_systems.cpp
        telemetry_systems.cpp
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


target_link_libraries(game_shared PUBLIC core)