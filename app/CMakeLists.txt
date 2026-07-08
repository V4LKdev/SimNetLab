add_executable(Server server_main.cpp)
target_link_libraries(Server
    PRIVATE
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_synthetic
        simnet_pipeline
)
simnet_configure_target(Server)

add_executable(Client client_main.cpp)
target_link_libraries(Client
    PRIVATE
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_game_client
        simnet_pipeline
        simnet_transport
)
if (SIMNET_ENABLE_RENDER)
    target_compile_definitions(Client PRIVATE SIMNET_ENABLE_RENDER=1)
    target_link_libraries(Client PRIVATE simnet_render)
endif ()
simnet_configure_target(Client)
