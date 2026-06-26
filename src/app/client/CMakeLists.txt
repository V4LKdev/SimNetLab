# Client App
add_executable(client_app src/client_main.cpp)

# --- Dependencies ---
target_link_libraries(client_app PRIVATE game_client)

# --- Compile Definitions ---
target_compile_definitions(client_app PRIVATE CLIENT_ONLY=1)

# --- Sanitizers ---
target_enable_sanitizers(client_app)