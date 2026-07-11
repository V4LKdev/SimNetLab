add_executable(Server server_main.cpp)
target_link_libraries(Server
    PRIVATE
        simnet_config
        simnet_game_server
        simnet_pipeline
        simnet_telemetry
        simnet_transport
)
simnet_configure_target(Server)

add_executable(PipelineLab pipeline_lab_main.cpp)
target_link_libraries(PipelineLab
    PRIVATE
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_synthetic
        simnet_pipeline
)
simnet_configure_target(PipelineLab)

add_executable(Client client_main.cpp)
target_link_libraries(Client
    PRIVATE
        simnet_config
        simnet_pipeline
        simnet_telemetry
        simnet_transport
)
simnet_configure_target(Client)
