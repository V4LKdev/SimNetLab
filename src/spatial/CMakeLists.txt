add_library(simnet_spatial)

target_sources(simnet_spatial
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/spatial.cppm
            import/spatial_types.cppm
            import/spatial_build.cppm
            import/spatial_query.cppm
    PRIVATE
        src/spatial_build.cpp
)

target_link_libraries(simnet_spatial
    PUBLIC
        simnet_core
)

simnet_configure_target(simnet_spatial)
