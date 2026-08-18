# simnet_config

`simnet_config` defines typed shared, Server-local, and Client-local runtime configuration. Import
the public API with `import simnet.config`.

The module exports configuration types, defaults, JSON loaders, default profile paths, runtime
fingerprints, and the canonical network compatibility fingerprint. Every JSON object rejects
unknown keys, invalid types, unsupported values, invalid ranges, and incompatible technique
combinations before returning a configuration. Missing optional fields use typed defaults.

Runtime fingerprints identify effective shared and role-local values for one process. They use
native value bytes and are stable only within the same ABI and byte order. The network compatibility
fingerprint uses a canonical byte representation for settings shared by connected processes.

The complete field, validation, fingerprint, and profile reference is
[`config/README.md`](../../config/README.md).
