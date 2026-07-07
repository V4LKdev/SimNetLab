find_package(spdlog CONFIG REQUIRED)

if (SIMNET_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif ()

add_library(simnet_telemetry)

target_sources(simnet_telemetry
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/telemetry.cppm
)

target_link_libraries(simnet_telemetry
    PUBLIC
        simnet_core
        simnet_config
    PRIVATE
        spdlog::spdlog
)

if (SIMNET_ENABLE_TRACY)
    target_compile_definitions(simnet_telemetry PUBLIC SIMNET_ENABLE_TRACY=1)
    target_link_libraries(simnet_telemetry PRIVATE Tracy::TracyClient)
endif ()

simnet_configure_target(simnet_telemetry)
