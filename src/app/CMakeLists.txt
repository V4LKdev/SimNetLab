# app/CMakeLists.txt
add_executable(sim_client client_main.cpp)
target_link_libraries(sim_client PRIVATE game_client)

add_executable(sim_server server_main.cpp)
target_link_libraries(sim_server PRIVATE game_server)

# Development definitions
set(DEV_DEFS "SIMNET_DEV_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
target_compile_definitions(sim_client PRIVATE ${DEV_DEFS})
target_compile_definitions(sim_server PRIVATE ${DEV_DEFS})

# Post-build: copy config folder to each executable's output directory
foreach (target sim_client sim_server)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:${target}>/config"
            COMMENT "Copying config to $<TARGET_FILE_DIR:${target}>/config"
    )
endforeach ()