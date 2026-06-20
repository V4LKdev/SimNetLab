#game/server
add_library(game_server STATIC
        server_world.cpp
)

target_include_directories(game_server PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_include_directories(game_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(game_server PUBLIC game_shared)