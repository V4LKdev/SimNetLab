/// \file   utils.cpp
/// \brief  Platform-specific path utilities.

module;

#include <filesystem>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__linux__)
#  include <unistd.h>
#endif

module simnet.utils;

namespace simnet::utils {
    std::filesystem::path get_executable_dir()
    {
        namespace fs = std::filesystem;
        char buffer[1024];
#if defined(_WIN32)
        GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
        return fs::path(buffer).parent_path();
#elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) return fs::path(std::string(buffer, len)).parent_path();
#endif
        return fs::current_path();
    }

    std::string resolve_runtime_path(const std::filesystem::path &relative_path)
    {
        namespace fs = std::filesystem;
#if defined(SIMNET_DEV_SOURCE_DIR)
        fs::path dev_path = fs::path(SIMNET_DEV_SOURCE_DIR) / relative_path;
        if (fs::exists(dev_path.parent_path())) return dev_path.string();
#endif
        fs::path prod_path = get_executable_dir() / relative_path;
        if (!fs::exists(prod_path.parent_path())) {
            fs::create_directories(prod_path.parent_path());
        }
        return prod_path.string();
    }
}
