add_library(simnet_pipeline)

target_sources(simnet_pipeline
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/pipeline.cppm
            import/pipeline_types.cppm
            import/pipeline_codec.cppm
    PRIVATE
        FILE_SET private_modules
        TYPE CXX_MODULES
        BASE_DIRS private
        FILES
            private/pipeline_wire.cppm
    PRIVATE
        src/raw_snapshot.cpp
)

target_link_libraries(simnet_pipeline
    PUBLIC
        simnet_core
        simnet_snapshot
)

simnet_configure_target(simnet_pipeline)
