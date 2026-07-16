add_executable(Server server_main.cpp server_runtime.cpp)
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

# Temporary legacy paired smoke runner. Remove after equivalent runtime process coverage exists.
add_executable(ServerSmoke server_smoke_main.cpp)
target_link_libraries(ServerSmoke
    PRIVATE
        simnet_config
        simnet_game_server
        simnet_pipeline
        simnet_telemetry
        simnet_transport
)
simnet_configure_target(ServerSmoke)

add_executable(Client client_main.cpp client_runtime.cpp)
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

# Temporary legacy paired smoke runner. Remove after equivalent runtime process coverage exists.
add_executable(ClientSmoke client_smoke_main.cpp)
target_link_libraries(ClientSmoke
    PRIVATE
        simnet_config
        simnet_game_client
        simnet_pipeline
        simnet_telemetry
        simnet_transport
)
simnet_configure_target(ClientSmoke)
