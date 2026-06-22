# core library
add_library(core STATIC
        net/net_pipeline.cpp
        net/connection_handler.cpp
        net/net_manager.cpp
        net/net_transport.cpp
)

# Public includes
target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
# Private includes
target_include_directories(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Find dependencies
find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)

# Link libraries
target_link_libraries(core
        PUBLIC flecs::flecs nlohmann_json::nlohmann_json unofficial::enet::enet
)

# Optional telemetry
if (BUILD_TELEMETRY)
    target_link_libraries(core PRIVATE telemetry)
endif ()

# Optional SIMD
if (BUILD_SIMD)
    target_link_libraries(core PRIVATE simd)
    target_compile_definitions(core PUBLIC USE_SIMD=1)
endif ()

# -------- Precompiled Header ----------
target_precompile_headers(core PRIVATE core.hpp)