find_package(flecs CONFIG REQUIRED)

add_executable(
        Server
        server_main.cpp
        server_runtime.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_compression_dictionary.cpp
        app_evidence.cpp
)

set_target_properties(Server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")

target_sources(
        Server
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}/app
        FILES
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_compression_dictionary.cppm
        app_evidence.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_snapshot_delivery.cppm
        server_replication.cppm
        server_runtime.cppm
)

target_link_libraries(
        Server
        PRIVATE
        simnet_config
        simnet_compression
        simnet_core
        simnet_game_server
        simnet_game_shared
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_snapshot
        simnet_spatial
        simnet_telemetry
        simnet_transport
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

if (SIMNET_ENABLE_SYNTHETIC)
    target_link_libraries(Server PRIVATE simnet_synthetic)
    target_compile_definitions(Server PRIVATE SIMNET_ENABLE_SYNTHETIC=1)
endif ()

if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Server PRIVATE simnet_render)
    target_sources(Server PRIVATE ${PROJECT_SOURCE_DIR}/app/common/app_visual_setup.cpp)
    target_sources(
            Server
            PRIVATE FILE_SET CXX_MODULES
            BASE_DIRS ${PROJECT_SOURCE_DIR}/app
            FILES ${PROJECT_SOURCE_DIR}/app/common/app_visual_setup.cppm
    )
    target_compile_definitions(
            Server
            PRIVATE
            SIMNET_ENABLE_RENDER=1
            SIMNET_BUILD_CONFIG="$<CONFIG>"
    )
endif ()

target_compile_definitions(
        Server
        PRIVATE
        SIMNET_COMPRESSION_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/compression"
)

simnet_configure_target(Server)
