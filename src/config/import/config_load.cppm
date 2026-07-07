module;

#include <filesystem>

/// @brief Runtime configuration loading and fingerprinting.
export module simnet.config:load;

import :types;

export namespace simnet
{
    /// Returns the default shared runtime config file path.
    [[nodiscard]] std::filesystem::path default_shared_config_path();

    /// Returns the default server runtime config file path.
    [[nodiscard]] std::filesystem::path default_server_config_path();

    /// Returns the default client runtime config file path.
    [[nodiscard]] std::filesystem::path default_client_config_path();

    /// Returns the default shared configuration.
    [[nodiscard]] SharedConfig default_shared_config();

    /// Returns the default server configuration.
    [[nodiscard]] ServerConfig default_server_config();

    /// Returns the default client configuration.
    [[nodiscard]] ClientConfig default_client_config();

    /// Loads shared configuration from a JSON file.
    [[nodiscard]] SharedConfig load_shared_config(std::filesystem::path const& path);

    /// Loads server configuration from a JSON file.
    [[nodiscard]] ServerConfig load_server_config(std::filesystem::path const& path);

    /// Loads client configuration from a JSON file.
    [[nodiscard]] ClientConfig load_client_config(std::filesystem::path const& path);

    /// Returns a traceability fingerprint for runtime configuration.
    /// (Raw-byte fingerprints are only stable within the same ABI/endian build.)
    [[nodiscard]] ConfigFingerprint fingerprint_runtime_config(
        SharedConfig const& shared,
        ServerConfig const& local
    ) noexcept;

    /// Returns a traceability fingerprint for runtime configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_runtime_config(
        SharedConfig const& shared,
        ClientConfig const& local
    ) noexcept;

    /// Returns a compatibility fingerprint for network-relevant configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_network_compatibility(SharedConfig const& config) noexcept;
}
