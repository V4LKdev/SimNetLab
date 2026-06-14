cmake_minimum_required(VERSION 3.25)
project(SimNetLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TELEMETRY_ENABLED "Build with telemetry, diagnostics, and benchmarking library" ON)
option(SIMD_ENABLED "Build with optional SIMD library" OFF)

# Include targets

if (TELEMETRY_ENABLED)
    add_subdirectory(src/telemetry)
endif ()

if (SIMD_ENABLED)
    add_subdirectory(src/simd)
endif ()

add_subdirectory(src/core)
add_subdirectory(src/network)
add_subdirectory(src/server)
add_subdirectory(src/client)

enable_testing()
add_subdirectory(tests)