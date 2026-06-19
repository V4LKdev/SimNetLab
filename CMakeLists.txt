# root lists
cmake_minimum_required(VERSION 3.25)
project(SimNetLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Control what gets compiled and linked at build time -----------------------
option(BUILD_TELEMETRY "Build with splog, Tracy, and benchmark support" ON)
option(BUILD_SIMD "Build with Highway SIMD" OFF)
option(BUILD_TESTS "Build Catch2 unit/integration tests" ON)
# --------------------------------------------------------------------------------

# Include targets
# these are conditionally included (logic inside their cmake lists files)
add_subdirectory(src/telemetry)
add_subdirectory(src/simd)

add_subdirectory(src/core)
add_subdirectory(src/game/shared)

add_subdirectory(src/app)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()