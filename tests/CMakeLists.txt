find_package(Catch2 CONFIG REQUIRED)
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

add_executable(SimNetTests
        aoi_tests.cpp
        app_compression_corpus_tests.cpp
        app_evidence_tests.cpp
        app_protocol_tests.cpp
        app_snapshot_delivery_tests.cpp
        boid_simulation_tests.cpp
        compression_tests.cpp
        config_tests.cpp
        core_bytes_tests.cpp
        lod_tests.cpp
        packetization_tests.cpp
        pipeline_tests.cpp
        replication_tests.cpp
        telemetry_tests.cpp
        transport_tests.cpp
        ${PROJECT_SOURCE_DIR}/app/app_common.cpp
        ${PROJECT_SOURCE_DIR}/app/app_compression_corpus.cpp
        ${PROJECT_SOURCE_DIR}/app/app_compression_dictionary.cpp
        ${PROJECT_SOURCE_DIR}/app/app_evidence.cpp
        ${PROJECT_SOURCE_DIR}/app/app_protocol.cpp
)

target_sources(SimNetTests
        PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}
        FILES
        ${PROJECT_SOURCE_DIR}/app/app_common.cppm
        ${PROJECT_SOURCE_DIR}/app/app_compression_corpus.cppm
        ${PROJECT_SOURCE_DIR}/app/app_compression_dictionary.cppm
        ${PROJECT_SOURCE_DIR}/app/app_evidence.cppm
        ${PROJECT_SOURCE_DIR}/app/app_protocol.cppm
        ${PROJECT_SOURCE_DIR}/app/app_snapshot_delivery.cppm
)

target_link_libraries(SimNetTests
        PRIVATE
        simnet_compression
        simnet_config
        simnet_core
        simnet_game_client
        simnet_game_server
        simnet_game_shared
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_snapshot
        simnet_telemetry
        simnet_transport
        Catch2::Catch2WithMain
)

target_compile_definitions(
        SimNetTests PRIVATE SIMNET_COMPRESSION_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/compression"
)

simnet_configure_target(SimNetTests)

include(Catch)
catch_discover_tests(
        SimNetTests
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "simnet."
        PROPERTIES TIMEOUT 10
)

add_test(
        NAME simnet.linux_collector
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/linux_collector_tests.py
)
set_tests_properties(simnet.linux_collector PROPERTIES TIMEOUT 10)

add_test(
        NAME simnet.perf_stat
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/perf_stat_tests.py
)
set_tests_properties(simnet.perf_stat PROPERTIES TIMEOUT 15)