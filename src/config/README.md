# simnet_config

`simnet_config` owns the typed shared, Server-local, and Client-local runtime configuration. Import
the public API with `import simnet.config`.

The module exports configuration value types, typed defaults, JSON loaders, default profile paths,
runtime fingerprints, and the canonical network compatibility fingerprint. It does not own CLI
parsing, application state, logging setup, or compile-time feature selection.

Every JSON root and nested object accepts only the fields in the current contract. Unknown fields,
invalid types, invalid enumerated values, invalid ranges, and incompatible technique combinations
are rejected before a configuration is returned. Missing optional fields retain their typed
defaults. The shipped default profiles load to the same values as the C++ defaults.

Runtime fingerprints identify the effective shared and role-local configuration for one process.
They use native value bytes and are stable only within the same ABI and byte order. The network
compatibility fingerprint uses a canonical byte representation for shared settings used by a
connected session.

The complete field, validation, fingerprint, and maintained-profile reference is
[`config/README.md`](../../config/README.md).

The public module depends only on the standard library. JSON parsing, core time conversion,
packetization validation, and `nlohmann_json` remain private implementation dependencies.
