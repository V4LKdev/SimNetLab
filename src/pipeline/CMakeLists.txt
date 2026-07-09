add_library(simnet_pipeline)

target_sources(simnet_pipeline
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/pipeline.cppm
            import/pipeline_types.cppm
            import/pipeline_codec.cppm
            import/pipeline_messages.cppm
    PRIVATE
        FILE_SET private_modules
        TYPE CXX_MODULES
        BASE_DIRS private
        FILES
            private/pipeline_wire.cppm
            private/pipeline_bitpack.cppm
            private/pipeline_quantize.cppm
            private/pipeline_records.cppm
            private/pipeline_validate.cppm
            private/pipeline_selection.cppm
            private/pipeline_signature.cppm
    PRIVATE
        src/pipeline_encode.cpp
        src/pipeline_decode.cpp
)

target_link_libraries(simnet_pipeline
    PUBLIC
        simnet_core
        simnet_snapshot
)

simnet_configure_target(simnet_pipeline)
