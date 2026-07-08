add_library(simnet_game_server)

target_sources(simnet_game_server
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/game_server.cppm
            import/game_server_snapshot.cppm
    PRIVATE
        src/game_server.cpp
)

target_link_libraries(simnet_game_server
    PUBLIC
        simnet_core
        simnet_snapshot
        simnet_game_shared
)

simnet_configure_target(simnet_game_server)
