cmake_minimum_required(VERSION 3.25)
project(SimNetLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include targets
add_subdirectory(src/core)
add_subdirectory(src/server)
add_subdirectory(src/client)