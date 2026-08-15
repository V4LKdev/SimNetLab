add_executable(
        Client
        client_main.cpp
        client_runtime.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cpp
        ${PROJECT_SOURCE_DIR}/app/common/app_compression_dictionary.cpp
)

set_target_properties(Client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")

target_sources(
        Client
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${PROJECT_SOURCE_DIR}/app
        FILES
        app_camera.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_common.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_compression_dictionary.cppm
        app_player_input.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_protocol.cppm
        ${PROJECT_SOURCE_DIR}/app/common/app_snapshot_delivery.cppm
        client_runtime.cppm
)

target_link_libraries(
        Client
        PRIVATE
        simnet_config
        simnet_compression
        simnet_core
        simnet_game_client
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_snapshot
        simnet_telemetry
        simnet_transport
)

if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Client PRIVATE simnet_render)
    target_sources(Client PRIVATE ${PROJECT_SOURCE_DIR}/app/common/app_visual_setup.cpp)
    target_sources(
            Client
            PRIVATE FILE_SET CXX_MODULES
            BASE_DIRS ${PROJECT_SOURCE_DIR}/app
            FILES ${PROJECT_SOURCE_DIR}/app/common/app_visual_setup.cppm
    )
    target_compile_definitions(
            Client
            PRIVATE
            SIMNET_ENABLE_RENDER=1
            SIMNET_BUILD_CONFIG="$<CONFIG>"
    )
endif ()

target_compile_definitions(
        Client
        PRIVATE
        SIMNET_COMPRESSION_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/compression"
)

simnet_configure_target(Client)
