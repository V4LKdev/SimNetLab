add_library(telemetry STATIC
        telemetry.cpp)

target_sources(telemetry PRIVATE
        logger.cpp
)

target_include_directories(telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)

target_link_libraries(telemetry
        PUBLIC
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        benchmark::benchmark
        Tracy::TracyClient
)

target_compile_definitions(telemetry PUBLIC
        TELEMETRY_ENABLED=1
        TRACY_ENABLED
)