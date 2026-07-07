add_library(simnet_core)

target_sources(simnet_core
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/core.cppm
            import/core_math.cppm
            import/core_time.cppm
            import/core_bytes.cppm
)

simnet_configure_target(simnet_core)
