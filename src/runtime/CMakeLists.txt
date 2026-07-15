add_library(simnet_runtime)

target_sources(simnet_runtime
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/runtime.cppm
            import/runtime_types.cppm
            import/runtime_plan.cppm
    PRIVATE
        src/runtime.cpp
)

target_link_libraries(simnet_runtime
    PUBLIC
        simnet_core
)

simnet_configure_target(simnet_runtime)
