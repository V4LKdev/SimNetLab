add_library(simnet_transport)

target_sources(simnet_transport
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/transport.cppm
            import/transport_types.cppm
            import/transport_server.cppm
            import/transport_client.cppm
    PRIVATE
        FILE_SET transport_private_modules TYPE CXX_MODULES
        BASE_DIRS private
        FILES
            private/transport_protocol.cppm
    PRIVATE
        src/transport_protocol.cpp
        src/transport_enet.cpp
)

target_link_libraries(simnet_transport
    PUBLIC
        simnet_core
)

find_package(unofficial-enet CONFIG REQUIRED)
target_link_libraries(simnet_transport PRIVATE unofficial::enet::enet)

simnet_configure_target(simnet_transport)
