# game_server
find_package(flecs CONFIG REQUIRED)

add_library(simnet_game_server)

target_sources(simnet_game_server
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/game_server.cppm
            import/game_server_player.cppm
            import/game_server_simulation.cppm
            import/game_server_snapshot.cppm
    PRIVATE
        src/game_server.cpp
)

target_link_libraries(simnet_game_server
    PUBLIC
        simnet_core
        simnet_snapshot
        simnet_spatial
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
    PRIVATE
        simnet_game_shared
        simnet_telemetry
)

simnet_configure_target(simnet_game_server)
