find_package(flecs CONFIG REQUIRED)

add_library(simnet_game_shared)

target_sources(simnet_game_shared
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/game_shared.cppm
)

target_link_libraries(simnet_game_shared
    PUBLIC
        simnet_core
        simnet_config
        simnet_snapshot
        simnet_spatial
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

simnet_configure_target(simnet_game_shared)
