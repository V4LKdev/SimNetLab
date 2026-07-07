add_library(simnet_transport)

target_sources(simnet_transport
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/transport.cppm
)

target_link_libraries(simnet_transport
    PUBLIC
        simnet_core
        simnet_config
        simnet_telemetry
)

if (SIMNET_ENABLE_ENET)
    find_package(unofficial-enet CONFIG REQUIRED)
    target_compile_definitions(simnet_transport PUBLIC SIMNET_ENABLE_ENET=1)
    target_link_libraries(simnet_transport PRIVATE unofficial::enet::enet)
endif ()

simnet_configure_target(simnet_transport)
