add_executable(client
        main.cpp
        renderer.cpp
)

find_package(raylib REQUIRED)

target_link_libraries(client
        PRIVATE core
        PRIVATE raylib
)