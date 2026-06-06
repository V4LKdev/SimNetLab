add_library(core STATIC
    src/core.cpp
)

target_include_directories(core PUBLIC src)

find_package(flecs CONFIG REQUIRED)

if(SIMNETLAB_USE_SYSTEM_DEPS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ENET REQUIRED libenet)

    target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC ${ENET_LIBRARIES}
    )

    target_include_directories(core
        PUBLIC ${ENET_INCLUDE_DIRS}
    )
else ()
    find_package(unofficial-enet CONFIG REQUIRED)

    target_link_libraries(core
        PUBLIC flecs::flecs
        PUBLIC unofficial::enet::enet
    )
endif ()