# root
cmake_minimum_required(VERSION 3.31)

project(
        SimNetLab
        VERSION 0.1.0
        DESCRIPTION "Networked simulation thesis project"
        LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(SimNetOptions)
include(SimNetTarget)
include(CTest)

add_subdirectory(src/core)
add_subdirectory(src/compression)
add_subdirectory(src/packetization)
add_subdirectory(src/runtime)
add_subdirectory(src/config)
add_subdirectory(src/snapshot)
add_subdirectory(src/telemetry)
add_subdirectory(src/spatial)
add_subdirectory(src/game_shared)
add_subdirectory(src/game_server)
add_subdirectory(src/game_client)
add_subdirectory(src/pipeline)
add_subdirectory(src/transport)

if (SIMNET_ENABLE_SYNTHETIC)
    add_subdirectory(src/synthetic)
endif ()

if (SIMNET_ENABLE_RENDER)
    add_subdirectory(src/render)
endif ()

add_subdirectory(app)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
