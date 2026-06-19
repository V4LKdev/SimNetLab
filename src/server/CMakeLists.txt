add_executable(server
        ../app/server_main.cpp
        ecs/server_ecs.cpp
        ecs/systems/spawn_boids.cpp
        ecs/systems/send_snapshot.cpp
)
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PRIVATE core network_protocol)
