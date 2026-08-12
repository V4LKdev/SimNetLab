# tests
find_package(Catch2 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

add_executable(SimNetTests
    app_compression_corpus_tests.cpp
    app_camera_tests.cpp
    app_evidence_tests.cpp
    app_player_input_tests.cpp
    app_protocol_tests.cpp
    app_snapshot_delivery_tests.cpp
    aoi_tests.cpp
    boid_simulation_tests.cpp
    config_tests.cpp
    compression_tests.cpp
    core_bytes_tests.cpp
    core_math_tests.cpp
    core_time_tests.cpp
    lod_tests.cpp
    pipeline_tests.cpp
    packetization_tests.cpp
    replication_tests.cpp
    runtime_tests.cpp
    snapshot_interpolation_tests.cpp
    snapshot_validation_tests.cpp
    spatial_tests.cpp
    telemetry_tests.cpp
    transport_tests.cpp
    ${PROJECT_SOURCE_DIR}/app/app_common.cpp
    ${PROJECT_SOURCE_DIR}/app/app_protocol.cpp
    ${PROJECT_SOURCE_DIR}/app/app_compression_corpus.cpp
    ${PROJECT_SOURCE_DIR}/app/app_compression_dictionary.cpp
    ${PROJECT_SOURCE_DIR}/app/app_evidence.cpp
)
target_sources(SimNetTests
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}
        FILES
            ${PROJECT_SOURCE_DIR}/app/app_camera.cppm
            ${PROJECT_SOURCE_DIR}/app/app_common.cppm
            ${PROJECT_SOURCE_DIR}/app/app_compression_corpus.cppm
            ${PROJECT_SOURCE_DIR}/app/app_compression_dictionary.cppm
            ${PROJECT_SOURCE_DIR}/app/app_evidence.cppm
            ${PROJECT_SOURCE_DIR}/app/app_player_input.cppm
            ${PROJECT_SOURCE_DIR}/app/app_protocol.cppm
            ${PROJECT_SOURCE_DIR}/app/app_snapshot_delivery.cppm
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
        simnet_compression
        simnet_game_client
        simnet_game_server
        simnet_game_shared
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_snapshot
        simnet_spatial
        simnet_telemetry
        simnet_transport
        zstd::libzstd
        Catch2::Catch2WithMain
)

if (SIMNET_ENABLE_RENDER)
    target_link_libraries(SimNetTests PRIVATE simnet_render)
endif ()

if (SIMNET_ENABLE_SYNTHETIC)
    target_link_libraries(SimNetTests PRIVATE simnet_synthetic)
endif ()

target_compile_definitions(
    SimNetTests PRIVATE SIMNET_COMPRESSION_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/compression"
)

simnet_configure_target(SimNetTests)

include(Catch)
catch_discover_tests(SimNetTests
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "simnet."
        PROPERTIES
        LABELS "safety-net"
        TIMEOUT 10
)

add_test(
    NAME simnet.linux_collector
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/linux_collector_tests.py
)
set_tests_properties(simnet.linux_collector PROPERTIES LABELS "safety-net" TIMEOUT 10)

add_test(
    NAME simnet.perf_stat
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/perf_stat_tests.py
)
set_tests_properties(simnet.perf_stat PROPERTIES LABELS "safety-net" TIMEOUT 15)

add_test(
    NAME simnet.tooling_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tooling_policy_tests.py
)
set_tests_properties(simnet.tooling_policy PROPERTIES LABELS "safety-net" TIMEOUT 10)
