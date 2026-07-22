add_executable(Server server_main.cpp server_runtime.cpp)
target_sources(Server PRIVATE FILE_SET CXX_MODULES FILES app_common.cppm)
target_link_libraries(Server
    PRIVATE
        simnet_config
        simnet_game_server
        simnet_pipeline
        simnet_runtime
        simnet_telemetry
        simnet_transport
)
if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Server PRIVATE simnet_render)
    target_compile_definitions(Server PRIVATE SIMNET_ENABLE_RENDER=1)
endif ()
simnet_configure_target(Server)

add_executable(Client client_main.cpp client_runtime.cpp)
target_sources(Client PRIVATE FILE_SET CXX_MODULES FILES app_common.cppm)
target_link_libraries(Client
    PRIVATE
        simnet_config
        simnet_game_client
        simnet_pipeline
        simnet_runtime
        simnet_telemetry
        simnet_transport
)
if (SIMNET_ENABLE_RENDER)
    target_link_libraries(Client PRIVATE simnet_render)
    target_compile_definitions(Client PRIVATE SIMNET_ENABLE_RENDER=1)
endif ()
simnet_configure_target(Client)
