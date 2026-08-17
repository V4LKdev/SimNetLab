find_package(Catch2 CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)

add_executable(SimNetTests
        aoi_tests.cpp
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
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cpp
        ${PROJECT_SOURCE_DIR}/app/server/app_evidence.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cpp
)

target_sources(SimNetTests
        PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}
        FILES
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cppm
        ${PROJECT_SOURCE_DIR}/app/server/app_evidence.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_snapshot_delivery.cppm
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
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

simnet_configure_target(SimNetTests)

include(Catch)
catch_discover_tests(
        SimNetTests
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "simnet."
        PROPERTIES TIMEOUT 10
)
