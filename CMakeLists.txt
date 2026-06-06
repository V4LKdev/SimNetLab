cmake_minimum_required(VERSION 3.25)

project(SimNetLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer natively installed packages on linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SIMNETLAB_USE_SYSTEM_DEPS ON)
else ()
    set(SIMNETLAB_USE_SYSTEM_DEPS OFF)
endif ()

add_subdirectory(src/core)
add_subdirectory(src/server)
add_subdirectory(src/client)
