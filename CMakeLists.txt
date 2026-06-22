# root lists
cmake_minimum_required(VERSION 3.25)
project(SimNetLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---- Control what gets compiled and linked at build time -----------------------
option(BUILD_TELEMETRY "Build with splog, Tracy, and benchmark support" ON)
option(BUILD_SIMD "Build with Highway SIMD" OFF)
option(BUILD_TESTS "Build Catch2 unit/integration tests" ON)
option(USE_UNITY_BUILD "Enable unity builds for faster compilation" ON)
# --------------------------------------------------------------------------------

# Unity Build
if (USE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif ()

# Precompiled Headers
set(CMAKE_PRECOMPILE_HEADERS ON)

# Include optional targets first (they are used by core)
add_subdirectory(src/telemetry)
add_subdirectory(src/simd)

# Core engine (no game logic)
add_subdirectory(src/core)

# Game libraries - define them before the executables
add_subdirectory(src/game/shared)
add_subdirectory(src/game/client)
add_subdirectory(src/game/server)

# Executables
add_subdirectory(src/app)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()