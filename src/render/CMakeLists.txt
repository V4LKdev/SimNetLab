find_package(raylib REQUIRED)

add_library(simnet_render)

target_sources(simnet_render
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/render.cppm
)

target_link_libraries(simnet_render
    PRIVATE
        simnet_core
        simnet_config
        simnet_snapshot
        simnet_spatial
        simnet_game_client
        raylib
)

simnet_configure_target(simnet_render)
