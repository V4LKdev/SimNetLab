if (BUILD_TELEMETRY)
    add_library(telemetry STATIC
            telemetry.cpp
            logger.cpp
    )
    target_compile_definitions(telemetry PUBLIC
            TELEMETRY_ENABLED=1
            TRACY_ENABLED
    )
    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(telemetry PUBLIC
            fmt::fmt
            spdlog::spdlog
            Tracy::TracyClient
    )
    target_include_directories(telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
else ()
    add_library(telemetry INTERFACE)
    target_include_directories(telemetry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
endif ()
