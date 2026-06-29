# telemetry
add_library(telemetry STATIC)

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# --- Module Interface (PUBLIC) ---
target_sources(telemetry
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include/
        FILES
        include/telemetry/telemetry.cppm
)

target_include_directories(telemetry PUBLIC include)

# --- Implementation (PRIVATE) ---
target_sources(telemetry
        PRIVATE
        src/telemetry.cpp
        src/export_csv.cpp
        src/export_json.cpp
)

# --- Conditional Tracy Support ---
if (ENABLE_TELEMETRY_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_compile_definitions(telemetry PUBLIC TRACY_ENABLE SIMNET_TELEMETRY_TRACY)
    target_link_libraries(telemetry PUBLIC Tracy::TracyClient)
endif ()

target_link_libraries(telemetry
        PRIVATE
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

# --- Language Standard ---
target_compile_features(telemetry PUBLIC cxx_std_23)

# --- Sanitizers ---
target_enable_sanitizers(telemetry)
