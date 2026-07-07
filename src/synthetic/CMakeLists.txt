add_library(simnet_synthetic)

target_sources(simnet_synthetic
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/synthetic.cppm
            import/synthetic_types.cppm
            import/synthetic_snapshot.cppm
    PRIVATE
        src/synthetic_snapshot.cpp
)

target_link_libraries(simnet_synthetic
    PUBLIC
        simnet_core
        simnet_snapshot
)

simnet_configure_target(simnet_synthetic)
