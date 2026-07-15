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
        BASE_DIRS src
        FILES
            src/transport_enet.cpp
)

target_link_libraries(simnet_transport
    PUBLIC
        simnet_core
)

if (SIMNET_ENABLE_ENET)
    find_package(unofficial-enet CONFIG REQUIRED)
    target_compile_definitions(simnet_transport PRIVATE SIMNET_ENABLE_ENET=1)
    target_link_libraries(simnet_transport PRIVATE unofficial::enet::enet)
endif ()

if (SIMNET_ENABLE_LOCAL_IPC AND UNIX AND NOT APPLE)
    target_compile_definitions(simnet_transport PRIVATE SIMNET_ENABLE_LOCAL_IPC=1)
endif ()

simnet_configure_target(simnet_transport)

add_executable(TransportSmoke src/transport_smoke.cpp)
target_link_libraries(TransportSmoke PRIVATE simnet_transport)
if (SIMNET_ENABLE_LOCAL_IPC AND UNIX AND NOT APPLE)
    target_compile_definitions(TransportSmoke PRIVATE SIMNET_ENABLE_LOCAL_IPC=1)
endif ()
simnet_configure_target(TransportSmoke)
