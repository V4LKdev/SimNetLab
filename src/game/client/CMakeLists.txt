#game/client
add_library(game_client STATIC
        client_world.cpp
        input_system.cpp
        render_system.cpp
)

target_include_directories(game_client PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_include_directories(game_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(game_client PUBLIC game_shared)