find_package(zstd CONFIG REQUIRED)

add_library(simnet_compression)

target_sources(simnet_compression
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES import/compression.cppm
    PRIVATE src/compression.cpp
)

target_link_libraries(simnet_compression
    PUBLIC simnet_core
    PRIVATE zstd::libzstd
)

simnet_configure_target(simnet_compression)
