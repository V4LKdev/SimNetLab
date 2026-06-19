# app/CMakeLists.txt
# Executable entry points

# ---- Client executable ----
add_executable(sim_client
        client_main.cpp
)

target_link_libraries(sim_client
        PRIVATE core
        # PRIVATE game_shared
        # PRIVATE game_client
)

# ---- Server executable ----
add_executable(sim_server
        server_main.cpp
)

target_link_libraries(sim_server
        PRIVATE core
        # PRIVATE game_shared
        # PRIVATE game_server
)