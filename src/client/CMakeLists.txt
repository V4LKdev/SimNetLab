add_executable(client
        main.cpp
        renderer.cpp
        network_client.cpp
)

find_package(raylib REQUIRED)

target_link_libraries(client
        PRIVATE core
        PRIVATE raylib
        PRIVATE network_protocol
)