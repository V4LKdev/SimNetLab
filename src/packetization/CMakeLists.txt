add_library(simnet_packetization)

target_sources(simnet_packetization
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES import/packetization.cppm
    PRIVATE src/packetization.cpp
)

target_link_libraries(simnet_packetization PUBLIC simnet_core)

simnet_configure_target(simnet_packetization)
