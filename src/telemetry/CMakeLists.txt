# telemetry
add_library(telemetry STATIC)

# --- Module Interface (PUBLIC) ---
target_sources(telemetry
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS include/
        FILES
        include/telemetry/telemetry.cppm
)

# --- Implementation (PRIVATE) ---
target_sources(telemetry
        PRIVATE
        src/telemetry.cpp
        src/export_csv.cpp
        src/export_json.cpp
)

# --- Conditional Tracy Support ---
if (ENABLE_TELEMETRY_TRACY)
    target_sources(telemetry PRIVATE src/tracy.cpp)
    target_compile_definitions(telemetry PRIVATE TELEMETRY_TRACY)
    target_link_libraries(telemetry PRIVATE Tracy::TracyClient)
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