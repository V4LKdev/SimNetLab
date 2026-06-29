# visualization
add_library(visualization STATIC)

find_package(raylib CONFIG REQUIRED)

target_sources(visualization
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include/
        FILES
        include/visualization/visualization.cppm
)

target_sources(visualization
        PRIVATE
        FILE_SET private_visualization_modules TYPE CXX_MODULES
        BASE_DIRS src/
        FILES
        src/raylib_boid_transform.cppm
        src/raylib_debug_gizmos.cppm
        src/raylib_instancing.cppm
        src/raylib_viewer_sidebar.cppm
)

target_sources(visualization
        PRIVATE
        src/raylib_simulation_viewer.cpp
)

target_include_directories(visualization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets)
target_compile_definitions(visualization PRIVATE SIMNET_DEV_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(visualization
        PUBLIC
        game_shared
        utils
        PRIVATE
        telemetry
        raylib
)

target_compile_features(visualization PUBLIC cxx_std_23)

target_enable_sanitizers(visualization)
