# render
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
        private/render_viewer_impl.hpp
        private/render_ui.hpp
        src/render_viewer.cpp
        src/render_ui_model.cpp
        src/render_viewer_panel.cpp
        src/render_viewer_scene.cpp
        src/render_viewer_viewport_ui.cpp
)

target_link_libraries(simnet_render
    PUBLIC
        simnet_core
    PRIVATE
        raylib
        simnet_telemetry
)

target_compile_definitions(simnet_render
    PRIVATE
        SIMNET_NERD_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/JetBrainsMonoNerdFont-Regular.ttf"
)

simnet_configure_target(simnet_render)
