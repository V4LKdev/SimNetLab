find_package(nlohmann_json CONFIG REQUIRED)

add_library(simnet_config)

target_sources(simnet_config
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS import
        FILES
            import/config.cppm
            import/config_types.cppm
            import/config_load.cppm
    PRIVATE
        src/config_load.cpp
)

target_link_libraries(simnet_config
    PUBLIC
        simnet_core
    PRIVATE
        nlohmann_json::nlohmann_json
)

simnet_configure_target(simnet_config)
