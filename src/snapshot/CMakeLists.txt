add_library(simnet_snapshot)

target_sources(simnet_snapshot
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/snapshot.cppm
            import/snapshot_types.cppm
            import/snapshot_validate.cppm
)

target_link_libraries(simnet_snapshot
    PUBLIC
        simnet_core
)

simnet_configure_target(simnet_snapshot)
