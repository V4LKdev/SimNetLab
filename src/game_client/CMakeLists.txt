add_library(simnet_game_client)

target_sources(simnet_game_client
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/game_client.cppm
            import/game_client_types.cppm
            import/game_client_apply.cppm
    PRIVATE
        src/game_client.cpp
)

target_link_libraries(simnet_game_client
    PUBLIC
        simnet_game_shared
        simnet_snapshot
)

simnet_configure_target(simnet_game_client)
