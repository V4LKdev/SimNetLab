# core library
add_library(core STATIC
        net/net_pipeline.cpp
        net/connection_handler.cpp
        net/net_manager.cpp
        net/real_net_transport.cpp
)

# PUBLIC: consumers include headers as <core/core.hpp>
target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
# PRIVATE: core's own .cpp files can include without the prefix
target_include_directories(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs nlohmann_json::nlohmann_json
        INTERFACE unofficial::enet::enet
)

# Optional libraries
target_link_libraries(core PUBLIC telemetry)

if (BUILD_SIMD)
    target_link_libraries(core PRIVATE simd)
    target_compile_definitions(core PRIVATE USE_SIMD=1)
endif ()