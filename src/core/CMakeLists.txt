add_library(core STATIC
        core.cpp
        simulation.cpp
        systems.cpp
        boid_steering.cpp
)

# Allow other targets linking to 'core' to find its headers
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(flecs CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(HWY CONFIG REQUIRED)

target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC unofficial::enet::enet
        PUBLIC hwy
)