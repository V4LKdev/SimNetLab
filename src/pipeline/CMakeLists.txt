add_library(simnet_pipeline)

target_sources(simnet_pipeline
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/pipeline.cppm
)

target_link_libraries(simnet_pipeline
    PUBLIC
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_spatial
)

simnet_configure_target(simnet_pipeline)
