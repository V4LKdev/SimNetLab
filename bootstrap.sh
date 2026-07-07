#!/usr/bin/env sh
set -eu

profile="${1:-debug}"

git submodule update --init --recursive

if [ ! -x "./vcpkg/vcpkg" ]; then
    ./vcpkg/bootstrap-vcpkg.sh
fi

cmake --preset "$profile"
cmake --build --preset "$profile" --target Server Client
