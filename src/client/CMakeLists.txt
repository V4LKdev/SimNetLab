add_executable(client
        src/main.cpp
        src/renderer.cpp
)

if(SIMNETLAB_USE_SYSTEM_DEPS)
    find_package(raylib REQUIRED)

    target_link_libraries(client
            PRIVATE core
            PRIVATE raylib
    )
else()
    find_package(raylib CONFIG REQUIRED)

    target_link_libraries(client
            PRIVATE core
            PRIVATE raylib
    )
endif()