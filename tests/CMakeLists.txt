# tests
find_package(Catch2 CONFIG REQUIRED)

add_executable(SimNetTests
    app_camera_tests.cpp
    app_protocol_tests.cpp
    boid_simulation_tests.cpp
    config_tests.cpp
    core_time_tests.cpp
    pipeline_tests.cpp
    replication_tests.cpp
    runtime_tests.cpp
    snapshot_interpolation_tests.cpp
    snapshot_validation_tests.cpp
    spatial_tests.cpp
    transport_tests.cpp
)
target_sources(SimNetTests
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}
        FILES
            ${PROJECT_SOURCE_DIR}/app/app_camera.cppm
            ${PROJECT_SOURCE_DIR}/app/app_protocol.cppm
)

if (SIMNET_ENABLE_RENDER)
    target_sources(SimNetTests PRIVATE render_tests.cpp)
endif ()

if (SIMNET_ENABLE_SYNTHETIC)
    target_sources(SimNetTests PRIVATE synthetic_snapshot_tests.cpp)
endif ()

target_link_libraries(SimNetTests
    PRIVATE
        simnet_core
        simnet_config
        simnet_game_client
        simnet_game_server
        simnet_pipeline
        simnet_runtime
        simnet_snapshot
        simnet_spatial
        simnet_transport
        Catch2::Catch2WithMain
)

if (SIMNET_ENABLE_RENDER)
    target_link_libraries(SimNetTests PRIVATE simnet_render)
endif ()

if (SIMNET_ENABLE_SYNTHETIC)
    target_link_libraries(SimNetTests PRIVATE simnet_synthetic)
endif ()

simnet_configure_target(SimNetTests)

include(Catch)
catch_discover_tests(SimNetTests
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "simnet."
        PROPERTIES
        LABELS "safety-net"
        TIMEOUT 10
)
