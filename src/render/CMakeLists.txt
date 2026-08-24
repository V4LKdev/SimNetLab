# render
find_package(raylib REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_library(simnet_render)

target_sources(simnet_render
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/render.cppm
            import/render_types.cppm
            import/render_viewer.cppm
    PRIVATE
        FILE_SET private_modules
        TYPE CXX_MODULES
        BASE_DIRS private
        FILES
            private/render_ui.cppm
            private/render_viewer_impl.cppm
    PRIVATE
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
        glfw
        simnet_telemetry
)

simnet_configure_target(simnet_render)
