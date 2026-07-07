module;

#include <filesystem>

export module simnet.config:load;

import :types;

export namespace simnet
{
    /// Returns the default server configuration.
    [[nodiscard]] ServerConfig default_server_config();

    /// Returns the default client configuration.
    [[nodiscard]] ClientConfig default_client_config();

    /// Loads server configuration from a JSON file.
    [[nodiscard]] ServerConfig load_server_config(std::filesystem::path const& path);

    /// Loads client configuration from a JSON file.
    [[nodiscard]] ClientConfig load_client_config(std::filesystem::path const& path);

    /// Returns a traceability fingerprint for runtime configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_runtime_config(ServerConfig const& config) noexcept;

    /// Returns a traceability fingerprint for runtime configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_runtime_config(ClientConfig const& config) noexcept;

    /// Returns a compatibility fingerprint for network-relevant configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_network_compatibility(ServerConfig const& config) noexcept;

    /// Returns a compatibility fingerprint for network-relevant configuration.
    [[nodiscard]] ConfigFingerprint fingerprint_network_compatibility(ClientConfig const& config) noexcept;
}
