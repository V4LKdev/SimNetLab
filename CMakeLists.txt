cmake_minimum_required(VERSION 4.3)

if (NOT CMAKE_TOOLCHAIN_FILE)
    set(_simnet_vcpkg_candidates
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake"
    )

    find_program(_simnet_vcpkg_executable NAMES vcpkg)
    if (_simnet_vcpkg_executable)
        get_filename_component(_simnet_vcpkg_root "${_simnet_vcpkg_executable}" DIRECTORY)
        list(APPEND _simnet_vcpkg_candidates
            "${_simnet_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
        )
    endif ()

    foreach (_simnet_vcpkg_toolchain IN LISTS _simnet_vcpkg_candidates)
        if (EXISTS "${_simnet_vcpkg_toolchain}")
            set(CMAKE_TOOLCHAIN_FILE "${_simnet_vcpkg_toolchain}" CACHE FILEPATH "vcpkg toolchain file")
            break()
        endif ()
    endforeach ()
endif ()

if (NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "vcpkg is required. Configure with -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake, "
        "set VCPKG_ROOT, or place vcpkg at ./vcpkg or ../vcpkg."
    )
endif ()

project(
    SimNetLab
    VERSION 0.1.0
    DESCRIPTION "Networked simulation thesis project"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(SimNetOptions)
include(SimNetSanitizers)
include(SimNetTarget)

add_subdirectory(src/core)
add_subdirectory(src/config)
add_subdirectory(src/snapshot)
add_subdirectory(src/synthetic)
add_subdirectory(src/telemetry)
add_subdirectory(src/spatial)
add_subdirectory(src/game_shared)
add_subdirectory(src/game_server)
add_subdirectory(src/game_client)
add_subdirectory(src/pipeline)
add_subdirectory(src/transport)
if (SIMNET_ENABLE_RENDER)
    add_subdirectory(src/render)
endif ()
if (SIMNET_ENABLE_BENCHMARKING)
    add_subdirectory(src/benchmarking)
endif ()
add_subdirectory(app)
