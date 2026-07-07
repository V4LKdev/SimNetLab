add_library(simnet_core)

target_sources(simnet_core
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/core.cppm
)

simnet_configure_target(simnet_core)
