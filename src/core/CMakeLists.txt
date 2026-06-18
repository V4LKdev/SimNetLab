# core library
add_library(core STATIC)

target_include_directories(core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC nlohmann_json::nlohmann_json
)

# telemetry & simd are always available (interface or static)
target_link_libraries(core PUBLIC telemetry simd)