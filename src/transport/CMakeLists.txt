find_package(unofficial-enet CONFIG REQUIRED)

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
    PRIVATE
        unofficial::enet::enet
)

simnet_configure_target(simnet_transport)
