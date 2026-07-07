add_library(simnet_game_server)

target_sources(simnet_game_server
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/game_server.cppm
)

target_link_libraries(simnet_game_server
    PUBLIC
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_spatial
        simnet_game_shared
)

simnet_configure_target(simnet_game_server)
