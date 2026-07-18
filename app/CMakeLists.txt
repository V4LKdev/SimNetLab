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
simnet_configure_target(Client)
