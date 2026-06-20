# app/CMakeLists.txt
# Executable entry points

add_executable(sim_client
        client_main.cpp
)
target_link_libraries(sim_client PRIVATE game_client)
# game_client transitively pulls in game_shared -> core

add_executable(sim_server
        server_main.cpp
)
target_link_libraries(sim_server PRIVATE game_server)
# game_server transitively pulls in game_shared -> core