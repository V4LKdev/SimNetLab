find_package(Catch2 CONFIG REQUIRED)

add_executable(SimNetTests
    config_tests.cpp
    core_time_tests.cpp
    pipeline_tests.cpp
    replication_tests.cpp
    runtime_tests.cpp
    spatial_tests.cpp
    transport_tests.cpp
)

if (SIMNET_ENABLE_RENDER)
    target_sources(SimNetTests PRIVATE render_tests.cpp)
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

simnet_configure_target(SimNetTests)

include(Catch)
catch_discover_tests(SimNetTests
    TEST_PREFIX "simnet."
    PROPERTIES
        LABELS "safety-net"
        TIMEOUT 10
)
