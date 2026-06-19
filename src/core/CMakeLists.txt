# core library
add_library(core STATIC
        net/net_pipeline.cpp
        net/connection_handler.cpp
        net/net_manager.cpp
        net/real_net_transport.cpp
)

target_include_directories(core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC nlohmann_json::nlohmann_json
        INTERFACE unofficial::enet::enet
)

# telemetry & simd are always available (interface or static)
target_link_libraries(core PUBLIC telemetry simd)