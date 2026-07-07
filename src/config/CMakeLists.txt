find_package(nlohmann_json CONFIG REQUIRED)

add_library(simnet_config)

# Keep bundled defaults beside each build tree so apps do not depend on cwd.
set(_simnet_default_config_dir "${PROJECT_BINARY_DIR}/config")
file(MAKE_DIRECTORY "${_simnet_default_config_dir}")
configure_file("${PROJECT_SOURCE_DIR}/config/shared_default.json"
    "${_simnet_default_config_dir}/shared_default.json"
    COPYONLY
)
configure_file("${PROJECT_SOURCE_DIR}/config/server_default.json"
    "${_simnet_default_config_dir}/server_default.json"
    COPYONLY
)
configure_file("${PROJECT_SOURCE_DIR}/config/client_default.json"
    "${_simnet_default_config_dir}/client_default.json"
    COPYONLY
)

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
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_definitions(simnet_config
    PRIVATE
        SIMNET_DEFAULT_CONFIG_DIR="${_simnet_default_config_dir}"
)

simnet_configure_target(simnet_config)
