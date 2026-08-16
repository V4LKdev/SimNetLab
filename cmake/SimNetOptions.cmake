# Features
option(SIMNET_ENABLE_TRACY              "Enable Tracy profiling integration"            OFF)
option(SIMNET_ENABLE_RENDER             "Build render support"                          ON)
option(SIMNET_ENABLE_SYNTHETIC          "Build synthetic research workload support"     OFF)

# Compiler
option(SIMNET_WARNINGS_AS_ERRORS        "Treat compiler warnings as errors"             ON)
option(SIMNET_ENABLE_ASAN               "Enable AddressSanitizer"                       OFF)
option(SIMNET_ENABLE_UBSAN              "Enable UndefinedBehaviorSanitizer"             OFF)
