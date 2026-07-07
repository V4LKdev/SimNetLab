find_package(benchmark CONFIG REQUIRED)

add_library(simnet_benchmarking)

target_sources(simnet_benchmarking
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include
        FILES
            include/benchmarking.cppm
)

target_link_libraries(simnet_benchmarking
    PRIVATE
        simnet_core
        simnet_config
        simnet_telemetry
        simnet_snapshot
        simnet_pipeline
        benchmark::benchmark
)

simnet_configure_target(simnet_benchmarking)
