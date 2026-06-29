# utils
add_library(utils STATIC)

find_package(nlohmann_json CONFIG REQUIRED)

# --- Module Interface (PUBLIC) ---
target_sources(utils
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include/
        FILES
        include/utils/utils.cppm
)

# --- Implementation (PRIVATE) ---
target_sources(utils
        PRIVATE
        src/config.cpp
        src/config_loader.cpp
        src/timestep.cpp
        src/utils.cpp
)

target_link_libraries(utils
        PUBLIC
        nlohmann_json::nlohmann_json
)

# --- Language Standard ---
target_compile_features(utils PUBLIC cxx_std_23)

target_compile_definitions(utils PRIVATE SIMNET_DEV_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# --- Sanitizers ---
target_enable_sanitizers(utils)
