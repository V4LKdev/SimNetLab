add_library(network_protocol INTERFACE
)

target_include_directories(network_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(unofficial-enet CONFIG REQUIRED)
target_link_libraries(network_protocol INTERFACE unofficial::enet::enet)