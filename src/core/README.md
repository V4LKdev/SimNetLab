`simnet_core` defines dependency-free project vocabulary.

Import it with:

```cpp
import simnet.core;
```

It currently exports math primitives, fixed-step time types, IDs, and byte spans. 
It must not depend on config, logging, snapshots, transport, rendering, or third-party libraries.
