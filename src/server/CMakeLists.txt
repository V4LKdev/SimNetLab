add_executable(server
        main.cpp
        network_server.cpp
)
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PRIVATE core network_protocol)
