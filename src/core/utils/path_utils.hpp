#pragma once
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace simnet::core::utils {
    /// Helper to extract the folder containing the running executable binary
    inline std::filesystem::path get_executable_dir()
    {
        namespace fs = std::filesystem;
        char buffer[1024];
#if defined(_WIN32)
        GetModuleFileNameA(NULL, buffer, sizeof(buffer));
        return fs::path(buffer).parent_path();
#elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) return fs::path(std::string(buffer, len)).parent_path();
#elif defined(__APPLE__)
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0) return fs::path(buffer).parent_path();
#endif
        return fs::current_path(); // Absolute fallback
    }

    // Resolves path relative to source root if in IDE dev mode, otherwise relative to binary
    inline std::string resolve_runtime_path(const std::filesystem::path &relative_path)
    {
        namespace fs = std::filesystem;

        // 1. IDE / Developer Mode: Prioritize the workspace source root if macro is present
#if defined(SIMNET_DEV_SOURCE_DIR)
        fs::path dev_path = fs::path(SIMNET_DEV_SOURCE_DIR) / relative_path;
        if (fs::exists(dev_path.parent_path())) {
            return dev_path.string();
        }
#endif

        // 2. Production / Compiled Build Mode: Fallback to the binary folder location
        fs::path prod_path = get_executable_dir() / relative_path;

        // Fail-safe: Automatically generate the log directory structure if it is missing
        if (!fs::exists(prod_path.parent_path())) {
            fs::create_directories(prod_path.parent_path());
        }

        return prod_path.string();
    }
}
