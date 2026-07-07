add_library(simnet_spatial)

target_sources(simnet_spatial
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/spatial.cppm
)

target_link_libraries(simnet_spatial
    PUBLIC
        simnet_core
        simnet_snapshot
)

simnet_configure_target(simnet_spatial)
