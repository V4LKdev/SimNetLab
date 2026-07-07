add_library(simnet_game_client)

target_sources(simnet_game_client
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/game_client.cppm
)

target_link_libraries(simnet_game_client
    PRIVATE
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_game_shared
)

simnet_configure_target(simnet_game_client)
