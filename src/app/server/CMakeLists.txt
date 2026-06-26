# Server App

add_executable(server_app src/server_main.cpp)

# --- Dependencies ---
target_link_libraries(server_app PRIVATE game_server)

# --- Compile Definitions ---
target_compile_definitions(server_app PRIVATE SERVER_ONLY=1)

# --- Sanitizers ---
target_enable_sanitizers(server_app)