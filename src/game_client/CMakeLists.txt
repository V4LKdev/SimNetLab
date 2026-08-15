# game_client
find_package(flecs CONFIG REQUIRED)

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
        simnet_core
        simnet_snapshot
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
    PRIVATE
        simnet_game_shared
)

simnet_configure_target(simnet_game_client)
