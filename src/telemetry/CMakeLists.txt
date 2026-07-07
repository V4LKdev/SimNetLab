find_package(spdlog CONFIG REQUIRED)

if (SIMNET_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif ()

add_library(simnet_telemetry)

target_sources(simnet_telemetry
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/telemetry.cppm
            import/telemetry_types.cppm
            import/telemetry_log.cppm
            import/telemetry_metrics.cppm
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/simnet/telemetry_trace.hpp
    PRIVATE
        src/telemetry.cpp
        src/telemetry_log.cpp
        src/telemetry_metrics.cpp
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
    target_link_libraries(simnet_telemetry PUBLIC Tracy::TracyClient)
endif ()

simnet_configure_target(simnet_telemetry)
