project-root/
├── CMakeLists.txt                          # Root orchestration (v3.28+)
├── CMakePresets.json                       # Debug/Release/ASAN/UBSAN presets
├── vcpkg.json                              # Manifest mode dependencies
├── vcpkg-configuration.json                # Registries (if needed)
│
├── cmake/                                  # Helper modules
│   ├── CompilerOptions.cmake               # C++26, -fPIC, etc.
│   ├── Warnings.cmake                      # -Wall -Wextra -Wpedantic
│   ├── Modules.cmake                       # C++20/26 module setup
│   ├── Sanitizers.cmake                    # ASAN/UBSAN config
│   └── Install.cmake                       # Install rules
│
├── core/                                   # Core library (no game logic)
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── core/
│   │       ├── net/
│   │       │   ├── NetworkManager.ixx      # Module interface
│   │       │   ├── Packet.ixx
│   │       │   └── Connection.ixx
│   │       ├── utils/
│   │       │   ├── Logger.ixx
│   │       │   └── Config.ixx
│   │       └── telemetry/
│   │           └── Telemetry.ixx           # Tracy wrapper (no raylib/Flecs)
│   └── src/
│       ├── net/
│       │   ├── NetworkManager.cpp          # #includes <enet/enet.h> here
│       │   ├── Packet.cpp
│       │   └── Connection.cpp
│       ├── utils/
│       │   ├── Logger.cpp
│       │   └── Config.cpp
│       └── telemetry/
│           └── Telemetry.cpp
│
├── game/                                   # Game logic (Flecs ECS)
│   ├── CMakeLists.txt
│   ├── shared/                             # SHARED library target
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── game/
│   │   │       ├── World.ixx               # ECS world definition
│   │   │       ├── Components.ixx          # ECS component types
│   │   │       └── Systems.ixx             # ECS system declarations
│   │   └── src/
│   │       ├── World.cpp                   # #includes <flecs.h> here
│   │       ├── Components.cpp
│   │       └── Systems.cpp
│   ├── server/                             # SERVER library target
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── game/
│   │   │       └── ServerSimulation.ixx
│   │   └── src/
│   │       └── ServerSimulation.cpp        # Depends on game_shared + enet
│   └── client/                             # CLIENT library target
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── game/
│       │       └── ClientSimulation.ixx
│       └── src/
│           └── ClientSimulation.cpp        # Depends on game_shared + raylib
│
├── app/                                    # Executable targets
│   ├── CMakeLists.txt
│   ├── server/
│   │   ├── CMakeLists.txt
│   │   └── src/
│   │       └── ServerMain.cpp              # Links game_server
│   └── client/
│       ├── CMakeLists.txt
│       └── src/
│           └── ClientMain.cpp              # Links game_client
│
├── tests/                                  # Catch2 unit tests
│   ├── CMakeLists.txt
│   ├── core/
│   │   └── test_network.cpp
│   └── game/
│       └── test_ecs.cpp
│
├── benchmarks/                             # Google Benchmark (Linux-only)
│   ├── CMakeLists.txt
│   └── network_bench.cpp                   # Uses tc-netem via script wrapper
│
├── scripts/                                # Build/dev scripts
│   ├── run_benchmark.sh                    # Sets up tc-netem, runs benchmark
│   └── README.md
│
└── docs/                                   # Documentation
    └── README.md

# Reasoning
|Practice|Source|Date|Credibility|
|---|---|---|---|
|C++ Modules with `.ixx` + `FILE_SET`|CMake 3.28 docs[](https://cmake.org/cmake/help/v3.28/manual/cmake-cxxmodules.7.html#limitations)|2023|Official CMake documentation|
|Target-centric CMake (`target_*`)|Modern CMake guide[](https://runebook.dev/en/docs/cmake/manual/cmake-language.7)|2025-09|Community-vetted best practices|
|`include/` vs `src/` separation|CMU library guide[](https://www.cs.cmu.edu/~cga/nao/doc/reference-documentation/qibuild/guide/how_to_write_a_library.html#how-to-write-a-library)|Undated|Top-tier academic institution|
|CMake Presets (`CMakePresets.json`)|cpp20120 boilerplate[](https://deepwiki.com/cpp20120/cmake_boilerplate/2.2-cmake-presets)|2025-07|Community boilerplate project|
|vcpkg manifest mode|Microsoft vcpkg FAQ[](https://learn.microsoft.com/en-us/vcpkg/about/faq)|2025|Official Microsoft documentation|
|Explicit source lists (no `GLOB`)|Modern CMake guide[](https://runebook.dev/en/docs/cmake/manual/cmake-language.7)|2025-09|Community-vetted best practices|
|Implicit defaults; no single-use vars|Beman Standard[](https://discourse.bemanproject.org/t/new-beman-standard-cmake-recommendations-implicit-defaults-and-no-single-use-variables/429)|2025-05|Community standardization effort|
|`PUBLIC`/`PRIVATE`/`INTERFACE` scopes|CMake docs|Undated|Official CMake documentation
