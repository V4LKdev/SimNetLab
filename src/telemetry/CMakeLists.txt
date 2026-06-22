# telemetry
if (BUILD_TELEMETRY)
    add_library(telemetry STATIC
            telemetry.cpp
            logger.cpp
            metrics_collector.cpp
    )
    target_compile_definitions(telemetry PUBLIC TELEMETRY_ENABLED=1 TRACY_ENABLE)
    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(telemetry
            PRIVATE fmt::fmt spdlog::spdlog
            PUBLIC Tracy::TracyClient
    )
    target_include_directories(telemetry PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    )
    target_include_directories(telemetry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_precompile_headers(telemetry PRIVATE telemetry.hpp)
else ()
    # Stub interface library: provides the header but no implementation
    add_library(telemetry INTERFACE)
    target_include_directories(telemetry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
endif ()