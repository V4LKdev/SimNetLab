# app
add_executable(Server server_main.cpp server_runtime.cpp app_evidence.cpp)
target_sources(
    Server
    PRIVATE FILE_SET CXX_MODULES FILES app_common.cppm app_evidence.cppm app_protocol.cppm
)
target_link_libraries(Server
    PRIVATE
        simnet_config
        simnet_game_server
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_spatial
        simnet_telemetry
        simnet_transport
)
if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Server PRIVATE simnet_render)
    target_sources(Server PRIVATE FILE_SET CXX_MODULES FILES app_visual_setup.cppm)
    target_compile_definitions(Server PRIVATE
        SIMNET_ENABLE_RENDER=1
        SIMNET_BUILD_CONFIG="$<CONFIG>"
    )
endif ()
simnet_configure_target(Server)

add_executable(Client client_main.cpp client_runtime.cpp)
target_sources(Client
    PRIVATE
        FILE_SET CXX_MODULES
        FILES app_camera.cppm app_common.cppm app_protocol.cppm
)
target_link_libraries(Client
    PRIVATE
        simnet_config
        simnet_game_client
        simnet_packetization
        simnet_pipeline
        simnet_runtime
        simnet_telemetry
        simnet_transport
)
if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Client PRIVATE simnet_render)
    target_sources(Client PRIVATE FILE_SET CXX_MODULES FILES app_visual_setup.cppm)
    target_compile_definitions(Client PRIVATE
        SIMNET_ENABLE_RENDER=1
        SIMNET_BUILD_CONFIG="$<CONFIG>"
    )
endif ()
simnet_configure_target(Client)
