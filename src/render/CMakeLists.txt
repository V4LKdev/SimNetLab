find_package(raylib REQUIRED)

add_library(simnet_render)

target_sources(simnet_render
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/render.cppm
            import/render_types.cppm
    PRIVATE
        src/render_viewer.cpp
)

target_link_libraries(simnet_render
    PUBLIC
        simnet_core
    PRIVATE
        raylib
        simnet_telemetry
)

simnet_configure_target(simnet_render)
