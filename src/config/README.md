`simnet_config` owns runtime configuration loaded from JSON.

Import it with:

```cpp
import simnet.config;
```

It exports client/server config structs, defaults, JSON loading, and runtime/network fingerprints. It does not own CLI parsing, live state, logging setup, or compile-time feature switches.
