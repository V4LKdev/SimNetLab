cmake_minimum_required(VERSION 4.1)
project(SimNetLab VERSION 0.1.0 LANGUAGES C CXX)

# --- C++23 Standard ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- C++ Modules ---
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# --- Build Options ---
option(BUILD_BENCHMARKS "Build Google Benchmark executables" OFF)
option(ENABLE_TELEMETRY_TRACY "Enable Tracy real‑time profiling in telemetry" ON)

# --- Output Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Compile Commands for IDE/LSP ---
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Sanitizers ---
include(cmake/Sanitizers.cmake)

# --- Build Options ---
option(BUILD_BENCHMARKS "Build Google Benchmark executables" OFF)

# --- Dependencies ---
find_package(spdlog CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

if (ENABLE_TELEMETRY_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif ()

if (BUILD_BENCHMARKS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(benchmark CONFIG REQUIRED)
endif ()

# --- Subdirectories ---
add_subdirectory(src/telemetry)
#add_subdirectory(src/core)
#add_subdirectory(src/game/shared)
#add_subdirectory(src/game/client)
#add_subdirectory(src/game/server)
#add_subdirectory(src/app)

# --- Tests ---
#enable_testing()
#add_subdirectory(src/tests)

# --- Benchmarks (optional, Linux-only) ---
if (BUILD_BENCHMARKS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    #add_subdirectory(src/benchmarks)
endif ()