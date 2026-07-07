find_package(flecs CONFIG REQUIRED)

add_library(simnet_game_shared)

target_sources(simnet_game_shared
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/game_shared.cppm
            import/game_shared_components.cppm
    PRIVATE
        src/game_shared_components.cpp
)

target_link_libraries(simnet_game_shared
    PUBLIC
        simnet_core
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

simnet_configure_target(simnet_game_shared)
