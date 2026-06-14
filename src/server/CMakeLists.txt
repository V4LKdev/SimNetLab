add_executable(server main.cpp
        network_server.cpp)

target_link_libraries(server
        PRIVATE core
        PRIVATE network_protocol
)