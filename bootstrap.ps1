$ErrorActionPreference = "Stop"

$Profile = if ($args.Count -gt 0) { $args[0] } else { "debug" }

git submodule update --init --recursive

if (-not (Test-Path ".\vcpkg\vcpkg.exe")) {
    .\vcpkg\bootstrap-vcpkg.bat
}

cmake --preset $Profile
cmake --build --preset $Profile --target Server Client
