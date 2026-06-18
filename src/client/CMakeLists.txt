add_executable(client
        ../app/client_main.cpp
        renderer.cpp
        network_client.cpp
        ecs/client_ecs.cpp
)
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(raylib REQUIRED)
target_link_libraries(client PRIVATE core network_protocol raylib)
